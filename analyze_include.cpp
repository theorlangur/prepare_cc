#include "analyze_include.h"
#include "compile_commands_processor.h"

#include <cctype>
#include <fstream>
#include <ios>
#include <string_view>
#include <algorithm>
#include <set>

#include "log.h"

auto spaceFinder(std::string_view &sv)
{
  return [&](auto b) {
    return std::find_if(b, sv.end(), [](char c) { return isspace(c); });
  };
}

auto nonSpaceFinder(std::string_view &sv)
{
  return [&](auto b) {
    return std::find_if_not(b, sv.end(), [](char c) { return isspace(c); });
  };
}

std::optional<std::string_view> matchIfndefDirective(std::string_view sv)
{
  auto space = spaceFinder(sv);
  auto not_space = nonSpaceFinder(sv);

  auto first = not_space(sv.begin());
  if (first != sv.end()) {
    if (sv.size() < sizeof("#ifndef") || ((*first != '#') && (*first != '/') && ((first + 1) == sv.end() || *(first + 1) != '/')))
      return {};
    ++first;
    if (*first == '/')
      ++first;

    auto macro_beg = not_space(first);
    if (macro_beg == sv.end())
      return {};
    auto macro_end = space(macro_beg);
    if (macro_end == sv.end())
      return {};
    std::string_view macro_directive(&*macro_beg, std::distance(macro_beg, macro_end));
    if (macro_directive != "ifndef")
      return {};

    auto guard_beg = not_space(macro_end);
    auto guard_end = space(guard_beg);
    return std::string_view(&*guard_beg, std::distance(guard_beg, guard_end));
  }
  return {};
}

std::optional<std::string_view> matchIncludeDirective(std::string_view sv)
{
  auto space = spaceFinder(sv);
  auto not_space = nonSpaceFinder(sv);

  auto first = not_space(sv.begin());
  if (first != sv.end()) {
    if (*first != '#')
      return {};

    ++first;

    auto macro_beg = not_space(first);
    if (macro_beg == sv.end())
      return {};

    auto macro_end = space(macro_beg);
    if (macro_end == sv.end())
      return {};
    std::string_view macro_directive(&*macro_beg, std::distance(macro_beg, macro_end));
    if (macro_directive != "include")
      return {};

    auto inc_beg = not_space(macro_end);
    if (inc_beg == sv.end())
      return {};
    if (*inc_beg != '"')
      return {};

    ++inc_beg;
    auto inc_end = space(inc_beg);
    --inc_end;
    if (*inc_end != '"')
      return {};

    return std::string_view(&*inc_beg, std::distance(inc_beg, inc_end));
  }
  return {};
}

std::optional<std::string> getHeaderGuard(fs::path h)
{
    std::optional<std::string> res;
    std::ifstream f(h, std::ios_base::in);
    char line[2048];
    bool first = true;
    while(f.getline(line, sizeof(line)))
    {
        const char *pLine = line;
        if (first)
        {
          first = false;
          if ((uint8_t)pLine[0] == 0xef && (uint8_t)pLine[1] == 0xbb && (uint8_t)pLine[2] == 0xbf)
            pLine += 3;
        }
        std::string_view sv(pLine);
        auto not_space = nonSpaceFinder(sv);
        auto first = not_space(sv.begin());
        if (first != sv.end())
        {
            auto guard = matchIfndefDirective(sv);
            if (guard.has_value())
            {
                res = *guard;
                break;
            }
        }
    }
    return res;
}



void getAllRelativeIncludesRecursive(fs::path const& boundary, fs::path const &target, IncludeList &includes, int l, std::set<fs::path> &visited)
{
    if (visited.find(target) != visited.end())
    {
        lWarn() << "attempting to again go inside " << target << "\n";
        return;
    }

    visited.insert(target);
    IncludeIterator ii(target);
    for (Include i : ii) {
      if (is_in_dir(boundary, i.file))
        getAllRelativeIncludesRecursive(boundary, i.file, includes, l + 1, visited);

      if (!i.guard.empty())
      {
        i.level = l;
        includes.emplace_back(i);
      }
      else
        lWarn() << "inc (no guard): " << i.file << "\n";
    }
}

IncludeList getAllRelativeIncludes(fs::path h, bool recursive)
{
    fs::path d = h;
    d.remove_filename();
    IncludeList res;
    if (recursive)
    {
	  std::set<fs::path> visited;
      getAllRelativeIncludesRecursive(d, h, res, 0, visited);
    }else
    {
      IncludeIterator ii(h);
      for (const Include &i : ii) {
        if (!i.guard.empty())
          res.emplace_back(i);
        else
          lWarn() << "inc (no guard): " << i.file << "\n";
      }
    }
    return res;
}


std::optional<Include> getNthRelativeInclude(fs::path h, int n)
{
  std::optional<Include> res;
  IncludeIterator ii(h);
  for(const Include& i : ii)
  {
    if (!--n)
      return i;
  }
  return {};
}

std::optional<Include> findClosestRelativeInclude(fs::path h, fs::path const& close_to, int skip)
{
  int minDist = 0;
  std::optional<Include> res;
  IncludeIterator ii(h);
  for(const Include& i : ii)
  {
    if ((--skip) >= 0)
      continue;
    fs::path f = i.file;
    f.remove_filename();
    fs::path::iterator fIt;
    if (is_in_dir(close_to, f, fIt))
    {
      //calculate 'distance'
      int dist = 0;
      for(; fIt != f.end(); ++fIt,++dist);
      if (!res || (dist < minDist))
      {
        res = i;
        minDist = dist;
      }
    }
  }
  return res;
}

  IncludeIterator::IncludeIterator(fs::path t):
    m_TargetDir(t),
    m_File(t, std::ios_base::in)
  {
    m_TargetDir.remove_filename();
  }

  bool IncludeIterator::next()
  {
    if(m_File.getline(m_Buffer, sizeof(m_Buffer)))
    {
        const char *pLine = m_Buffer;
        if (m_First)
        {
          m_First = false;
          if ((uint8_t)pLine[0] == 0xef && (uint8_t)pLine[1] == 0xbb && (uint8_t)pLine[2] == 0xbf)
            pLine += 3;
        }

        bool res = false;
        std::string_view sv(pLine);
        auto inc = matchIncludeDirective(sv);
        if (inc.has_value())
        {
            auto inc_str = *inc;
            fs::path inc_path = inc_str;
            if (inc_path.is_relative()) {
              inc_path = m_TargetDir;
              inc_path += inc_str;
              inc_path = inc_path.lexically_normal();
            }
            auto guard = getHeaderGuard(inc_path);
            std::string _g;
            if (guard.has_value())
                _g = std::move(*guard);

            m_Include = Include(m_Line, std::move(_g), std::move(inc_path));
            res = true;
        }
        ++m_Line;
        return res;
    }else
      m_Finished = true;

    return false;
  }

  bool IncludeIterator::at_end() const
  {
    return m_Finished;
  }

  const Include& IncludeIterator::getInclude() const
  {
    return m_Include;
  }

  void IncludeIterator::Iter::operator++()
  {
    while(!i.at_end() && !i.next());
  }

  const Include &IncludeIterator::Iter::operator*()
  {
    return i.getInclude();
  }

  bool IncludeIterator::Iter::operator!=(Stop)
  {
    return !i.at_end();
  }