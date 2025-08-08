#ifndef __THREAD_POOL_HPP
#define __THREAD_POOL_HPP

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>

#include "../pch/common.hpp"

class ThreadPool {
    public:
        ThreadPool(const int threadCount);
        ~ThreadPool();

        void enqueue(std::function<void()> task);
        void stop();
    private:
        void workerLoop();

        std::vector<std::thread> workers;
        std::queue<std::function<void()>> tasks;

        std::mutex queueMutex;
        std::condition_variable condition;
        std::atomic<bool> isStopping{false};
};

#endif