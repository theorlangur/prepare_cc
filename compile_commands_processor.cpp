#include "compile_commands_processor.h"

#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include "json.hpp"

#include "analyze_include.h"
#include "generate_header_blocks.h"

using json_filter_func = std::function<bool(nlohmann::json &entry, fs::path file)>;

nlohmann::json internProcessCompileCommands(fs::path compile_commands_json, json_filter_func filter)
{
    nlohmann::json res;
    nlohmann::json cbd;
    std::ifstream _f(compile_commands_json);

    _f >> cbd;
    if (cbd.is_array())
    {
        for(auto &el : cbd.items())
        {
            auto obj =  el.value();
            if (obj.is_object() && obj.contains("file") && obj.contains("command") && obj.contains("directory"))
            {
                auto _jfile = obj["file"];
                if (_jfile.is_string() && filter)
                {
                    //ok to process
                    fs::path target_file(_jfile.get<std::string>());
                    if (filter(obj, std::move(target_file)))
                      res.push_back(obj);
                }
            }
        }
    }
    return res;
}

void PrepareForClangD(nlohmann::json &obj, fs::path target, bool cl) 
{
    auto headerBlocks = generateHeaderBlocksForBlockFile(target);
    if (headerBlocks.has_value() && !headerBlocks->headers.empty())
    {
      std::string inc_stdafx(cl ? "/clang:--include " : "--include=");
      inc_stdafx += headerBlocks->target.string();

      std::string inc_before(cl ? "/clang:--include " : "--include=");
      inc_before += headerBlocks->include_before.string();

      std::string inc_after(cl ? "/clang:--include " : "--include=");
      inc_after += headerBlocks->include_after.string();

      nlohmann::json deps;
      auto inc = getNthRelativeInclude(target, 2);
      if (inc.has_value() && (inc->file.extension() == ".cpp" || inc->file.extension() == ".CPP")) 
      {
        nlohmann::json cpp_dep;
        cpp_dep["file"] = inc->file.string();
        cpp_dep["add"].push_back(inc_stdafx);
        deps.push_back(cpp_dep);
      }

        nlohmann::json rem_c;
        if (!cl)
			rem_c.push_back("1:-c"); // remove compile target of the original command
        else
			rem_c.push_back("1:/bigobj"); // remove compile target of the original command
        fs::path dir_stdafx = headerBlocks->target;
        dir_stdafx.remove_filename();

        for (auto &h : headerBlocks->headers) {
          fs::path rel = h.header.lexically_relative(dir_stdafx);
          if (!rel.empty() && (*rel.begin() == ".."))
            continue;
          nlohmann::json h_dep;
          h_dep["file"] = h.header.string();
          h_dep["remove"] = rem_c;
          nlohmann::json add_args;
          std::string def(cl ? "/D " : "-D");
          def += h.define;
          add_args.push_back(def);

          if (cl)
          {
              add_args.push_back("/TP");//compile as C++
          }

          add_args.push_back(inc_before);
          add_args.push_back(inc_stdafx);
          add_args.push_back(inc_after);

          h_dep["add"] = add_args;

          deps.push_back(h_dep);

          obj["dependencies"] = deps;
        }
    }
}

void PrepareForCcls(nlohmann::json &obj, fs::path target, bool cl) 
{
    auto headerBlocks = generateHeaderBlocksForBlockFile(target);
    if (headerBlocks.has_value() && !headerBlocks->headers.empty())
    {
      std::string inc_stdafx(cl ? "/clang:--include " : "--include=");
      inc_stdafx += headerBlocks->target.string();

      std::string inc_before(cl ? "/clang:--include " : "--include=");
      inc_before += headerBlocks->include_before.string();

      std::string inc_after(cl ? "/clang:--include " : "--include=");
      inc_after += headerBlocks->include_after.string();

      nlohmann::json deps;
      auto inc = getNthRelativeInclude(target, 2);
      if (inc.has_value() && (inc->file.extension() == ".cpp" || inc->file.extension() == ".CPP")) 
      {
        nlohmann::json cpp_dep;
        cpp_dep["file"] = inc->file.string();
        cpp_dep["add"].push_back(inc_stdafx);
        deps.push_back(cpp_dep);
      }

        nlohmann::json rem_c;
        if (!cl)
			rem_c.push_back("1:-c"); // remove compile target of the original command
        else
			rem_c.push_back("1:/bigobj"); // remove compile target of the original command
        fs::path dir_stdafx = headerBlocks->target;
        dir_stdafx.remove_filename();

        for (auto &h : headerBlocks->headers) {
          fs::path rel = h.header.lexically_relative(dir_stdafx);
          if (!rel.empty() && (*rel.begin() == ".."))
            continue;
          nlohmann::json h_dep;
          h_dep["file"] = h.header.string();
          h_dep["remove"] = rem_c;
          nlohmann::json add_args;
          if (cl)
          {
              add_args.push_back("/TP"); //compile as C++
			  add_args.push_back("/bigobj");
          }
          else
			  add_args.push_back("-c");
          add_args.push_back(headerBlocks->dummy_cpp.string());
          std::string def(cl ? "/D " : "-D");
          def += h.define;
          add_args.push_back(def);

          add_args.push_back(inc_before);
          add_args.push_back(inc_stdafx);
          add_args.push_back(inc_after);

          h_dep["add"] = add_args;

          deps.push_back(h_dep);

          obj["dependencies"] = deps;
        }
    }
}

