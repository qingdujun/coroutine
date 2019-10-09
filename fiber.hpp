#pragma once

#if __APPLE__ && __MACH__
#include <sys/ucontext.h> //macOS X
#else
#include <ucontext.h>     //Linux
#endif

#include <array>
#include <vector>
#include <memory>

namespace co {

constexpr kStackSize = 1024 * 1024;//1M
constexpr kFiberSize = 16;
constexpr kIdle      = -1;

namespace fibers {

class this_fiber;

class fiber {
    friend this_fiber;
public:
    template< typename Fn, typename ... Args >
    fiber(Fn &&, Args&& ...);
    
    ~fiber();

private:
    ucontext_t context_;   //上下文
    int        cid_;       //协程cid
    int        size_;      //栈使用了多少
    int        capacity_;  //栈总容量
    std::shared_ptr<char[]> stack_; //协程栈
}; // class fiber

} // namespace fibers

class this_fiber {
    friend fiber;
 public:
    static void yield();
    static void resume(int cid);

 private:
    static std::array<char, kStackSize> stack_;
    static ucontext_t context_;
    static int size_;       //当前协程数量
    static int capacity_;   //可开启协程上限
    static int running_cid_;//当前运行的协程cid
    static std::vector<fiber> fiber_list_; //所有协程对象
}; // class this_fiber

} // namespace co
