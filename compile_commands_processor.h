#ifndef COMPILE_COMMANDS_PROCESSOR_H_
#define COMPILE_COMMANDS_PROCESSOR_H_

#include <vector>
#include <filesystem>
#include <regex>

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

  bool is_filtered_in(fs::path const& f) const;
  bool is_filtered_out(fs::path const& f) const;
  bool is_skipped(fs::path const& f) const;
  std::string modify_command(std::string cmd) const;
  bool from_json_file(fs::path config_json, const fs::path &base);
};

bool processCompileCommandsTo(CCOptions const& options);

bool is_in_dir(fs::path const& parent, fs::path const& child);
bool is_in_dir(fs::path const& parent, fs::path const& child, fs::path::iterator &childIt);

#endif