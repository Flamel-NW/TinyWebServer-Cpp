#include "server.h"
#include "http_conn.h"
#include "pch.h"
#include <mysql/my_command.h>


using namespace std;

Server::Server() {
    // http_conn类对象
    users_ = new HttpConn[MAX_FD];

    // root文件夹路径
    char server_path[200];
    getcwd(server_path, 200);
    root_dir_ = string(server_path) + "/root";

    // 定时器
    users_timer_ = new ClientData[MAX_FD];
}

Server::~Server() {
    close(epollfd_);
    close(listenfd_);
    close(pipefd_[1]);
    close(pipefd_[0]);
    delete[] users_;
    delete[] users_timer_;
    delete thread_pool_;
}

void Server::init(std::string username, std::string password, std::string db_name,
        int port, bool actor_pattern, bool opt_linger, int trig_mode,
        bool close_log, bool write_log, int thread_pool_size, int conn_pool_size) {
    username_ = username;
    password_ = password;
    db_name_ = db_name;
    port_ = port;
    actor_pattern_ = actor_pattern;
    opt_linger_ = opt_linger;
    trig_mode_ = trig_mode;
    close_log_ = close_log;
    write_log_ = write_log;
    thread_pool_size_ = thread_pool_size;
    conn_pool_size_ = conn_pool_size;
}

void Server::init_conn_pool() {
    // 初始化数据库连接池
    conn_pool_ = ConnPool::get_instance();
    conn_pool_->init("localhost", username_, password_, db_name_, 3306, conn_pool_size_, close_log_);

    // 初始化数据库读取表
    users_->init_mysql_result(conn_pool_);
}

// 初始化线程池
void Server::init_thread_pool() {
    thread_pool_ = new ThreadPool<HttpConn>(conn_pool_, actor_pattern_, thread_pool_size_);
}

// 初始化日志
void Server::init_log() {
    if (!close_log_)
    {
        if (write_log_)
            Log::get_instance()->init("./ServerLog", close_log_, 2000, 800000, 800);
        else
            Log::get_instance()->init("./ServerLog", close_log_, 2000, 800000, 0);
    }
}

void Server::init_trig_mode() {
    // LT + LT
    if (trig_mode_ == 0) {
        listenfd_trig_mode_ = false;
        connfd_trig_mode_ = false;
    // LT + ET
    } else if (trig_mode_ == 1) {
        listenfd_trig_mode_ = false;
        connfd_trig_mode_ = true;
    // ET + LT
    } else if (trig_mode_ == 2) {
        listenfd_trig_mode_ = true;
        connfd_trig_mode_ = false;
    // ET + ET
    } else if (trig_mode_ == 3) {
        listenfd_trig_mode_ = true;
        connfd_trig_mode_ = true;
    }
}

void Server::event_listen()
{
    // 网络编程基础步骤
    listenfd_ = socket(PF_INET, SOCK_STREAM, 0);
    if (listenfd_ < 0) {
        STDERR_FUNC_LINE();
        exit(EXIT_FAILURE);
    }

    // 优雅关闭连接
    struct linger tmp = { opt_linger_, 1 };
    setsockopt(listenfd_, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port_);

    int flag = 1;
    setsockopt(listenfd_, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    ret = bind(listenfd_, (struct sockaddr*) &address, sizeof(address));
    if (ret < 0) {
        STDERR_FUNC_LINE();
        exit(EXIT_FAILURE);
    }
    ret = listen(listenfd_, 5);
    if (ret < 0) {
        STDERR_FUNC_LINE();
        exit(EXIT_FAILURE);
    }

    Utils::timeslot_ = TIMESLOT;

    // epoll创建内核事件表
    epoll_event events_[MAX_EVENT_NUMBER];
    epollfd_ = epoll_create(5);
    if (epollfd_ == -1) {
        STDERR_FUNC_LINE();
        exit(EXIT_FAILURE);
    }

    Utils::add_fd(epollfd_, listenfd_, false, listenfd_trig_mode_);
    HttpConn::epollfd_ = epollfd_;

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd_);
    if (ret == -1) {
        STDERR_FUNC_LINE();
        exit(EXIT_FAILURE);
    }
    Utils::set_nonblock(pipefd_[1]);
    Utils::add_fd(epollfd_, pipefd_[0], false, 0);

    Utils::add_sig(SIGPIPE, SIG_IGN);
    Utils::add_sig(SIGALRM, Utils::sig_handler, false);
    Utils::add_sig(SIGTERM, Utils::sig_handler, false);

    alarm(TIMESLOT);

    // 工具类,信号和描述符基础操作
    Utils::pipefd_ = pipefd_;
    Utils::epollfd_ = epollfd_;
}

