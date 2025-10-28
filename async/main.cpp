/*
 * Initial scenario:
 * - We start with a single thread io operations processing
 *
 */
#include "common/helpers.hpp"
#include "threadpool.hpp"

#include <fcntl.h>
#include <filesystem>
#include <future>
#include <print>
#include <string>
#include <string_view>
#include <unistd.h>
#include <unordered_set>
#include <variant>
#include <vector>

namespace fs = std::filesystem;

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

bool writeToFile(const fs::path& path,
                 std::string_view data,
                 std::optional<off_t> offset = std::nullopt)
{
    // Open the file using linux api
    const auto fd = open(path.c_str(), O_WRONLY | O_CREAT | (offset ? 0 : O_APPEND), 0644);
    if (fd == -1)
    {
        std::println("write: open failed");
        return false;
    }
    if (offset)
    {
        if (::lseek(fd, *offset, SEEK_SET) == (off_t)-1)
        {
            ::close(fd);
            std::println("write: lseek failed");
            return false;
        }
    }
    ssize_t bytesWritten = write(fd, data.data(), data.size());
    if (bytesWritten == -1)
    {
        std::println("write: write failed");
        close(fd);
        return false;
    }
    close(fd);
    return bytesWritten == static_cast<ssize_t>(data.size());
}

bool writeToFileInChunks(const fs::path& path, std::string_view data, size_t nChunks)
{
    size_t totalSize = data.size();
    size_t offset = 0;
    size_t chunkSize = totalSize / nChunks;
    while (offset < totalSize)
    {
        size_t currentChunkSize = std::min(chunkSize, totalSize - offset);
        if (!writeToFile(path, data.substr(offset, currentChunkSize), offset))
        {
            std::println("writeToFileInChunks: writeToFile failed at offset {}", offset);
            return false;
        }
        offset += currentChunkSize;
    }
    return true;
}

using ResultType = std::variant<bool, std::future<bool>>;

ResultType processOperation(const Operation& op, ThreadPool& threadPool)
{
    return std::visit(
        overloaded{[&threadPool](const ReadOperation& readOp) -> ResultType
                   {
                       return readFileHasValidNumberOfDigitsAsync(readOp.path, threadPool);
                   },
                   [](const WriteOperation& writeOp) -> ResultType
                   {
                       return writeToFile(writeOp.path, writeOp.data);
                   },
                   [](const WriteInChunksOperation& writeOp) -> ResultType
                   {
                       return writeToFileInChunks(writeOp.path, writeOp.data, writeOp.chunkSize);
                   }},
        op);
}

class Component
{
public:

    Component()
    {
        mOperations.reserve(NUM_OPERATIONS);
        for (size_t i = 0; i < NUM_OPERATIONS; ++i)
        {
            mOperations.push_back(createRandomOperation(mBuffer));
        }
    }

    void eventLoop(size_t iterations)
    {
        for (size_t i = 0; i < iterations; ++i)
        {
            runIteration();
            refillOperationsIfNeeded();
        }
    }

    void refillOperationsIfNeeded()
    {
        while (mOperations.size() < NUM_OPERATIONS)
        {
            mOperations.push_back(createRandomOperation(mBuffer));
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

    ThreadPool mThreadPool{NUM_THREADS};
};

int main()
{
    Component component{};
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    component.eventLoop(NUM_ITERATIONS);
    std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    std::println("Async - Execution time: {} ms", duration);

    // remove files
    for (size_t i = 0; i < MAX_FILE_INDEX; ++i)
    {
        fs::path path = fs::path("file_" + std::to_string(i) + ".txt");
        if (fs::exists(path))
        {
            fs::remove(path);
        }
    }
    return 0;
}
