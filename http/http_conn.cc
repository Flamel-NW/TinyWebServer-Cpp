#include "log.h"

#include "http_conn.h"

using namespace std;

constexpr char OK_200_TITLE[] = "OK";
constexpr char ERROR_400_TITLE[] = "Bad Request";
constexpr char ERROR_400_FORM[] = "Your request has bad syntax or is inherently impossible to staisfy.\n";
constexpr char ERROR_403_TITLE[] = "Forbidden";
constexpr char ERROR_403_FORM[] = "You do not have permission to get file form this server.\n";
constexpr char ERROR_404_TITLE[] = "Not Found";
constexpr char ERROR_404_FORM[] = "The requested file was not found on this server.\n";
constexpr char ERROR_500_TITLE[] = "Internal Error";
constexpr char ERROR_500_FORM[] = "There was an unusual problem serving the request file.\n";

unordered_map<string, string> users;
Mutex mutex;

int HttpConn::user_count_ = 0;
int HttpConn::epollfd_ = -1;


// 初始化新接收的连接
// check_state_默认为分析请求行状态
void HttpConn::init() {
    mysql_ = nullptr;
    bytes_sent_ = 0;
    bytes_unsent_ = 0;
    check_state_ = CHECK_STATE_REQUEST_LINE;
    linger_ = false;
    method_ = GET;
    url_ = nullptr;
    version_ = nullptr;
    content_length_ = 0;
    host_ = nullptr;
    start_line_ = 0;
    checked_idx_ = 0;
    read_idx_ = 0;
    write_idx_ = 0;
    cgi_ = 0;
    state_ = false;
    timer_flag_ = false;
    improve_ = false;

    bzero(read_buf_, READ_BUFFER_SIZE);
    bzero(write_buf_, WRITE_BUFFER_SIZE);
    bzero(real_file_, FILENAME_LEN);
}

// 初始化连接，外部调用初始化套接字地址
void HttpConn::init(int sockfd, const sockaddr_in& addr, const char* root_dir, bool trig_mode, bool close_log, string username, string password, string db_name) {
    sockfd_ = sockfd;
    address_ = addr;

    Utils::add_fd(epollfd_, sockfd_, true, trig_mode);
    user_count_++;

    // 当浏览器出现连接重置时，可能时网站根目录出错或http响应格式出错，或者访问的文件中内容完全为空
    root_dir_ = root_dir;
    trig_mode_ = trig_mode;
    close_log_ = close_log;

    strcpy(username_, username.c_str());
    strcpy(password_, password.c_str());
    strcpy(db_name_, db_name.c_str());

    init();
}

void HttpConn::init_mysql_result(ConnPool* conn_pool) {
    // 先从连接池取一个连接
    MYSQL* mysql = nullptr;
    ConnRaii mysql_conn(&mysql, conn_pool);
    // 在user表中检索username，passwd数据，浏览器端输入
    if (mysql_query(mysql, "SELECT username, passwd FROM user"))
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
    // 从表中检索完整的结果集
    MYSQL_RES* result = mysql_store_result(mysql);
    // 返回结果集中的列数
    int numfields = mysql_num_fields(result);
    // 返回所有字段结构的数组
    MYSQL_FIELD* fields = mysql_fetch_fields(result);
    // 从结果集中获取下一行，将对应的用户名和密码，存入map中
    while (MYSQL_ROW row = mysql_fetch_row(result)) {
        string temp1(row[0]);
        string temp2(row[1]);
        users[temp1] = temp2;
    }
}

// 关闭连接，关闭一个连接，客户总量减一
void HttpConn::close(bool real_close) {
    if (real_close && (sockfd_ != -1)) {
        Utils::remove_fd(epollfd_, sockfd_);
        sockfd_ = -1;
        user_count_--;
    }
}

