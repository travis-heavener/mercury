#include "thread_pool.hpp"

#include "../conf/conf.hpp"
#include "../logs/logger.hpp"

ThreadPool::ThreadPool() {
    std::unique_lock<std::mutex> lock(queueMutex);
    workers.reserve(conf::MAX_THREADS_PER_CHILD);
    try {
        for (size_t i = 0; i < conf::IDLE_THREADS_PER_CHILD; ++i)
            addWorker(false);
    } catch (...) {
        // A failed constructor has no destructor: join workers already started.
        lock.unlock();
        stop();
        throw;
    }
}

ThreadPool::~ThreadPool() {
    stop();
}

// Called under queueMutex. The worker object stays at the same address when
// the vector grows or when other workers are removed.
void ThreadPool::addWorker(const bool isTemporary) {
    auto worker = std::make_unique<ThreadWrapper>(isTemporary);
    ThreadWrapper* identity = worker.get();
    workers.push_back(std::move(worker));
    try {
        identity->setThread(std::thread([this, identity] { workerLoop(*identity); }));
    } catch (...) {
        workers.pop_back();
        throw;
    }
}

bool ThreadPool::enqueue(std::function<void()> task) {
    std::vector<std::unique_ptr<ThreadWrapper>> toJoin;
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        if (isStopping) return false;
        pruneTempThreads(toJoin);

        size_t idleWorkers = 0;
        for (const auto& worker : workers)
            if (!worker->isInUse) ++idleWorkers;

        // Grow before accepting the task, so a thread creation failure cannot
        // leave a queued task whose caller believes it was rejected
        if (tasks.size() + 1 > idleWorkers && workers.size() < conf::MAX_THREADS_PER_CHILD)
            addWorker(true);
        tasks.push(std::move(task));
    }
    condition.notify_one();

    // Join finished temporary threads outside queueMutex
    for (const auto& worker : toJoin) {
        if (worker->getThread().joinable())
            worker->getThread().join();
    }
    return true;
}

void ThreadPool::workerLoop(ThreadWrapper& thisThread) {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            condition.wait(lock, [this] { return isStopping || !tasks.empty(); });
            if (isStopping && tasks.empty()) return;

            task = std::move(tasks.front());
            tasks.pop();
            thisThread.isInUse = true;
        }

        // Request handlers own their cleanup. A task failure must not terminate
        // the process or permanently remove a worker from the pool.
        try {
            task();
        } catch (...) {
            ERROR_LOG << "Unhandled worker task exception" << std::endl;
        }

        {
            std::lock_guard<std::mutex> lock(queueMutex);
            thisThread.isInUse = false;
            if (thisThread.isTemporary && tasks.empty()) {
                thisThread.isDone = true;
                return;
            }
        }
    }
}

// Prune finished temporary workers under queueMutex without joining inline
void ThreadPool::pruneTempThreads(std::vector<std::unique_ptr<ThreadWrapper>>& pruned) {
    for (auto it = workers.begin(); it != workers.end();) {
        if (!(*it)->isDone) {
            ++it;
            continue;
        }
        pruned.push_back(std::move(*it));
        it = workers.erase(it);
    }
}

void ThreadPool::stop() {
    std::lock_guard<std::mutex> stopLock(stopMutex);
    std::vector<std::unique_ptr<ThreadWrapper>> joining;
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        isStopping = true;
        joining.swap(workers);
    }
    condition.notify_all();

    // Join without holding the queue lock; workers drain accepted tasks first.
    for (const auto& worker : joining)
        if (worker->getThread().joinable()) worker->getThread().join();

    // Discard any remaining queued tasks to release captured resources
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        while (!tasks.empty())
            tasks.pop();
    }
}

void ThreadPool::getUsageInfo(size_t& usedThreads, size_t& totalThreads, size_t& pendingConnections) {
    std::lock_guard<std::mutex> lock(queueMutex);
    for (const auto& worker : workers)
        if (!worker->isDone) {
            usedThreads += worker->isInUse ? 1 : 0;
            ++totalThreads;
        }
    pendingConnections += tasks.size();
}
