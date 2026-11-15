#include "thread_pool.hpp"

#include <stdexcept>

#include "../conf/conf.hpp"
#include "../logs/logger.hpp"

ThreadWrapper::ThreadWrapper(const bool isTemporary) : isTemporary(isTemporary) {}

ThreadWrapper::~ThreadWrapper() {
    this->join();
}

#ifndef _WIN32
void ThreadWrapper::start(std::function<void()> fn) {
    auto pFn = new std::function<void()>(std::move(fn));
    pthread_attr_t attr;
    pthread_attr_init(&attr);

    // Bound worker stack to 512 KB to reduce memory fragmentation and improve cache locality
    size_t stackSize = 512 * 1024;
    #if defined(PTHREAD_STACK_MIN)
        if (stackSize < static_cast<size_t>(PTHREAD_STACK_MIN))
            stackSize = PTHREAD_STACK_MIN;
    #endif
    pthread_attr_setstacksize(&attr, stackSize);

    auto runner = [](void* arg) -> void* {
        auto pTask = static_cast<std::function<void()>*>(arg);
        try {
            (*pTask)();
        } catch (...) {
            // Unhandled worker task exception
        }
        delete pTask;
        return nullptr;
    };

    const int rc = pthread_create(&this->threadId, &attr, runner, pFn);
    pthread_attr_destroy(&attr);
    if (rc != 0) {
        delete pFn;
        throw std::runtime_error("Failed to spawn worker thread w/ bounded stack");
    }
    this->isJoinable = true;
}

bool ThreadWrapper::joinable() const {
    return this->isJoinable;
}

void ThreadWrapper::join() {
    if (this->isJoinable) {
        pthread_join(this->threadId, nullptr);
        this->isJoinable = false;
    }
}
#else
void ThreadWrapper::start(std::function<void()> fn) {
    this->thread = std::thread(std::move(fn));
}

bool ThreadWrapper::joinable() const {
    return this->thread.joinable();
}

void ThreadWrapper::join() {
    if (this->thread.joinable())
        this->thread.join();
}
#endif

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
        identity->start([this, identity] { workerLoop(*identity); });
    } catch (...) {
        workers.pop_back();
        throw;
    }
}

bool ThreadPool::enqueue(std::function<void()> task,
                         std::function<void()> onExpire,
                         std::chrono::milliseconds timeout) {
    std::vector<std::unique_ptr<ThreadWrapper>> toJoin;
    std::vector<std::function<void()>> expiredCallbacks;

    // Run collected callbacks and join threads on every exit path outside queueMutex
    auto executeCleanup = [&]() {
        for (const auto& cb : expiredCallbacks) {
            try {
                if (cb) cb();
            } catch (...) {
                // Suppress callback exception
            }
        }
        expiredCallbacks.clear();

        for (const auto& worker : toJoin) {
            if (worker && worker->joinable()) {
                try {
                    worker->join();
                } catch (...) {
                    // Suppress join exception
                }
            }
        }
        toJoin.clear();
    };

    struct CleanupGuard {
        decltype(executeCleanup)& cleanup;
        ~CleanupGuard() { cleanup(); }
    } guard{executeCleanup};

    bool isAccepted = false;
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        if (isStopping) return false;
        pruneTempThreads(toJoin);

        // Prune expired tasks before checking queue ceiling
        const auto now = std::chrono::steady_clock::now();
        for (auto it = tasks.begin(); it != tasks.end();) {
            if (it->deadline != std::chrono::steady_clock::time_point::max() && it->deadline <= now) {
                if (it->onExpire) {
                    try {
                        expiredCallbacks.push_back(std::move(it->onExpire));
                    } catch (...) {
                        // Run callback and cleanup outside queueMutex on allocation failure
                        auto fallbackCb = std::move(it->onExpire);
                        tasks.erase(it);
                        lock.unlock();
                        if (fallbackCb) {
                            try { fallbackCb(); } catch (...) {}
                        }
                        executeCleanup();
                        lock.lock();
                        if (isStopping) return false;
                        it = tasks.begin();
                        continue;
                    }
                }
                it = tasks.erase(it);
            } else {
                ++it;
            }
        }

        // Reject new tasks when queued tasks reach configured backlog ceiling
        if (tasks.size() >= conf::MAX_REQUEST_BACKLOG)
            return false;

        size_t idleWorkers = 0;
        for (const auto& worker : workers)
            if (!worker->isInUse) ++idleWorkers;

        // Grow before accepting the task, so a thread creation failure cannot
        // leave a queued task whose caller believes it was rejected
        if (tasks.size() + 1 > idleWorkers && workers.size() < conf::MAX_THREADS_PER_CHILD)
            addWorker(true);

        const auto deadline = (timeout.count() > 0)
            ? (now + timeout)
            : std::chrono::steady_clock::time_point::max();

        tasks.push_back(QueuedTask{
            .fn = std::move(task),
            .deadline = deadline,
            .onExpire = std::move(onExpire)
        });
        isAccepted = true;
    }
    if (isAccepted)
        condition.notify_one();

    return isAccepted;
}

