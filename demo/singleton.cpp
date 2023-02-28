/*
 * 单例模式的懒汉和饿汉实现
 */
#include <bits/stdc++.h>
using namespace std;

// 最简单的懒汉模式：在全局访问入口中声明静态变量
// 局部静态变量在C++11后也是线程安全的
class Singleton {
private:
    Singleton();
    ~Singleton();
    Singleton(const Singleton& other) = delete;
    Singleton& operator=(const Singleton& t) = delete;

public:
    static Singleton* getInstance() {
        static Singleton instance;
        return &instance;
    }
};

// 加锁实现的懒汉单例模式
class Singleton2 {
private:
    static Singleton2* instance;
    static pthread_mutex_t mutex;

    Singleton2();
    ~Singleton2();
    Singleton2(const Singleton2& other) = delete;
    Singleton2& operator=(const Singleton2& t) = delete;

public:
    static Singleton2* getInstance() {
        pthread_mutex_lock(&mutex);
        if (instance == nullptr) {
            instance = new Singleton2();
        }
        pthread_mutex_unlock(&mutex);
        return instance;
    }
};
Singleton2* Singleton2::instance = nullptr;
pthread_mutex_t Singleton2::mutex = PTHREAD_MUTEX_INITIALIZER;

// 饿汉式单例，天生线程安全，因为在main函数前已经初始化
class Singleton3 {
private:
    static Singleton3* instance;

    Singleton3();
    ~Singleton3();
    Singleton3(const Singleton3& other) = delete;
    Singleton3& operator=(const Singleton3& t) = delete;

public:
    static Singleton3* getInstance() { return instance; }
};
Singleton3* Singleton3::instance = new Singleton3();
