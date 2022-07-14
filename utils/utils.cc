#include "http_conn.h"

#include "pch.h"
#include "utils.h"

int* Utils::pipefd_ = nullptr;
TimerList Utils::timer_list_;
int Utils::epollfd_ = -1;
int Utils::timeslot_ = 0;

// 信号处理函数
void Utils::sig_handler(int sig) {
    // 为保证函数的可重入性，保留原来的errno
    // 可重入性表示中断口再次进入该函数，环境变量与之前相同，不会丢失数据
    int t_errno = errno;
    int msg = sig;
    // 讲信号值从管道写端写入，传输字符类型，而非整型
    send(pipefd_[1], (char*) &msg, 1, 0);
    // 将errno还原
    errno = t_errno;
}

// 设置信号函数
void Utils::add_sig(int sig, void (*handler)(int), bool restart) {
    // 创建sigaction结构体变量
    struct sigaction sa;
    bzero(&sa, sizeof(sa));
    // 信号处理函数中仅仅发送信号量，不做对应逻辑处理
    sa.sa_handler = handler;
    if (restart)
        sa.sa_flags |= SA_RESTART;
    // 将所有信号添加到信号集中
    sigfillset(&sa.sa_mask);
    // 执行sigaction函数
    if (sigaction(sig, &sa, nullptr) == -1) {
        STDERR_FUNC_LINE();
        exit(EXIT_FAILURE);
    }
}

// 定时器回调函数，删除非活动连接在socket上的注册时间，并关闭
void Utils::cb_func(ClientData* user_data) {
    // 删除非活动连接在socket上的注册时间
    epoll_ctl(epollfd_, EPOLL_CTL_DEL, user_data->sockfd, 0);
    if (user_data == nullptr) {
        STDERR_FUNC_LINE();
        exit(EXIT_FAILURE);
    }
    // 关闭文件描述符
    close(user_data->sockfd);
    // 减少连接数
    HttpConn::user_count_--;
}

// 定时处理任务，重新定时以不断触发SIGALRM信号
void Utils::timer_handler() {
    timer_list_.tick();
    alarm(timeslot_);
}

void Utils::show_error(int connfd, const char* info) {
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

// 对文件描述符设置非阻塞
int Utils::set_nonblock(int fd) {
    int old_flags = fcntl(fd, F_GETFL);
    int new_flags = old_flags | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_flags);
    return old_flags;
}

// 将内核时间表注册读事件，LT/ET模式，选择开启EPOLLONESHOT
void Utils::add_fd(int epollfd, int fd, bool oneshot, bool trig_mode) {
    epoll_event event;
    event.data.fd = fd;

    if (trig_mode) 
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP;

    if (oneshot) 
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    set_nonblock(fd);
}

// 从内核时间表删除描述符
void Utils::remove_fd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

// 将事件重置为EPOLLONESHOT
void Utils::modify_fd(int epollfd, int fd, int events, int trig_mode) {
    epoll_event event;
    event.data.fd = fd;

    if (trig_mode == 1)
        event.events = events | EPOLLONESHOT | EPOLLET | EPOLLRDHUP;
    else
        event.events = events | EPOLLONESHOT | EPOLLRDHUP;

    epoll_ctl(epollfd_, EPOLL_CTL_MOD, fd, &event);
}
