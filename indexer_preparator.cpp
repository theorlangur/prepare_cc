#include "indexer_preparator.h"
#include "log.h"

std::string convert_separators(std::string in, bool convert) {
  if (convert)
    std::replace(in.begin(), in.end(), '\\', '/');
  return in;
}

IndexerPreparator::IndexerPreparator(CCOptions const &opts)
    : opts(opts), cl(opts.clang_cl),
      conv_sep(cl && opts.t == IndexerType::CCLS),
      inc_base(cl ? "/clang:--include" : "--include=") {}

void IndexerPreparator::Prepare(nlohmann::json &obj, fs::path target,
                                json_list &to_add) {
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

void IndexerPreparator::process_header(HeaderBlocks::Header &h) {
  pHeader = &h;
  do_process_header_begin();
  do_process_header_set_file(convert_separators(h.header.string(), conv_sep));
  do_process_header_remove_args(cl ? "/bigobj" : "-c");
  if (cl)
    do_process_header_add_args("/TP");

  if (opts.t == IndexerType::CCLS) {
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

IndexerPreparatorWithDependencies::IndexerPreparatorWithDependencies(
    CCOptions const &opts)
    : IndexerPreparator(opts) {
  if (!cl)
    rem_c.push_back("1:-c"); // remove compile target of the original command
  else
    rem_c.push_back(
        "1:/bigobj"); // remove compile target of the original command
}

void IndexerPreparatorWithDependencies::do_start() { deps.clear(); }

void IndexerPreparatorWithDependencies::do_finalize() {
  pToAdd->emplace_back(std::move(*pObj));
}

void IndexerPreparatorWithDependencies::do_closest_cpp_include(
    Include &inc) {
  nlohmann::json cpp_dep;
  cpp_dep["file"] = convert_separators(inc.file.string(), conv_sep);
  cpp_dep["add"].push_back(inc_stdafx);
  lDbg() << "Cpp dependency: " << cpp_dep["file"] << "\n";
  deps.push_back(cpp_dep);
}

void IndexerPreparatorWithDependencies::do_process_header_begin() {
  h_dep.clear();
  add_args.clear();
}
void IndexerPreparatorWithDependencies::do_process_header_set_file(
    std::string f) {
  h_dep["file"] = f;

  h_dep["remove"] = rem_c;
}
void IndexerPreparatorWithDependencies::do_process_header_remove_args(
    const char *pWhat) {
  // dummy
}
void IndexerPreparatorWithDependencies::do_process_header_add_args(
    std::string what) {
  add_args.push_back(what);
}
void IndexerPreparatorWithDependencies::do_process_header_end() {
  h_dep["add"] = add_args;
  deps.push_back(h_dep);
}

void IndexerPreparatorWithDependencies::do_header_blocks_end() {
  (*pObj)["dependencies"] = deps;
}
