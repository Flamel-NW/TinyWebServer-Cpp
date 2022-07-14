#ifndef UTILS_H
#define UTILS_H

#include "pch.h"

#include "timer.h"


class Utils {
public:
    Utils() = delete;
    
    // 信号处理函数
    static void sig_handler(int sig);
    // 设置信号函数
    static void add_sig(int sig, void (*handler)(int), bool restart = true);
    // 定时器回调函数，删除非活动连接在socket上的注册时间，并关闭
    static void cb_func(ClientData* user_data);
    // 定时处理任务，重新定时以不断触发SIGALRM信号
    static void timer_handler();
    static void show_error(int connfd, const char* info);

    // 对文件描述符设置非阻塞
    static int set_nonblock(int fd);
    // 将内核时间表注册读事件，LT/ET模式，选择开启EPOLLONESHOT
    static void add_fd(int epollfd, int fd, bool oneshot, bool trig_mode);
    // 从内核时间表删除描述符
    static void remove_fd(int epollfd, int fd);
    // 将事件重置为EPOLLONESHOT
    static void modify_fd(int epollfd, int fd, int events, int trig_mode);

    static int* pipefd_;
    static TimerList timer_list_;
    static int epollfd_;
    static int timeslot_;
};

#endif
