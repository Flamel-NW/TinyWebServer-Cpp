#ifndef TIMER_H
#define TIMER_H

#include "pch.h"

#include "log.h"

// 连接结构体成员需要用到定时器类
// 需要前向声明
class TimerUtil;

// 连接资源
struct ClientData
{
    sockaddr_in address;                // 客户端socket地址
    int sockfd;                         // socket文件描述符
    TimerUtil* timer;                   // 定时器
};

// 定时器类
struct TimerUtil {
    time_t expire;                      // 超时时间
    void (* callback) (ClientData*);    // 回调函数
    ClientData* user_data;              // 连接资源
    TimerUtil* pre;                     // 前向定时器
    TimerUtil* next;                    // 后继定时器
};

// 定时器容器类
class TimerList {
public:
    // 常规销毁链表
    ~TimerList();

    // 添加定时器,内部调用私有成员add_timer
    void add_timer(TimerUtil* timer);
    // 调整定时器，任务发生变化时，调整的定时器在链表中的位置
    void modify_timer(TimerUtil* timer);
    // 删除定时器
    void del_timer(TimerUtil* timer);
    // 定时任务处理函数
    void tick();

private:
    // 私有成员，被公有成员add_timer和modify_timer调用
    // 主要用于调整链表内部节点
    void add_timer(TimerUtil* timer, TimerUtil* head);

    // 头尾节点
    TimerUtil* head_;
    TimerUtil* tail_;
};

#endif
