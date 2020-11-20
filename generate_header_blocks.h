#ifndef GENERATE_HEADER_BLOCKS_H_
#define GENERATE_HEADER_BLOCKS_H_

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct HeaderBlocks
{
    struct Header
    {
        fs::path header;
        std::string define;
		fs::path include_before;
		fs::path include_after;
    };
    fs::path target;
    fs::path include_before;
    fs::path include_after;
    fs::path dummy_cpp;

    std::vector<Header> headers;
};

std::optional<HeaderBlocks> generateHeaderBlocks(fs::path header, fs::path saveTo, bool separate_includes);
std::optional<HeaderBlocks> generateHeaderBlocksForBlockFile(fs::path block_cpp, std::string target_subdir, bool separate_includes);

#endif