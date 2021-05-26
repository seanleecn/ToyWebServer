#include "./config/config.h"

int main(int argc, char *argv[])
{
    // 需要修改的数据库信息,登录名,密码,库名
    string user = "root";
    string passwd = "0419";
    string databasename = "webserDb";

    // 命令行解析
    Config config;
    config.parse_arg(argc, argv);
    
    // 服务器初始化
    // new了两个数组保存连接和定时器
    WebServer server;

    // 把解析的参数赋值给服务器
    server.init(config.PORT, user, passwd, databasename, config.LOGWrite,
                config.OPT_LINGER, config.TRIGMode, config.sql_num, config.thread_num,
                config.close_log, config.actor_model);

    // 初始化日志
    // 单例模式获取一个日志的实例
    server.log_write();

    // 初始化数据库连接池
    // 同时读取了数据库里面已经有的用户密码存在一个map里
    server.sql_pool();

    // 创建线程池
    // 会创建m_thread_num个线程
    server.thread_pool();

    // 设置epoll的触发模式
    // TODO:这个感觉没必要
    server.trig_mode();

    // 设置监听socket，epoll和定时器
    server.eventListen();

    // 事件回环（即服务器主线程）
    server.eventLoop();

    return 0;
}