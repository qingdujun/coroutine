#pragma once

#if __APPLE__ && __MACH__
#include <sys/ucontext.h> //macOS X
#else
#include <ucontext.h>     //Linux
#endif

#include <array>
#include <vector>
#include <memory>
#include <funtional>
#include <mutex>
#include <condition_variable>

namespace co {

constexpr kStackSize = 1024 * 1024;//1M
constexpr kFiberSize = 16;
constexpr kIdle      = -1;

namespace fibers {

class this_fiber;

class fiber {
    friend this_fiber;

    enum STATUS { kReady, kRunning, kSuspend, kDead };

    using Task = std::function<void()>;
 public:

    template< typename Fn, typename ... Args >
    fiber(Fn &&, Args&& ...);
    
    ~fiber() = default;

    void join();
 
 private:
    template< typename Fn, typename ... Args >
    static void running(Fn&&, Args&& ...);
    
    void clear();

 private:
    ucontext_t context_;   //上下文
    int        cid_;       //协程cid
    int        size_;      //栈使用了多少
    int        capacity_;  //栈总容量
    std::shared_ptr<char[]> stack_; //协程栈
    STATUS     status_;    //协程状态

    std::mutex stop_mutex;
    std::condition_variable stop_condvar;
}; // class fiber

} // namespace fibers

class this_fiber {
    friend fibers::fiber;

 public:
    static void yield();

 private:
    static void save_stack(fibers::fiber* fb, const char* ebp);
    static void switch_fiber(const int from_cid, const int to_cid);
    static void fifo();

 private:
    static char stack_[kStackSize];
    static ucontext_t context_;
    static int  number_;       //当前协程数量
    static int  max_number_;   //可开启协程上限
    static int  running_cid_;  //当前运行的协程cid
    static std::vector<fibers::fiber*> fiber_list_; //所有协程对象
}; // class this_fiber

} // namespace co
