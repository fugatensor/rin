#include "suka.h"
#include <iostream>
#include <iterator>
#include <string>

int main(int argc, char* argv[]) {
    bool check = false, standard_input = false, positional = false;
    std::string path, base_directory;
    auto usage = [&]() {
        std::cerr << "usage: suka [--check] FILE\n"
                  << "       suka [--check] --stdin [--base-dir DIRECTORY]\n";
        return 2;
    };
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (!positional && argument == "--") { positional = true; continue; }
        if (!positional && argument == "--help") {
            std::cout << "usage: suka [--check] FILE\n"
                      << "       suka [--check] --stdin [--base-dir DIRECTORY]\n";
            return 0;
        }
        if (!positional && argument == "--version") { std::cout << suka_version() << '\n'; return 0; }
        if (!positional && argument == "--check") check = true;
        else if (!positional && argument == "--stdin") standard_input = true;
        else if (!positional && argument == "--base-dir") {
            if (++i == argc) return usage();
            base_directory = argv[i];
        } else if ((!positional && !argument.empty() && argument[0] == '-') || !path.empty()) return usage();
        else path = argument;
    }
    if ((standard_input && !path.empty()) || (!standard_input && path.empty()) ||
        (!standard_input && !base_directory.empty())) return usage();
    suka_result* result;
    if (standard_input) {
        const std::string source{std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>()};
        result = check ? suka_check_source(source.c_str(), base_directory.c_str())
                       : suka_run_source(source.c_str(), base_directory.c_str());
    } else result = check ? suka_check_file(path.c_str()) : suka_run_file(path.c_str());
    if (!result) { std::cerr << "suka: out of memory\n"; return 2; }
    std::cout << suka_result_output(result);
    std::cerr << suka_result_error(result);
    const int status = suka_result_status(result);
    suka_result_free(result);
    return status;
}
