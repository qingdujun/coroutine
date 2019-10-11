#include "fiber.hpp"

void fn(const std::string& str, int n) {
    for (int i = 0; i < n; ++i) {
        std::cout << str << " : " << i << std::endl;
        co::this_fiber::yield(); 
    }
}

int main(int argc, char* argv[]) {
    co::fibers::fiber f1(fn, "abc", 15);
    co::fibers::fiber f2(fn, "efg", 2);
    co::fibers::fiber f1(fn, "hij", 8);
    co::fibers::fiber f1(fn, "klm", 10);

    f1.join();
    f2.join();
    f3.join();
    f4.join();

    std::cout << "done" << std::endl;

    return 0;
}
