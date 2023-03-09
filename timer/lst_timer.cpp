#include "../http/http_conn.h"
#include "lst_timer.h"

//============================================
//=========== 定时器容器的成员函数实现 ===========
//============================================
sort_timer_lst::sort_timer_lst() {
    head = nullptr;
    tail = nullptr;
}
sort_timer_lst::~sort_timer_lst() {
    util_timer* tmp = head;
    while (tmp) {
        head = tmp->next;
        delete tmp;
        tmp = head;
    }
}

// 将目标定时器添加到链表中
void sort_timer_lst::add_timer(util_timer* timer) {
    if (!timer) {
        return;
    }
    if (!head) {
        head = tail = timer;
        return;
    }
    if (timer->expire < head->expire) {
        timer->next = head;
        head->prev = timer;
        head = timer;
        return;
    }
    add_timer(timer, head);
}

// 把timer插入到lst_head后面
void sort_timer_lst::add_timer(util_timer* timer, util_timer* lst_head) {
    util_timer* prev = lst_head;
    util_timer* tmp = prev->next;
    while (tmp) {
        if (timer->expire < tmp->expire) {
            prev->next = timer;
            timer->next = tmp;
            tmp->prev = timer;
            timer->prev = prev;
            break;
        }
        prev = tmp;
        tmp = tmp->next;
    }
    if (!tmp) {
        prev->next = timer;
        timer->prev = prev;
        timer->next = NULL;
        tail = timer;
    }
}
// 当定时任务发生变化,调整对应定时器在链表中的位置
void sort_timer_lst::adjust_timer(util_timer* timer) {
    if (!timer) {
        return;
    }
    util_timer* tmp = timer->next;
    if (!tmp || (timer->expire < tmp->expire)) {
        return;
    }
    // 删掉重新添加
    if (timer == head) {
        head = head->next;
        head->prev = NULL;
        timer->next = NULL;
        add_timer(timer, head);
    } else {
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        add_timer(timer, timer->next);
    }
}
// 将超时的定时器从链表中删除
void sort_timer_lst::del_timer(util_timer* timer) {
    if (!timer) {
        return;
    }
    if ((timer == head) && (timer == tail)) {
        delete timer;
        head = NULL;
        tail = NULL;
        return;
    }
    if (timer == head) {
        head = head->next;
        head->prev = NULL;
        delete timer;
        return;
    }
    if (timer == tail) {
        tail = tail->prev;
        tail->next = NULL;
        delete timer;
        return;
    }
    timer->prev->next = timer->next;
    timer->next->prev = timer->prev;
    delete timer;
}
// 定时任务处理函数
void sort_timer_lst::tick() {
    // 定时器容器为空
    if (head == nullptr) {
        return;
    }

    time_t cur = time(NULL); // 获取当前时间
    util_timer* tmp = head;
    while (tmp) {
        // 没有定时器超时
        if (cur < tmp->expire) {
            break;
        }
        tmp->cb_func(tmp->user_data); // 定时器超时，调用回调函数处理
        head = tmp->next;             // 从定时器容器里删除
        if (head) {
            head->prev = NULL;
        }
        delete tmp;
        tmp = head;
    }
}

//============================================
//============== 定时器回调函数 ================
//============================================
// 定时器回调函数 从内核事件表删除事件，关闭文件描述符，释放连接资源
void real_cb_func(client_data* user_data) {
    // 删除非活动连接在socket上的注册事件
    epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    close(user_data->sockfd);  // 关闭文件描述符
    http_conn::m_user_count--; // 减少连接数

#ifdef TERMINAL_DEBUG
    printf("[Debug] 用户 %s:%d 连接被删除, 服务器当前人数: %d\n", inet_ntoa(user_data->address.sin_addr),
           user_data->address.sin_port, http_conn::m_user_count);
#endif
}

//==============================================
//============== Utils类成员函数实现 =============
//==============================================
int* Utils::u_pipefd = 0;
int Utils::u_epollfd = 0;

void Utils::init(int timeslot) {
    m_TIMESLOT = timeslot;
}

int Utils::setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

// 将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void Utils::addfd(int epollfd, int fd, bool one_shot, int TRIGMode) {
    epoll_event _event;
    _event.data.fd = fd;

    if (1 == TRIGMode)
        // EPOLLRDHUP 可以作为一种读关闭的标志，注意不能读的意思内核不能再往内核缓冲区中增加新的内容
        // 已经在内核缓冲区中的内容，用户态依然能够读取到
        _event.events = EPOLLIN | EPOLLET | EPOLLRDHUP; // ET(epoll边沿触发)
    else
        _event.events = EPOLLIN | EPOLLRDHUP; // LT(默认，epoll水平触发)

    if (one_shot)
        _event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &_event); // 注册新的fd到epfd中，event指定了内核需要监听的事件
    setnonblocking(fd);
}
// 信号处理函数中仅仅通过管道发送信号值，不处理信号对应的逻辑，缩短异步执行时间，减少对主程序的影响
void Utils::sig_handler(int sig) {
    // 为保证函数的可重入性，保留原来的errno
    // 可重入性表示中断后再次进入该函数，环境变量与之前相同，不会丢失数据
    int save_errno = errno;
    int msg = sig;

    // 将信号值从管道写端写入，传输字符类型，而非整型
    send(u_pipefd[1], (char*)&msg, 1, 0);

    // 将原来的errno赋值为当前的errno
    errno = save_errno;
}
// 设置信号函数
void Utils::addsig(int sig, void(handler)(int), bool restart) {
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart)
        sa.sa_flags |= SA_RESTART;

    // 将所有信号添加到信号集中
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1); // 执行sigaction函数: sig参数指出要捕获的信号类型，sa参数指定新的信号处理方式
}
// 定时处理任务，重新定时以不断触发SIGALRM信号
void Utils::timer_handler() {
    m_timer_lst.tick();
    alarm(m_TIMESLOT);
}
void Utils::show_error(int connfd, const char* info) {
    send(connfd, info, strlen(info), 0);
    close(connfd);
}