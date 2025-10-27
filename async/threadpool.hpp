#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

class ThreadPool
{
public:
    using Task = std::move_only_function<void()>;

    ThreadPool(size_t numThreads)
    {
        for (size_t i = 0; i < numThreads; ++i)
        {
            mThreads.emplace_back(
                [this]()
                {
                    while (true)
                    {
                        Task task;
                        {
                            std::unique_lock<std::mutex> lock(mMutex);
                            mCondition.wait(lock,
                                            [this]()
                                            {
                                                return mStop || !mTasks.empty();
                                            });
                            if (mStop && mTasks.empty())
                            {
                                return;
                            }
                            task = std::move(mTasks.front());
                            mTasks.pop();
                        }
                        task();
                    }
                });
        }
    }

    ~ThreadPool()
    {
        {
            std::unique_lock<std::mutex> lock(mMutex);
            mStop = true;
        }
        mCondition.notify_all();
        for (std::thread& thread: mThreads)
        {
            thread.join();
        }
    }


    void enqueue(Task&& task)
    {
        {
            std::lock_guard<std::mutex> lock(mMutex);
            mTasks.push(std::move(task));
        }
        mCondition.notify_one();
    }

private:
    std::vector<std::thread> mThreads;

    std::queue<Task> mTasks;
    std::mutex mMutex;
    std::condition_variable mCondition;
    bool mStop = false;
};
