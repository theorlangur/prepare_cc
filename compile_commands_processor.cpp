#include "compile_commands_processor.h"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <system_error>

#include "analyze_include.h"
#include "generate_header_blocks.h"
#include "indexer_preparator.h"

#include "log.h"

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

    std::unique_ptr<IndexerPreparator> indexer;
    if (!options.no_dependencies)
    {
      lInfo() << "Preparing compile_commands with dependencies\n";
      indexer.reset(new IndexerPreparatorWithDependencies(options));
    }
    else
    {
      lInfo() << "Preparing compile_commands without dependencies\n";
      indexer.reset(new IndexerPreparatorCanonical(options));
    }

    std::set<fs::path> seen_paths;
    nlohmann::json res = internProcessCompileCommands(options.compile_commands_json,
     [&](nlohmann::json &entry, fs::path file, json_list &to_add)->bool{
            file = file.lexically_normal();
         if (options.is_filtered_out(file))
         {
            lInfo() << "Filtered out: " << file << "\n";
            return false;
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

         if (!options.is_filtered_in(file))
         {
            to_add.emplace_back(std::move(entry));
            lInfo() << "Not filtered in, adding as-is:" << file << "\n";
            return true;
         }

        fs::path d = file;
        d.remove_filename();
        //only once per directory
        if (seen_paths.find(d) != seen_paths.end())
        {
            lInfo() << "This path was already processed, taking quick path for :" << file << "\n";
            indexer->QuickPrepare(entry, file, to_add);
            return true;
        }

        seen_paths.insert(d);

        lInfo() << "Preparation: "
                << file << "\n";

        indexer->Prepare(entry, file, to_add);

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

void CCOptions::read_pch_config(std::string key, nlohmann::json &obj, const fs::path &base)
{
  if (!obj.is_array())
  {
    lWarn() << "Expected 'array' type for key '" << key << "'.\nGot " << obj.type_name() << " instead. Skipping.\n";
    return;
  }
  lInfo() << "Adding '"<<key<<"':\n";
  for (auto const &fout : obj) {
    if (fout.is_object())
    {
      if (fout.contains("file"))
      {
        PCH pch;
        pch.file = fout["file"].get<std::string>();
        if (pch.file.is_relative())
          pch.file = base / pch.file;
        
        if (fout.contains("pch"))
        {
          pch.dep = fout["pch"].get<std::string>();
          if (pch.dep.is_relative())
            pch.dep = base / pch.dep;
        }

        if (fout.contains("cmd"))
          pch.cmd = fout["cmd"].get<std::string>();

        if (fout.contains("cmd-from"))
        {
          pch.cmd_from = fout["cmd-from"].get<std::string>();
          if (pch.dep.is_relative())
            pch.cmd_from = base / pch.cmd_from;
        }

        if (fout.contains("apply-for"))
        {
          nlohmann::json apply_for = fout["apply-for"];
          read_path_list("apply-for", apply_for, base, pch.apply_for);
        }
        if (fout.contains("allow-includes-from"))
        {
            nlohmann::json allow_includes = fout["allow-includes-from"];
            read_path_list("allow-includes-from", allow_includes, base, pch.allow_includes_from);
        }

        if (!fs::exists(pch.file))
        {
          lErr() << "PCH target: " << pch.file << " doesn't exist. Skipping\n";
          throw std::runtime_error("PCH file must exist!");
        }

        if (!pch.dep.empty() && !fs::exists(pch.dep))
        {
          lErr() << "PCH dependency: " << pch.dep << " doesn't exist. Skipping\n";
          throw std::runtime_error("PCH dependency if specified must exist!");
        }

        PCHs.push_back(pch);
      }else
      {
        lWarn() << "No 'file' key for PCH found. Skipping\n";
      }
    }else
    {
      lWarn() << "Expected 'object'.\nGot " << fout.type_name() << " instead. Skipping.\n";
    }
  }
}

void CCOptions::read_replace_list(std::string key, nlohmann::json &obj, const fs::path &base)
{
  if (!obj.is_array())
  {
    lWarn() << "Expected 'array' type for key '" << key << "'.\nGot " << obj.type_name() << " instead. Skipping.\n";
    return;
  }
  lInfo() << "Adding '"<<key<<"':\n";
  for (auto const &fout : obj) {
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
}

void CCOptions::read_skip_deps(std::string key, nlohmann::json &obj, const fs::path &base)
{
  if (!obj.is_array())
  {
    lWarn() << "Expected 'array' type for key '" << key << "'.\nGot " << obj.type_name() << " instead. Skipping.\n";
    return;
  }
  lInfo() << "Adding 'skip-deps'\n";
  for(auto const &skip : obj)
  {
    if (skip.is_string())
      skip_dep.push_back(std::regex(skip.get<std::string>()));
    else
      throw "for skipping dependency a string representing a regular expression was expected";
  }
}

void CCOptions::read_str(std::string key, nlohmann::json &obj, const fs::path &base, StrRef ref)
{
  if (!obj.is_string())
  {
    lWarn() << "Expected 'string' type for key '" << key << "'.\nGot " << obj.type_name() << " instead. Skipping.\n";
    return;
  }
  ref = obj.get<std::string>();
  lInfo() << "got '" << key << "' field with value:" << ref << "\n";
}

void CCOptions::read_path(std::string key, nlohmann::json &obj, const fs::path &base, PathRef ref)
{
  if (!obj.is_string())
  {
    lWarn() << "Expected 'string' type for key '" << key << "'.\nGot " << obj.type_name() << " instead. Skipping.\n";
    return;
  }
  ref = obj.get<std::string>();
  lInfo() << "got '" << key << "' field with value:" << ref << "\n";
  if (ref.is_relative())
  {
    ref = base / ref;
    lInfo() << "'"<< key << "' in absolute form:" << ref
            << "\n";
  }
}

void CCOptions::read_bool(std::string key, nlohmann::json &obj, const fs::path &base, BoolRef ref)
{
  if (!obj.is_boolean())
  {
    lWarn() << "Expected 'boolean' type for key '" << key << "'.\nGot " << obj.type_name() << " instead. Skipping.\n";
    return;
  }
  ref = obj.get<bool>();
  lInfo() << "'"<< key <<"':" << ref << "\n";
}

void CCOptions::read_path_list(std::string key, nlohmann::json &obj, const fs::path &base, PathVecRef ref)
{
  if (!obj.is_array())
  {
    lWarn() << "Expected 'array' type for key '" << key << "'.\nGot " << obj.type_name() << " instead. Skipping.\n";
    return;
  }
  lInfo() << "Adding '"<<key<<"':\n";
  for (auto const &fin : obj) {
    fs::path fin_p = fin.get<std::string>();
    if (fin_p.is_relative())
      fin_p = base / fin_p;
    fin_p = fin_p.lexically_normal();
    lInfo() << fin_p << "\n";
    ref.push_back(fin_p);
  }
}

const std::map<std::string, CCOptions::Reader> CCOptions::g_OptionReaders({
  {"from", &CCOptions::read_tpl<&CCOptions::compile_commands_json>},
  {"to", &CCOptions::read_tpl<&CCOptions::save_to>},
  {"clang-cl", &CCOptions::read_tpl<&CCOptions::clang_cl>},
  {"include-dir", &CCOptions::read_tpl<&CCOptions::include_dir>},
  {"no-dependencies", &CCOptions::read_tpl<&CCOptions::no_dependencies>},
  {"dynamic-pch", &CCOptions::read_tpl<&CCOptions::dynamic_pch>},
  {"filter-in", &CCOptions::read_tpl<&CCOptions::filter_in>},
  {"filter-out", &CCOptions::read_tpl<&CCOptions::filter_out>},
  {"cmd-modifiers", &CCOptions::read_replace_list},
  {"skip-deps", &CCOptions::read_skip_deps},
  {"pch", &CCOptions::read_pch_config},
});

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
          auto it = g_OptionReaders.find(e.key());
          if (it != g_OptionReaders.end())
          {
            (this->*(it->second))(e.key(), e.value(), base);
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


bool CCOptions::PCH::can_be_applied_for(fs::path const& p) const
{
  return std::find_if(apply_for.begin(), apply_for.end(), [&](const fs::path &d){
    return is_in_dir(d, p);
  }) != apply_for.end();
}

std::string to_lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {return tolower(c); });
    return s;
}

std::string to_upper(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {return toupper(c); });
    return s;
}

std::string find_real_name(fs::path p, std::string search)
{
    std::error_code err;
    fs::path cmp_against = p / search;
    for (auto& x : fs::directory_iterator(p, fs::directory_options::skip_permission_denied))
    {
        fs::path xp = p / x;
        if (fs::equivalent(xp, cmp_against, err))
        {
            //try if a directory iterator can be created
            try
            {
              if (x.is_directory())
              {
                  auto iter = fs::directory_iterator(xp);
                  iter->exists();
              }
            }catch(...)
            {
              continue;
            }
            return x.path().filename().string();
        }
    }
    return search;
}

fs::path to_real_path(fs::path p, bool upper)
{
    if (!p.is_absolute())
        return p;

    auto i = p.begin();
    fs::path res(upper ? to_upper(i->string()) : to_lower(i->string()));
    ++i;
    res /= *i;
    for (++i; i != p.end(); ++i)
    {
        res /= find_real_name(res, i->string());
    }
    res = res.lexically_normal();
    return res;
}
