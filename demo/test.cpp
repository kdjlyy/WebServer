#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>

using namespace std;

int a = 0;

int main() {
    condition_variable cv;
    std::thread t([&]() {
        while (true) {
            this_thread::sleep_for(chrono::seconds(1));
            a++;
            cout << a << endl;
            if (a == 5) {
                cv.notify_all();
            }
        }
    });

    mutex mtx;
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, [&]() { return a > 10; });
    cout << "a = " << a << endl;
    return 0;
}
