#ifndef _CONNECTION_POOL_
#define _CONNECTION_POOL_

#include <cstdio>
#include <list>
#include <mysql/mysql.h>
#include <error.h>
#include <cstring>
#include <iostream>
#include <string>
#include "../lock/locker.hpp"
#include "../log/log.h"

using namespace std;

// 单例模式 这个是利用局部静态变量懒汉模式实现单例
// C++11之后局部静态变量线程安全
class connection_pool
{
public:
	MYSQL *GetConnection();				   // 获取数据库连接
	bool ReleaseConnection(MYSQL *conn);   // 释放连接
	int GetFreeConn() const;			   // 获取连接
	void DestroyPool();					   // 销毁所有连接(析构调用)
	static connection_pool *GetInstance(); // 获取一个实例

	void init(const string &url, const string &User, const string &PassWord, const string &DataBaseName, int Port, int MaxConn, int close_log);

private:
	connection_pool();
	~connection_pool();

	int m_MaxConn;			// 最大连接数
	int m_CurConn;			// 当前已使用的连接数
	int m_FreeConn;			// 当前空闲的连接数
	locker lock;			// 锁
	list<MYSQL *> connList; // 连接池
	sem reserve;			// 信号量

public:
	string m_url;		   // 主机地址
	string m_Port;		   // 数据库端口号
	string m_User;		   // 登陆数据库用户名
	string m_PassWord;	   // 登陆数据库密码
	string m_DatabaseName; // 使用数据库名
	int m_close_log;	   // 日志开关
};

class connectionRAII
{
public:
	connectionRAII(MYSQL **con, connection_pool *connPool);
	~connectionRAII();

private:
	MYSQL *conRAII;
	connection_pool *poolRAII;
};

#endif