void Server::event_loop()
{
    bool timeout = false;
    bool stop_server = false;

    while (!stop_server)
    {
        int number = epoll_wait(epollfd_, events_, MAX_EVENT_NUMBER, -1);
        if (number < 0 && errno != EINTR) {
            LOG_ERROR("epoll failure");
            break;
        }

        for (int i = 0; i < number; i++) {
            int sockfd = events_[i].data.fd;

            // 处理新到的客户连接
            if (sockfd == listenfd_) {
                if (!accept_client_data())
                    continue;

            // 服务器端关闭连接，移除对应的定时器
            } else if (events_[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                TimerUtil* timer = users_timer_[sockfd].timer;
                close_conn(timer, sockfd);

            // 处理信号  
            } else if ((sockfd == pipefd_[0]) && (events_[i].events & EPOLLIN)) {
                if (!recv_signal(timeout, stop_server))
                    LOG_ERROR("recv signal failure");

            // 处理客户连接上接收到的数据
            } else if (events_[i].events & EPOLLIN) {
                read_actor(sockfd);

            } else if (events_[i].events & EPOLLOUT) {
                write_actor(sockfd);
            }
        }

        if (timeout) {
            Utils::timer_handler();
            LOG_INFO("timer tick");
            timeout = false;
        }
    }
}

void Server::init_timer(int connfd, struct sockaddr_in client_address)
{
    users_[connfd].init(connfd, client_address, root_dir_.c_str(), connfd_trig_mode_, close_log_, username_, password_, db_name_);

    // 初始化client_data数据
    // 创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
    users_timer_[connfd].address = client_address;
    users_timer_[connfd].sockfd = connfd;
    TimerUtil* timer = new TimerUtil;
    timer->user_data = &users_timer_[connfd];
    timer->callback = Utils::cb_func;
    time_t cur = time(nullptr);
    timer->expire = cur + 3 * TIMESLOT;
    users_timer_[connfd].timer = timer;
    Utils::timer_list_.add_timer(timer);
}

// 若有数据传输，则将定时器往后延迟3个单位
// 并对新的定时器在链表上的位置进行调整
void Server::delay_timer(TimerUtil* timer)
{
    time_t cur = time(nullptr);
    timer->expire = cur + 3 * TIMESLOT;
    Utils::timer_list_.modify_timer(timer);

    LOG_INFO("delay timer once");
}

// 服务器端关闭连接，移除对应的定时器
void Server::close_conn(TimerUtil* timer, int sockfd)
{
    timer->callback(&users_timer_[sockfd]);
    if (timer)
        Utils::timer_list_.del_timer(timer);

    LOG_INFO("close fd %d", users_timer_[sockfd].sockfd);
}

bool Server::accept_client_data()
{
    struct sockaddr_in client_address;
    socklen_t client_addrlength = sizeof(client_address);

    if (!listenfd_trig_mode_) {
        int connfd = accept(listenfd_, (struct sockaddr*) &client_address, &client_addrlength);
        if (connfd < 0) {
            LOG_ERROR("accept error: errno is: %d", errno);
            return false;
        }
        if (HttpConn::user_count_ >= MAX_FD) {
            Utils::show_error(connfd, "Internal server busy");
            LOG_ERROR("Internal server busy");
            return false;
        }
        init_timer(connfd, client_address);
    } else {
        while (true) {
            int connfd = accept(listenfd_, (struct sockaddr*) &client_address, &client_addrlength);
            if (connfd < 0) {
                LOG_ERROR("%s: errno is: %d", "accept error", errno);
                break;
            }
            if (HttpConn::user_count_ >= MAX_FD) {
                Utils::show_error(connfd, "Internal server busy");
                LOG_ERROR("Internal server busy");
                break;
            }
            init_timer(connfd, client_address);
        }
        return false;
    }
    return true;
}

bool Server::recv_signal(bool &timeout, bool &stop_server)
{
    int ret = 0;
    char signals[1024];
    ret = recv(pipefd_[0], signals, sizeof(signals), 0);
    if (ret == -1 || ret == 0) {
        return false;
    }

    for (int i = 0; i < ret; i++) {
        if (signals[i] == SIGALRM) 
            timeout = true;
        if (signals[i] == SIGTERM) 
            stop_server = true;
    }

    return true;
}

void Server::read_actor(int sockfd)
{
    TimerUtil* timer = users_timer_[sockfd].timer;

    // reactor
    if (actor_pattern_) {
        if (timer) 
            delay_timer(timer);
        // 若监测到读事件，将该事件放入请求队列
        thread_pool_->append(&users_[sockfd], false);
        while (true) {
            if (users_[sockfd].improve_) {
                if (users_[sockfd].timer_flag_) {
                    close_conn(timer, sockfd);
                    users_[sockfd].timer_flag_ = false;
                }
                users_[sockfd].improve_ = false;
                break;
            }
        }
    // proactor
    } else {
        if (users_[sockfd].read_once()) {
            LOG_INFO("deal with the client(%s)", inet_ntoa(users_[sockfd].get_address()->sin_addr));
            // 若监测到读事件，将该事件放入请求队列
            thread_pool_->append(&users_[sockfd]);
            if (timer) 
                delay_timer(timer);
        } else {
            close_conn(timer, sockfd);
        }
    }
}

void Server::write_actor(int sockfd)
{
    TimerUtil* timer = users_timer_[sockfd].timer;

    // reactor
    if (actor_pattern_) {
        if (timer) 
            delay_timer(timer);
        // 若监测到写事件，将该事件放入请求队列
        thread_pool_->append(&users_[sockfd], true);
        while (true) {
            if (users_[sockfd].improve_) {
                if (users_[sockfd].timer_flag_) {
                    close_conn(timer, sockfd);
                    users_[sockfd].timer_flag_ = false;
                }
                users_[sockfd].improve_ = false;
                break;
            }
        }
    // proactor
    } else {
        if (users_[sockfd].write()) {
            LOG_INFO("send data to the client(%s)", inet_ntoa(users_[sockfd].get_address()->sin_addr));
            if (timer) 
                delay_timer(timer);
        } else {
            close_conn(timer, sockfd);
        }
    }
}
