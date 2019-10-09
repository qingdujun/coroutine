#include "fiber.hpp"

#include <assert.h>

namespace co {

namespace fibers {

template <typename Fn, typename ...Args >
fiber::fiber(Fn && fn, Args&& ...args)
    : capacity_(0), size_(0), stack_(nullptr) {
    assert (this_fiber::size_ < this_fiber::capacity_ - 1); //协程上限
    //分配协程cid
    for (int cid = 0; cid < capacity_; ++cid) {
        if (this_fiber::fiber_list_[cid] == nullptr) {
            cid_ = cid;
            this_fiber::fiber_list_[cid] = this; //unsafe
            ++this_fiber::size_;
            break;
        }
    }
    getcontext(&context_); //将当前上下文保存在context_中
    context_.uc_stack.ss_sp = this_fiber::stack_;
    context_.uc_stack.ss_size = kStackSize;
    context_.uc_link = &this_fiber::context_;
    makecontext(&context_, fn, sizeof...(Args), args); //修改getcontext的上下文，并绑定函数
    swapcontext(&this_fiber::context_, &context_);     //协程切换
}

fiber::~fiber() {
    --this_fiber::size_;
    this_fiber::fiber_list_[cid_] = nullptr;
    this_fiber::running_cid_ = kIdle;
}

void fiber::save_stack(const char* ebp, const char* esp) {
    //TODO: 这里只适合栈地址空间向下增长(ebp->esp)
    //----- ebp(high)
    //|...|   
    //|...|
    //----- esp(low)
    assert(ebp - esp <= kStackSize); //限制协程使用的栈空间大小
    if (capacity_ < ebp - esp) {
        capacity_ = ebp - esp;
        stack_ = std::make_shared<char[]>(capacity_);
    }
    size_ = ebp - esp;
    memcpy(stack_, esp, size_);
}

} // namespace fibers

//初始化this_fiber
int this_fiber::size_       = 0;
int this_fiber::capacity_   = kFiberSize;
int this_fiber::running_cid = kIdle;
std::vector<fiber> this_fiber::fiber_list_(kFiberSize, nullptr);

void this_fiber::yield() {
    char dummy = 0;
    assert(running_cid_ != kIdle);

    save_stack(stack_ + kStackSize, &dummy); //保存栈空间
    auto fb = fiber_list_[running_cid_];
    if (fb == nullptr) {
        return;
    }
    running_cid_ = kIdle;
    swapcontext(fb->context_, context_); //切换到主协程
}

void this_fiber::resume(int cid) {
    assert(running_cid == kIdle);
    assert(cid >= 0 && cid < capacity_);
    
    auto fb = fiber_list_[cid];
    if (fb == nullptr) {
        return;
    }
    memcpy(stack_ + kStackSize - fb->size_, fb->stack_, fb->size_); //恢复栈空间
    running_cid = cid;
    swapcontext(context_, fb->context_); //切换到子协程
}

} //namespace co
