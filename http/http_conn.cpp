#include <fstream>
#include <mysql/mysql.h>
#include "http_conn.h"

// 定义http响应的一些状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file form this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the request file.\n";

locker m_lock;
map<string, string> users; // 用户名和密码

// http_conn静态变量初始化
int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;
Utils* http_conn::utils = new Utils();

int setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

void http_conn::initmysql_result(connection_pool* connPool) {
    // 先从连接池中取一个连接
    MYSQL* mysql = NULL;
    connectionRAII mysqlcon(&mysql, connPool);

    // 在user表中检索username，passwd数据，浏览器端输入
    if (mysql_query(mysql, "SELECT username,passwd FROM user")) {
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
    }

    // 从表中检索完整的结果集
    MYSQL_RES* result = mysql_store_result(mysql);
    if (!result) {
        LOG_ERROR("MySQL store result failed~");
        return;
    }

    // 返回结果集中的列数
    int num_fields = mysql_num_fields(result);

    // 返回所有字段结构的数组
    MYSQL_FIELD* fields = mysql_fetch_fields(result);

    // 从结果集中获取下一行，将对应的用户名和密码，存入map中
    while (MYSQL_ROW row = mysql_fetch_row(result)) {
        string temp1(row[0]);
        string temp2(row[1]);
        users[temp1] = temp2;
    }
}

