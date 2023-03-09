#ifndef WEBSERVER_H
#define WEBSERVER_H
// #define TERMINAL_DEBUG
#include <arpa/inet.h>
#include <cassert>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "./http/http_conn.h"
#include "./threadpool/threadpool.h"

class WebServer {
public:
    WebServer();
    ~WebServer();

    void init(int port, string mysql_host, int mysql_port, string user,
              string passWord, string databaseName, int log_write, int opt_linger,
              int trigmode, int sql_num, int thread_num, int close_log,
              int actor_model);

    void thread_pool();
    void sql_pool();
    void log_write();
    void trig_mode();
    void eventListen();
    void eventLoop();
    void timer(int connfd, struct sockaddr_in client_address);
    bool dealclientdata();
    bool dealwithsignal(bool& timeout, bool& stop_server);
    void dealwithread(int sockfd);
    void dealwithwrite(int sockfd);

public:
    // 基础
    int m_port;
    char* m_root;
    int m_log_write;
    int m_close_log;
    int m_actormodel;

    int m_pipefd[2];
    int m_epollfd;
    http_conn* users; // 维护与服务端建立连接的所有客户端

    // 数据库相关
    connection_pool* m_connPool;
    string m_mysql_host;   // 登陆数据库host
    int m_mysql_port;      // 数据库端口号
    string m_user;         // 登陆数据库用户名
    string m_passWord;     // 登陆数据库密码
    string m_databaseName; // 使用数据库名
    int m_sql_num;

    // 线程池相关
    threadpool<http_conn>* m_pool;
    int m_thread_num;

    // epoll_event相关:用于存储epoll事件表中就绪事件的event数组
    epoll_event events[MAX_EVENT_NUMBER];

    int m_listenfd;
    int m_OPT_LINGER;
    int m_TRIGMode;
    int m_LISTENTrigmode;
    int m_CONNTrigmode;
    // Utils utils;
};
#endif
