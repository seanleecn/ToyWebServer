#include "./webserver/webserver.h"

int main(int argc, char *argv[])
{
    string user = "root";             // mysql账号
    string passwd = "0419";           // mysql密码
    string databasename = "webserDb"; // 数据库名
    string rootPath = "../root";      // 资源目录

    // 命令行解析
    Config config;
    config.parse_arg(argc, argv);

    // 服务器初始化
    // new了两个数组保存连接和定时器
    WebServer server;

    // 把解析的参数赋值给服务器
    server.init(user, passwd, databasename, rootPath, config);

    // 单例模式获取一个日志的实例
    // TODO:待看
    server.log_write();

    // 初始化数据库连接池
    // 同时读取了数据库里面已经有的用户密码存在一个map里
    server.sql_pool();

    // 创建线程池
    // 会创建m_thread_num个线程
    server.thread_pool();

    // 设置监听socket，epoll和定时器
    server.eventListen();

    // 事件回环(即服务器主线程)
    server.eventLoop();

    return 0;
}