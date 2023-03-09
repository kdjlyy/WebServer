#ifndef LST_TIMER
#define LST_TIMER
// #define TERMINAL_DEBUG
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <mutex>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>

#include <time.h>
#include "../log/log.h"

class util_timer;

// 连接资源
struct client_data {
    sockaddr_in address; // 客户端socket地址
    int sockfd;          // socket文件描述符
    util_timer* timer;   // 定时器
};

// 定时器类
class util_timer {
public:
    util_timer() : user_data(nullptr), prev(nullptr), next(nullptr) {}

public:
    time_t expire;                 // 超时时间
    void (*cb_func)(client_data*); // 定时器回调函数
    client_data* user_data;        // 连接资源
    util_timer* prev;              // 前向定时器
    util_timer* next;              // 后继定时器
};

// 定时器容器类(双向链表)
class sort_timer_lst {
public:
    sort_timer_lst();
    ~sort_timer_lst();

    void add_timer(util_timer* timer);    // 将目标定时器添加到链表中
    void adjust_timer(util_timer* timer); // 当定时任务发生变化,调整对应定时器在链表中的位置
    void del_timer(util_timer* timer);    // 将超时的定时器从链表中删除
    void tick();

private:
    void add_timer(util_timer* timer, util_timer* lst_head);

    util_timer* head;
    util_timer* tail;
};

class Utils {
public:
    Utils() {}
    ~Utils() {}

    void init(int timeslot);

    // 对文件描述符设置非阻塞
    static int setnonblocking(int fd);
    // 将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
    static void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);
    // 信号处理函数
    static void sig_handler(int sig);
    // 设置信号函数
    static void addsig(int sig, void(handler)(int), bool restart = true);

    // 定时处理任务，重新定时以不断触发SIGALRM信号
    void timer_handler();

    static void show_error(int connfd, const char* info);

public:
    static int* u_pipefd;
    sort_timer_lst m_timer_lst;
    static int u_epollfd;
    int m_TIMESLOT;
};

// 定时器回调函数
void real_cb_func(client_data* user_data);

#endif
