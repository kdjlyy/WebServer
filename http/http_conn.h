// #define PRINT_REQUEST_DATA // 输出用户请求元数据

#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <map>
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

#include "../CGImysql/sql_connection_pool.h"
#include "../lock/locker.h"
#include "../log/log.h"
#include "../timer/lst_timer.h"

class http_conn {
public:
    static const int FILENAME_LEN = 200;       // 设置读取文件的名称m_real_file大小
    static const int READ_BUFFER_SIZE = 2048;  // 设置读缓冲区m_read_buf大小
    static const int WRITE_BUFFER_SIZE = 1024; // 设置写缓冲区m_write_buf大小

    // 报文的请求方法，本项目只用到GET和POST
    enum METHOD {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };

    // 主状态机的状态
    enum CHECK_STATE {
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };

    // 报文解析的结果
    enum HTTP_CODE {
        NO_REQUEST,  // 请求不完整，需要继续读取请求报文数据
        GET_REQUEST, // 获得了完整的HTTP请求
        BAD_REQUEST, // HTTP请求报文有语法错误
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,   // 表示请求文件存在，且可以访问
        INTERNAL_ERROR, // 服务器内部错误，该结果在主状态机逻辑switch的default下，一般不会触发
        CLOSED_CONNECTION
    };

    // 从状态机的状态
    enum LINE_STATUS {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };

public:
    http_conn() {}
    ~http_conn() {}

public:
    // 初始化套接字地址，函数内部会调用私有方法init
    void init(int sockfd, const sockaddr_in& addr, char* root, int TRIGMode, int close_log,
              string user, string passwd, string sqlname);
    void close_conn(bool real_close = true); // 关闭http连接
    void process();
    bool read_once(); // 读取浏览器端发来的全部数据
    bool write();     // 响应报文写入函数
    sockaddr_in* get_address() {
        return &m_address;
    }
    void initmysql_result(connection_pool* connPool); // 同步线程初始化数据库读取表

    /*
     * 两个标志位的作用就是：Reactor模式下，当子线程执行读写任务出错时，来通知主线程关闭子线程的客户连接
     * 对于improv标志，其作用是保持主线程和子线程的同步；
     * 对于time_flag标志，其作用是标识子线程读写任务是否成功
    */
    int timer_flag;
    int improv;

private:
    void init();
    HTTP_CODE process_read();                 // 从m_read_buf读取，并处理请求报文
    bool process_write(HTTP_CODE ret);        // 向m_write_buf写入响应报文数据
    HTTP_CODE parse_request_line(char* text); // 主状态机解析报文中的请求行数据
    HTTP_CODE parse_headers(char* text);      // 主状态机解析报文中的请求头数据
    HTTP_CODE parse_content(char* text);      // 主状态机解析报文中的请求内容
    HTTP_CODE do_request();                   // 生成响应报文

    // m_start_line是已经解析的字符，get_line用于将指针向后偏移，指向未处理的字符
    char* get_line() { return m_read_buf + m_start_line; };
    LINE_STATUS parse_line(); // 从状态机读取一行，分析是请求报文的哪一部分
    void unmap();

    // 根据响应报文格式，生成对应8个部分，以下函数均由do_request调用
    bool add_response(const char* format, ...);
    bool add_content(const char* content);
    bool add_status_line(int status, const char* title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger(); // 用于控制TCP连接的关闭方式：Connection:keep-alive或close
    bool add_blank_line();

public:
    static int m_epollfd; // 调用epoll_create()创建的句柄
    static int m_user_count;
    MYSQL* mysql;
    int m_state; // 读为0, 写为1

private:
    int m_sockfd;
    sockaddr_in m_address;

    char m_read_buf[READ_BUFFER_SIZE]; // 存储读取的请求报文数据
    int m_read_idx;                    // 缓冲区中m_read_buf中数据的最后一个字节的下一个位置
    int m_checked_idx;                 // m_read_buf读取的位置m_checked_idx
    int m_start_line;                  // m_read_buf中已经解析的字符个数

    char m_write_buf[WRITE_BUFFER_SIZE]; // 存储发出的响应报文数据
    int m_write_idx;                     // 指示m_write_buf中的长度

    CHECK_STATE m_check_state; // 主状态机的状态
    METHOD m_method;           // 请求方法

    // 以下为解析请求报文中对应的6个变量
    char m_real_file[FILENAME_LEN]; // 存储读取文件的名称
    char* m_url;
    char* m_version;
    char* m_host;
    int m_content_length;
    bool m_linger;

    char* m_file_address;    // 读取服务器上的文件地址
    struct stat m_file_stat; // 用于描述文件的属性，包括文件的大小、修改时间等

    struct iovec m_iv[2]; // io向量机制iovec，为了减少系统调用的次数，采取用于分散读和集中写的函数，readv和writev
    int m_iv_count;

    int cgi;             // 是否启用的POST
    char* m_string;      // 存储请求头数据
    int bytes_to_send;   // 剩余发送字节数
    int bytes_have_send; // 已发送字节数
    char* doc_root;

    map<string, string> m_users;
    int m_TRIGMode;
    int m_close_log;

    char sql_user[100];
    char sql_passwd[100];
    char sql_name[100];
};

#endif
