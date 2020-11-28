#include "compile_commands_processor.h"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <system_error>
#include "json.hpp"

#include "analyze_include.h"
#include "generate_header_blocks.h"

#include "log.h"

using json_list = std::vector<nlohmann::json>;
using json_filter_func = std::function<bool(nlohmann::json &entry, fs::path file, json_list &to_add)>;

nlohmann::json internProcessCompileCommands(fs::path compile_commands_json, json_filter_func filter)
{
    nlohmann::json res;
    nlohmann::json cbd;
    std::ifstream _f(compile_commands_json);

    _f >> cbd;
    if (cbd.is_array())
    {
        json_list temp;
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
                    filter(obj, std::move(target_file), temp);
                    for(auto &obj : temp)
                      res.push_back(std::move(obj));
                    temp.clear();
                }
            }
        }
    }
    return res;
}

std::string convert_separators(std::string in, bool convert)
{
  if (convert)
    std::replace(in.begin(), in.end(), '\\', '/');
  return in;
}

class IndexerPreparator
{
  public:
    IndexerPreparator(CCOptions const& opts): 
      opts(opts),
      cl(opts.clang_cl),
      conv_sep(cl && opts.t == IndexerType::CCLS),
      inc_base(cl ? "/clang:--include" : "--include=")
    {
    }

    void Prepare(nlohmann::json &obj, fs::path target, json_list &to_add)
    {
      this->target = std::move(target);
      this->pObj = &obj;
      this->pToAdd = &to_add;

      do_start();

      auto headerBlocks = generateHeaderBlocksForBlockFile(
          this->target, opts.include_dir, opts.include_per_file);
      if (headerBlocks.has_value() && !headerBlocks->headers.empty()) {
        pHeaderBlocks = &*headerBlocks;

        inc_stdafx = inc_base;
        inc_stdafx += headerBlocks->target.string();

        inc_before = inc_base;
        inc_before += headerBlocks->include_before.string();

        inc_after = inc_base;
        inc_after += headerBlocks->include_after.string();

        dir_stdafx = headerBlocks->target;
        dir_stdafx.remove_filename();

        auto inc = findClosestRelativeInclude(this->target, dir_stdafx, 1);
        if (inc.has_value() && (inc->file.extension() == ".cpp" ||
                                inc->file.extension() == ".CPP")) {
          do_closest_cpp_include(*inc);
        } else {
          lInfo()
              << "Didn't find any included cpp file (so no cpp dependency in "
                 "json) for file: "
              << target << "\n";
        }

        for (auto &h : headerBlocks->headers) {
          if (!is_in_dir(dir_stdafx, h.header)) {
            lInfo() << "Ignoring header " << h.header << "\n"
                    << "as it's not in the dir: " << dir_stdafx << "\n";
            continue;
          }
          if (opts.is_skipped(h.header)) {
            lInfo() << "Skipping header " << h.header << "\n";
            continue;
          }

          process_header(h);
        }

        do_header_blocks_end();
      } else {
        lInfo() << "Wasn't able to generate header block files for "
                << this->target << "\n";
      }

      do_finalize();
    }
  protected:
    virtual void do_start() = 0;
    virtual void do_finalize() = 0;
    virtual void do_closest_cpp_include(Include &inc) = 0;

    virtual void do_process_header_begin() = 0;
    virtual void do_process_header_set_file(std::string f) = 0;
    virtual void do_process_header_remove_args(const char *pWhat) = 0;
    virtual void do_process_header_add_args(std::string what) = 0;
    virtual void do_process_header_end() = 0;

    virtual void do_header_blocks_end() = 0;

    void process_header(HeaderBlocks::Header &h)
    {
        pHeader = &h;
        do_process_header_begin();
        do_process_header_set_file(convert_separators(h.header.string(), conv_sep));
        do_process_header_remove_args(cl ? "/bigobj" : "-c");
        if (cl)
          do_process_header_add_args("/TP");

        if (opts.t == IndexerType::CCLS)
        {
          if (cl)
            do_process_header_add_args("/bigobj");
          else
            do_process_header_add_args("-c");
          do_process_header_add_args(pHeaderBlocks->dummy_cpp.string());
        }

        if (!h.define.empty()) {
          std::string def(cl ? "/D " : "-D");
          def += h.define;
          do_process_header_add_args(def);
        }

        if (!h.include_before.empty()) {
          std::string inc_b(inc_base);
          inc_b += h.include_before.string();
          do_process_header_add_args(inc_b);
        } else
          do_process_header_add_args(inc_before);

        do_process_header_add_args(inc_stdafx);

        if (!h.include_after.empty()) {
          std::string inc_a(inc_base);
          inc_a += h.include_after.string();
          do_process_header_add_args(inc_a);
        } else
          do_process_header_add_args(inc_after);

        do_process_header_end();
    }

    //call args
    nlohmann::json *pObj;
    json_list *pToAdd;
    fs::path target;
    //temp stuff
    std::string inc_stdafx;
    std::string inc_before;
    std::string inc_after;
    fs::path dir_stdafx;
    HeaderBlocks::Header *pHeader;//current header

