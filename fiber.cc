#include "fiber.hpp"

#include <assert.h>

namespace co {

namespace fibers {

template <typename Fn, typename ...Args >
fiber::fiber(Fn && fn, Args&& ...args)
    : capacity_(0), size_(0), status_(kReady), cid_(-1) {
    assert(this_fiber::number_ < this_fiber::max_number_); //协程上限
    //分配协程cid
    for (int cid = 0; cid < this_fiber::max_number_; ++cid) {
        if (this_fiber::fiber_list_[cid] == nullptr) {
            cid_ = cid;
            this_fiber::fiber_list_[cid] = this; //unsafe
            ++this_fiber::number_;
            break;
        }
    }
    assert(cid_ != -1);
    getcontext(&context_); //将当前上下文保存在context_中
    context_.uc_stack.ss_sp   = this_fiber::stack_;
    context_.uc_stack.ss_size = kStackSize;
    context_.uc_link          = &this_fiber::context_;
    // makecontext: Task执行完毕之后会自动执行setcontext(context_.uc_link)，也就是返回主协程
    makecontext(&context_, reinterpret_cast<Task>(running), sizeof...(Args) + 1, fn,  args); 
    // swapcontext: 将当前上下文存储在this_fiber::context_，并激活context_作为上下文
    swapcontext(&this_fiber::context_, &context_); //协程切换
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
int this_fiber::running_cid_ = kIdle;
std::vector<fiber*> this_fiber::fiber_list_(kFiberSize, nullptr);

void this_fiber::save_stack(fiber* fb, const char* ebp) {
    //Tips:这里只适合栈地址空间向下增长的架构
    //----- ebp   = &this_fiber::stack_[kStackSize]
    //|...|
    //|...| esp(dummy)
    //|   |
    //|   |
    //-----       = &this_fiber::stack_[0]
    // ...        = 0x00
    char esp = 0; //栈地址定界
    assert(ebp - &esp <= kStackSize); //限制协程使用的栈空间大小
    if (fb->capacity_ < ebp - &esp) {
        fb->capacity_ = ebp - &esp;
        fb->stack_    = std::make_shared<char[]>(fb->capacity_);
    }
    fb->size_ = ebp - &esp;
    memcpy(fb->stack_, &esp, fb->size_);
}


void this_fiber::yield() {
    assert(running_cid_ != kIdle);
    
    auto fb = fiber_list_[running_cid_];
    assert(fb != nullptr);

    save_stack(fb, stack_ + kStackSize);   //保存栈空间
    fb->status_  = fibers::fiber::kSuspend;
    int cid = fifo();
    if (cid == kIdle) {
        running_cid_ = kIdle;
        swap(fb->context_, context_);
    } else {
        switch_fiber(running_cid_, cid);
    }
}

int this_fiber::fifo() {
    //“不严格的”先进先出调度算法
    for (int cid = 0; cid < this_fiber::max_number_; ++cid) {
        if (this_fiber::fiber_list_[cid] != nullptr) {
            return cid;           
        }    
    }
    return kIdle;
}

void this_fiber::switch_fiber(const int from_cid, const int to_cid) {
    assert(from_cid >= 0 && from_cid < max_number_);
    assert(to_cid >= 0 && to_cid < max_number_);
    
    auto from_fb = fiber_list_[from_cid];
    auto to_fb = fiber_list_[to_cid];
    assert(from_fb != nullptr && to_fb != nullptr);
    //Tips:这里只适合栈地址空间向下增长的架构
    //-----  ebp = &this_fiber::stack_[kStackSize]
    //|...|
    //|...|  esp = &this_fiber::stack_ + kStackSize - fb->size_
    //|   |
    //-----      = &this_fiber::stack_[0]
    // ...       = 0x00
    // 恢复操作: 将填充this_fiber::stack_的[esp, ebp]区间
    memcpy(stack_ + kStackSize - to_fb->size_, to_fb->stack_, to_fb->size_); //恢复栈空间
    running_cid_   = to_cid;
    to_fb->status_ = fibers::fiber::kRunning;
    swapcontext(&from_fb->context_, &fb->context_); //切换协程
}

} //namespace co
