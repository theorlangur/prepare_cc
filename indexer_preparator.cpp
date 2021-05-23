#include "indexer_preparator.h"
#include "compile_commands_processor.h"
#include "log.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iterator>

void remove_search_and_next(std::string &where, std::string_view const & what);

/*************************************************************************/
/*IndexerPreparator                                                      */
/*************************************************************************/
IndexerPreparator::IndexerPreparator(CCOptions const &opts)
    : opts(opts), cl(opts.clang_cl),
      inc_base(cl ? "/clang:--include" : "--include="),
      compile_target(cl ? "/bigobj" : "-c"),
      define_opt(cl ? "/D " : "-D"),
      PCHs(opts.PCHs) {}

std::string IndexerPreparator::add_pch_include(std::string cmd, fs::path pch) const
{
    std::string inc_stdafx{inc_base};
    inc_stdafx += pch.string();
    size_t pos = cmd.find("include");
    if (pos == std::string::npos)
      pos = cmd.find(' ');
    else
      pos = cmd.rfind(' ', pos);

    if (pos == std::string::npos)
      pos = cmd.size();

    inc_stdafx.insert(0, " ");
    cmd.insert(pos, inc_stdafx);
    return std::move(cmd);
} 

void IndexerPreparator::add_header_type(std::string &cmd) const
{
    if (cl)
      cmd += " /clang:";
    else
      cmd += " ";
    cmd += "-xc++-header";
} 

void IndexerPreparator::add_target(std::string &cmd, std::string const& tgt) const
{
    cmd = cmd + " " + std::string(compile_target) + " " + tgt;
} 

bool IndexerPreparator::try_apply_pch(nlohmann::json &obj, fs::path target) const
{
  fs::path dir = target;
  dir.remove_filename();
  if (auto i = pchForPath.find(dir); i != pchForPath.end())
  {
    obj["command"] = add_pch_include(obj["command"], PCHs[i->second].file);
    return true;
  }else
  {
    auto pchIt = std::find_if(PCHs.begin(), PCHs.end(), [&](const CCOptions::PCH &p){
      return p.can_be_applied_for(target);
    });

    if (pchIt != PCHs.end())
    {
      obj["command"] = add_pch_include(obj["command"], pchIt->file);
      return true;
    }
  }
  return false;
}

void IndexerPreparator::QuickPrepare(nlohmann::json &obj, fs::path target, json_list &to_add)
{
  fs::path dir = target;
  dir.remove_filename();
  if (auto i = pchForPath.find(dir); i != pchForPath.end())
    obj["command"] = add_pch_include(obj["command"], PCHs[i->second].file);
  to_add.emplace_back(std::move(obj));
}