    HeaderBlocks *pHeaderBlocks;

    //config stuff
    CCOptions const& opts;
    bool cl;
    bool conv_sep;
    std::string inc_base;
};

class IndexerPreparatorWithDependencies: public IndexerPreparator
{
  public:
    IndexerPreparatorWithDependencies(CCOptions const &opts)
        : IndexerPreparator(opts) {
      if (!cl)
        rem_c.push_back(
            "1:-c"); // remove compile target of the original command
      else
        rem_c.push_back(
            "1:/bigobj"); // remove compile target of the original command
    }

  private:
    virtual void do_start() override
    {
      deps.clear();
    }

    virtual void do_finalize() override
    {
      pToAdd->emplace_back(std::move(*pObj));
    }

    virtual void do_closest_cpp_include(Include &inc) override
    {
        nlohmann::json cpp_dep;
        cpp_dep["file"] = convert_separators(inc.file.string(), conv_sep);
        cpp_dep["add"].push_back(inc_stdafx);
        lDbg() << "Cpp dependency: " << cpp_dep["file"] << "\n";
        deps.push_back(cpp_dep);
    }

    virtual void do_process_header_begin() override 
    { 
      h_dep.clear();
      add_args.clear();
    }
    virtual void do_process_header_set_file(std::string f) override
    {
      h_dep["file"] = f;

      h_dep["remove"] = rem_c;
    }
    virtual void do_process_header_remove_args(const char *pWhat) override
    {
      //dummy
    }
    virtual void do_process_header_add_args(std::string what) override
    {
      add_args.push_back(what);
    }
    virtual void do_process_header_end() override
    {
        h_dep["add"] = add_args;
        deps.push_back(h_dep);
    }

    virtual void do_header_blocks_end() override
    {
      (*pObj)["dependencies"] = deps;
    }

    nlohmann::json deps;
    nlohmann::json rem_c;
    nlohmann::json h_dep;
    nlohmann::json add_args;
};

