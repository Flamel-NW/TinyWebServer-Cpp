#ifndef SERVER_H
#define SERVER_H

#include "pch.h"

#include "timer.h"
#include "http_conn.h"
#include "thread_pool.h"


class Server
{
public:
    enum {
        MAX_FD = 65536,             //最大文件描述符
        MAX_EVENT_NUMBER = 10000,   //最大事件数
        TIMESLOT = 5                //最小超时单位
    };

    Server();
    ~Server();

    void init(std::string username, std::string password, std::string db_name,
        int port, bool actor_pattern, bool opt_linger, int trig_mode,
        bool close_log, bool write_log, int thread_pool_size, int conn_pool_size);

    void init_thread_pool();
    void init_conn_pool();
    void init_log();
    void init_trig_mode();

    void event_listen();
    void event_loop();

    void init_timer(int connfd, struct sockaddr_in client_address);
    void delay_timer(TimerUtil* timer);
    void close_conn(TimerUtil* timer, int sockfd);

    bool accept_client_data();
    bool recv_signal(bool& timeout, bool& stop_server);
    
    void read_actor(int sockfd);
    void write_actor(int sockfd);

public:
    //基础
    int port_;
    std::string root_dir_;
    bool close_log_;
    bool write_log_;
    bool actor_pattern_;

    int pipefd_[2];
    int epollfd_;
    HttpConn* users_;

    //数据库连接池相关
    ConnPool* conn_pool_;
    std::string username_;  // 登陆数据库用户名
    std::string password_;  // 登陆数据库密码
    std::string db_name_;   // 使用数据库名
    int conn_pool_size_;

    //线程池相关
    ThreadPool<HttpConn>* thread_pool_;
    int thread_pool_size_;

    //epoll_event相关
    epoll_event events_[MAX_EVENT_NUMBER];

    int listenfd_;
    bool opt_linger_;
    int trig_mode_;
    bool listenfd_trig_mode_;
    bool connfd_trig_mode_;

    //定时器相关
    ClientData* users_timer_;
};

#endif
