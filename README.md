![Bower](https://img.shields.io/bower/l/bootstrap)
![GitHub code size in bytes](https://img.shields.io/github/languages/code-size/kdjlyy/WebServer)
![GitHub last commit (by committer)](https://img.shields.io/github/last-commit/kdjlyy/WebServer)


WebServer
===============

使用C++实现的Web高性能服务器，基于Linux网络编程，解析浏览器发送的HTTP请求报文并响应，可用于文字、图片、视频等资源的传输，使用webbench本地压测结果可处理数万并发请求。
- 基于队列实现的线程池，支持Reactor和模拟Proactor两种网络模式，支持epoll的水平触发和边沿触发；
- 使用单例模式实现了日志系统，支持同步写入和异步写入，支持按天、按行分割；实现了数据库连接池，数据库连接的获取与释放通过RAII机制封装；
- 使用主从状态机解析HTTP请求报文，支持GET和POST请求，使用IO向量和mmap加速文件传输速度；
- 实现了信号处理机制，定时产生SIGALAM信号清理服务器的非活动连接。
目录
-----

| [框架](#框架) | [压力测试](#压力测试) |[更新日志](#更新日志) | [快速运行](#快速运行) | [个性化运行](#个性化运行) |
|:--------:|:--------:|:--------:|:--------:|:--------:|


框架
-------------
<div align=center><img src="http://ww1.sinaimg.cn/large/005TJ2c7ly1ge0j1atq5hj30g60lm0w4.jpg" height="765"/> </div>


压力测试
-------------
在关闭日志后，使用Webbench对服务器进行压力测试，对listenfd和connfd分别采用ET和LT模式，均可实现上万的并发连接，下面列出的是两者组合后的测试结果. 
> * 并发连接总数：10000
> * 访问服务器时间：5s
> * 所有访问均成功

1. Proactor LT + LT 模式: `31112 QPS`
```bash
# webbench -c 10000 -t 5 http://127.0.0.1:9006/
Webbench - Simple Web Benchmark 1.5
Copyright (c) Radim Kolar 1997-2004, GPL Open Source Software.

Benchmarking: GET http://127.0.0.1:9006/
10000 clients, running 5 sec.

Speed=1866732 pages/min, 3484566 bytes/sec.
Requests: 155561 susceed, 0 failed.
```
2. Proactor LT + ET模式: `35768 QPS`
```bash
# webbench -c 10000 -t 5 http://127.0.0.1:9006/
Webbench - Simple Web Benchmark 1.5
Copyright (c) Radim Kolar 1997-2004, GPL Open Source Software.

Benchmarking: GET http://127.0.0.1:9006/
10000 clients, running 5 sec.

Speed=2146104 pages/min, 4006060 bytes/sec.
Requests: 178842 susceed, 0 failed.
```
3. Proactor ET + LT模式: `26643 QPS`
```bash
# webbench -c 10000 -t 5 http://127.0.0.1:9006/
Webbench - Simple Web Benchmark 1.5
Copyright (c) Radim Kolar 1997-2004, GPL Open Source Software.

Benchmarking: GET http://127.0.0.1:9006/
10000 clients, running 5 sec.

Speed=1598604 pages/min, 2984016 bytes/sec.
Requests: 133217 susceed, 0 failed.
```
4. Proactor ET + ET模式: `28526 QPS`
```bash
# webbench -c 10000 -t 5 http://127.0.0.1:9006/
Webbench - Simple Web Benchmark 1.5
Copyright (c) Radim Kolar 1997-2004, GPL Open Source Software.

Benchmarking: GET http://127.0.0.1:9006/
10000 clients, running 5 sec.

Speed=1711596 pages/min, 3194979 bytes/sec.
Requests: 142633 susceed, 0 failed.
```
5. Reactor LT + ET模式: `12907 QPS`
```bash
# webbench -c 10000 -t 5 http://127.0.0.1:9006/
Webbench - Simple Web Benchmark 1.5
Copyright (c) Radim Kolar 1997-2004, GPL Open Source Software.

Benchmarking: GET http://127.0.0.1:9006/
10000 clients, running 5 sec.

Speed=774420 pages/min, 1445606 bytes/sec.
Requests: 64535 susceed, 0 failed.
```


更新日志
-------


快速运行
------------
* 服务器测试环境
	* Ubuntu版本20.04
	* MySQL版本5.7
* 浏览器测试环境
	* Windows、Linux均可
	* Chrome
	* FireFox
	* 其他浏览器暂无测试

* 测试前确认已安装MySQL数据库

    ```C++
    // 建立yourdb库
    create database yourdb;

    // 创建user表
    USE yourdb;
    CREATE TABLE user(
        username char(50) NULL,
        passwd char(50) NULL
    )ENGINE=InnoDB;

    // 添加数据
    INSERT INTO user(username, passwd) VALUES('name', 'passwd');
    ```

* 修改main.cpp中的数据库初始化信息

    ```C++
    // 需要修改的数据库信息,登录名,密码,库名
    string mysql_host = "172.17.0.2";
    int mysql_port = 3306;
    string user = "root";
    string passwd = "123456";
    string databasename = "web_server";
    ```

* build

    ```C++
    make cleanall
    sh ./build.sh
    ```

* 启动server

    ```C++
    bash ./run.sh
    ```

* 浏览器端

    ```C++
    127.0.0.1:9006
    ```

个性化运行
------

```C++
./server [-p port] [-l LOGWrite] [-m TRIGMode] [-o OPT_LINGER] [-s sql_num] [-t thread_num] [-c close_log] [-a actor_model]
```

温馨提示:以上参数不是非必须，不用全部使用，根据个人情况搭配选用即可.

* -p，自定义端口号
	* 默认9006
* -l，选择日志写入方式，默认同步写入
	* 0，同步写入
	* 1，异步写入
* -m，listenfd和connfd的模式组合，默认使用LT + LT
	* 0，表示使用LT + LT
	* 1，表示使用LT + ET
    * 2，表示使用ET + LT
    * 3，表示使用ET + ET
* -o，优雅关闭连接，默认不使用
	* 0，不使用
	* 1，使用
* -s，数据库连接数量
	* 默认为8
* -t，线程数量
	* 默认为8
* -c，关闭日志，默认打开
	* 0，打开日志
	* 1，关闭日志
* -a，选择反应堆模型，默认Proactor
	* 0，Proactor模型
	* 1，Reactor模型

测试示例命令与含义

```C++
./server -p 9007 -l 1 -m 0 -o 1 -s 10 -t 10 -c 1 -a 1
```

- [x] 端口9007
- [x] 异步写入日志
- [x] 使用LT + LT组合
- [x] 使用优雅关闭连接
- [x] 数据库连接池内有10条连接
- [x] 线程池内有10条线程
- [x] 关闭日志
- [x] Reactor反应堆模型
