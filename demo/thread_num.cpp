#include <iostream>
#include <mutex>
#include <thread>

using namespace std;

int number;
mutex mutex_number;

const int MAXNUM = 10;

// 打印奇数
void add_1() {
    while (1) {
        mutex_number.lock();
        if (number >= MAXNUM) {
            mutex_number.unlock();
            break;
        }
        if (number % 2 == 0) {
            number++;
            cout << "mythread_1: " << number << endl; // 输出
        }

        mutex_number.unlock();
    }
    cout << "mythread_1 finish" << endl; // mythread_1完成
}

// 打印偶数
void add_2() {
    while (1) {
        mutex_number.lock();

        if (number >= MAXNUM) {
            mutex_number.unlock();
            break;
        }
        if (number % 2 == 1) {
            number++;
            cout << "mythread_2: " << number << endl; // 输出
        }

        mutex_number.unlock();
    }
    cout << "mythread_2 finish" << endl; // mythread_2完成
}

int main() {
    number = 0;

    cout << endl
         << "Create and Start!" << endl;

    thread mythread_1(add_1);
    thread mythread_2(add_2);

    mythread_1.join();
    mythread_2.join();

    cout << endl
         << "Finish and Exit!" << endl;
    return 0;
}