void IndexerPreparator::Prepare(nlohmann::json &obj, fs::path target,
                                json_list &to_add) {
  this->target = std::move(target);
  this->pObj = &obj;
  this->pToAdd = &to_add;

  if (cl)
  {
    std::string _cmd = (*pObj)["command"];
    hasTPInCommand = _cmd.find("/TP") != std::string::npos;
  }

  do_start();

  auto headerBlocks = generateHeaderBlocksForBlockFile(
      this->target, opts.include_dir, opts.include_per_file);
  if (headerBlocks.has_value() && !headerBlocks->headers.empty()) {
    pHeaderBlocks = &*headerBlocks;

    do_check_pch();

    if (!inc_pch.empty())
      (*pObj)["command"] = add_pch_include((*pObj)["command"], inc_pch);

    inc_stdafx = inc_base;
    inc_stdafx += headerBlocks->target.string();

    inc_before = inc_base;
    inc_before += headerBlocks->include_before.string();

    inc_after = inc_base;
    inc_after += headerBlocks->include_after.string();

    dir_stdafx = headerBlocks->target;
    dir_stdafx.remove_filename();

    auto inc = findClosestRelativeInclude(this->target, dir_stdafx, 1);
    if (inc.has_value() &&
        (inc->file.extension() == ".cpp" || inc->file.extension() == ".CPP")) {
      do_closest_cpp_include(*inc);
    } else {
      lInfo() << "Didn't find any included cpp file (so no cpp dependency in "
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
    lInfo() << "Wasn't able to generate header block files for " << this->target
            << "\n";
  }

  do_finalize();
}

void IndexerPreparator::add_single_pch(pch_it i)
{
  std::string cmd;
  if (!i->cmd.empty())
    cmd = i->cmd;
  else
  {
    cmd = (*pObj)["command"];
    remove_search_and_next(cmd, compile_target);
    if (!cl) remove_search_and_next(cmd, "-o");

    if (!i->dep.empty())
      cmd = add_pch_include(cmd, i->dep);

    add_header_type(cmd);
    add_target(cmd, i->file.string());
  }

  nlohmann::json pch_cmd;
  pch_cmd["directory"] = (*pObj)["directory"];
  pch_cmd["file"] = i->file;
  pch_cmd["command"] = cmd;

  pToAdd->emplace_back(std::move(pch_cmd));
}

void IndexerPreparator::do_check_pch()
{
  inc_pch.clear();
  inc_pch_base.clear();
  fs::path stdafx = pHeaderBlocks->target;
  auto i = std::find_if(PCHs.begin(), PCHs.end(), [&](CCOptions::PCH &p){return p.file == stdafx;}); 
  if (i == PCHs.end())
  {
    auto i = std::find_if(PCHs.begin(), PCHs.end(), [&](const CCOptions::PCH &p){
      return p.can_be_applied_for(target);
    });

    if (i != PCHs.end())
    {
      inc_pch = i->file;
      inc_pch_base = i->dep;

      fs::path dir = inc_pch;
      dir.remove_filename();
      pchForPath[dir] = std::distance(PCHs.begin(), i);
    }
    return;
  }

  fs::path dir = target;
  dir.remove_filename();
  pchForPath[dir] = std::distance(PCHs.begin(), i);

  inc_pch = stdafx;
  inc_pch_base = i->dep;

  add_single_pch(i);

  //find and add dedicated for the same path
  i = PCHs.begin();
  while(true)
  {
    i = std::find_if(i, PCHs.end(), [&](CCOptions::PCH const& p){return p.cmd_from == stdafx;});
    if (i == PCHs.end())
      break;
    add_single_pch(i);
    ++i;
  }
}

void IndexerPreparator::process_header(HeaderBlocks::Header &h) {
  pHeader = &h;
  do_process_header_begin();
  do_process_header_set_file(h.header.string());
  do_process_header_remove_args(compile_target);

  if (cl/* && !hasTPInCommand*/)
    do_process_header_add_args("/TP");

  if (!inc_pch.empty())
  {
    do_process_header_remove_args(inc_base);
    if (!inc_pch_base.empty())
    {
      std::string pch_base(inc_base);
      pch_base += inc_pch_base.string();
      do_process_header_add_args(pch_base);//--include=<path-to-pch>
    }
  }

  if (!h.define.empty()) {
    std::string def(define_opt);
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

/*************************************************************************/
/*IndexerPreparatorWithDependencies                                      */
/*************************************************************************/
IndexerPreparatorWithDependencies::IndexerPreparatorWithDependencies(
    CCOptions const &opts)
    : IndexerPreparator(opts) {
}

void IndexerPreparatorWithDependencies::do_start() { deps.clear(); }

void IndexerPreparatorWithDependencies::do_finalize() {
  pToAdd->emplace_back(std::move(*pObj));
}

void IndexerPreparatorWithDependencies::do_closest_cpp_include(
    Include &inc) {
  nlohmann::json cpp_dep;
  cpp_dep["file"] = inc.file.string();
  if (inc_pch != inc_stdafx)
    cpp_dep["add"].push_back(inc_stdafx);
  lDbg() << "Cpp dependency: " << cpp_dep["file"] << "\n";
  deps.push_back(cpp_dep);
}

void IndexerPreparatorWithDependencies::do_process_header_begin() {
  h_dep.clear();
  add_args.clear();
  rem_c.clear();
}
void IndexerPreparatorWithDependencies::do_process_header_set_file(
    std::string f) {
  h_dep["file"] = f;
}
void IndexerPreparatorWithDependencies::do_process_header_remove_args(
    std::string_view what) {
  // dummy
  std::string t("1:");
  t += what;
  rem_c.push_back(t);
}
void IndexerPreparatorWithDependencies::do_process_header_add_args(
    std::string what) {
  add_args.push_back(what);
}
void IndexerPreparatorWithDependencies::do_process_header_end() {
  h_dep["add"] = add_args;
  h_dep["remove"] = rem_c;
  deps.push_back(h_dep);
}

void IndexerPreparatorWithDependencies::do_header_blocks_end() {
  (*pObj)["dependencies"] = deps;
}

void remove_search_and_next(std::string &where, std::string_view const & what)
{
    size_t from = 0;
    size_t res = 0;
    while ((res = where.find(what, from)) != std::string::npos)
    {
        if (!res || std::isspace(where[res - 1]))
        {
            size_t end = res + what.size();
            if (end == where.size() || std::isspace(where[end]))
            {
                //found it
                break;
            }
        }
    }

    size_t to = where.size();
    auto nextArgBeg =
        std::find_if(where.begin() + res + what.size(), where.end(),
                     [](char c) { return !std::isspace(c); });
    if (nextArgBeg != where.end())
    {
      auto nextArgEnd = std::find_if(nextArgBeg, where.end(),
                                     [](char c) { return std::isspace(c); });
      if (nextArgEnd != where.end())
        to = std::distance(where.begin(), nextArgEnd);
    }

    where.erase(res, to - res);
}

/*************************************************************************/
/*IndexerPreparatorCanonical                                             */
/*************************************************************************/
IndexerPreparatorCanonical::IndexerPreparatorCanonical(CCOptions const &opts)
    : IndexerPreparator(opts) 
    {
    }
void IndexerPreparatorCanonical::do_start()
{
    cleaned_cmd = (*pObj)["command"];
    remove_search_and_next(cleaned_cmd, compile_target);
    if (!cl)
        remove_search_and_next(cleaned_cmd, "-o");
}
void IndexerPreparatorCanonical::do_finalize()
{
}
void IndexerPreparatorCanonical::do_closest_cpp_include(Include &inc)
{
  nlohmann::json cpp_dep = *pObj;
  std::string f = inc.file.string();
  cpp_dep["file"] = f;
  std::string cmd = cleaned_cmd;
  cmd = cmd + " " + std::string(compile_target) + " " + f + " " + inc_stdafx;
  cpp_dep["command"] = cmd;
  lDbg() << "Cpp dependency: " << cpp_dep["file"] << "\n";
  pToAdd->emplace_back(std::move(cpp_dep));
}

void IndexerPreparatorCanonical::do_process_header_begin()
{
    entry = *pObj;
    entry_cmd = cleaned_cmd;
}

void IndexerPreparatorCanonical::do_process_header_set_file(std::string f)
{
    file = f;
    entry["file"] = f;
}

void IndexerPreparatorCanonical::do_process_header_remove_args(std::string_view what)
{
    if (what != compile_target)
        remove_search_and_next(entry_cmd, what);
}

void IndexerPreparatorCanonical::do_process_header_add_args(std::string what)
{
    entry_cmd += " ";
    entry_cmd += what;
}
void IndexerPreparatorCanonical::do_process_header_end()
{
    add_header_type(entry_cmd);
    add_target(entry_cmd, file);

    entry["command"] = entry_cmd;
    pToAdd->emplace_back(std::move(entry));
}
void IndexerPreparatorCanonical::do_header_blocks_end()
{
    //dummy
}
