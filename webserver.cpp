#include "webserver.h"

WebServer::WebServer() {
    // http_conn类对象
    users = new http_conn[MAX_FD];

    // root文件夹路径
    char server_path[200];
    getcwd(server_path, 200);
    char root[6] = "/root";
    WebServer::m_root = (char*)malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(m_root, server_path);
    strcat(m_root, root);

    // 定时器
    users_timer = new client_data[MAX_FD];
}

WebServer::~WebServer() {
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[1]);
    close(m_pipefd[0]);
    delete[] users;
    delete[] users_timer;
    delete m_pool;
}

void WebServer::init(int port, string mysql_host, int mysql_port, string user, string passWord, string databaseName, int log_write,
                     int opt_linger, int trigmode, int sql_num, int thread_num, int close_log, int actor_model) {
    m_mysql_host = mysql_host;
    m_mysql_port = mysql_port;
    m_port = port;
    m_user = user;
    m_passWord = passWord;
    m_databaseName = databaseName;
    m_sql_num = sql_num;
    m_thread_num = thread_num;
    m_log_write = log_write;
    m_OPT_LINGER = opt_linger;
    m_TRIGMode = trigmode;
    m_close_log = close_log;
    m_actormodel = actor_model;
}

void WebServer::trig_mode() {
    // LT + LT
    if (0 == m_TRIGMode) {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 0;
    }
    // LT + ET
    else if (1 == m_TRIGMode) {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 1;
    }
    // ET + LT
    else if (2 == m_TRIGMode) {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 0;
    }
    // ET + ET
    else if (3 == m_TRIGMode) {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 1;
    }
}

void WebServer::log_write() {
    // m_close_log 0:打开日志 1:关闭日志
    if (0 == m_close_log) {
        // 初始化日志 m_log_write 0:同步写入 1:异步写入
        if (1 == m_log_write)
            Log::get_instance()->init("./ServerLog", m_close_log, 3000, 800000, 800); // 异步需要设置阻塞队列长度
        else
            Log::get_instance()->init("./ServerLog", m_close_log, 3000, 800000, 0);
    }
}

void WebServer::sql_pool() {
    // 初始化数据库连接池
    m_connPool = connection_pool::GetInstance();
    m_connPool->init(m_mysql_host, m_user, m_passWord, m_databaseName, m_mysql_port, m_sql_num, m_close_log);

    // 初始化数据库读取表
    users->initmysql_result(m_connPool);
}

void WebServer::thread_pool() {
    // 线程池
    WebServer::m_pool = new threadpool<http_conn>(m_actormodel, m_connPool, m_thread_num);
}

void WebServer::timer(int connfd, struct sockaddr_in client_address) {
    // 用已连接描述符connfd来标识用户的HTTP连接,这里初始化用户连接
    WebServer::users[connfd].init(connfd, client_address, m_root, m_CONNTrigmode, m_close_log, m_user, m_passWord, m_databaseName);

    // 初始化client_data数据
    // 创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
    users_timer[connfd].address = client_address;
    users_timer[connfd].sockfd = connfd;
    util_timer* timer = new util_timer;
    timer->user_data = &users_timer[connfd];
    timer->cb_func = cb_func;
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    users_timer[connfd].timer = timer;
    utils.m_timer_lst.add_timer(timer);
}

// 若有数据传输，则将定时器往后延迟3个单位
// 并对新的定时器在链表上的位置进行调整
void WebServer::adjust_timer(util_timer* timer) {
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    utils.m_timer_lst.adjust_timer(timer);

    LOG_INFO("%s", "adjust timer once");
}

// [断开连接]处理异常事件,调用定时器对应的回调函数（从内核事件表删除事件，关闭文件描述符，释放连接资源），删除定时器
void WebServer::deal_timer(util_timer* timer, int sockfd) {
    timer->cb_func(&users_timer[sockfd]);
    if (timer) {
        utils.m_timer_lst.del_timer(timer);
    }

    LOG_INFO("close fd %d", users_timer[sockfd].sockfd);
}

