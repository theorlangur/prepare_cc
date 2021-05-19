#ifndef COMPILE_COMMANDS_PROCESSOR_H_
#define COMPILE_COMMANDS_PROCESSOR_H_

#include <vector>
#include <filesystem>
#include <regex>
#include "json.hpp"

namespace fs = std::filesystem;

enum class IndexerType
{
    ClangD,
    CCLS
};

struct CCOptions
{
  struct Replace
  {
    std::regex replace;
    std::string with;
  };
  struct PCH
  {
    fs::path file;
    fs::path dep;
  };
  fs::path compile_commands_json;
  fs::path save_to;
  std::vector<fs::path> filter_in;
  std::vector<fs::path> filter_out;
  std::vector<Replace> command_modifiers;
  std::vector<std::regex> skip_dep;
  IndexerType t = IndexerType::ClangD;
  bool clang_cl = false;
  std::string include_dir;
  bool include_per_file = false;
  bool no_dependencies = false;
  std::vector<PCH> PCHs;

  bool is_filtered_in(fs::path const& f) const;
  bool is_filtered_out(fs::path const& f) const;
  bool is_skipped(fs::path const& f) const;
  std::string modify_command(std::string cmd) const;
  bool from_json_file(fs::path config_json, const fs::path &base);

  using StrMemPtr = std::string CCOptions::*;
  using PathMemPtr = fs::path CCOptions::*;
  using BoolMemPtr = bool CCOptions::*;
  using PathVecMemPtr = std::vector<fs::path> CCOptions::*;

  //generic
  void read_str(std::string key, nlohmann::json &obj, const fs::path &base, StrMemPtr ptr);
  void read_path(std::string key, nlohmann::json &obj, const fs::path &base, PathMemPtr ptr);
  void read_bool(std::string key, nlohmann::json &obj, const fs::path &base, BoolMemPtr ptr);
  void read_path_list(std::string key, nlohmann::json &obj, const fs::path &base, PathVecMemPtr ptr);

  template<StrMemPtr ptr> void read_tpl(std::string key, nlohmann::json &obj, const fs::path &base){read_str(std::move(key), obj, base, ptr);}
  template<PathMemPtr ptr> void read_tpl(std::string key, nlohmann::json &obj, const fs::path &base){read_path(std::move(key), obj, base, ptr);}
  template<BoolMemPtr ptr> void read_tpl(std::string key, nlohmann::json &obj, const fs::path &base){read_bool(std::move(key), obj, base, ptr);}
  template<PathVecMemPtr ptr> void read_tpl(std::string key, nlohmann::json &obj, const fs::path &base){read_path_list(std::move(key), obj, base, ptr);}

  //specific
  void read_pch_config(std::string key, nlohmann::json &obj, const fs::path &base);
  void read_replace_list(std::string key, nlohmann::json &obj, const fs::path &base);
  void read_skip_deps(std::string key, nlohmann::json &obj, const fs::path &base);

  using Reader = void(CCOptions::*)(std::string key, nlohmann::json &obj, const fs::path &base);

  static std::map<std::string, Reader> g_OptionReaders;
};

bool processCompileCommandsTo(CCOptions const& options);

bool is_in_dir(fs::path const& parent, fs::path const& child);
bool is_in_dir(fs::path const& parent, fs::path const& child, fs::path::iterator &childIt);

#endif