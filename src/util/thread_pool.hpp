#ifndef __THREAD_POOL_HPP
#define __THREAD_POOL_HPP

#include <array>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>

class ThreadWrapper {
    public:
        ThreadWrapper(const bool isTemporary) : isTemporary(isTemporary) {};
        void setThread(std::thread t) { thread = std::move(t); };
        std::thread& getThread() { return thread; };
        bool isTemporary;
        bool isDone = false;
        bool isInUse = false;
    private:
        std::thread thread;
};

class ThreadPool {
    public:
        ThreadPool();
        ~ThreadPool();

        void enqueue(std::function<void()> task);
        void stop();
        void getUsageInfo(size_t& usedThreads, size_t& totalThreads, size_t& pendingConnections);
    private:
        void workerLoop(ThreadWrapper& thisThread);
        void pruneTempThreads();

        std::vector<ThreadWrapper> workers;
        std::queue<std::function<void()>> tasks;

        std::mutex queueMutex;
        std::condition_variable condition;
        std::atomic<bool> isStopping{false};
        std::atomic<bool> shouldPruneWorkers{false};
};

#endif