#include "./config/config.h"

int main(int argc, char *argv[])
{
    //需要修改的数据库信息,登录名,密码,库名
    string user = "root";
    string passwd = "0419";
    string databasename = "webserDb";

    //命令行解析
    Config config;
    config.parse_arg(argc, argv);

    WebServer server;

    //1.初始化
    server.init(config.PORT, user, passwd, databasename, config.LOGWrite,
                config.OPT_LINGER, config.TRIGMode, config.sql_num, config.thread_num,
                config.close_log, config.actor_model);

    //2.日志
    server.log_write();

    //3.数据库
    server.sql_pool();

    //4.线程池
    server.thread_pool();

    //5.触发模式
    server.trig_mode();

    //6.监听
    server.eventListen();

    //7.运行
    server.eventLoop();

    return 0;
}