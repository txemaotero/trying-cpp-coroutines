#include <algorithm>
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <coroutine>
#include <functional>
#include <future>
#include <mutex>
#include <print>
#include <queue>
#include <ranges>
#include <thread>
#include <utility>
#include <vector>

using namespace std::chrono_literals;

class ThreadPool
{
public:
    explicit ThreadPool(size_t num_threads):
        stop_(false)
    {
        for (size_t i = 0; i < num_threads; ++i)
        {
            workers_.emplace_back(
                [this]()
                {
                    while (true)
                    {
                        std::move_only_function<void()> task;

                        {
                            std::unique_lock<std::mutex> lock(queue_mutex_);
                            cv_.wait(lock,
                                     [this]()
                                     {
                                         return stop_ || !tasks_.empty();
                                     });

                            if (stop_ && tasks_.empty())
                                return;

                            task = std::move(tasks_.front());
                            tasks_.pop();
                        }

                        task();
                    }
                });
        }
    }

    ~ThreadPool()
    {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            stop_ = true;
        }
        cv_.notify_all();

        for (auto& t: workers_)
            t.join();
    }

    void push(std::move_only_function<void()> func)
    {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            if (stop_)
                throw std::runtime_error("ThreadPool has been stopped");
            tasks_.push(std::move(func));
        }
        cv_.notify_one();
    }

private:
    std::vector<std::thread> workers_;
    std::queue<std::move_only_function<void()>> tasks_;

    std::mutex queue_mutex_;
    std::condition_variable cv_;
    std::atomic<bool> stop_;
};

void setup() {}

int parallelizableCall()
{
    return rand() % 100;
}

int posprocess(const int a)
{
    return a + (rand() % 50);
}

int foo()
{
    setup();
    int a = parallelizableCall();
    return posprocess(a);
}

int countFoos()
{
    constexpr unsigned NFOOS{1000};
    std::vector<int> results;
    results.reserve(NFOOS);
    for (auto _: std::ranges::iota_view{0u, NFOOS})
    {
        results.push_back(foo());
    }
    return std::ranges::count_if(results,
                                 [](auto i)
                                 {
                                     return i > 70;
                                 });
}

std::move_only_function<int()> fooPar(ThreadPool& tp)
{
    setup();
    std::promise<int> pms;
    auto fut = pms.get_future();
    tp.push(
        [pms = std::move(pms)]() mutable
        {
            pms.set_value(parallelizableCall());
        });
    return [fut = std::move(fut)]() mutable -> int
    {
        return posprocess(fut.get());
    };
}

int countFoosPar()
{
    constexpr unsigned NFOOS{1000};
    ThreadPool tp{4};
    std::vector<std::move_only_function<int()>> results;
    results.reserve(NFOOS);
    for (auto _: std::ranges::iota_view{0u, NFOOS})
    {
        results.emplace_back(fooPar(tp));
    }
    return std::ranges::count_if(results,
                                 [](auto&& t)
                                 {
                                     return t() > 70;
                                 });
}

// --------------------------- MainDispatcher (main-thread executor) ---------------------------
class Dispatcher
{
public:
    void post(std::move_only_function<void()> f)
    {
        {
            std::scoped_lock lk(m_);
            q_.push(std::move(f));
        }
        cv_.notify_one();
    }

    // Blocks until a task is available, then runs one.
    void run_one()
    {
        std::move_only_function<void()> f;
        {
            std::unique_lock lk(m_);
            cv_.wait(lk,
                     [this]
                     {
                         return !q_.empty();
                     });
            if (q_.empty())
                return;
            f = std::move(q_.front());
            q_.pop();
        }
        f();
    }

    // Tries to run one task; returns false if none pending.
    bool try_run_one()
    {
        std::move_only_function<void()> f;
        {
            std::scoped_lock lk(m_);
            if (q_.empty())
                return false;
            f = std::move(q_.front());
            q_.pop();
        }
        f();
        return true;
    }

private:
    std::mutex m_;
    std::condition_variable cv_;
    std::queue<std::move_only_function<void()>> q_;
};

// Awaitable that, on suspend, queues the callback to thread pool. Once finished, queue resumption
// on Dispatcher
template<typename CB>
struct ThreadPoolCallAndBackToDispatcher
{
public:
    using ValueType = std::invoke_result_t<CB>;

