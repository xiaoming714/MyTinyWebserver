#ifndef LST_TIMER
#define LST_TIMER

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>

#include <time.h>
#include "../log/log.h"

// 连接资源结构体成员需要用到定时器类
// 需要前向声明
class util_timer;

// 客户端的连接资源
struct client_data
{
    // 客户端socket地址
    sockaddr_in address;
    // socket文件描述符
    int sockfd;
    // 定时器
    util_timer *timer;
};

// 定时器类
class util_timer
{
public:
    util_timer() : prev(NULL), next(NULL) {}

public:
    // 超时时间=连接时刻+固定事件（TIMESLOT），
    time_t expire;
    // 回调函数
    void (*cb_func)(client_data *);
    // 连接资源
    client_data *user_data;
    // 前驱定时器
    util_timer *prev;
    // 后继定时器
    util_timer *next;
};

// 定时器容器类，用超时时间来进行判断，超时时间小的定时器在前，也就是越靠前的定时器越早淘汰
class sort_timer_lst
{
public:
    sort_timer_lst();
    ~sort_timer_lst();

    void add_timer(util_timer *timer);
    void adjust_timer(util_timer *timer);
    void del_timer(util_timer *timer);
    void tick();

private:
    // 添加定时器，内部调用私有成员add_timer
    void add_timer(util_timer *timer, util_timer *lst_head);
    util_timer *head;
    util_timer *tail;
};

// 工具类
class Utils
{
public:
    Utils() {}
    ~Utils() {}

    void init(int timeslot);

    // 对文件描述符设置非阻塞
    int setnonblocking(int fd);
    // 将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
    void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);
    // 信号处理函数
    static void sig_handler(int sig);
    // 设置信号函数
    void addsig(int sig, void(handler)(int), bool restart = true);
    // 定时处理任务，重新定时以不断触发SIGALRM信号
    void timer_handler();
    // connfd出现问题，向info发送错误信息，并关闭连接
    void show_error(int connfd, const char *info);

public:
    // 管道描述符
    static int *u_pipefd;
    // 定时器链表
    sort_timer_lst m_timer_lst;
    // epoll句柄
    static int u_epollfd;
    // 过期时间
    int m_TIMESLOT;
};
// 到达超时时间关闭连接的回调函数
void cb_func(client_data *user_data);

#endif