bool WebServer::dealclientdata() {
    struct sockaddr_in client_address; // 用于保存客户端socket信息
    socklen_t client_addrlength = sizeof(client_address);
    // epoll的LT（默认）和ET
    // LT 持续通知直到处理事件完毕
    // ET 只通知一次，不管事件是否处理完毕,可以使得就绪队列上的新的就绪事件能被快速处理,可以避免共享epoll_fd场景下，发生类似惊群问题。
    if (0 == m_LISTENTrigmode) // LT
    {
        int connfd = accept(m_listenfd, (struct sockaddr*)&client_address, &client_addrlength);
        if (connfd < 0) {
            LOG_ERROR("%s:errno is:%d", "accept error", errno);
            return false;
        }
        if (http_conn::m_user_count >= MAX_FD) {
            utils.show_error(connfd, "Internal server busy");
            LOG_ERROR("%s", "Internal server busy");
            return false;
        }
        WebServer::timer(connfd, client_address);
    } else // ET
    {
        while (1) {
            int connfd = accept(m_listenfd, (struct sockaddr*)&client_address, &client_addrlength);
            if (connfd < 0) {
                LOG_ERROR("%s:errno is:%d", "accept error", errno);
                break;
            }
            if (http_conn::m_user_count >= MAX_FD) {
                utils.show_error(connfd, "Internal server busy");
                LOG_ERROR("%s", "Internal server busy");
                break;
            }
            WebServer::timer(connfd, client_address);
        }
        return false;
    }
    return true;
}

bool WebServer::dealwithsignal(bool& timeout, bool& stop_server) {
    int ret = 0;
    int sig;
    char signals[1024];

    // 从管道读端读出信号值，成功返回字节数，失败返回-1
    // 正常情况下，这里的ret返回值总是1，只有14和15两个ASCII码对应的字符
    ret = recv(m_pipefd[0], signals, sizeof(signals), 0);
    if (ret == -1) {
        return false;
    } else if (ret == 0) {
        return false;
    } else {
        for (int i = 0; i < ret; ++i) {
            switch (signals[i]) {
            case SIGALRM: {
                timeout = true;
                break;
            }
            case SIGTERM: {
                stop_server = true;
                break;
            }
            }
        }
    }
    return true;
}

