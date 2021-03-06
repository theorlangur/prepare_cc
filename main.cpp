#include <exception>
#include <iostream>
#include <regex>
#include <string_view>
#include "analyze_include.h"
#include "generate_header_blocks.h"
#include "compile_commands_processor.h"
#include "log.h"

int main(int argc, char *argv[])
{
    CCOptions opts;
    bool print_help = false;
    fs::path base = fs::current_path();

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
        }
        else if (arg == "--to") {
            ++i;
            if (i < argc)
                opts.save_to = argv[i];
            else
                print_help = true;
        }
        else if (arg == "--filter-in") {
            ++i;
            if (i < argc)
                opts.filter_in.push_back(argv[i]);
            else
                print_help = true;
        }
        else if (arg == "--filter-out") {
            ++i;
            if (i < argc)
                opts.filter_out.push_back(argv[i]);
            else
                print_help = true;
        }
        else if (arg == "--config") {
            ++i;
            if (i < argc)
                opts.from_json_file(argv[i], base);
            else
                print_help = true;
        }
        else if (arg == "--verbose") {
            ++i;
            if (i < argc) {
                std::string_view _t = argv[i];
                if (_t == "error")
                  setGlobalLogLevel(Log::Error);
                else if (_t == "warning")
                  setGlobalLogLevel(Log::Warning);
                else if (_t == "info")
                  setGlobalLogLevel(Log::Info);
                else if (_t == "dbg")
                  setGlobalLogLevel(Log::Debug);
            }
            else
                setGlobalLogLevel(Log::Info);
        }
        else if (arg == "--help")
            print_help = true;
        else if (arg == "--clang-cl")
            opts.clang_cl = true;
        else if (arg == "--base")
        {
            ++i;
            base = argv[i];
            if (base.is_relative())
                base = fs::current_path() / base;
            base = to_real_path(base, false);
            if (!fs::exists(base))
                throw std::runtime_error("Base directory doesn't exist");
        }
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
      std::cout << "Usage: prepare_cc [--base <dir>] --config "
                   "<path-to-json-config-file> --from "
                   "<path-to-compile_commands.json> [--to "
                   "<path-to-output-file>] [--clang-cl] [--filter-in "
                   "<path-to-process-commands>] [--filter-out "
                   "<path-to-process-commands>] [--type <ccls|clangd>] "
                   "[--verbose [error|warning|info|dbg]] [--help]\n";
      return 0;
    }

    if (opts.save_to.empty())
    {
      lInfo() << "destination to store was not provided so using source:\n"
              << opts.compile_commands_json << "\n";
      opts.save_to = opts.compile_commands_json;
    }

    processCompileCommandsTo(opts);
    return 0;
}