void ThreadPool::pruneExpired() {
    std::vector<std::function<void()>> expiredCallbacks;

    // Run collected callbacks on every exit path outside queueMutex
    auto executeCleanup = [&]() {
        for (const auto& cb : expiredCallbacks) {
            try {
                if (cb) cb();
            } catch (...) {
                // Suppress callback exception
            }
        }
        expiredCallbacks.clear();
    };

    struct CleanupGuard {
        decltype(executeCleanup)& cleanup;
        ~CleanupGuard() { cleanup(); }
    } guard{executeCleanup};

    {
        std::unique_lock<std::mutex> lock(queueMutex);
        if (tasks.empty()) return;
        const auto now = std::chrono::steady_clock::now();
        for (auto it = tasks.begin(); it != tasks.end();) {
            if (it->deadline != std::chrono::steady_clock::time_point::max() && it->deadline <= now) {
                if (it->onExpire) {
                    try {
                        expiredCallbacks.push_back(std::move(it->onExpire));
                    } catch (...) {
                        // Run callback and cleanup outside queueMutex on allocation failure
                        auto fallbackCb = std::move(it->onExpire);
                        tasks.erase(it);
                        lock.unlock();
                        if (fallbackCb) {
                            try { fallbackCb(); } catch (...) {}
                        }
                        executeCleanup();
                        lock.lock();
                        if (isStopping) return;
                        it = tasks.begin();
                        continue;
                    }
                }
                it = tasks.erase(it);
            } else {
                ++it;
            }
        }
    }
}

void ThreadPool::workerLoop(ThreadWrapper& thisThread) {
    while (true) {
        QueuedTask task;
        std::vector<std::function<void()>> expiredCallbacks;

        // Run collected callbacks on every exit path outside queueMutex
        auto executeCleanup = [&]() {
            for (const auto& cb : expiredCallbacks) {
                try {
                    if (cb) cb();
                } catch (...) {
                    // Suppress callback exception
                }
            }
            expiredCallbacks.clear();
        };

        struct CleanupGuard {
            decltype(executeCleanup)& cleanup;
            ~CleanupGuard() { cleanup(); }
        } guard{executeCleanup};

        {
            std::unique_lock<std::mutex> lock(queueMutex);
            condition.wait(lock, [this] { return isStopping || !tasks.empty(); });
            if (isStopping && tasks.empty()) return;

            const auto now = std::chrono::steady_clock::now();
            while (!tasks.empty()) {
                if (tasks.front().deadline != std::chrono::steady_clock::time_point::max() && tasks.front().deadline <= now) {
                    if (tasks.front().onExpire) {
                        try {
                            expiredCallbacks.push_back(std::move(tasks.front().onExpire));
                        } catch (...) {
                            // Run callback and cleanup outside queueMutex on allocation failure
                            auto fallbackCb = std::move(tasks.front().onExpire);
                            tasks.pop_front();
                            lock.unlock();
                            if (fallbackCb) {
                                try { fallbackCb(); } catch (...) {}
                            }
                            executeCleanup();
                            lock.lock();
                            continue;
                        }
                    }
                    tasks.pop_front();
                    continue;
                }
                task = std::move(tasks.front());
                tasks.pop_front();
                break;
            }

            if (!task.fn) {
                if (isStopping) {
                    lock.unlock();
                    executeCleanup();
                    return;
                }
                lock.unlock();
                executeCleanup();
                continue;
            }
            thisThread.isInUse = true;
        }

        // Execute expired callbacks outside queueMutex
        executeCleanup();

        // Request handlers own their cleanup. A task failure must not terminate
        // the process or permanently remove a worker from the pool.
        try {
            task.fn();
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
    std::vector<std::function<void()>> remainingCallbacks;

    // Run collected callbacks outside queueMutex on any exit path
    auto executeCleanup = [&]() {
        for (const auto& cb : remainingCallbacks) {
            try {
                if (cb) cb();
            } catch (...) {
                // Suppress callback exception
            }
        }
        remainingCallbacks.clear();
    };

    struct CleanupGuard {
        decltype(executeCleanup)& cleanup;
        std::condition_variable& cond;
        ~CleanupGuard() {
            cond.notify_all();
            cleanup();
        }
    } guard{executeCleanup, condition};

    {
        std::unique_lock<std::mutex> lock(queueMutex);
        isStopping = true;
        condition.notify_all();
        joining.swap(workers);
        while (!tasks.empty()) {
            if (tasks.front().onExpire) {
                try {
                    remainingCallbacks.push_back(std::move(tasks.front().onExpire));
                } catch (...) {
                    // Run callback and cleanup outside queueMutex on allocation failure
                    auto fallbackCb = std::move(tasks.front().onExpire);
                    tasks.pop_front();
                    lock.unlock();
                    if (fallbackCb) {
                        try { fallbackCb(); } catch (...) {}
                    }
                    executeCleanup();
                    lock.lock();
                    continue;
                }
            }
            tasks.pop_front();
        }
    }
    condition.notify_all();

    // Execute cancellation callbacks outside queueMutex
    executeCleanup();

    // Join without holding the queue lock; workers drain accepted tasks first.
    for (const auto& worker : joining)
        if (worker->joinable()) worker->join();
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
