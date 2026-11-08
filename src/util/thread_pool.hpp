#ifndef __THREAD_POOL_HPP
#define __THREAD_POOL_HPP

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#ifndef _WIN32
    #include <pthread.h>
#endif

class ThreadWrapper {
    public:
        ThreadWrapper(const bool isTemporary);
        ~ThreadWrapper();

        // Used to start worker thread execution w/ bounded stack
        void start(std::function<void()> fn);

        // Check if thread is joinable
        bool joinable() const;

        // Join thread if active
        void join();

        bool isTemporary;
        bool isDone = false;
        bool isInUse = false;

    private:
        #ifndef _WIN32
            pthread_t threadId{};
            bool isJoinable = false;
        #else
            std::thread thread;
        #endif
};

class ThreadPool {
    public:
        ThreadPool();
        ~ThreadPool();

        bool enqueue(std::function<void()> task);
        void stop();
        void getUsageInfo(size_t& usedThreads, size_t& totalThreads, size_t& pendingConnections);
    private:
        void workerLoop(ThreadWrapper& thisThread);
        void pruneTempThreads(std::vector<std::unique_ptr<ThreadWrapper>>& pruned);
        void addWorker(const bool isTemporary);

        std::vector<std::unique_ptr<ThreadWrapper>> workers;
        std::queue<std::function<void()>> tasks;

        std::mutex queueMutex;
        std::mutex stopMutex;
        std::condition_variable condition;
        std::atomic<bool> isStopping{false};
};

#endif