#include <iostream>
#include <list>
#include <mysql/mysql.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include "sql_connection_pool.h"

using namespace std;

// 构造初始化
void connection_pool::init(string url, string User, string PassWord, string DBName, int Port, int MaxConn, int close_log) {
    m_url = url;
    m_Port = Port;
    m_User = User;
    m_PassWord = PassWord;
    m_DatabaseName = DBName;
    m_close_log = close_log;

    // 创建MaxConn条数据库连接
    for (int i = 0; i < MaxConn; i++) {
        MYSQL* con = NULL;
        con = mysql_init(con);

        if (con == NULL) {
            LOG_ERROR("MySQL Error");
            exit(1);
        }
        con = mysql_real_connect(con, url.c_str(), User.c_str(), PassWord.c_str(), DBName.c_str(), Port, NULL, 0);

        if (con == NULL) {
            LOG_ERROR("MySQL Error");
            exit(1);
        }
        connList.push_back(con);
        ++m_FreeConn;
    }

    // 将信号量初始化为最大连接次数
    reserve = sem(m_FreeConn);

    m_MaxConn = m_FreeConn;
}

// 当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL* connection_pool::GetConnection() {
    if (0 == connList.size())
        return NULL;

    reserve.wait(); // V(-1)

    lock.lock();

    MYSQL* con = connList.front();
    connList.pop_front();

    --m_FreeConn;
    ++m_CurConn;

    lock.unlock();
    return con;
}

// 释放当前使用的连接
bool connection_pool::ReleaseConnection(MYSQL* con) {
    if (NULL == con)
        return false;

    lock.lock();

    connList.push_back(con);
    ++m_FreeConn;
    --m_CurConn;

    lock.unlock();

    reserve.post(); // P(+1)
    return true;
}

// 销毁数据库连接池
void connection_pool::DestroyPool() {

    lock.lock();
    if (connList.size() > 0) {
        list<MYSQL*>::iterator it;
        for (it = connList.begin(); it != connList.end(); ++it) {
            MYSQL* con = *it;
            mysql_close(con);
        }
        m_CurConn = 0;
        m_FreeConn = 0;
        connList.clear();
    }

    lock.unlock();
}

// 当前空闲的连接数
int connection_pool::GetFreeConn() {
    return this->m_FreeConn;
}

connection_pool::~connection_pool() {
    DestroyPool();
}

// 将数据库连接的获取与释放通过RAII机制封装，避免手动释放
connectionRAII::connectionRAII(MYSQL** SQL, connection_pool* connPool) {
    *SQL = connPool->GetConnection();

    conRAII = *SQL;
    poolRAII = connPool;
}
connectionRAII::~connectionRAII() {
    poolRAII->ReleaseConnection(conRAII);
}