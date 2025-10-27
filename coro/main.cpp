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

#include "common/helpers.hpp"


coro::task<size_t> countNumbersInFile(const fs::path& path,
                                      coro::thread_pool& threadpool,
                                      coro::io_scheduler& scheduler)
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
    co_await scheduler.schedule();
    close(fd);
    co_return count;
}

coro::task<bool> readFileHasValidNumberOfDigits(const fs::path& path,
                                                coro::thread_pool& threadpool,
                                                coro::io_scheduler& scheduler)
{
    co_await scheduler.schedule();
    size_t count = co_await countNumbersInFile(path, threadpool, scheduler);
    co_return count % 10 == 0;
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

coro::task<bool> processOperation(const Operation& op,
                                  coro::thread_pool& threadpool,
                                  coro::io_scheduler& scheduler)
{
    co_return co_await std::visit(
        overloaded{[&threadpool, &scheduler](const ReadOperation& readOp) -> coro::task<bool>
                   {
                       co_return co_await readFileHasValidNumberOfDigits(readOp.path, threadpool, scheduler);
                   },
                   [](const WriteOperation& writeOp) -> coro::task<bool>
                   {
                       co_return writeToFile(writeOp.path, writeOp.data);
                   },
                   [](const WriteInChunksOperation& writeOp) -> coro::task<bool>
                   {
                       co_return writeToFileInChunks(writeOp.path, writeOp.data, writeOp.chunkSize);
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
        std::vector<coro::task<bool>> tasks;
        for (auto& op: mOperations)
        {
            tasks.push_back(processOperation(op, *mThreadPool, *scheduler));
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
    std::shared_ptr<coro::io_scheduler> scheduler{
        coro::io_scheduler::make_shared(coro::io_scheduler::options{
            .thread_strategy = coro::io_scheduler::thread_strategy_t::spawn,
            .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_inline})};
};

int main()
{
    Component component{};
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    component.eventLoop(NUM_ITERATIONS);
    std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    std::println("Coro - Execution time: {} ms", duration);

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
