#include <condition_variable>
#include <functional>
#include <iostream>
#include <list>
#include <mutex>
#include <thread>
#include <vector>

class ThreadPool {
public:
    explicit ThreadPool(int num_threads) : stop(false) {
        // 构造函数中创建线程池
        for (int i = 0; i < num_threads; i++) {
            threads.emplace_back([this, i] {
                while (true) {
                    std::function<void()> task;
                    // {}是为了限制 std::unique_lock 的作用范围，确保锁在任务获取和执行完成之后就被自动释放。
                    // std::unique_lock 在构造函数中会自动上锁，析构函数中会自动释放锁
                    // std::unique_lock 对象也能保证在其自身析构时它所管理的 Mutex 对象能够被正确地解锁（即使没有显式地调用 unlock 函数）
                    {
                        std::unique_lock<std::mutex> lock(queue_mutex);
                        /*
                        线程等待在wait上时，只有得到notify，抢到锁，才会起来检查条件是否成立
                            1. 若成立，则不会释放锁阻塞，会继续向下执行
                            2. 若不成立，释放锁，等待notify
                        */
                        this->condition.wait(lock, [this] {
                            return this->stop || !this->tasks.empty();
                        });
                        if (this->stop && this->tasks.empty()) {
                            return;
                        }
                        // 取出队首任务并执行
                        task = std::move(this->tasks.front());
                        this->tasks.pop_front();
                    }
                    task();
                }
            });
        }
    }

    ~ThreadPool() {
        // 析构函数中停止并等待所有线程退出
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for (std::thread& thread : threads) {
            thread.join();
        }
    }

    /*
    函数模版
    使用enqueue将任务封装成一个函数对象，也使得任务的传递更加方便灵活，可以使用函数指针、函数对象、Lambda表达式等等方式来表示任务
    该函数采用了右值引用(T&& f)的方式来传递任务，
    这样可以保证传递的任务对象的资源可以被移动，而不是拷贝，从而避免了资源的不必要浪费和拷贝的开销。
    T && 传入右值引用用于移动，由于修改右值不会对其他变量进行修改，这样我们就可以实现诸如SWAP(a,t.a)的操作，而不用去申请新的内存空间

    如果参数是右值引用，那么它可以接收任何形式的参数，并且不会有复制的代价。
    代价是，如果该函数如果要把参数传递给另一个参数是右值引用的函数，需要用std::forward。
    */
    template <class T>
    void enqueue(T&& f) {
        // 向任务队列中添加一个任务，并通知一个线程去执行
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            tasks.emplace_back(std::forward<T>(f));
        }
        condition.notify_one();
    }

    // template <class T>
    // void enqueue(T f) {
    //     // 向任务队列中添加一个任务，并通知一个线程去执行
    //     {
    //         std::unique_lock<std::mutex> lock(queue_mutex);
    //         tasks.emplace_back(f);
    //     }
    //     condition.notify_one();
    // }

private:
    std::list<std::function<void()>> tasks; // 任务队列(线程共享资源,操作前需要先上锁)
    std::vector<std::thread> threads;       // 工作线程组
    std::mutex queue_mutex;                 // 保护任务队列的互斥锁
    std::condition_variable condition;      // 控制线程等待和唤醒的条件变量
    bool stop;                              // 标记线程池是否停止(线程共享资源,操作前需要先上锁)
};

int main() {
    ThreadPool pool(4); // 创建线程池，指定线程数为4

    for (int i = 0; i < 8; i++) {
        pool.enqueue([i] {
            // 每个任务都是一个lambda表达式，输出线程ID和任务序号，并在1秒后输出任务完成的消息
            std::cout << "Thread " << std::this_thread::get_id() << " processing task " << i << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(1));
            std::cout << "Thread " << std::this_thread::get_id() << " finished task " << i << std::endl;
        });
    }

    return 0;
}
