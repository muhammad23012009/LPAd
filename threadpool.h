#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <functional>
#include <thread>
#include <condition_variable>
#include <queue>

class ThreadPool
{
public:
    ThreadPool(size_t threads);
    ~ThreadPool() {
        for (auto& worker : m_workers)
            worker.join();
    }

    void post(std::function<void()> task)
    {
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_tasks.push(std::move(task));
        }
        m_cv.notify_one();
    }

private:
    std::vector<std::thread> m_workers;
    std::condition_variable m_cv;
    std::mutex m_mutex;
    std::queue<std::function<void()>> m_tasks;
};

#endif