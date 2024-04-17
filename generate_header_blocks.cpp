#include "generate_header_blocks.h"
#include "analyze_include.h"
#include "json.hpp"
#include "log.h"
#include <filesystem>
#include <fstream>
#include <string_view>

std::optional<HeaderBlocks> generateHeaderBlocks(fs::path header, fs::path saveTo, CCOptions const& opts)
{
  if (!fs::exists(header) || !fs::exists(saveTo))
  {
    lDbg() << "generateHeaderBlocks either source or destination (or both) "
              "don't exist. Aborting.\n"
           << "Source: " << header << "\n"
           << "Dest: " << saveTo << "\n";
    return {};
  }

  IncludeList includes = getAllRelativeIncludes(header, true, opts);
  if (!includes.empty())
  {
      HeaderBlocks res;
      res.target = header;

      auto addHeader = [&](IncludeConstIter i)
      {
          HeaderBlocks::Header h;
          h.header = i->file;
          //h.define = i->guard;
          res.headers.push_back(std::move(h));
      };

      for(IncludeConstIter i = includes.begin(); i != includes.end(); ++i)
          addHeader(i);

      return std::move(res);
  }else
  {
    lDbg() << "No includes found: " << header << "\n";
  }

  return {};
}

std::optional<HeaderBlocks> generateHeaderBlocksForBlockFile(fs::path block_cpp, std::string target_subdir, CCOptions const& opts)
{
  if (!fs::exists(block_cpp))
  {
    lWarn() << "Target file for header blocks generation doesn't exist: " << block_cpp
           << "\n";
    return {};
  }

  fs::path dir = block_cpp;
  dir.remove_filename();

  if (!target_subdir.empty())
  {
	  dir /= target_subdir;

	  if (!fs::exists(dir))
	  {
		  std::error_code ec;
		  if (!fs::create_directory(dir, ec))
		  {
			  lErr() << "Could not create target directory for generated files at " << dir << "\n";
			  return {};
		  }
	  }
  }

  auto inc = getNthRelativeInclude(block_cpp);
  if (inc.has_value())
      return generateHeaderBlocks(inc->file, dir, opts);
  else
      lDbg() << "No first include to generate block files from: " << block_cpp << "\n";

  return {};
}