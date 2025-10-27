#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

constexpr size_t MAX_FILE_INDEX = 100;
constexpr size_t NUM_OPERATIONS = 50;
constexpr size_t NUM_ITERATIONS = 10;

struct ReadOperation
{
    fs::path path;
};

struct WriteOperation
{
    fs::path path;
    std::string_view data;
};

struct WriteInChunksOperation
{
    fs::path path;
    std::string_view data;
    size_t chunkSize;
};

template<class... Ts>
struct overloaded: Ts...
{
    using Ts::operator()...;
};

using Operation = std::variant<ReadOperation, WriteOperation, WriteInChunksOperation>;

std::string generateRandomString(size_t length);
Operation CreateRandomOperation(const std::string& buffer);


