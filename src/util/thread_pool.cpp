#include "thread_pool.hpp"

#include "../conf/conf.hpp"

ThreadPool::ThreadPool() {
    // Create idle threads
    workers.reserve(conf::MAX_THREADS_PER_CHILD);

    for (size_t i = 0; i < conf::IDLE_THREADS_PER_CHILD; ++i) {
        workers.emplace_back( ThreadWrapper(false) );
        workers.back().setThread( std::thread([this] { this->workerLoop(workers.back()); }) );
    }
}

ThreadPool::~ThreadPool() {
    stop();
}

void ThreadPool::enqueue(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        tasks.push(std::move(task));

        // Check if there are too many connections pending
        if (tasks.size() > workers.size() && workers.size() < conf::MAX_THREADS_PER_CHILD) {
            // Create a new temporary thread
            workers.emplace_back( ThreadWrapper(true) );
            workers.back().setThread( std::thread([this] { this->workerLoop(workers.back()); }) );
        }
    }

    // Notify next available worker
    condition.notify_one();
}

// Continuously load tasks onto worker threads
void ThreadPool::workerLoop(ThreadWrapper& thisThread) {
    while (true) {
        std::function<void()> task;

        {
            // Prevent race condition
            std::unique_lock<std::mutex> lock(queueMutex);
            condition.wait(lock, [this] {
                return isStopping || !tasks.empty();
            });

            // Exit early if stopping
            if (isStopping && tasks.empty())
                return;

            // Pop task from front
            task = std::move(tasks.front());
            tasks.pop();
        }

        thisThread.isInUse = true;
        task(); // Run task
        thisThread.isInUse = false;

        // If this is a temporary thread and the backlog is decreasing in size, destroy self
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            if (thisThread.isTemporary && tasks.size() < workers.size()) {
                // Tell the ThreadPool to prune temporary threads
                thisThread.isDone = true;
                this->shouldPruneWorkers = true;
                return;
            } else if (!thisThread.isTemporary && this->shouldPruneWorkers) {
                // Invoke prune
                this->pruneTempThreads();
            }
        }
    }
}

// Prunes any finished threads
void ThreadPool::pruneTempThreads() {
    // Already called while locked, no need to lock here!
    for (size_t i = 0; i < workers.size(); ++i) {
        if (!workers[i].isTemporary || !workers[i].isDone) continue;

        // Temp thread is done, erase it
        std::thread& t = workers[i].getThread();
        if (t.joinable())
            t.join();

        workers.erase(workers.begin() + i);
        --i;
    }

    // Reset flag
    this->shouldPruneWorkers = false;
}

// Join each existing thread for shutdown
void ThreadPool::stop() {
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        isStopping = true;
    }

    condition.notify_all();

    // Join all threads
    while (!workers.empty()) {
        // Join thread
        std::thread& t = workers.back().getThread();
        if (t.joinable())
            t.join();

        // Destroy ThreadWrapper
        workers.pop_back();
    }
}

// Gathers usage info for this ThreadPool
void ThreadPool::getUsageInfo(size_t& usedThreads, size_t& totalThreads, size_t& pendingConnections) {
    // Protect against consecutive IO to workers
    std::lock_guard<std::mutex> lock(queueMutex);
    for (const ThreadWrapper& tw : workers)
        usedThreads += tw.isInUse ? 1 : 0;
    totalThreads += workers.size();
    pendingConnections += tasks.size();
}