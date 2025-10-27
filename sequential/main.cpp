/*
 * Initial scenario:
 * - We start with a single thread io operations processing
 *
 */
#include <fcntl.h>
#include <print>
#include <string>
#include <string_view>
#include <unistd.h>
#include <variant>
#include <vector>

#include "common/helpers.hpp"


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
        std::println("countNumbersInFile: open failed for file {}", path.string());
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

bool processOperation(const Operation& op)
{
    return std::visit(
        overloaded{[](const ReadOperation& readOp) -> bool
                   {
                       return readFileHasValidNumberOfDigits(readOp.path);
                   },
                   [](const WriteOperation& writeOp) -> bool
                   {
                       return writeToFile(writeOp.path, writeOp.data);
                   },
                   [](const WriteInChunksOperation& writeOp) -> bool
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
            mOperations.push_back(CreateRandomOperation(mBuffer));
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
            mOperations.push_back(CreateRandomOperation(mBuffer));
        }
    }

    void runIteration()
    {
        for (auto it = mOperations.begin(); it != mOperations.end();)
        {
            bool remove = processOperation(*it);
            if (remove)
            {
                it = mOperations.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

private:
    std::vector<Operation> mOperations;
    const std::string mBuffer = generateRandomString(5 * 1024 * 1024);
};

int main()
{
    Component component{};
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    component.eventLoop(NUM_ITERATIONS);
    std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    std::println("Sequential - Execution time: {} ms", duration);

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
