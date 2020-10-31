#include <exception>
#include <iostream>
#include <regex>
#include <string_view>
#include "analyze_include.h"
#include "generate_header_blocks.h"
#include "compile_commands_processor.h"

int main(int argc, char *argv[])
{
    //getHeaderGuard("/home/orlangur/myapps/cpp/gma3/source/lib_db/playback/db_pb_master_pool_collect.h");
    //getAllRelativeIncludes("/home/orlangur/myapps/cpp/gma3/source/lib_db/stdafx.h");
    //return 0;
    CCOptions opts;
    bool print_help = false;

    try
    {
      for (int i = 0; i < argc; ++i) {
        std::string_view arg(argv[i]);
        if (arg == "--from") {
          ++i;
          if (i < argc)
            opts.compile_commands_json = argv[i];
          else
            print_help = true;
        } else if (arg == "--to") {
          ++i;
          if (i < argc)
            opts.save_to = argv[i];
          else
            print_help = true;
        } else if (arg == "--filter-in") {
          ++i;
          if (i < argc)
            opts.filter_in.push_back(argv[i]);
          else
            print_help = true;
        } else if (arg == "--filter-out") {
          ++i;
          if (i < argc)
            opts.filter_out.push_back(argv[i]);
          else
            print_help = true;
        } else if (arg == "--config") {
          ++i;
          if (i < argc)
            opts.from_json_file(argv[i]);
          else
            print_help = true;
        } else if (arg == "--type") {
          ++i;
          if (i < argc) {
            std::string_view _t = argv[i];
            if (_t == "ccls")
              opts.t = IndexerType::CCLS;
            else if (_t == "clangd")
              opts.t = IndexerType::ClangD;
          } else
            print_help = true;
        } else if (arg == "--help")
          print_help = true;
      }
    }
    catch(const std::exception &e)
    {
        std::cout << "Encountered an error:\n" << e.what() << "\n";
        print_help = true;
    }catch(const char* pMsg)
    {
        std::cout << "Encountered an error:\n" << pMsg << "\n";
        print_help = true;
    }catch(...)
    {
        std::cout << "Encountered some error\n";
        print_help = true;
    }

    if (opts.compile_commands_json.empty())
    {
        std::cout << "--from is missing\n";
        print_help = true;
    }

    if (print_help)
    {
      std::cout << "Usage: prepare_cc --config <path-to-json-config-file> --from <path-to-compile_commands.json> [--to <path-to-output-file>] [--filter-in <path-to-process-commands>] [--filter-out <path-to-process-commands>] [--type <ccls|clangd>] [--help]\n";
      return 0;
    }

    if (opts.save_to.empty())
        opts.save_to = opts.compile_commands_json;

    processCompileCommandsTo(opts);
    return 0;
}