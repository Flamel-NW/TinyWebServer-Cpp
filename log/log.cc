#include "log.h"

using namespace std;

Log::~Log() {
    if (fp_ != nullptr)
        fclose(fp_);
}

void* Log::async_write_log() {
    string temp;
    // 从阻塞队列中取出一个日志string，写入文件
    while (log_queue_->pop(temp)) {
        mutex_.lock();
        fputs(temp.c_str(), fp_);
        mutex_.unlock();
    }
    return nullptr;
}

// 异步需要设置阻塞队列的长度，同步不需要设置
bool Log::init(const char* filename, bool close_log, int buf_size, int max_lines, int max_queue_size) {
    // 如果设置了max_queue_size，则设置为异步
    if (max_queue_size > 0) {
        // 设置写入方式
        isasync_ = true;
        // 创建并设置阻塞队列长度
        log_queue_ = new BlockingQueue<string>(max_queue_size);
        pthread_t tid;
        // flush_log_thread为的回调函数，这里表示创建线程异步写日志
        pthread_create(&tid, nullptr, flush_log_thread, nullptr);
    }

    close_log_ = close_log;
    // 输出内容的长度
    buf_size_ = buf_size;
    buf_ = new char[buf_size_];
    bzero(buf_, sizeof(char) * buf_size_);
    // 日志的最大行数
    max_lines_ = max_lines;

    time_t t = time(nullptr);
    struct tm* p_tm = localtime(&t);
    // 从后往前找到第一个/的位置
    const char* p = strrchr(filename, '/');
    char log_name[256];
    bzero(log_name, sizeof(log_name));

    // 相当于自定义日志名
    // 若输入的文件名没有/，则直接将时间+文件名作为日志名
    if (p == nullptr) {
        snprintf(log_name, 512, "%d_%02d_%02d_%s", p_tm->tm_year + 1900, p_tm->tm_mon + 1, p_tm->tm_mday, filename);
    } else {
        // 将/的位置向后移动一个位置，然后复制到filename_中
        // p - filename + 1是文件所在路径文件夹的长度
        // dir_相当于./
        strcpy(filename_, p + 1);
        strncpy(dir_, filename, p - filename + 1);
        // 后面的参数和format有关
        snprintf(log_name, 512, "%s%d_%02d_%02d_%s", dir_, p_tm->tm_year + 1900, p_tm->tm_mon + 1, p_tm->tm_mday, filename);
    }
    date_ = p_tm->tm_mday;

    fp_ = fopen(log_name, "a");
    if (fp_ == nullptr)
        return false;
    return true;
}

void Log::write_log(int level, const char* format, ...) {
    struct timeval now = { 0, 0 };
    gettimeofday(&now, nullptr);
    time_t t = now.tv_sec;
    struct tm* p_tm = localtime(&t);
    char str[16];
    bzero(str, sizeof(str));

    // 日志分级
    if (level == 0) strcpy(str, "[debug]:");
    else if (level == 1) strcpy(str, "[info]:");
    else if (level == 2) strcpy(str, "[warn]:");
    else if (level == 3) strcpy(str, "[error]:");
    else strcpy(str, "[info]:");
    
    mutex_.lock();
    // 更新现有行数
    count_++;

    // 日志不是今天或写入的日志行数是最大行的倍数
    // max_lines_为最大行数
    if (date_ != p_tm->tm_mday || count_ % max_lines_ == 0) {
        char new_log[256];
        bzero(new_log, sizeof(new_log));
        fflush(fp_);
        fflush(fp_);
        char tail[16];
        bzero(tail, sizeof(tail));

        // 格式化日志名的中间部分
        snprintf(tail, 16, "%d_%02d_%02d_", p_tm->tm_year + 1900, p_tm->tm_mon + 1, p_tm->tm_mday);

        // 如果时间不是今天，则创建今天的日志，更新date_和count_
        if (date_ != p_tm->tm_mday) {
            snprintf(new_log, 512, "%s%s%s", dir_, tail, filename_);
            date_ = p_tm->tm_mday;
            count_ = 0;
        } else {
            // 超过了最大行，在之前的日志名基础上加后缀，count_/max_lines_
            snprintf(new_log, 512, "%s%s%s.%lld", dir_, tail, filename_, count_ / max_lines_);
        }
        fp_ = fopen(new_log, "a");
    }

    mutex_.unlock();
    
    va_list valist;
    // 将传入的format参数赋值给valist，便于格式化输出
    va_start(valist, format);

    string temp;
    mutex_.lock();

    // 写入内容格式：时间 + 内容
    // 时间格式化，snprintf成功返回写字符的总数，其中不包括结尾的空字符
    int n = snprintf(buf_, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
        p_tm->tm_year + 1900, p_tm->tm_mon + 1, p_tm->tm_mday,
        p_tm->tm_hour, p_tm->tm_min, p_tm->tm_sec, now.tv_usec, str);
    // 内容格式化，用于向字符串中打印数据、数据格式用户自定义，返回写入到字符数组str中的字符个数，其中不包括结尾的空字符
    int m = vsnprintf(buf_ + n, buf_size_ - 1, format, valist);
    buf_[n + m] = '\n';
    buf_[n + m + 1] = '\0';
    temp = buf_;

    mutex_.unlock();

    // 若isasync_为true表示异步，默认为同步
    // 若异步，则将日志信息加入阻塞队列，同步则加锁向文件中写
    if (isasync_ && !log_queue_->full()) {
        log_queue_->push(temp);
    } else {
        mutex_.lock();
        fputs(temp.c_str(), fp_);
        mutex_.unlock();
    }
    va_end(valist);
}
