#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <cstdio>
#include <exception>
#include <list>
#include <pthread.h>
#include "../http/http_conn.h"
#include "../lock/locker.h"
#include "../mysql/sql_connection_pool.h"

// 类模版
template <typename T>
class threadpool {
public:
    /*thread_number是线程池中线程的数量，max_requests是请求队列中最多允许的、等待处理的请求的数量，connPool是数据库连接池指针*/
    threadpool(int actor_model, connection_pool* connPool, int thread_number = 8, int max_request = 10000);
    ~threadpool();

    // 向请求队列中插入任务请求
    bool append(T* request, int state);
    bool append_p(T* request);

private:
    // 线程处理函数和运行函数设置为私有属性
    // 工作线程运行的函数，它不断从工作队列中取出任务并执行之
    static void* worker(void* arg);
    void run();

private:
    int m_thread_number;         // 线程池中的线程数
    int m_max_requests;          // 请求队列中允许的最大请求数
    pthread_t* m_threads;        // 描述线程池的数组，其大小为m_thread_number
    std::list<T*> m_workqueue;   // 请求队列
    locker m_queuelocker;        // 保护请求队列的互斥锁(RAII)
    sem m_queuestat;             // 是否有任务需要处理的信号量
    connection_pool* m_connPool; // 数据库连接池
    int m_actor_model;           // 模型切换
};

// threadpool constructor
// 创建m_thread_number个线程，线程ID存放在m_threads数组里，并把线程设置为脱离状态
template <typename T>
threadpool<T>::threadpool(int actor_model, connection_pool* connPool, int thread_number, int max_requests)
    : m_actor_model(actor_model), m_thread_number(thread_number), m_max_requests(max_requests), m_threads(nullptr), m_connPool(connPool) {
    if (thread_number <= 0 || max_requests <= 0)
        throw std::exception();
    m_threads = new pthread_t[m_thread_number]; // m_threads[i]存储线程ID
    if (!m_threads)
        throw std::exception();

    for (int i = 0; i < thread_number; ++i) {
        // 循环创建线程，并将工作线程按要求进行运行
        // 具体的，类对象传递时用this指针，传递给静态函数worker后，将其转换为线程池类，并调用私有成员函数run。
        if (pthread_create(m_threads + i, nullptr, worker, this) != 0) {
            delete[] m_threads;
            throw std::exception();
        }
        if (pthread_detach(m_threads[i])) // 设置为脱离状态，线程的资源可以被系统自动回收
        {
            delete[] m_threads;
            throw std::exception();
        }
    }
}

template <typename T>
threadpool<T>::~threadpool() {
    delete[] m_threads;
}

// 向请求队列中添加任务
// 通过list容器创建请求队列，向队列中添加时，通过互斥锁保证线程安全，添加完成后通过信号量提醒有任务要处理，最后注意线程同步。
template <typename T>
bool threadpool<T>::append(T* request, int state) {
    m_queuelocker.lock();

    if (m_workqueue.size() >= m_max_requests) {
        m_queuelocker.unlock();
        return false;
    }
    request->m_state = state;
    m_workqueue.push_back(request); // 添加任务
    m_queuelocker.unlock();

    m_queuestat.post(); // 信号量提醒有任务要处理 V操作
    return true;
}

template <typename T>
bool threadpool<T>::append_p(T* request) {
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests) {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

// 线程处理函数
template <typename T>
void* threadpool<T>::worker(void* arg) {
    // 将参数强转为线程池类类型，调用成员方法
    threadpool* pool = (threadpool*)arg;
    pool->run();
    return pool;
}

// run执行任务
// 主要实现，工作线程从请求队列中取出某个任务进行处理，注意线程同步。
template <typename T>
void threadpool<T>::run() {
    while (true) {
        m_queuestat.wait();   // 信号量等待 P操作
        m_queuelocker.lock(); // 被唤醒后先加互斥锁,保证请求队列线程安全
        if (m_workqueue.empty()) {
            m_queuelocker.unlock();
            continue;
        }

        // 从请求队列中取出第一个任务并删除
        T* request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();

        if (!request)
            continue;

        // Reactor模型(主线程只负责监听文件描述符上是否有事件发生，有的话立即通知工作线程读写数据、接受新连接及处理客户请求)
        if (1 == m_actor_model) {
            // http_conn::m_state; 读HTTP报文并将响应报文发送给浏览器端为0, 直接将响应报文发送给浏览器端为1
            if (0 == request->m_state) {
                if (request->read_once()) // 循环读取客户数据，直到无数据可读或对方关闭连接
                {
                    // request->improv = 1;
                    connectionRAII mysqlcon(&request->mysql, m_connPool);
                    request->process(); // http_conn::process() 处理HTTP请求的入口函数
                } else {
                    // 读取客户端请求数据出错，需要关闭HTTP连接
                    request->deal_timer_close_connection();
                }
            } else { // 写
                // http写(从响应报文缓冲区/mmap内存映射区读取数据，将响应报文发送给浏览器端)
                if (!request->write()) {
                    // 短链接，需要关闭HTTP连接
                    request->deal_timer_close_connection();
                }
            }
        } else // Proactor模型(主线程和内核负责处理读写数据、接收新连接等I/O操作，工作线程仅负责业务逻辑，如处理客户请求)
        {
            connectionRAII mysqlcon(&request->mysql, m_connPool);
            request->process();
        }
    }
}
#endif
