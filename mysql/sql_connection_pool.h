#ifndef _CONNECTION_POOL_
#define _CONNECTION_POOL_

#include <error.h>
#include <iostream>
#include <list>
#include <mysql/mysql.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include "../lock/locker.h"
#include "../log/log.h"

using namespace std;

class connection_pool {
public:
    MYSQL* GetConnection();              // 获取数据库连接
    bool ReleaseConnection(MYSQL* conn); // 释放连接
    int GetFreeConn();                   // 获取连接
    void DestroyPool();                  // 销毁所有连接

    // 单例模式
    static connection_pool* GetInstance() {
        static connection_pool connPool;
        return &connPool;
    }

    void init(string url, string User, string PassWord, string DataBaseName, int Port, int MaxConn, int close_log);

private:
    connection_pool() : m_CurConn(0), m_FreeConn(0){};
    ~connection_pool();
    connection_pool(const connection_pool& other) = delete;
    connection_pool& operator=(const connection_pool& t) = delete;

    int m_MaxConn;  // 最大连接数
    int m_CurConn;  // 当前已使用的连接数
    int m_FreeConn; // 当前空闲的连接数
    locker lock;
    list<MYSQL*> connList; // 连接池
    sem reserve;

public:
    string m_url;          // 主机地址
    string m_Port;         // 数据库端口号
    string m_User;         // 登陆数据库用户名
    string m_PassWord;     // 登陆数据库密码
    string m_DatabaseName; // 使用数据库名
    int m_close_log;       // 日志开关
};

// 将数据库连接的获取与释放通过RAII机制封装，避免手动释放
class connectionRAII {
public:
    // 双指针对MYSQL *con修改
    connectionRAII(MYSQL** con, connection_pool* connPool);
    ~connectionRAII();

private:
    MYSQL* conRAII;
    connection_pool* poolRAII;
};

#endif
