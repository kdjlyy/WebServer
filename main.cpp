#include "config.h"

int main(int argc, char* argv[]) {
    // 需要修改的数据库信息,登录名,密码,库名
    string mysql_host = "172.17.0.2";
    int mysql_port = 3306;
    string user = "root";
    string passwd = "123456";
    string databasename = "web_server";

    // 命令行解析
    Config config;
    config.parse_arg(argc, argv);

    WebServer server;

    // 初始化
    // actor_model: 0-Proactor模型 1-Reactor模型
    server.init(config.PORT, mysql_host, mysql_port, user, passwd, databasename, config.LOGWrite,
                config.OPT_LINGER, config.TRIGMode, config.sql_num, config.thread_num,
                config.close_log, config.actor_model);

    // 日志
    server.log_write();

    // 数据库
    server.sql_pool();

    // 线程池
    server.thread_pool();

    // 触发模式
    server.trig_mode();

    // 监听
    server.eventListen();

    // 运行
    server.eventLoop();

    return 0;
}
