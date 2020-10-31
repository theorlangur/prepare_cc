#ifndef ANALYZER_INCLUDE_H_
#define ANALYZER_INCLUDE_H_

#include <vector>
#include <string>
#include <filesystem>
#include <optional>

namespace fs = std::filesystem;

struct Include
{
    int lineNumber;
    std::string guard;
    fs::path file;

    Include() = default;
    Include(int l, std::string g, fs::path f): lineNumber(l), guard(std::move(g)), file(std::move(f)) {}
};
using IncludeList = std::vector<Include>;
using IncludeConstIter = IncludeList::const_iterator;

struct IncludeConstSpan
{
    IncludeConstIter from;//include of interest
    IncludeConstIter to;//past the last of the 'after' includes that needs to be silenced out with guards
};

std::optional<std::string> getHeaderGuard(fs::path h);
IncludeList getAllRelativeIncludes(fs::path h);
std::optional<Include> getNthRelativeInclude(fs::path h, int n = 1);

#endif
