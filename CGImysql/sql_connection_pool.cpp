#include <mysql/mysql.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <list>
#include <pthread.h>
#include <iostream>
#include "sql_connection_pool.h"

using namespace std;

connection_pool::connection_pool()
{
	m_CurConn = 0;	// 当前已使用连接
	m_FreeConn = 0; // 当前空闲的连接数
}

// 单例模式返回线程池，在C++0x后如此写法线程安全
connection_pool *connection_pool::GetInstance()
{
	static connection_pool connPool;
	return &connPool;
}

// 构造初始化
void connection_pool::init(string url, string User, string PassWord, string DBName, int Port, int MaxConn, int close_log)
{
	// 初始化数据库信息
	m_url = url;
	m_Port = Port;
	m_User = User;
	m_PassWord = PassWord;
	m_DatabaseName = DBName;
	m_close_log = close_log;

	// 创建MaxConn条数据库连接
	for (int i = 0; i < MaxConn; i++)
	{
		MYSQL *con = NULL;
		con = mysql_init(con);

		if (con == NULL)
		{
			LOG_ERROR("MySQL Error");
			exit(1);
		}
		con = mysql_real_connect(con, url.c_str(), User.c_str(), PassWord.c_str(), DBName.c_str(), Port, NULL, 0);

		if (con == NULL)
		{
			LOG_ERROR("MySQL Error");
			exit(1);
		}
		// 更新连接池和空闲连接数量
		connList.push_back(con);
		++m_FreeConn;
	}
	// 将信号量初始化为最大连接次数
	reserve = sem(m_FreeConn);
	m_MaxConn = m_FreeConn;
}

// 当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL *connection_pool::GetConnection()
{
	MYSQL *con = NULL;
	// 如果连接池中没有连接，则返回NULL
	if (0 == connList.size())
		return NULL;
	// 取出连接，信号量原子减1，为0则等待
	reserve.wait();
	// 访问临界资源加锁
	lock.lock();
	// 拿出一个连接
	con = connList.front();
	connList.pop_front();
	// 这里的两个变量，并没有用到，非常鸡肋...
	--m_FreeConn;
	++m_CurConn;
	// 离开临界区解锁
	lock.unlock();
	return con;
}

// 释放当前使用的连接
bool connection_pool::ReleaseConnection(MYSQL *con)
{
	// 判空
	if (NULL == con)
		return false;
	// 访问临界资源枷锁
	lock.lock();
	// 将con放回连接池
	connList.push_back(con);
	++m_FreeConn;
	--m_CurConn;
	// 解锁
	lock.unlock();
	// 释放连接原子加1
	reserve.post();
	return true;
}

// 销毁数据库连接池
void connection_pool::DestroyPool()
{
	lock.lock();
	if (connList.size() > 0)
	{
		// 通过迭代器遍历，关闭数据库连接
		list<MYSQL *>::iterator it;
		for (it = connList.begin(); it != connList.end(); ++it)
		{
			MYSQL *con = *it;
			mysql_close(con);
		}
		m_CurConn = 0;
		m_FreeConn = 0;
		// 清空list
		connList.clear();
	}
	lock.unlock();
}

// 当前空闲的连接数
int connection_pool::GetFreeConn()
{
	return this->m_FreeConn;
}

connection_pool::~connection_pool()
{
	DestroyPool();
}

// 双指针对MYSQL *con修改
connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connPool)
{
	*SQL = connPool->GetConnection();
	conRAII = *SQL;
	poolRAII = connPool;
}

connectionRAII::~connectionRAII()
{
	poolRAII->ReleaseConnection(conRAII);
}