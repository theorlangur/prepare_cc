#ifndef ANALYZER_INCLUDE_H_
#define ANALYZER_INCLUDE_H_

#include <fstream>
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
std::optional<Include> findClosestRelativeInclude(fs::path h, fs::path const& close_to, int skip = 0);

class IncludeIterator
{
public:
  struct Stop{};
  struct Iter
  {
      void operator++();
      const Include& operator*();
      bool operator!=(Stop);

      IncludeIterator &i;
  };
  IncludeIterator(fs::path t);

  bool next();
  bool at_end() const;

  const Include& getInclude() const;

  auto begin() {auto i = Iter{*this}; ++i; return i;}
  auto end() {return Stop{};}
private:
  fs::path m_Target;
  fs::path m_TargetDir;
  bool m_Finished = false;
  Include m_Include;
  char m_Buffer[2048];
  std::ifstream m_File;
  int m_Line = 0;
  bool m_First = true;
};

#endif
