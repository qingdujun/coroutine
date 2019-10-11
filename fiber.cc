#include "fiber.hpp"

#include <assert.h>

namespace co {

namespace fibers {

template <typename Fn, typename ...Args >
fiber::fiber(Fn&& fn, Args&& ...args)
    : capacity_(0), size_(0), status_(kReady), cid_(this_fiber::running_cid_) {
    assert(this_fiber::number_ < this_fiber::max_number_); //协程上限
    
    //分配协程cid
    while (this_fiber::fiber_list_[++cid_ % this_fiber::max_number_] != nullptr) {
        ;
    }
    cid_ %= this_fiber::max_number_;
    this_fiber::fiber_list_[cid_] = this; //unsafe
    ++this_fiber::number_;
    //保存上下文
    getcontext(&context_);
    context_.uc_stack.ss_sp   = this_fiber::shared_stack_;
    context_.uc_stack.ss_size = kStackSize;
    // 统一调度: 总是将fiber_list_[kIdle]放置主协程
    assert(this_fiber::fiber_list_[kIdle] != nullptr);
    context_.uc_link = &(this_fiber::fiber_list_[kIdle]->context_);
    // makecontext: Task执行完毕之后会自动执行setcontext(context_.uc_link)，也就是返回主协程
    makecontext(&context_, reinterpret_cast<Task>(running), sizeof...(Args) + 1, fn,  args); 
}

void fiber::clear() {
    --this_fiber::number_;
    this_fiber::fiber_list_[cid_] = nullptr; //回收cid
    this_fiber::running_cid_      = kIdle;
}

template< typename Fn, typename ... Args >
void fiber::running(Fn&& fn, Args&& ..args) {
    status_ = kRunning;
    fn(args); //执行真正函数体
    clear();  //清理工作
    status_ = kDead;
    stop_condvar.notify_all();
}

void fiber::join() {
    std::unique_lock<std::mutex> locker(stop_mutex);
    while (status_ != kDead) {
        stop_condvar.wait();
    }
}

} // namespace fibers

//初始化this_fiber
int this_fiber::size_        = 0;
int this_fiber::capacity_    = kFiberSize;
int this_fiber::running_cid_ = kIdle - 1;
std::vector<fiber*> this_fiber::fiber_list_(kFiberSize, nullptr);

void this_fiber::save_stack(fiber* fb, const char* ebp) {
    //Tips:这里只适合栈地址空间向下增长的架构
    //----- ebp   = &shared_stack_[kStackSize]
    //|...|
    //|...| esp(dummy)
    //|   |
    //|   |
    //-----       = &shared_stack_[0]
    // ...        = 0x00
    char esp = 0; //栈地址定界
    assert(ebp - &esp <= kStackSize); //限制协程使用的栈空间大小
    //所有协程运行时使用的都是全局shared_stack_。但是,当协程被切换出去时，
    //需保存栈现场，以供恢复运行时使用
    if (fb->capacity_ < ebp - &esp) {
        fb->capacity_ = ebp - &esp;
        fb->stack_    = std::make_shared<char[]>(fb->capacity_);
    }
    fb->size_ = ebp - &esp;
    memcpy(fb->stack_, &esp, fb->size_);
}


void this_fiber::yield() {
    assert(running_cid_ != kIdle);
    //协程调度算法为fifo
    int cid = fifo();
    if (cid == running_cid_) {
        return;
    }
    
    auto from_fb = fiber_list_[running_cid_];
    auto to_fb   = fiber_list_[cid];
    assert(from_fb != nullptr && to_fb != nullptr);

    save_stack(from_fb, shared_stack_ + kStackSize); //保存栈空间
    from_fb->status_  = fibers::fiber::kSuspend;
    switch_fiber(from_fb, to_fb);
}

int this_fiber::fifo() {
    int cid = running_cid_;
    //先进先出调度算法
    while (fiber_list_[++cid % max_number_] == nullptr) {
        ;
    }
    return (cid >= max_number_) ? kIdle : cid;
}

void this_fiber::switch_fiber(fibers::fiber* from_fb, fibers::fiber* to_fb) {
    assert(from_fb != nullptr && to_fb != nullptr);
    //Tips:这里只适合栈地址空间向下增长的架构
    //-----  ebp = &shared_stack_[kStackSize]
    //|...|
    //|...|  esp = &shared_stack_ + kStackSize - fb->size_
    //|   |
    //-----      = &shared_stack_[0]
    // ...       = 0x00
    to_fb->status_ = fibers::fiber::kRunning;
    running_cid_   = to_fb->cid_;
    switch (to_fb->status_) {
        case fibers::fiber::kReady: {
            swapcontext(&from_fb->context_, &to_fb->context_);
            break;
        }
        case fibers::fiber::kSuspend: {
            // 恢复协程栈: 将填充shared_stack_的[esp, ebp]区间
            memcpy(shared_stack_ + kStackSize - to_fb->size_, to_fb->stack_, to_fb->size_);
            // swapcontext: 将当前上下文存储在from_fb->context_，并激活to_fb->context_作为上下文
            swapcontext(&from_fb->context_, &to_fb->context_);
            break;
        }
        default: {
            assert(false);
        }
    }
}

} //namespace co
