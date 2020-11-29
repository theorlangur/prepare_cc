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

    void Prepare(nlohmann::json &obj, fs::path target, json_list &to_add);
  protected:
    virtual void do_start() = 0;
    virtual void do_finalize() = 0;
    virtual void do_closest_cpp_include(Include &inc) = 0;

    virtual void do_process_header_begin() = 0;
    virtual void do_process_header_set_file(std::string f) = 0;
    virtual void do_process_header_remove_args(std::string_view what) = 0;
    virtual void do_process_header_add_args(std::string what) = 0;
    virtual void do_process_header_end() = 0;

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
    IndexerPreparatorWithDependencies(CCOptions const &opts);
  private:
    virtual void do_start() override;
    virtual void do_finalize() override;
    virtual void do_closest_cpp_include(Include &inc) override;
    virtual void do_process_header_begin() override;
    virtual void do_process_header_set_file(std::string f) override;
    virtual void do_process_header_remove_args(std::string_view what) override;
    virtual void do_process_header_add_args(std::string what) override;
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
    virtual void do_process_header_remove_args(std::string_view what) override;
    virtual void do_process_header_add_args(std::string what) override;
    virtual void do_process_header_end() override;
    virtual void do_header_blocks_end() override;

    std::string cleaned_cmd;//no compile target, no output
    std::string compile_option;

    nlohmann::json entry;
    std::string entry_cmd;
    std::string file;
};

#endif