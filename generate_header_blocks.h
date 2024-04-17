#ifndef GENERATE_HEADER_BLOCKS_H_
#define GENERATE_HEADER_BLOCKS_H_

#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include "compile_commands_processor.h"

namespace fs = std::filesystem;

struct HeaderBlocks
{
    struct Header
    {
        fs::path header;
        std::string define;
    };
    fs::path target;
    std::vector<Header> headers;
};

std::optional<HeaderBlocks> generateHeaderBlocks(fs::path header, fs::path saveTo);
std::optional<HeaderBlocks> generateHeaderBlocksForBlockFile(fs::path block_cpp, std::string target_subdir, CCOptions const& opts);

#endif