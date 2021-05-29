#include "generate_header_blocks.h"
#include "analyze_include.h"
#include "json.hpp"
#include "log.h"
#include <filesystem>
#include <fstream>
#include <string_view>

std::optional<HeaderBlocks> generateHeaderBlocks(fs::path header, fs::path saveTo, bool separate_includes)
{
  if (!fs::exists(header) || !fs::exists(saveTo))
  {
    lDbg() << "generateHeaderBlocks either source or destination (or both) "
              "don't exist. Aborting.\n"
           << "Source: " << header << "\n"
           << "Dest: " << saveTo << "\n";
    return {};
  }

  IncludeList includes = getAllRelativeIncludes(header, true);
  if (!includes.empty())
  {
      HeaderBlocks res;
      res.target = header;
      res.dummy_cpp = saveTo;
      res.dummy_cpp /= "dummy.cpp";
      if (!separate_includes)
      {
		  res.include_before = saveTo;
		  res.include_after = saveTo;
		  res.include_before /= "include_before.h";
		  res.include_after /= "include_after.h";
      }

      std::ofstream _dummy(res.dummy_cpp);
      _dummy << "namespace {};\n";

      std::ofstream _before(res.include_before);
      std::ofstream _after(res.include_after);

      std::string_view define_self("_CLANGD_CODE_COMPLETE_");//INCLUDE_AFTER_WITHOUT_TARGET

      auto addHeader = [&](IncludeConstIter i, std::string_view directive)
      {
          HeaderBlocks::Header h;
          h.header = i->file;

		  std::ofstream _xbefore;
		  std::ofstream _xafter;

          if (separate_includes)
          {
              h.include_before = saveTo;
              h.include_after = saveTo;

              h.include_before /= i->file.filename();
              h.include_after /= i->file.filename();
              h.include_before += ".before.inc";
              h.include_after += ".after.inc";

              _xbefore.open(h.include_before, std::ios::out);
              _xafter.open(h.include_after, std::ios::out);

			  _xbefore << "#ifndef "<< define_self <<"\n";
			  _xbefore << "#define " << i->guard << "\n";
			  _xbefore << "#endif\n";
          }else 
          { 
			  h.define = i->guard + "_INCLUDE";
			  _before << '#' << directive << " defined(" << h.define << ")\n";
			  _before << "#ifndef "<<define_self<<"\n";
			  _before << "#define " << i->guard << "\n";
			  _before << "#endif\n";
          }
          int lev = i->level;
          for (auto j = i + 1; j != includes.end(); ++j)
          {
              if (j->level < lev)
              {
                  lev = j->level;
                  continue;//when popping up we don't block outer header files
              }
              if (!separate_includes)
				  _before << "#define " << j->guard << "\n";
              else
				  _xbefore << "#define " << j->guard << "\n";
          }

          if (!separate_includes)
          {
			  _after << '#' << directive << " defined(" << h.define << ") && !defined("<<define_self<<")\n";
			  _after << "#undef " << i->guard << "\n";
          }
          else
          {
			  _xafter << "#undef " << i->guard << "\n";
          }

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
  }else
  {
    lDbg() << "No includes found: " << header << "\n";
  }

  return {};
}

std::optional<HeaderBlocks> generateHeaderBlocksForBlockFile(fs::path block_cpp, std::string target_subdir, bool separate_includes)
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
      return generateHeaderBlocks(inc->file, dir, separate_includes);
  else
      lDbg() << "No first include to generate block files from: " << block_cpp << "\n";

  return {};
}