#include "analyze_include.h"

#include <cctype>
#include <fstream>
#include <string_view>
#include <algorithm>
#include <iostream>

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

IncludeList getAllRelativeIncludes(fs::path h)
{
    IncludeList res;
    fs::path d = h;
    d.remove_filename();
    int line_num = 0;
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
        auto inc = matchIncludeDirective(sv);
        if (inc.has_value())
        {
            auto inc_str = *inc;
            fs::path inc_path = inc_str;
            if (inc_path.is_relative()) {
              inc_path = d;
              inc_path += inc_str;
              inc_path = inc_path.lexically_normal();
            }
            auto guard = getHeaderGuard(inc_path);
            if (guard.has_value())
                res.emplace_back(line_num, std::move(*guard), std::move(inc_path));
            else
              std::cout << "inc (no guard): " << inc_path << "\n";
        }
        ++line_num;
    }
    return res;
}

std::optional<Include> getNthRelativeInclude(fs::path h, int n)
{
    fs::path d = h;
    d.remove_filename();
    int line_num = 0;
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
        auto inc = matchIncludeDirective(sv);
        if (inc.has_value())
        {
            --n;
            auto inc_str = *inc;
            fs::path inc_path = inc_str;
	    if (inc_path.is_relative())
            {
		    inc_path = d;
		    inc_path += inc_str;
		    inc_path = inc_path.lexically_normal();
            }
            auto guard = getHeaderGuard(inc_path);
            std::string _g;
            if (guard.has_value())
                _g = std::move(*guard);

            if (!n)
              return Include(line_num, std::move(_g), std::move(inc_path));
        }
        ++line_num;
    }
    return {};
}
