#include "helpers.hpp"


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

Operation CreateRandomOperation(const std::string& buffer)
{
    const auto dice = rand() % 3;
    const fs::path filePath{"file_" + std::to_string(rand() % MAX_FILE_INDEX) + ".txt"};
    if (dice == 0)
    {
        return ReadOperation{filePath};
    }
    const size_t start = static_cast<size_t>(rand()) % (buffer.size() - 1024 * 1024);
    const std::string_view dataView{buffer.data() + start, 1024 * 1024};
    if (dice == 1)
    {
        return WriteOperation{filePath, dataView};
    }
    else
    {
        const size_t nChunks = static_cast<size_t>(rand()) % 5u + 5u;
        return WriteInChunksOperation{filePath, dataView, nChunks};
    }
}