void WebServer::dealwithread(int sockfd) {
    util_timer* timer = users_timer[sockfd].timer;

    // reactor
    if (1 == m_actormodel) {
        LOG_INFO("[Reactor Mode] deal with the client(%s) read request", inet_ntoa(users[sockfd].get_address()->sin_addr));

        // 若监测到读事件，将该事件放入请求队列
        WebServer::m_pool->append(WebServer::users + sockfd, 0);

        if (timer) {
            adjust_timer(timer);
        }

        // TODO 工作线程将读/写请求队列上的连接里的数据读进去read_buf里循环才结束
        // ?
        // https://github.com/qinguoyi/TinyWebServer/issues/70
        while (true) {
            if (1 == users[sockfd].improv) {
                if (1 == users[sockfd].timer_flag) {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    } else {
        // proactor
        if (users[sockfd].read_once()) { // 读入对应缓冲区
            LOG_INFO("[Proactor Mode] deal with the client(%s) read request", inet_ntoa(users[sockfd].get_address()->sin_addr));

            // 若监测到读事件，将该事件放入请求队列
            WebServer::m_pool->append_p(users + sockfd);

            if (timer) {
                adjust_timer(timer);
            }

        } else {
            deal_timer(timer, sockfd);
        }
    }
}

void WebServer::dealwithwrite(int sockfd) {
    util_timer* timer = users_timer[sockfd].timer;
    // reactor
    if (1 == m_actormodel) {
        LOG_INFO("[Reactor Mode] append client(%s) to response queue", inet_ntoa(users[sockfd].get_address()->sin_addr));
        if (timer) {
            adjust_timer(timer);
        }

        // append()的status为1表示不需要解析HTTP报文，直接将HTTP响应报文发送给客户端
        WebServer::m_pool->append(users + sockfd, 1);

        // TODO 这里主线程将http_conn加入了读写任务请求队列中，并直接将HTTP响应报文发送给客户端
        while (true) {
            if (1 == users[sockfd].improv) {
                if (1 == users[sockfd].timer_flag) {
                    // 关闭连接
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    } else {
        // proactor
        if (users[sockfd].write()) {
            LOG_INFO("[Proactor Mode] send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));
            if (timer) {
                adjust_timer(timer);
            }
        } else {
            deal_timer(timer, sockfd);
        }
    }
}

void WebServer::eventListen() {
    // 网络编程基础步骤(创建TCP套接字)
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(m_listenfd >= 0);

    // 优雅关闭连接
    if (0 == m_OPT_LINGER) {
        struct linger tmp = { 0, 1 };
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    } else if (1 == m_OPT_LINGER) {
        struct linger tmp = { 1, 1 };
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(m_port);

    // 设置地址复用
    int flag = 1;
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));

    ret = bind(m_listenfd, (struct sockaddr*)&address, sizeof(address));
    assert(ret >= 0);
    ret = listen(m_listenfd, 5);
    assert(ret >= 0);

    utils.init(TIMESLOT);

    // 创建一个额外的文件描述符来唯一标识内核中的epoll事件表
    m_epollfd = epoll_create(5);
    assert(m_epollfd != -1);

    // 调用epoll_ctl(),将内核事件表注册读事件，ET模式，将m_listenfd设置为非阻塞，当listen到新的客户连接时，listenfd变为就绪事件
    utils.addfd(m_epollfd, m_listenfd, false, m_LISTENTrigmode); // false表示不开启EPOLLONESHOT
    http_conn::m_epollfd = m_epollfd;

    /* 用于信号通知逻辑 */
    // socketpair创建全双工管道，创建好的套接字分别是m_pipefd[0](读)和m_pipefd[1](写),这对套接字可以用于全双工通信
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, WebServer::m_pipefd);
    assert(ret != -1);
    // send是将信息发送给套接字缓冲区，如果缓冲区满了，则会阻塞，这时候会进一步增加信号处理函数的执行时间，为此，将其修改为非阻塞。
    utils.setnonblocking(m_pipefd[1]);

    // 设置管道读端为LT非阻塞,向内核事件表注册
    utils.addfd(m_epollfd, m_pipefd[0], false, 0);

    utils.addsig(SIGPIPE, SIG_IGN); // 程序员收到SIGPIPE信号不会退出，直接把这个信号忽略掉
    utils.addsig(SIGALRM, utils.sig_handler, false);
    utils.addsig(SIGTERM, utils.sig_handler, false); // SIGTERM信号表示终端发送到程序的终止请求

    // //每隔TIMESLOT时间触发SIGALRM信号给目前的进程
    alarm(TIMESLOT);

    // 工具类,信号和描述符基础操作
    Utils::u_pipefd = m_pipefd;
    Utils::u_epollfd = m_epollfd;
}

void WebServer::eventLoop() {
    bool timeout = false; // 超时标志
    bool stop_server = false;

    while (!stop_server) {
        int number = epoll_wait(WebServer::m_epollfd, WebServer::events, MAX_EVENT_NUMBER, -1);
        // 当系统调用被信号中断时，会返回 EINTR 错误。这个错误通常是由于系统调用被信号中断，而不是由于程序本身的错误。可能是因为时钟信号。
        if (number < 0 && errno != EINTR) {
            LOG_ERROR("%s", "epoll failure");
            break;
        }

        // 处理就绪的文件描述符
        for (int i = 0; i < number; i++) {
            int sockfd = events[i].data.fd; // 事件表中就绪的socket文件描述符

            // 客户连接事件:创建新连接，初始化定时器
            if (sockfd == WebServer::m_listenfd) {
                dealclientdata();
            }
            // 异常事件:服务器端关闭连接，移除对应的定时器
            // EPOLLRDHUP和EPOLLHUP可能是服务端关闭连接导致的, EPOLLERR表示用户连接描述符发生错误
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                util_timer* timer = users_timer[sockfd].timer;
                deal_timer(timer, sockfd);
            }
            // 信号事件:管道读端对应文件描述符发生读事件
            else if ((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN)) {
                // SIGALRM和SIGTERM信号
                bool flag = dealwithsignal(timeout, stop_server);
                if (false == flag)
                    LOG_ERROR("%s", "deal with signal failure");
            }
            // 读写事件:处理用户连接上接收到的数据（此时sockfd == connfd）
            else if (events[i].events & EPOLLIN) {
                dealwithread(sockfd);
            } else if (
                events[i].events & EPOLLOUT) { // 服务器子线程调用process_write完成响应报文，随后注册epollout事件
                dealwithwrite(sockfd);
            }
        }

        // SIGALARM信号来临时,timeout=true
        // 处理定时器为非必须事件，收到信号并不是立马处理,完成读写事件后，再进行处理
        if (timeout) {
            utils.timer_handler(); // 调用定时任务处理函数,从定时任务容器里删除过期的定时器，清理连接
            // LOG_INFO("%s", "timer tick");
            timeout = false;
        }
    }
}