// 循环读取客户端数据，直到无数据可读，或对方关闭连接
// 非阻塞ET工作模式下，需要一次性将数据读完
bool HttpConn::read_once() {
    if (read_idx_ >= READ_BUFFER_SIZE) 
        return false;
    
    int bytes_read = 0;

    if (trig_mode_ == 0) {
        bytes_read = recv(sockfd_, read_buf_ + read_idx_, READ_BUFFER_SIZE - read_idx_, 0);
        read_idx_ += bytes_read;
        if (bytes_read <= 0)
            return false;
        return true;
    } else {
        while (true) {
            // 从套接字接收数据，存储在read_buf_缓冲区
            bytes_read = recv(sockfd_, read_buf_ + read_idx_, READ_BUFFER_SIZE - read_idx_, 0);
            if (bytes_read == -1) {
                // 非阻塞ET模式下，需要一次性将数据读完
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    break;
                return false;
            } else if (bytes_read == 0) 
                return false;
            // 更新read_idx_
            read_idx_ += bytes_read;
        }
        return true;
    }
}

void HttpConn::process() {
    HttpCode read_ret = process_read();

    // NO_REQUEST, 表示请求不完整，需要继续接收请求数据
    if (read_ret == NO_REQUEST) {
        // 注册并监听读事件
        Utils::modify_fd(epollfd_, sockfd_, EPOLLIN, trig_mode_);
        return;
    }

    // 调用process_write完成报文响应
    bool write_ret = process_write(read_ret);
    if (!write_ret)
        close();
        
    // 注册并监听写事件
    Utils::modify_fd(epollfd_, sockfd_, EPOLLOUT, trig_mode_);
}

