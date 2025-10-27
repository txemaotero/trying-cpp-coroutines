/*
 * Initial scenario:
 * - We start with a single thread io operations processing
 *
 */

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wshorten-64-to-32"
#pragma clang diagnostic ignored "-Wimplicit-int-conversion"
#pragma clang diagnostic ignored "-Wsign-conversion"
#include <coro/coro.hpp>
#pragma clang diagnostic pop

#include <fcntl.h>
#include <filesystem>
#include <print>
#include <string>
#include <string_view>
#include <unistd.h>
#include <variant>
#include <vector>

namespace fs = std::filesystem;

std::string generateRandomString(size_t length)
{
    std::string str;
    str.resize(length);
    for (size_t i = 0; i < length; ++i)
    {
        str[i] = 'A' + (rand() % 26);
    }
    return str;
}

struct ReadOperation
{
    fs::path path;
};

struct WriteOperation
{
    fs::path path;
    std::string_view data;
};

using Operation = std::variant<ReadOperation, WriteOperation>;

Operation CreateRandomOperation(const std::string& buffer)
{
    if (rand() % 2 == 0)
    {
        return ReadOperation{fs::path("file_" + std::to_string(rand() % 100) + ".txt")};
    }
    else
    {
        size_t start = static_cast<size_t>(rand()) % (buffer.size() - 1024 * 1024);
        return WriteOperation{fs::path("file_" + std::to_string(rand() % 100) + ".txt"),
                              std::string_view(buffer.data() + start, 1024)};
    }
}

coro::task<size_t> countNumbersInFile(const fs::path& path,
                                      coro::thread_pool& threadpool)
{
    if (!fs::exists(path))
    {
        co_return 0;
    }
    // Open the file using linux api

    const auto fd = open(path.c_str(), O_RDONLY);
    if (fd == -1)
    {
        co_return 0;
    }

    co_await threadpool.schedule();
    size_t count = 0;
    char buffer[4096];
    ssize_t bytesRead;
    while ((bytesRead = read(fd, buffer, sizeof(buffer))) > 0)
    {
        for (ssize_t i = 0; i < bytesRead; ++i)
        {
            if (buffer[i] >= '0' && buffer[i] <= '9')
            {
                ++count;
            }
        }
    }
    close(fd);
    co_return count;
}

coro::task<bool> readFileHasValidNumberOfDigits(const fs::path& path, coro::thread_pool& threadpool)
{
    size_t count = co_await countNumbersInFile(path, threadpool);
    co_return count % 10 == 0;
}

bool writeToFile(const fs::path& path, std::string_view data)
{
    // Open the file using linux api
    const auto fd = open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd == -1)
    {
        return false;
    }
    ssize_t bytesWritten = write(fd, data.data(), data.size());
    if (bytesWritten == -1)
    {
        close(fd);
        return false;
    }
    close(fd);
    return bytesWritten == static_cast<ssize_t>(data.size());
}

template<class... Ts>
struct overloaded: Ts...
{
    using Ts::operator()...;
};

coro::task<bool> processOperation(const Operation& op, coro::thread_pool& threadpool)
{
    if (std::holds_alternative<ReadOperation>(op))
    {
        co_return co_await readFileHasValidNumberOfDigits(std::get<ReadOperation>(op).path,
                                                          threadpool);
    }
    else if (std::holds_alternative<WriteOperation>(op))
    {
        co_return writeToFile(std::get<WriteOperation>(op).path, std::get<WriteOperation>(op).data);
    }
    co_return false;
}

class Component
{
public:
    static constexpr size_t numOperations = 1000;

    Component()
    {
        mOperations.reserve(numOperations);
        for (size_t i = 0; i < numOperations; ++i)
        {
            mOperations.push_back(CreateRandomOperation(mBuffer));
        }
    }

    void eventLoop(size_t iterations)
    {
        for (size_t i = 0; i < iterations; ++i)
        {
            std::println("Iteration {}", i + 1);
            runIteration();
            refillOperationsIfNeeded();
        }
    }

    void refillOperationsIfNeeded()
    {
        while (mOperations.size() < numOperations)
        {
            mOperations.push_back(CreateRandomOperation(mBuffer));
        }
    }

    void runIteration()
    {
        std::vector<coro::task<bool>> tasks;
        for (auto& op: mOperations)
        {
            tasks.push_back(processOperation(op, *mThreadPool));
        }
        auto results = coro::sync_wait(coro::when_all(std::move(tasks)));
        std::unordered_set<size_t> indicesToRemove;
        for (size_t i = 0; i < results.size(); ++i)
        {
            if (results[i].return_value())
            {
                indicesToRemove.insert(i);
            }
        }
        size_t i = 0;
        mOperations.erase(std::remove_if(mOperations.begin(),
                                         mOperations.end(),
                                         [&indicesToRemove, &i](const Operation&)
                                         {
                                             return indicesToRemove.find(i++) !=
                                                    indicesToRemove.end();
                                         }),
                          mOperations.end());
    }

private:
    std::vector<Operation> mOperations;
    const std::string mBuffer = generateRandomString(5 * 1024 * 1024);
    std::shared_ptr<coro::thread_pool> mThreadPool{
        coro::thread_pool::make_shared(coro::thread_pool::options{.thread_count = 4})};
};

int main()
{
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    Component component{};
    component.eventLoop(10);
    std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    std::println("Execution time: {} ms", duration);

    // remove files
    for (int i = 0; i < 100; ++i)
    {
        fs::path path = fs::path("file_" + std::to_string(i) + ".txt");
        if (fs::exists(path))
        {
            fs::remove(path);
        }
    }
    return 0;
}
