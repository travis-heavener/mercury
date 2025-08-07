#include "thread_pool.hpp"

ThreadPool::ThreadPool(const int threadCount) {
    for (int i = 0; i < threadCount; ++i)
        workers.emplace_back([this] { this->workerLoop(); });
}

ThreadPool::~ThreadPool() {
    stop();
}

void ThreadPool::enqueue(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        tasks.push(std::move(task));
    }
    condition.notify_one();
}

// Continuously load tasks onto worker threads
void ThreadPool::workerLoop() {
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

        task(); // Run task
    }
}

// Join each existing thread for shutdown
void ThreadPool::stop() {
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        isStopping = true;
    }

    condition.notify_all();

    // Join all threads
    while (workers.size() > 0) {
        std::thread& t = workers.back();

        // Join thread
        if (t.joinable())
            t.join();

        workers.pop_back();
    }
}