bool processCompileCommandsTo(CCOptions const& options)
{
    if (!fs::exists(options.compile_commands_json))
      return false;

    std::set<fs::path> seen_paths;
    nlohmann::json res = internProcessCompileCommands(options.compile_commands_json,
     [&](nlohmann::json &entry, fs::path file)->bool{
         if (options.is_filtered_out(file))
            return false;

         if (!options.is_filtered_in(file))
            return true;
        
        if (!options.command_modifiers.empty() && entry["command"].is_string())
          entry["command"] = options.modify_command(entry["command"].get<std::string>());

        fs::path d = file;
        d.remove_filename();
        //only once per directory
        if (seen_paths.find(d) != seen_paths.end())
            return true;

        seen_paths.insert(d);

        if (options.t == IndexerType::CCLS)
          PrepareForCcls(entry, file, options.clang_cl);
        else if (options.t == IndexerType::ClangD)
          PrepareForClangD(entry, file, options.clang_cl);

         return true;
    });

    std::ofstream _out_json(options.save_to);
    _out_json << std::setw(4) << res << std::endl;

    return true;
}

bool CCOptions::is_filtered_in(fs::path const &f) const {
  if (filter_in.empty())
    return true;
  for (fs::path const &d : filter_in)
    if (*f.lexically_relative(d).begin() != "..")
      return true;
  return false;
}

bool CCOptions::is_filtered_out(fs::path const &f) const {
  if (filter_out.empty())
    return false;
  for (fs::path const &d : filter_out)
    if (*f.lexically_relative(d).begin() != "..")
      return true;
  return false;
}

std::string CCOptions::modify_command(std::string cmd) const {
  for (Replace const &r : command_modifiers)
    cmd = std::regex_replace(cmd, r.replace, r.with);
  return std::move(cmd);
}

bool CCOptions::from_json_file(fs::path config_json) 
{
    nlohmann::json cfg;
    std::ifstream _f(config_json);
    if (_f)
    {
      _f >> cfg;
      if (cfg.is_object())
      {
        for (auto const &e : cfg.items()) {
            if (e.key() == "from" && e.value().is_string())
                compile_commands_json = e.value().get<std::string>();
            else if (e.key() == "to" && e.value().is_string())
                save_to = e.value().get<std::string>();
            else if (e.key() == "clang-cl" && e.value().is_boolean())
                clang_cl = e.value().get<bool>();
          else if (e.key() == "type" && e.value().is_string())
          {
            if (e.value() == "clangd") t = IndexerType::ClangD;
            else if (e.value() == "ccls") t = IndexerType::CCLS;
          }
          else if (e.key() == "filter-in" && e.value().is_array())
          {
            for(auto const& fin : e.value())
				filter_in.push_back(fin.get<std::string>());
          }
          else if (e.key() == "filter-out" && e.value().is_array())
          {
				for (auto const& fout : e.value())
					filter_out.push_back(fout.get<std::string>());
          }else if (e.key() == "cmd-modifiers" && e.value().is_array())
          {
            for(auto const& fout : e.value())
            {
              Replace r;
              if (fout.is_string())
              {
                r.replace = std::regex(fout.get<std::string>());
              }else if (fout.is_object() && fout.contains("what"))
              {
                r.replace = std::regex(fout["what"].get<std::string>());
                if (fout.contains("with"))
                  r.with = fout["with"];
              }else
                throw "unsupported replace element format";
              command_modifiers.push_back(r);
            }
          }
        }
        return true;
      }
    }
  return false;
}