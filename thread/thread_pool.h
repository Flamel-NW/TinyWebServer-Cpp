#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include "pch.h"

#include "lock.h"
#include "log.h"
#include "mysql_conn.h"

template <typename T>
class ThreadPool {
public:
    ThreadPool(ConnPool* conn_pool, bool actor_pattern, int thread_pool_size = 8, int max_request = 10000);
    ~ThreadPool();
    bool append(T* request);
    bool append(T* request, bool state);

private:
    // 工作线程运行的函数，它不断从工作队列中取出任务并执行之
    static void* worker(void* arg);
    void run();

    int thread_pool_size_;             // 线程池中的线程数
    int max_requests_;              // 请求队列中允许的最大请求数
    pthread_t* threads_;            // 描述线程池的数组，其大小为thread_pool_size_
    std::list<T*> workqueue_;       // 请求队列
    Mutex queue_mutex_;             // 保护请求队列的互斥锁
    Sem queue_sem_;                 // 是否有请求队列的互斥锁
    ConnPool* conn_pool_;           // 数据库连接池
    bool actor_pattern_;   // 模型切换
};


template <typename T>
ThreadPool<T>::ThreadPool(ConnPool* conn_pool, bool actor_pattern, int thread_pool_size, int max_requests)
    : conn_pool_(conn_pool), actor_pattern_(actor_pattern), thread_pool_size_(thread_pool_size), max_requests_(max_requests), threads_(nullptr) {
    if (thread_pool_size <= 0 || max_requests <= 0) {
        STDERR_FUNC_LINE();
        exit(EXIT_FAILURE);
    }
    threads_ = new pthread_t[thread_pool_size];
    if (threads_ == nullptr) {
        STDERR_FUNC_LINE();
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < thread_pool_size; i++) {
        if (pthread_create(&threads_[i], NULL, worker, this) != 0) {
            delete[] threads_;
            STDERR_FUNC_LINE();
            exit(EXIT_FAILURE);
        }
        if (pthread_detach(threads_[i]) != 0) {
            delete[] threads_;
            STDERR_FUNC_LINE();
            exit(EXIT_FAILURE);
        }
    }
}

template <typename T>
ThreadPool<T>::~ThreadPool() {
    delete[] threads_;
}

template <typename T>
bool ThreadPool<T>::append(T* request) {
    queue_mutex_.lock();
    if (workqueue_.size() > max_requests_) {
        queue_mutex_.unlock();
        return false;
    }
    workqueue_.push_back(request);
    queue_mutex_.unlock();
    queue_sem_.post();
    return true;
}

template <typename T>
bool ThreadPool<T>::append(T* request, bool state) {
    queue_mutex_.lock();
    if (workqueue_.size() > max_requests_) {
        queue_mutex_.unlock();
        return false;
    }
    request->state_ = state;
    workqueue_.push_back(request);
    queue_mutex_.unlock();
    queue_sem_.post();
    return true;
}

template <typename T>
void* ThreadPool<T>::worker(void* arg) {
    ThreadPool* pool = (ThreadPool*) arg;
    pool->run();
    return pool;
}

template <typename T>
void ThreadPool<T>::run() {
    while (true) {
        queue_sem_.wait();
        queue_mutex_.lock();
        if (workqueue_.empty()) {
            queue_mutex_.unlock();
            continue;
        }
        T* request = workqueue_.front();
        workqueue_.pop_front();
        queue_mutex_.unlock();
        if (request == nullptr)
            continue;
        if (actor_pattern_) {
            if (!request->state_) {
                if (request->read_once()) {
                    request->improve_ = true;
                    ConnRaii mysql_conn(&request->mysql_, conn_pool_);
                    request->process();
                } else {
                    request->improve_ = true;
                    request->timer_flag_ = true;
                }
            } else {
                if (!request->write())
                    request->timer_flag_ = true;
                request->improve_ = true;
            }
        } else {
            ConnRaii mysql_conn(&request->mysql_, conn_pool_);
            request->process();
        }
    }
}

#endif