HttpConn::HttpCode HttpConn::process_read() {
    // 初始化状态机状态、http请求结果
    LineState line_state = LINE_STATE_OK;
    HttpCode ret = NO_REQUEST;
    char* text = nullptr;

    // parse_line为从状态机的具体实现
    while ((check_state_ == CHECK_STATE_CONTENT && line_state == LINE_STATE_OK) ||
        (line_state = parse_line()) == LINE_STATE_OK) {
        text = get_line();
        // start_line_是每个数据行在read_buf_中的起始位置
        // checked_idx_表示状态机在read_buf_中读取的位置
        start_line_ = checked_idx_;
        LOG_INFO("%s", text);

        // 主状态机的三种状态转移逻辑
        if (check_state_ == CHECK_STATE_REQUEST_LINE) {
            // 解析请求行
            ret = parse_request_line(text);
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
        }
        else if (check_state_ == CHECK_STATE_HEADER) {
            // 解析请求头
            ret = parse_headers(text);
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            // 完整解析GET请求后，跳转到报文响应函数
            else if (ret == GET_REQUEST) 
                return do_request();
        }
        else if (check_state_ == CHECK_STATE_CONTENT) {
            // 解析请求体
            ret = parse_content(text);
            // 完整解析POST请求后，跳转到报文响应函数
            if (ret == GET_REQUEST) 
                return do_request();
            // 解析完请求体即完成报文解析，避免再次进入循环，更新line_state_
            line_state = LINE_STATE_OPEN;
        }
        else {
            return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}

// 从状态机，用于分析出一行内容
// 返回值为行的读取状态，有LINE_STATE_OK, LINE_STATE_BAD, LINE_STATE_OPEN
HttpConn::LineState HttpConn::parse_line() {
    // read_idx_指向缓冲区read_buf_的数据末尾的下一个字节
    // checked_idx_指向从状态机当前正在分析的字节
    for (char temp; checked_idx_ < read_idx_; checked_idx_++) {
        // temp为将要分析的字节
        temp = read_buf_[checked_idx_];
        if (temp == '\r') {
            // 下一个字符达到了buffer的结尾，则接受不完整，需要继续接收
            if ((checked_idx_ + 1) == read_idx_) {
                return LINE_STATE_OPEN;
            // 下一个字符是\n，将\r\n改为\0\0
            } else if (read_buf_[checked_idx_ + 1] == '\n') {
                read_buf_[checked_idx_++] = '\0';
                read_buf_[checked_idx_++] = '\0';
                return LINE_STATE_OK;
            }
            // 如果都不符合，则返回语法错误
            return LINE_STATE_BAD;

        // 如果当前字符是\n，也有可能读取到完整行
        // 一般是上次读到\r就到buffer末尾了，没有接收完整，再次接收时会出现这种情况
        } else if (temp == '\n') {
            // 前一个字符时\r，则接收完整
            if (checked_idx_ > 1 && read_buf_[checked_idx_ - 1] == '\r') {
                read_buf_[checked_idx_++] = '\0';
                read_buf_[checked_idx_++] = '\0';
                return LINE_STATE_OK;
            }
            return LINE_STATE_BAD;
        }
    }
    // 并没有接收到\r\n，需要继续接收
    return LINE_STATE_OPEN;
}

// 解析http请求行，获得请求方法，目标url及http版本号
HttpConn::HttpCode HttpConn::parse_request_line(char* text) {
    // 在http报文中，请求行用来说明请求类型、要访问的资源一挤所使用的http版本，其中各个部分之间通过\t或者空格分隔
    // 请求行中最先含有空格和\t任一字符的位置并返回
    url_ = strpbrk(text, "\t");
    // 如果没有空格或\t，则报文格式有误
    if (url_ == nullptr)
        return BAD_REQUEST;
    // 将该位置改为'\0'，用于将前面数据取出
    *(url_++) = '\0';
    // 取出数据，并通过与GET和POST比较，以确定请求方式
    char* method = text;
    if (strcasecmp(method, "GET") == 0) {
        method_ = GET;
    } else if (strcasecmp(method, "POST") == 0) {
        method_ = POST;
        cgi_ = 1;
    } else {
        return BAD_REQUEST;
    }

    // url_此时跳过了第一个空格或\t字符，但不知道之后是否还有
    // 将url_向后偏移，通过查找，继续跳过空格和\t字符，指向请求资源的第一个字符
    url_ += strspn(url_, " \t");
    // 使用与判断请求方式的相同逻辑，判断http版本号
    version_ = strpbrk(url_, " \t");
    if (version_ == nullptr)
        return BAD_REQUEST;
    *(version_++) = '\0';
    version_ += strspn(version_, " \t");
    // 仅支持HTTP/1.1
    if (strcasecmp(version_, "HTTP/1.1") != 0) 
        return BAD_REQUEST;

    // 对请求资源的前7个字符进行判断
    // 这里主要是有些报文的请求资源中会待用http://，这里需要对这种情况进行单独处理
    if (strncasecmp(url_, "http://", 7) == 0) {
        url_ += 7;
        url_ = strchr(url_, '/');
    }
    // 同样增加https情况
    if (strncasecmp(url_, "https://", 8) == 0) {
        url_ += 8;
        url_ = strchr(url_, '/');
    }

    // 一般的不会带有上述两种符号，直接是单独的/或者/后面带访问资源
    if (url_ == nullptr || url_[0] != '/')
        return BAD_REQUEST;
    // 当url_为/时，显示欢迎界面
    if (strlen(url_) == 1)
        strcat(url_, "judge.html");
    // 请求行处理完毕，将主状态机转移处理请求头
    check_state_ = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

// 解析http请求的一个头部信息
HttpConn::HttpCode HttpConn::parse_headers(char* text) {
    // 判断是空行还是请求头
    if (text[0] == '\0') {
        // 判断是GET还是POST请求
        if (content_length_ != 0) {
            // POST需要跳转到消息体处理状态
            check_state_ = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    // 解析请求头部连接字段
    } else if (strncasecmp(text, "Connection:", 11) == 0) {
        text += 11;
        // 跳过空格和\t字符
        text += strspn(text, " \t");
        // 如果是长连接，则将linger_标志设置为true
        if (strcasecmp(text, "keep-alive") == 0)
            linger_ = true;
    // 解析请求头部内容长度字段
    } else if (strncasecmp(text, "Content-length:", 15) == 0) {
        text += 15;
        text += strspn(text, " \t");
        content_length_ = atol(text);
    // 解析请求头部的HOST字段
    } else if (strncasecmp(text, "Host:", 5) == 0) {
        text += 5;
        text += strspn(text, " \t");
        host_ = text;
    } else {
        LOG_INFO("oop! unknow header: %s", text);
    }
    return NO_REQUEST;
}

// 判断http请求是否被完整读入
HttpConn::HttpCode HttpConn::parse_content(char* text) {
    // 判断buffer中是否读入了消息体
    if (read_idx_ >= (content_length_ + checked_idx_)) {
        text[content_length_] = '\0';
        // POST请求中最后为输入的用户名和密码
        content_ = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

void HttpConn::jump_to(const char url[]) {
    const int len = strlen(root_dir_);
    char* url_real = (char*) malloc(sizeof(char) * 200);
    strcpy(url_real, url);
    strncpy(real_file_ + len, url_real, strlen(url_real));
    free(url_real);
}

HttpConn::HttpCode HttpConn::do_request() {
    // 将初始化的real_file_赋值为网站根目录
    strcpy(real_file_, root_dir_);
    int len = strlen(root_dir_);
    // 找到url_中/的位置
    const char* p = strrchr(url_, '/');

    // 处理cgi_
    // 实现登录和注册校验
    if (cgi_ == 1 && (*(p + 1) == '2' || *(p + 1) == '3')) {
        // 根据标志判断是登录检测还是注册检测
        char flag = url_[1];

        char* url_real = (char*) malloc(sizeof(char) * 200);
        strcpy(url_real, "/");
        strcat(url_real, url_ + 2);
        strncpy(real_file_ + len, url_real, FILENAME_LEN - len - 1);
        free(url_real);

        // 将用户名和密码提取出来
        // user=123&passwd=123
        char name[100], password[100];
        int i;
        // 以&为分隔符，前面的为用户名
        for (i = 5; content_[i] != '&'; i++) 
            name[i - 5] = content_[i];
        name[i - 5] = '\0';
        // 以&为分隔符，后面的为密码
        int j = 0;
        for (i += 10; content_[i] != '\0'; i++, j++)
            password[j] = content_[i];
        password[j] = '\0';

        // 同步线程登录校验

        // 如果是注册，先检测数据库中是否有重名的
        // 没有重名的，进行增加数据
        if (*(p + 1) == '3') {
            char* sql_insert = (char*) malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcpy(sql_insert, "'");
            strcpy(sql_insert, name);
            strcpy(sql_insert, "', '");
            strcpy(sql_insert, password);
            strcpy(sql_insert, "')");

            // 判断map中能否找到重复的用户名
            if (users.find(name) == users.end()) {
                // 向数据库中插入数据时，需要通过锁来同步数据
                mutex.lock();
                int res = mysql_query(mysql_, sql_insert);
                users.insert(pair<string, string>(name, password));
                mutex.unlock();
                // 校验成功，跳转登录页面
                if (res == 0)
                    strcpy(url_, "/log.html");
                // 校验失败，跳转注册失败页面
                else
                    strcpy(url_, "/registerError.html");
            } else {
                strcpy(url_, "/registerError.html");
            }
        // 如果是登录，直接判断
        // 若浏览器端输入的登录名和密码在表中可以查找到，返回1，否则返回0
        } else if (*(p + 1) == '2') {
            if (users.find(name) != users.end() && users[name] == password)
                strcpy(url_, "/welcome.html");
            else
                strcpy(url_, "/logError.html");
        }
    }

    // 如果请求资源为/0，表示跳转注册页面
    if (*(p + 1) == '0') jump_to("/register.html");
    // 如果请求资源为/1，表示跳转登录页面
    else if (*(p + 1) == '1') jump_to("/log.html");    
    // 如果请求资源为/5，表示跳转图片页面   
    else if (*(p + 1) == '5') jump_to("/picture.html");  
    // 如果请求资源为/5，表示跳转视频页面   
    else if (*(p + 1) == '6') jump_to("/video.html");   
    // 如果请求资源为/5，表示跳转关注页面   
    else if (*(p + 1) == '7') jump_to("/fans.html");
    // 如果以上均不符合，直接将url_与网站目录拼接
    // 这里的情况是welcome界面，请求服务器上的一个图片
    else strncpy(real_file_ + len, url_, FILENAME_LEN - len - 1);
    

    // 通过stat获取请求资源文件信息，成功则将信息更新到file_stat_结构体
    // 失败返回NO_RESOURCE状态，表示资源不存在
    if (stat(real_file_, &file_stat_) < 0)
        return NO_RESOURCE;
    // 判断文件的权限，是否可读，不可读则返回FORBIDDEN_REQUEST状态
    if (!(file_stat_.st_mode & S_IROTH))
        return FORBIDDEN_REQUEST;
    // 判断文件类型，如果是目录，则返回BAD_REQUEST状态，表示请求报文有误
    if (S_ISDIR(file_stat_.st_mode))
        return BAD_REQUEST;

    // 以只读方式获取文件描述符，通过mmap将该文件映射到内存中
    int fd = open(real_file_, O_RDONLY);
    file_address_ = (char*) mmap(0, file_stat_.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    // 避免文件描述符的浪费和占用
    close(fd);
    // 表示请求文件存在，且可以访问
    return FILE_REQUEST;
}

bool HttpConn::add_response(const char* format, ...) {
    // 如果写入内容超出write_buf_大小则报错
    if (write_idx_ >= WRITE_BUFFER_SIZE)
        return false;
    // 定义可变参数列表
    va_list arg_list;
    // 将变量arg_list初始化为传入参数
    va_start(arg_list, format);
    // 将数据format从可变参数列表写入缓冲区写，返回写入数据的长度
    int len = vsnprintf(write_buf_ + write_idx_, WRITE_BUFFER_SIZE - 1 - write_idx_, format, arg_list);
    // 如果写入数据长度超过缓冲区剩余空间，则报错
    if (len >= (WRITE_BUFFER_SIZE - 1 - write_idx_)) {
        va_end(arg_list);
        return false;
    }
    // 更新wirte_idx_位置
    write_idx_ += len;
    // 清空可变参数列表
    va_end(arg_list);
    LOG_INFO("request: %s", write_buf_);
    return true;
}

// 添加状态行
bool HttpConn::add_status_line(int status, const char* title) {
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

// 添加消息报头，具体的添加文本长度，连接状态和空行
bool HttpConn::add_headers(int content_len) {
    bool ret = true;
    ret = ret && add_content_length(content_len);
    ret = ret && add_linger();
    ret = ret && add_blank_line();
    return ret;
}

// 添加Content-Length，表示响应报文的长度
bool HttpConn::add_content_length(int content_len) {
    return add_response("Content_Length:%d\r\n", content_len);
}

// 添加文本类型，这里是html
bool HttpConn::add_content_type() {
    return add_response("Content-Type:%s\r\n", "text/html");
}

// 添加连接状态，通知浏览器端是保持连接还是关闭
bool HttpConn::add_linger() {
    return add_response("Connection:%s\r\n", linger_ ? "keep-alive" : "close");
}

// 添加空行
bool HttpConn::add_blank_line() {
    return add_response("%s", "\r\n");
}

// 添加文本content
bool HttpConn::add_content(const char* content) {
    return add_response("%s", content);
}

bool HttpConn::process_write(HttpCode ret) {
    // 内部错误，500
    if (ret == INTERNAL_ERROR) {
        // 状态行
        add_status_line(500, ERROR_500_TITLE);
        // 消息报头
        add_headers(strlen(ERROR_500_FORM));
        if (!add_content(ERROR_500_FORM))
            return false;
    // 报文语法有误，404
    } else if (ret == BAD_REQUEST) {
        add_status_line(404, ERROR_404_TITLE);
        add_headers(strlen(ERROR_404_FORM));
        if (!add_content(ERROR_404_FORM))
            return false;
    // 资源没有访问权限，403
    } else if (ret == FORBIDDEN_REQUEST) {
        add_status_line(403, ERROR_403_TITLE);
        add_headers(strlen(ERROR_403_FORM));
        if (!add_content(ERROR_403_FORM))
            return false;

    // 文件存在，200
    } else if (ret == FILE_REQUEST) {
        add_status_line(200, OK_200_TITLE);
        if (file_stat_.st_size != 0) {
            add_headers(file_stat_.st_size);
            // 第一个iovec指针指向响应报文缓冲区，长度指向write_idx_
            iv_[0].iov_base = write_buf_;
            iv_[0].iov_len = write_idx_;
            // 第二个iovec指针指向mmap返回的文件指针，长度指向文件大小
            iv_[1].iov_base = file_address_;
            iv_[1].iov_len = file_stat_.st_size;
            iv_count_ = 2;
            // 发送的全部数据为响应报文头部信息和文件大小
            bytes_unsent_ = write_idx_ + file_stat_.st_size;
            return true;
        } else {
            // 如果请求的资源大小为0，则返回空白html文件
            constexpr char OK_STRING[] = "<html><body></body></html>";
            add_headers(strlen(OK_STRING));
            if (!add_content(OK_STRING))
                return false;
        }
    } else {
        return false;
    }
    // 除FILE_REQUEST状态外，其余状态只申请一个iovec，指向响应报文缓冲区
    iv_[0].iov_base = write_buf_;
    iv_[1].iov_len = write_idx_;
    iv_count_ = 1;
    bytes_unsent_ = write_idx_;
    return true;
}

void HttpConn::unmap() {
    if (file_address_) {
        munmap(file_address_, file_stat_.st_size);
        file_address_ = nullptr;
    }
}

bool HttpConn::write() {
    int temp = 0;
    // 若要发送的数据长度为0
    // 表示响应报文为空，一般不会出现这种情况
    if (bytes_unsent_ == 0) {
        Utils::modify_fd(epollfd_, sockfd_, EPOLLIN, trig_mode_);
        init();
        return true;
    }

    while (true) {
        // 将响应报文的状态行、消息头、空行和响应正文发送给浏览器端
        temp = writev(sockfd_, iv_, iv_count_);
        // 正常发送，temp为发送的字节数
        if (temp < 0) {
            // 判断缓冲区是否满了
            if (errno == EAGAIN) {
                // 重新注册写事件
                Utils::modify_fd(epollfd_, sockfd_, EPOLLOUT, trig_mode_);
                return true;
            }
            // 如果发送失败，但不是缓冲区问题，取消映射
            unmap();
            return false;
        }

        // 更新已发送字节数
        bytes_sent_ += temp;
        bytes_unsent_ -= temp;
        // 第一个iovec头部的数据已发送完，发送第二个iovec数据
        if (bytes_sent_ >= iv_[0].iov_len) {
            // 不再继续发送头部信息
            iv_[0].iov_len = 0;
            iv_[1].iov_base = file_address_ + (bytes_sent_ - write_idx_);
            iv_[1].iov_len = bytes_unsent_;
        // 继续发送第一个iovec头部信息的数据
        } else {
            iv_[0].iov_base = write_buf_ + bytes_sent_;
            iv_[0].iov_len -= bytes_sent_;
        }

        // 判断条件，数据已全部发送完
        if (bytes_unsent_ <= 0) {
            unmap();
            // 在epoll树上重置EPOLLONESHOT事件
            Utils::modify_fd(epollfd_, sockfd_, EPOLLIN, trig_mode_);
            // 浏览器请求为长连接
            if (linger_) {
                // 重新初始化http对象
                init();
                return true;
            } else {
                return false;
            }
        }
    }
    return false;
}
