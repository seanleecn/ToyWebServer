# ToyWebServer

Linux下基于C++的**玩具级**服务器，参考了下面两个项目。

> Linux高性能服务器编程，游双著.
> 
> https://github.com/qinguoyi/TinyWebServer

# 项目简介

* 使用 **线程池** + **非阻塞socket** + **epoll(ET和LT均实现)** + **事件处理(Reactor和模拟Proactor均实现)** 的并发模型
* 使用**状态机**解析HTTP请求报文，支持解析**GET和POST**请求
* (TODO)访问服务器数据库实现web端用户**注册、登录**功能，可以请求播放服务器**图片和视频文件**
* (TODO)实现**同步/异步日志系统**，记录服务器运行状态

## 项目总览

```
./ToyWebServer
├── config          参数配置解析
├── connpool        数据库连接池(TODO)
├── http            HTTP连接处理 
├── lock            封装互斥锁和信号量
├── log             日志系统(TODO)
├── threadpool      线程池
├── timer           定时器
├── root            网页数据
├── webbench-1.5    压力测试
├── webserver       服务器
├── CMakeLists.txt  
├── README.md
└── main.cpp
```

## 数据库连接池

* 单例模式，保证唯一
* list实现连接池
* 连接池为静态大小
* 互斥锁实现线程安全

## HTTP连接处理 

* **从状态机**按行读取请求数据，更新从状态机状态
* **主状态机**根据从状态机状态，决定响应请求还是继续读取

## 日志系统

* 自定义阻塞队列
* 单例模式创建日志
* 同步\异步日志

## 线程池

* 主线程往工作队列中插入任务
* 工作线程通过竞争来取得任务并执行任务

## 定时器

* 基于升序链表的定时器
* alarm函数周期性地触发SIGALRM信号
* 信号处理函数利用管道通知主循环定时任务

# 测试

* 安装MySQL

    ```bash
    sudo apt install mysql-server libmysqlclient-dev
    ```
    
* 创建密码表单
    ```sql
    <bash> sudo mysql
    
    // 建立webserDb库(可自定义)
    create database webserDb;
    USE webserDb;
    
    // 创建user表
    CREATE TABLE user(
        username char(50) NULL,
        passwd char(50) NULL
    )ENGINE=InnoDB;
    
    // 添加用户数据(也可以不添加，仅测试)
    INSERT INTO user(username, passwd) VALUES('lx', '123');

    // 查看当前全部用户密码
    use webserDb;
    show tables;
    select * from user;
    ```
    
* 修改``main.cpp``中的参数

    ```C++
    //登录mysql的用户密码，不是在数据库中添加的用户数据
    string user = "root";
    string passwd = "1234";
    string databasename = "webserDb";
    ```

* cmake & make 

    ```bash
    mkdir build && cd build
    cmake ..
    make
    ```

* 默认启动

    ```bash
    ./toy_web_server #如果使用root登录mysql,要用sudo运行
    ```
* 自定义启动
  
    ```bash
    ./toy_web_server [-p port] [-l LOGWrite] [-m TRIGMode] [-o OPT_LINGER] [-s sql_num] [-t thread_num] [-c close_log] [-a actor_model]
    
    -p，自定义端口号
        * 9006(默认)
    -l，写入方式
    	* 0，同步写入(默认)
    	* 1，异步写入
    -m，epoll中lisfd和connfd的触发模式
    	* 0，LT + LT(默认)
    	* 1，LT + ET
        * 2，ET + LT
        * 3，ET + ET
    -o，socket的linger选项
    	* 0，不使用(默认)
    	* 1，使用
    -s，数据库连接数量
    	* 8(默认)
    -t，线程数量
        * 8(默认)
    -c，日志
        * 0，打开日志
        * 1，关闭日志(默认)
    -a，时间模型
        * 0，Proactor(默认)
        * 1，Reactor
    ```

* 浏览器打开

    ```C++
    127.0.0.1:9006 #本地测试，如果在服务器上改成公网ip或者开启端口转发
    ```
    TODO
    插图
    
* 压力测试    
  
    * 测试参数：全部默认启动参数
    * 硬件配置：2核CPU + 2G内存 的阿里云服务器 
    * webbench测试
    
    ```bash
    ./webbench -c 4000 -t 5 http://127.0.0.1:9006/
    ```
    TODO
    插图