    explicit ThreadPoolCallAndBackToDispatcher(ThreadPool& _pool,
                                               Dispatcher& _afterDispatcher,
                                               CB&& _callback):
        pool{_pool},
        afterDispatcher{_afterDispatcher},
        callback{std::forward<CB>(_callback)}
    {}

    bool await_ready() const noexcept
    {
        return false;
    }

    // Suspends the awaiting coroutine; compute on pool; resume on dispatcher.
    template<class Promise>
    void await_suspend(std::coroutine_handle<Promise> h)
    {
        h.promise().dispatcher = &afterDispatcher;
        pool.push(
            [this, h]()
            {
                value = callback();
                h.promise().dispatcher->post(
                    [h]()
                    {
                        h.resume();
                    });
            });
    }

    ValueType await_resume()
    {
        assert(value);
        return *value;
    }

private:
    ThreadPool& pool;
    Dispatcher& afterDispatcher;
    CB callback;
    std::optional<ValueType> value{};
};

template<class T>
class EagerTaskBackToDispatcher
{
public:
    struct promise_type
    {
        std::optional<T> value;
        std::exception_ptr ex;
        Dispatcher* dispatcher = nullptr;

        EagerTaskBackToDispatcher get_return_object()
        {
            return EagerTaskBackToDispatcher{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_never initial_suspend() noexcept
        {
            return {};
        }

        std::suspend_always final_suspend() noexcept
        {
            return {};
        }

        void unhandled_exception()
        {
            ex = std::current_exception();
        }

        template<class U>
        void return_value(U&& v)
        {
            value.emplace(std::forward<U>(v));
        }
    };

    using handle_t = std::coroutine_handle<promise_type>;

    explicit EagerTaskBackToDispatcher(handle_t h = {}):
        h_(h)
    {}

    EagerTaskBackToDispatcher(EagerTaskBackToDispatcher&& o) noexcept:
        h_(std::exchange(o.h_, {}))
    {}

    EagerTaskBackToDispatcher& operator=(EagerTaskBackToDispatcher&& o) noexcept
    {
        if (this != &o)
        {
            if (h_)
                h_.destroy();
            h_ = std::exchange(o.h_, {});
        }
        return *this;
    }

    EagerTaskBackToDispatcher(const EagerTaskBackToDispatcher&) = delete;
    EagerTaskBackToDispatcher& operator=(const EagerTaskBackToDispatcher&) = delete;

    ~EagerTaskBackToDispatcher()
    {
        if (h_)
            h_.destroy();
    }

    bool ready() const
    {
        return !h_ || h_.done();
    }

    T result()
    {
        auto& p = h_.promise();
        if (p.ex)
            std::rethrow_exception(p.ex);
        return std::move(*p.value);
    }

    // Expose the raw handle if you need it (optional).
    handle_t handle() const
    {
        return h_;
    }

private:
    handle_t h_;
};

// --------------------------- Waiting/pumping until all done ---------------------------
template<class TaskT>
void waitForAll(std::vector<TaskT>& tasks, Dispatcher& dispatcher)
{
    while (!std::ranges::all_of(tasks, std::identity{}, &TaskT::ready))
    {
        dispatcher.run_one();
    }
}

// --------------------------- The coroutine API you wanted ---------------------------
auto parallelizableCallCoroWrapper(ThreadPool& tp, Dispatcher& dispatcher)
{
    return ThreadPoolCallAndBackToDispatcher(tp, dispatcher, &parallelizableCall);
}

EagerTaskBackToDispatcher<int> fooCoro(ThreadPool& tp, Dispatcher& dispatcher)
{
    setup();
    int a = co_await parallelizableCallCoroWrapper(tp, dispatcher);
    co_return posprocess(a);
}

// --------------------------- Example usage ---------------------------
int countFoosCoro()
{
    constexpr unsigned NFOOS{1000};
    ThreadPool tp{4};
    Dispatcher mainDispatcher;

    std::vector<EagerTaskBackToDispatcher<int>> results;
    results.reserve(NFOOS);

    for (auto _: std::ranges::iota_view{0u, NFOOS})
    {
        results.push_back(fooCoro(tp, mainDispatcher)); // eager start
    }

    waitForAll(results, mainDispatcher);

    return std::ranges::count_if(results,
                                 [](auto& t)
                                 {
                                     return t.result() > 70;
                                 });
}

int main()
{
    std::println("Sec:  {}", countFoos());
    std::println("Par:  {}", countFoosPar());
    std::println("Coro: {}", countFoosCoro());
    return 0;
}
