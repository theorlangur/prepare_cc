#include "generate_header_blocks.h"
#include "analyze_include.h"
#include <filesystem>
#include <fstream>
#include <string_view>

std::optional<HeaderBlocks> generateHeaderBlocks(fs::path header, fs::path saveTo)
{
  if (!fs::exists(header) || !fs::exists(saveTo))
    return {};

  IncludeList includes = getAllRelativeIncludes(header);
  if (!includes.empty())
  {
      HeaderBlocks res;
      res.target = header;
      res.include_before = saveTo;
      res.include_after = saveTo;
      res.dummy_cpp = saveTo;
      res.include_before /= "include_before.h";
      res.include_after /= "include_after.h";
      res.dummy_cpp /= "dummy.cpp";

      std::ofstream _dummy(res.dummy_cpp);
      _dummy << "namespace {};\n";

      std::ofstream _before(res.include_before);
      std::ofstream _after(res.include_after);

      auto addHeader = [&](IncludeConstIter i, std::string_view directive)
      {
          HeaderBlocks::Header h;
          h.header = i->file;
          h.define = i->guard + "_INCLUDE";

          _before << '#' << directive << " defined(" << h.define << ")\n";
          _before << "#ifndef INCLUDE_AFTER_WITHOUT_TARGET\n";
          _before << "#define " << i->guard << "\n";
          _before << "#endif\n";
          for(auto j = i + 1; j != includes.end(); ++j)
              _before << "#define " << j->guard << "\n";

          _after << '#' << directive << " defined(" << h.define << ") && !defined(INCLUDE_AFTER_WITHOUT_TARGET)\n";
          _after << "#undef " << i->guard << "\n";

          res.headers.push_back(std::move(h));
      };

      addHeader(includes.begin(), "if");
      for(IncludeConstIter i = includes.begin() + 1; i != includes.end(); ++i)
      {
          addHeader(i, "elif");
      }

      _before << "#endif";
      _after << "#endif";

      return std::move(res);
  }

  return {};
}

std::optional<HeaderBlocks> generateHeaderBlocksForBlockFile(fs::path block_cpp)
{
  if (!fs::exists(block_cpp))
    return {};

  fs::path dir = block_cpp;
  dir.remove_filename();

  auto inc = getNthRelativeInclude(block_cpp);
  if (inc.has_value())
      return generateHeaderBlocks(inc->file, dir);

  return {};
}