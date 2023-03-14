#ifndef INDEXER_PREPARATOR_H_
#define INDEXER_PREPARATOR_H_

#include "compile_commands_processor.h"
#include "json.hpp"
#include "analyze_include.h"
#include "generate_header_blocks.h"

using json_list = std::vector<nlohmann::json>;

class IndexerPreparator
{
  public:
    IndexerPreparator(CCOptions const& opts);
    virtual ~IndexerPreparator() = default;

    void QuickPrepare(nlohmann::json &obj, fs::path target, json_list &to_add);
    void Prepare(nlohmann::json &obj, fs::path target, json_list &to_add);
  protected:
    std::string add_pch_include(std::string cmd, fs::path pch) const; 
    void add_header_type(std::string &cmd) const; 
    void add_target(std::string &cmd, std::string const& tgt) const; 
    bool try_apply_pch(nlohmann::json &obj, fs::path target) const;


    virtual void do_start() = 0;
    virtual void do_finalize() = 0;
    virtual void do_closest_cpp_include(Include &inc) = 0;

    void do_check_pch();
    using pch_it = decltype(CCOptions::PCHs)::const_iterator;
    void add_single_pch(pch_it i);

    virtual void do_process_header_begin() = 0;
    virtual void do_process_header_set_file(std::string f) = 0;
    virtual void do_process_header_remove_args(std::string_view what, int count = 1) = 0;
    virtual void do_process_header_add_args(std::string what) = 0;
    virtual void do_process_header_end() = 0;
    virtual void do_process_header_add_dynamic_pch(std::string dynpch, const std::string &inc_stdafx, bool skipAsPchDependency) {};

    virtual void do_header_blocks_end() = 0;

    void process_header(HeaderBlocks::Header &h);

    //call args
    nlohmann::json *pObj;
    json_list *pToAdd;
    fs::path target;
    //temp stuff
    std::string inc_stdafx;
    std::string inc_before;
    std::string inc_after;
    std::string inc_after_file;
    fs::path dir_stdafx;
    HeaderBlocks::Header *pHeader;//current header
    fs::path inc_pch;
    fs::path inc_pch_base;

    HeaderBlocks *pHeaderBlocks;

    using pch_index_t = int;
    std::map<fs::path, pch_index_t> pchForPath;

    //config stuff
    CCOptions const& opts;
    bool cl;
    std::string_view inc_base;
    std::string_view compile_target;
    std::string_view define_opt;
    std::string_view xheader_opt;
    decltype(CCOptions::PCHs) PCHs;
};

class IndexerPreparatorWithDependencies: public IndexerPreparator
{
  public:
    IndexerPreparatorWithDependencies(CCOptions const &opts);
  private:
    virtual void do_start() override;
    virtual void do_finalize() override;
    virtual void do_closest_cpp_include(Include &inc) override;
    virtual void do_process_header_begin() override;
    virtual void do_process_header_set_file(std::string f) override;
    virtual void do_process_header_remove_args(std::string_view what, int count = 1) override;
    virtual void do_process_header_add_args(std::string what) override;
    virtual void do_process_header_add_dynamic_pch(std::string dynpch, const std::string &inc_stdafx, bool skipAsPchDependency) override;
    virtual void do_process_header_end() override;
    virtual void do_header_blocks_end() override;

    nlohmann::json deps;
    nlohmann::json rem_c;
    nlohmann::json h_dep;
    nlohmann::json add_args;
};

//no dependencies, will multiply compile_commands.json entries
class IndexerPreparatorCanonical: public IndexerPreparator
{
  public:
    IndexerPreparatorCanonical(CCOptions const &opts);
  private:
    virtual void do_start() override;
    virtual void do_finalize() override;
    virtual void do_closest_cpp_include(Include &inc) override;
    virtual void do_process_header_begin() override;
    virtual void do_process_header_set_file(std::string f) override;
    virtual void do_process_header_remove_args(std::string_view what, int count = 1) override;
    virtual void do_process_header_add_args(std::string what) override;
    virtual void do_process_header_end() override;
    virtual void do_header_blocks_end() override;

    std::string cleaned_cmd;//no compile target, no output

    nlohmann::json entry;
    std::string entry_cmd;
    std::string file;
};

#endif
