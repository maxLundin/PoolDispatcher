//#include "lib/PoolDispatcher.h"
//#include "lib/TaskQueue.h"


#include <bits/stdc++.h>

//int main() {
//
////    auto pool = PoolDispatcher(4);
////    std::this_thread::sleep_for(std::chrono::milliseconds(100));
//
//    TaskQueue queue(1);
//    auto h1 = queue.enqueue([] { std::cout << "Hi"; }, 0);
//    int a = 4;
//    auto x = [a]() -> int { return a; };
//    auto h2 = queue.enqueue(std::function<int()>(x), 0);
//    std::this_thread::sleep_for(std::chrono::milliseconds(50));
//    std::cout << queue.execute(h2);
//    queue.execute(h1);
//}

void print_int (std::future<int>& fut) {
//    int x = fut.get();
//    std::cout << x;
//    int x1 = fut.get();
//    std::cout << "value: " << x << " " << x1 << '\n';
}

int main ()
{
    std::promise<int> prom;                      // create promise

    std::future<int> fut = prom.get_future();    // engagement with future


//    std::thread th1(print_int, std::ref(fut));  // send future to new thread
    prom.set_value (10);
    std::cout << fut.get();
    // fulfill promise
//     (synchronizes with getting the future)
//    th1.join();
    return 0;
}