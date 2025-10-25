/*
 * Initial scenario:
 * - We start with a single thread io operations processing
 *
 */
#include "threadpool.hpp"

#include <fcntl.h>
#include <filesystem>
#include <print>
#include <string>
#include <string_view>
#include <unistd.h>
#include <unordered_set>
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

size_t countNumbersInFile(const fs::path& path)
{
    if (!fs::exists(path))
    {
        return 0;
    }
    // Open the file using linux api

    const auto fd = open(path.c_str(), O_RDONLY);
    if (fd == -1)
    {
        return 0;
    }
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
    return count;
}

bool readFileHasValidNumberOfDigits(const fs::path& path)
{
    size_t count = countNumbersInFile(path);
    return count % 10 == 0;
}

std::future<size_t> countNumbersInFileAsync(const fs::path& path, ThreadPool& threadPool)
{
    std::promise<size_t> promise;
    std::future<size_t> future = promise.get_future();
    if (!fs::exists(path))
    {
        promise.set_value(0);
        return future;
    }

    const auto fd = open(path.c_str(), O_RDONLY);
    if (fd == -1)
    {
        promise.set_value(0);
        return future;
    }
    threadPool.enqueue(
        [fd, prom = std::move(promise)]() mutable
        {
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
            prom.set_value(count);
        });

    return std::async(std::launch::deferred,
                      [fd, future = std::move(future)]() mutable -> size_t
                      {
                          future.wait();
                          if (close(fd) == -1)
                          {
                              return 0;
                          }
                          return future.get();
                      });
}

std::future<bool> readFileHasValidNumberOfDigitsAsync(const fs::path& path, ThreadPool& threadPool)
{
    std::future<size_t> countFut = countNumbersInFileAsync(path, threadPool);
    return std::async(std::launch::deferred,
                      [cf = std::move(countFut)]() mutable
                      {
                          size_t count = cf.get();
                          return count % 10 == 0;
                      });
}

bool isValidNumberCount(size_t count)
{
    return count % 10 == 0;
}

std::future<bool> isValidNumberCount(std::future<size_t> countFuture)
{
    return std::async(std::launch::deferred,
                      [cf = std::move(countFuture)]() mutable
                      {
                          size_t count = cf.get();
                          return count % 10 == 0;
                      });
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

using ResultType = std::variant<bool, std::future<bool>>;

ResultType processOperation(const Operation& op, ThreadPool& threadPool)
{
    return std::visit(overloaded{[&threadPool](const ReadOperation& readOp) -> ResultType
                                 {
                                     return readFileHasValidNumberOfDigitsAsync(readOp.path,
                                                                                threadPool);
                                 },
                                 [](const WriteOperation& writeOp) -> ResultType
                                 {
                                     return writeToFile(writeOp.path, writeOp.data);
                                 }},
                      op);
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
        std::vector<ResultType> results;
        results.reserve(mOperations.size());
        for (size_t i = 0; i < mOperations.size(); ++i)
        {
            results.push_back(processOperation(mOperations[i], mThreadPool));
        }
        std::unordered_set<size_t> indicesToErase;
        indicesToErase.reserve(mOperations.size());
        for (size_t i = 0; i < results.size(); ++i)
        {
            std::visit(overloaded{[&indicesToErase, i](bool remove)
                                  {
                                      if (remove)
                                      {
                                          indicesToErase.insert(i);
                                      }
                                  },
                                  [&indicesToErase, i](std::future<bool> removeFut)
                                  {
                                      if (removeFut.get())
                                      {
                                          indicesToErase.insert(i);
                                      }
                                  }},
                       std::move(results[i]));
        }
        size_t i = 0;
        mOperations.erase(std::remove_if(mOperations.begin(),
                                         mOperations.end(),
                                         [&indicesToErase, &i](const Operation&)
                                         {
                                             return indicesToErase.contains(i++);
                                         }),
                          mOperations.end());
    }

private:
    std::vector<Operation> mOperations;
    const std::string mBuffer = generateRandomString(5 * 1024 * 1024);

    ThreadPool mThreadPool{4};
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