bool processCompileCommandsTo(CCOptions const& options)
{
    if (!fs::exists(options.compile_commands_json))
    {
      lWarn() << "Compile commands json file doesn't exit:\n"
              << options.compile_commands_json << "\n";
      return false;
    }

    lWarn() << "Processing compile commands from:\n"
            << options.compile_commands_json << "\n";
    IndexerPreparatorWithDependencies indexer(options);
    std::set<fs::path> seen_paths;
    nlohmann::json res = internProcessCompileCommands(options.compile_commands_json,
     [&](nlohmann::json &entry, fs::path file, json_list &to_add)->bool{
            file = file.lexically_normal();
         if (options.is_filtered_out(file))
         {
            lInfo() << "Filtered out: " << file << "\n";
            return false;
         }

         if (!options.is_filtered_in(file))
         {
            to_add.emplace_back(std::move(entry));
            lInfo() << "Not filtered in, adding as-is:" << file << "\n";
            return true;
         }
        
        if (!options.command_modifiers.empty() && entry["command"].is_string())
        {
          std::string before = entry["command"].get<std::string>();
          entry["command"] = options.modify_command(entry["command"].get<std::string>());
          std::string after = entry["command"].get<std::string>();
          if (before != after)
          {
            lInfo() << "Applied cmd modifiers to " << file << "\n";
            lDbg() << "before: " << before << "\n"
                   << "after: " << after << "\n";
          }
        }

        fs::path d = file;
        d.remove_filename();
        //only once per directory
        if (seen_paths.find(d) != seen_paths.end())
        {
            to_add.emplace_back(std::move(entry));
            lInfo() << "This path was already processed so adding this as-is:" << file << "\n";
            return true;
        }

        seen_paths.insert(d);

        if (options.t == IndexerType::CCLS) 
        {
          lInfo() << "Preparation for ccls: "
                  << file << "\n";
        }
        else if (options.t == IndexerType::ClangD)
        {
          lInfo() << "Preparation for clangd: "
                  << file << "\n";
        }

        indexer.Prepare(entry, file, to_add);

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
    if (is_in_dir(d, f))
      return true;
  return false;
}

bool CCOptions::is_filtered_out(fs::path const &f) const {
  if (filter_out.empty())
    return false;
  for (fs::path const &d : filter_out)
    if (is_in_dir(d, f))
      return true;
  return false;
}

bool CCOptions::is_skipped(fs::path const &f) const
{
  for (const auto &re : skip_dep)
    if (std::regex_search(f.string(), re))
      return true;
  return false;
}

std::string CCOptions::modify_command(std::string cmd) const {
  for (Replace const &r : command_modifiers)
    cmd = std::regex_replace(cmd, r.replace, r.with);
  return std::move(cmd);
}

bool CCOptions::from_json_file(fs::path config_json, const fs::path &base) 
{
    if (!base.empty() && !base.is_absolute())
        throw std::runtime_error("Base directory must be absolute");

    lInfo() << "loading config from " << config_json << "\n"
            << "base dir: " << base << "\n";

    nlohmann::json cfg;
    std::ifstream _f(config_json);
    if (_f)
    {
      _f >> cfg;
      if (cfg.is_object())
      {
        for (auto const &e : cfg.items()) {
          if (e.key() == "from" && e.value().is_string()) {
            compile_commands_json = e.value().get<std::string>();
            lInfo() << "got 'from' field with value:" << compile_commands_json << "\n";
            if (compile_commands_json.is_relative())
            {
              compile_commands_json = base / compile_commands_json;
              lInfo() << "'from' in absolute form:" << compile_commands_json
                      << "\n";
            }
          } else if (e.key() == "to" && e.value().is_string()) {
            save_to = e.value().get<std::string>();
            lInfo() << "got 'to' field with value:" << save_to << "\n";
            if (save_to.is_relative())
            {
              save_to = base / save_to;
              lInfo() << "'to' in absolute form:" << save_to << "\n";
            }
          } 
          else if (e.key() == "clang-cl" && e.value().is_boolean())
          {
            clang_cl = e.value().get<bool>();
            lInfo() << "'clang-cl':" << clang_cl << "\n";
          }
          else if (e.key() == "include-dir" && e.value().is_string())
          {
              include_dir = e.value().get<std::string>();
			  lInfo() << "'include-dir':" << include_dir << "\n";
          }
          else if (e.key() == "include-per-file" && e.value().is_boolean())
          {
            include_per_file = e.value().get<bool>();
            lInfo() << "'include-per-file':" << clang_cl << "\n";
          }
          else if (e.key() == "type" && e.value().is_string()) {
            lInfo() << "'type':" << e.value() << "\n";
            if (e.value() == "clangd")
              t = IndexerType::ClangD;
            else if (e.value() == "ccls")
              t = IndexerType::CCLS;
            else
             lWarn() << "unknown option for 'type' field:" << e.value() << "\n";
          } else if (e.key() == "filter-in" && e.value().is_array()) {
            lInfo() << "Adding 'filter-in':\n";
            for (auto const &fin : e.value()) {
              fs::path fin_p = fin.get<std::string>();
              if (fin_p.is_relative())
                fin_p = base / fin_p;
              fin_p = fin_p.lexically_normal();
              lInfo() << fin_p << "\n";
              filter_in.push_back(fin_p);
            }
          } else if (e.key() == "filter-out" && e.value().is_array()) {
            lInfo() << "Adding 'filter-out':\n";
            for (auto const &fout : e.value()) {
              fs::path fout_p = fout.get<std::string>();
              if (fout_p.is_relative())
                fout_p = base / fout_p;
              fout_p = fout_p.lexically_normal();
              lInfo() << fout_p << "\n";
              filter_out.push_back(fout_p);
            }
          } else if (e.key() == "cmd-modifiers" && e.value().is_array()) {
            lInfo() << "Adding 'cmd-modifiers':\n";
            for (auto const &fout : e.value()) {
              std::string reStr;
              Replace r;
              if (fout.is_string()) {
                r.replace = std::regex(reStr = fout.get<std::string>());
              } else if (fout.is_object() && fout.contains("what")) {
                r.replace = std::regex(reStr = fout["what"].get<std::string>());
                if (fout.contains("with"))
                  r.with = fout["with"];
              } else
                throw "unsupported replace element format";
              lInfo() << "Replace '" << reStr << "' with '"<< r.with <<"'\n";
              command_modifiers.push_back(r);
            }
          }else if (e.key() == "skip-deps" && e.value().is_array())
          {
            lInfo() << "Adding 'skip-deps'\n";
            for(auto const &skip : e.value())
            {
              if (skip.is_string())
                skip_dep.push_back(std::regex(skip.get<std::string>()));
              else
                throw "for skipping dependency a string representing a regular expression was expected";
            }
          }
        }
        return true;
      }else
        lErr() << "Top element is expected to be object but it's not.\nIn json config file " << config_json << "\n";
    }else
      lErr() << "Could not open json config file " << config_json << "\n";
  return false;
}

bool is_in_dir(fs::path const& parent, fs::path const& child, fs::path::iterator &childIt)
{
  if (!parent.is_absolute() || !child.is_absolute())
    throw std::runtime_error("is_in_dir supports only absolute paths");
  auto pIt = parent.begin();
  auto pEnd = parent.end();

  auto cIt = child.begin();
  auto cEnd = child.end();

  fs::path builtChild;
  while((pIt != pEnd) && !pIt->empty() && cIt != cEnd)
  {
    builtChild /= *cIt;
    ++pIt;
    ++cIt;
  }
  childIt = cIt;
  std::error_code err;

  return fs::equivalent(parent, builtChild, err);
}

bool is_in_dir(fs::path const& parent, fs::path const& child)
{
  fs::path::iterator dummy;
  return is_in_dir(parent, child, dummy);
}