// 将内核事件表注册读事件，ET模式下开启EPOLLONESHOT
// 与Utils::addfd重复了
void addfd(int epollfd, int fd, bool one_shot, int TRIGMode) {
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP;

    if (one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

// 从内核时间表删除描述符
void removefd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

// 将事件重置为EPOLLONESHOT
void modfd(int epollfd, int fd, int ev, int TRIGMode) {
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode)
        event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    else
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;

    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

// 关闭连接，关闭一个连接，客户总量减一
void http_conn::close_conn(bool real_close) {
    if (real_close && (m_sockfd != -1)) {
        LOG_INFO("close %d\n", m_sockfd);
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

// 初始化连接,外部调用初始化套接字地址
void http_conn::init(int connfd, const sockaddr_in& addr, char* root, int TRIGMode,
                     int close_log, string user, string passwd, string sqlname, int actor_mode) {
    m_sockfd = connfd; // 已连接描述符connfd
    m_address = addr;  // 客户端socket信息(ip:port)

    // 将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
    // 监听已连接描述符，开启EPOLLONESHOT，因为我们希望每个socket在任意时刻都只被一个线程处理
    // connfd也被注册到了内核事件表中，可以由eventLoop的epoll_wait()捕捉到
    addfd(http_conn::m_epollfd, connfd, true, m_TRIGMode);
    http_conn::m_user_count++;

    // 当浏览器出现连接重置时，可能是网站根目录出错或http响应格式出错或者访问的文件中内容完全为空
    doc_root = root;
    m_TRIGMode = TRIGMode;
    m_close_log = close_log;

    strcpy(sql_user, user.c_str());
    strcpy(sql_passwd, passwd.c_str());
    strcpy(sql_name, sqlname.c_str());

    // users_timer = new client_data();
    // users_timer->timer = new util_timer();
    m_actor_mode = actor_mode;

    http_conn::init(); // 另一个init重载,设置默认值
}

// 初始化新接受的连接
// check_state默认为分析请求行状态
void http_conn::init() {
    mysql = NULL;
    bytes_to_send = 0;
    bytes_have_send = 0;
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    cgi = 0;
    m_state = 0;
    // users_timer = new client_data();
    // timer_flag = 0;
    // improv = 0;

    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}

// 从状态机，用于分析出一行内容
// 返回值为行的读取状态，有LINE_OK,LINE_BAD,LINE_OPEN
http_conn::LINE_STATUS http_conn::parse_line() {
    char temp;
    for (; m_checked_idx < m_read_idx; ++m_checked_idx) {
        temp = m_read_buf[m_checked_idx];
        if (temp == '\r') {
            if ((m_checked_idx + 1) == m_read_idx)
                return LINE_OPEN;
            else if (m_read_buf[m_checked_idx + 1] == '\n') {
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        } else if (temp == '\n') {
            if (m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r') {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

// read_once读取浏览器端发送来的请求报文，直到无数据可读或对方关闭连接，读取到m_read_buffer中，并更新m_read_idx
// 非阻塞ET工作模式下，需要一次性将数据读完
bool http_conn::read_once() {
    if (m_read_idx >= READ_BUFFER_SIZE) {
        return false;
    }
    int bytes_read = 0;

    // LT读取数据
    if (0 == m_TRIGMode) {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        m_read_idx += bytes_read;

        if (bytes_read <= 0) {
            return false;
        }

        return true;
    }
    // ET读数据
    else {
        while (true) {
            bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
            if (bytes_read == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    break;
                return false;
            } else if (bytes_read == 0) {
                return false;
            }
            m_read_idx += bytes_read;
        }
        return true;
    }
}

// 解析http请求行，获得请求方法，目标url及http版本号
// GET / HTTP/1.1
// POST /2CGISQL.cgi HTTP/1.1
http_conn::HTTP_CODE http_conn::parse_request_line(char* text) {
    LOG_INFO("request line:%s", text);
    m_url = strpbrk(text, " \t"); // 返回text中第一次出现" "或\t的位置
    if (!m_url) {
        return BAD_REQUEST;
    }
    *m_url++ = '\0';
    char* method = text;
    if (strcasecmp(method, "GET") == 0)
        m_method = GET;
    else if (strcasecmp(method, "POST") == 0) {
        m_method = POST;
        cgi = 1;
    } else
        return BAD_REQUEST;

    m_url += strspn(m_url, " \t"); // m_url中第一个不在字符串" \t"中出现的字符下标
    m_version = strpbrk(m_url, " \t");
    if (!m_version)
        return BAD_REQUEST;
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");
    if ((strcasecmp(m_version, "HTTP/1.1") != 0) && strcasecmp(m_version, "HTTP/1.0") != 0)
        return BAD_REQUEST;
    if (strncasecmp(m_url, "http://", 7) == 0) { // 用来比较参数s1 和s2 字符串前n个字符，比较时会自动忽略大小写的差异
        m_url += 7;
        m_url = strchr(m_url, '/'); // 一个串中查找给定字符的第一个匹配之处
    }

    if (strncasecmp(m_url, "https://", 8) == 0) {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }

    if (!m_url || m_url[0] != '/')
        return BAD_REQUEST;

    // 当url为/时，显示欢迎界面
    if (strlen(m_url) == 1)
        strcat(m_url, "judge.html");

    // 请求行处理完毕，将主状态机转移处理请求头
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

// 解析http请求的一个头部和空行信息
http_conn::HTTP_CODE http_conn::parse_headers(char* text) {
    if (text[0] == '\0') {                       // 空行
        if (m_content_length != 0) {             // 判断是GET还是POST请求
            m_check_state = CHECK_STATE_CONTENT; // POST需要跳转到消息体处理状态
            return NO_REQUEST;                   // POST还需继续请求
        }
        return GET_REQUEST;
    } else if (strncasecmp(text, "Connection:", 11) == 0) {
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0) {
            m_linger = true; // 如果是长连接，则将linger标志设置为true
        }
    } else if (strncasecmp(text, "Content-length:", 15) == 0) {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    } else if (strncasecmp(text, "Host:", 5) == 0) {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    } else {
        LOG_INFO("oop!unknow header: %s", text);
    }
    return NO_REQUEST;
}

// 判断http请求是否被完整读入
http_conn::HTTP_CODE http_conn::parse_content(char* text) {
    if (m_read_idx >= (m_content_length + m_checked_idx)) {
        text[m_content_length] = '\0';
        // POST请求中最后为输入的用户名和密码
        m_string = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

// 解析用户HTTP请求
http_conn::HTTP_CODE http_conn::process_read() {
    // 初始化从状态机状态、HTTP请求解析结果
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = 0;

    // parse_line为从状态机的具体实现
    // m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK是POST请求解析content的判断逻辑
    // 因为此时不能通过parse_line()处理,POST请求末尾不是"\r\n"
    while ((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) || ((line_status = parse_line()) == LINE_OK)) {
        text = get_line(); // 获取一整行字符

        // m_start_line是每一个数据行在m_read_buf中的起始位置
        // m_checked_idx表示从状态机在m_read_buf中读取的位置
        m_start_line = m_checked_idx;
        // LOG_INFO("[process_read->get_line()]: %s", text);

        // 主状态机的三种状态转移逻辑
        switch (m_check_state) {
        case CHECK_STATE_REQUESTLINE: {
            ret = parse_request_line(text); // 解析请求行
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            break;
        }
        case CHECK_STATE_HEADER: {
            ret = parse_headers(text); // 解析请求头
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            else if (ret == GET_REQUEST) {
                return do_request(); // 完整解析GET请求后，跳转到报文响应函数
            }
            // POST请求会返回NO_REQUEST,触发break
            break;
        }
        case CHECK_STATE_CONTENT: {
            ret = parse_content(text); // 解析消息体
            if (ret == GET_REQUEST)
                return do_request(); // 完整解析POST请求后，跳转到报文响应函数
            // 将line_status变量更改为LINE_OPEN,此时可以跳出循环,结束报文解析任务(解析出错了)
            line_status = LINE_OPEN;
            break;
        }
        default:
            return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::do_request() {
    strcpy(m_real_file, doc_root); // 将初始化的m_real_file赋值为网站根目录
    int len = strlen(doc_root);
    const char* p = strrchr(m_url, '/'); // 查找最后一次出现某个字符的位置

    // 处理cgi
    if (cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3')) { // POST 2-登陆校验  3-注册校验
        char* m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/");
        strcat(m_url_real, m_url + 2);
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
        free(m_url_real);

        // 将用户名和密码提取出来
        // user=123&passwd=123
        char name[100], password[100];
        int i;
        for (i = 5; m_string[i] != '&'; ++i)
            name[i - 5] = m_string[i];
        name[i - 5] = '\0';

        int j = 0;
        for (i = i + 10; m_string[i] != '\0'; ++i, ++j)
            password[j] = m_string[i];
        password[j] = '\0';

        if (*(p + 1) == '3') {
            // 如果是注册，先检测数据库中是否有重名的
            // 没有重名的，进行增加数据
            char* sql_insert = (char*)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");

            if (users.find(name) == users.end()) {
                m_lock.lock();
                int res = mysql_query(mysql, sql_insert);
                users.insert(pair<string, string>(name, password));
                m_lock.unlock();

                if (!res)
                    strcpy(m_url, "/log.html");
                else
                    strcpy(m_url, "/registerError.html");
            } else
                strcpy(m_url, "/registerError.html");
        }
        // 如果是登录，直接判断
        // 若浏览器端输入的用户名和密码在表中可以查找到，返回1，否则返回0
        else if (*(p + 1) == '2') {
            if (users.find(name) != users.end() && users[name] == password)
                strcpy(m_url, "/welcome.html");
            else
                strcpy(m_url, "/logError.html");
        }
    }

    if (*(p + 1) == '0') { // 注册
        char* m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/register.html");
        // 将网站目录和/register.html进行拼接，更新到m_real_file中
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    } else if (*(p + 1) == '1') { // 登陆
        char* m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/log.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    } else if (*(p + 1) == '5') {
        char* m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    } else if (*(p + 1) == '6') {
        char* m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    } else if (*(p + 1) == '7') {
        char* m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/fans.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    } else
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);

    if (stat(m_real_file, &m_file_stat) < 0)
        return NO_RESOURCE;

    if (!(m_file_stat.st_mode & S_IROTH)) // 判断文件的权限，是否可读，不可读则返回FORBIDDEN_REQUEST状态
        return FORBIDDEN_REQUEST;

    if (S_ISDIR(m_file_stat.st_mode))
        return BAD_REQUEST;

    // 以只读方式获取文件描述符，通过mmap将该文件映射到内存中
    int fd = open(m_real_file, O_RDONLY);
    m_file_address = (char*)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);

    return FILE_REQUEST; // 表示请求文件存在，且可以访问
}

void http_conn::unmap() {
    if (m_file_address) {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

// http写(从响应报文缓冲区/mmap内存映射区读取数据，将响应报文发送给浏览器端)
bool http_conn::write() {
#ifdef TERMINAL_DEBUG
    printf("开始准备发送连接 %s:%d 的响应报文\n", inet_ntoa(users_timer->address.sin_addr), users_timer->address.sin_port);
#endif
    int temp = 0;

    //若要发送的数据长度为0,表示响应报文为空，一般不会出现这种情况
    if (bytes_to_send == 0) {
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        init();
        return true;
    }

    int mv0_len = m_iv[0].iov_len;

    while (1) {
        // 将响应报文的状态行、消息头、空行和响应正文发送给浏览器端
        temp = writev(m_sockfd, m_iv, m_iv_count);

        if (temp < 0) {
            // 判断缓冲区是否满了
            if (errno == EAGAIN) {
                modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode); // 重新注册写事件
                return true;
            }
            // 如果发送失败，但不是缓冲区问题，取消映射
            unmap();
            return false;
        }

        bytes_have_send += temp;
        bytes_to_send -= temp;

        // 第一个iovec头部信息的数据已发送完，发送第二个iovec数据
        // if (bytes_have_send >= m_iv[0].iov_len) {
        //     // 不再继续发送头部信息
        //     m_iv[0].iov_len = 0;
        //     m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
        //     m_iv[1].iov_len = bytes_to_send;
        // } else {
        //     m_iv[0].iov_base = m_write_buf + bytes_have_send;
        //     // m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        //     m_iv[0].iov_len = m_iv[0].iov_len - temp;
        // }

        if (bytes_have_send >= mv0_len) {
            // 不再继续发送头部信息
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        } else {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - temp;
        }

        // 判断条件，数据已全部发送完
        if (bytes_to_send <= 0) {
            unmap();
            // // 在epoll树上重置EPOLLONESHOT事件
            // modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
#ifdef TERMINAL_DEBUG
            printf("连接 %s:%d 的响应报文发送完毕 是否为长连接: %d\n",
                   inet_ntoa(users_timer->address.sin_addr), users_timer->address.sin_port, m_linger);
#endif
            // 浏览器的请求为长连接
            if (m_linger) {
                // 在epoll树上重置EPOLLONESHOT事件
                modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
                http_conn::init(); // 重新初始化HTTP对象
                return true;
            }
            return false;
        }
    }
}

bool http_conn::add_response(const char* format, ...) {
    if (m_write_idx >= WRITE_BUFFER_SIZE) // 如果写入内容超出m_write_buf大小则报错
        return false;

    va_list arg_list;           // 定义可变参数列表
    va_start(arg_list, format); // 将变量arg_list初始化为传入参数

    // 将数据format从可变参数列表写入缓冲区写，返回写入数据的长度
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    // 如果写入的数据长度超过缓冲区剩余空间，则报错
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx)) {
        va_end(arg_list);
        return false;
    }

    m_write_idx += len; // 更新m_write_idx位置
    va_end(arg_list);   // 清空可变参列表

    LOG_INFO("[Response write_buf]: %s", m_write_buf);

    return true;
}

// 添加状态行：http/1.1 状态码 状态消息
bool http_conn::add_status_line(int status, const char* title) {
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

// 添加消息报头: content-length和connection
bool http_conn::add_headers(int content_len) {
    return add_content_length(content_len) && add_linger() && add_blank_line();
}
bool http_conn::add_content_length(int content_len) {
    return add_response("Content-Length:%d\r\n", content_len);
}
bool http_conn::add_linger() {
    return add_response("Connection:%s\r\n", (m_linger == true) ? "keep-alive" : "close");
}
bool http_conn::add_blank_line() {
    return add_response("%s", "\r\n");
}

bool http_conn::add_content_type() {
    return add_response("Content-Type:%s\r\n", "text/html");
}
bool http_conn::add_content(const char* content) {
    return add_response("%s", content);
}

// 服务器子线程调用process_write向m_write_buf中写入响应报文
bool http_conn::process_write(HTTP_CODE ret) {
    switch (ret) {
    case INTERNAL_ERROR: {
        add_status_line(500, error_500_title);
        add_headers(strlen(error_500_form));
        if (!add_content(error_500_form))
            return false;
        break;
    }
    case BAD_REQUEST: {
        add_status_line(404, error_404_title);
        add_headers(strlen(error_404_form));
        if (!add_content(error_404_form))
            return false;
        break;
    }
    case FORBIDDEN_REQUEST: {
        add_status_line(403, error_403_title);
        add_headers(strlen(error_403_form));
        if (!add_content(error_403_form))
            return false;
        break;
    }
    case FILE_REQUEST: {
        add_status_line(200, ok_200_title);
        // 如果请求的资源存在
        if (m_file_stat.st_size != 0) {
            add_headers(m_file_stat.st_size);
            // 第一个iovec指针指向响应报文缓冲区，长度指向m_write_idx
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            // 第二个iovec指针指向mmap返回的文件指针，长度指向文件大小
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            // 发送的全部数据为响应报文头部信息和文件大小
            bytes_to_send = m_write_idx + m_file_stat.st_size;
            return true;
        } else {
            // 如果请求的资源大小为0，则返回空白html文件
            const char* ok_string = "<html><body></body></html>";
            add_headers(strlen(ok_string));
            if (!add_content(ok_string))
                return false;
        }
    }
    default:
        return false;
    }

    // 除FILE_REQUEST状态外，其余状态只申请一个iovec，指向响应报文缓冲区
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}

// 处理HTTP请求的入口函数
// 各子线程通过process函数对任务进行处理，调用process_read函数和process_write函数分别完成报文解析与报文响应两个任务。
void http_conn::process() {

#ifdef PRINT_REQUEST_DATA
    std::cout << "---------- client " << inet_ntoa(m_address.sin_addr) << ":" << m_address.sin_port
              << " ------------" << std::endl
              << m_read_buf << std::endl
              << "-------------------------------------------" << std::endl;
#endif

    HTTP_CODE read_ret = process_read(); // 对我们读入该connfd读缓冲区的请求报文进行解析

#ifdef TERMINAL_DEBUG
    printf("[Debug][process] 连接 %s:%d 请求报文已解析完成\n",
           inet_ntoa(m_address.sin_addr), m_address.sin_port);
#endif

    // NO_REQUEST，表示请求不完整，需要继续接收请求数据
    // FILE_REQUEST,请求完成
    if (read_ret == NO_REQUEST) {

        LOG_INFO("client %s:%d request not finish, enter a new loop",
                 inet_ntoa(m_address.sin_addr), m_address.sin_port);

        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode); // 注册并监听读事件
        return;
    }

    // 调用process_write完成报文响应
    bool write_ret = process_write(read_ret);
    if (!write_ret) {
        close_conn();
    }

    LOG_DEBUG("client %s:%d process response data finished",
              inet_ntoa(m_address.sin_addr), m_address.sin_port);

#ifdef TERMINAL_DEBUG
    printf("[Debug][process] 连接 %s:%d 响应报文已构造完成，等待发送\n",
           inet_ntoa(m_address.sin_addr), m_address.sin_port);
#endif

    // 服务器子线程调用process_write完成响应报文，随后注册epollout事件。
    // 服务器主线程检测写事件，并调用http_conn::write函数将响应报文发送给浏览器端。
    modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
}

void http_conn::deal_timer_close_connection() {
    users_timer.timer->cb_func(&users_timer);
    // TODO: 断开客户端连接数需要删除定时器链表上的对应节点
    // 在Reoactor模式下这个过程是工作线程做的，然而定时器链表不是线程安全的，目前这个过程会产生Segment fault
    // 所以目前禁用了
    // 在Proactor模式下这个过程是主线程做的，正常工作

    if (m_actor_mode == 0) {
        // Proactor模式下, 主线程调用，删除客户连接定时器
        if (users_timer.timer) {
            utils->m_timer_lst.del_timer(users_timer.timer);
        }
    }
    LOG_INFO("close fd %d", users_timer.sockfd);
}

// 初始化客户端HTTP连接的定时器并加入定时器双向链表中
bool http_conn::http_timer_init() {
    users_timer.address = m_address;
    users_timer.sockfd = m_sockfd;

    util_timer* timer = new util_timer();
    timer->user_data = &users_timer;
    timer->cb_func = real_cb_func;
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    users_timer.timer = timer;

    utils->m_timer_lst.add_timer(timer);
    return true;
}

bool http_conn::adjust_timer() {
    if (users_timer.timer) {
        time_t cur = time(NULL);
        users_timer.timer->expire = cur + 3 * TIMESLOT;
        utils->m_timer_lst.adjust_timer(users_timer.timer);

        LOG_INFO("client: %d %s", m_sockfd, "adjust timer once");
        return true;
    }
    return false;
}

void http_conn::http_timer_handler() {
    utils->timer_handler();
}
