#include "rin/pass.h"
#include <iostream>
#include <iterator>
#include <string>

int main(int argc, char* argv[]) {
    bool check = false, standard_input = false, positional = false;
    std::string path, base_directory;
    auto usage = [&]() {
        std::cerr << "usage: rin [--check] FILE\n"
                  << "       rin [--check] --stdin [--base-dir DIRECTORY]\n";
        return 2;
    };
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (!positional && argument == "--") { positional = true; continue; }
        if (!positional && argument == "--help") {
            std::cout << "usage: rin [--check] FILE\n"
                      << "       rin [--check] --stdin [--base-dir DIRECTORY]\n";
            return 0;
        }
        if (!positional && argument == "--version") { std::cout << rin::version << '\n'; return 0; }
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
    rin::pass::detail::Result result{0, {}, {}};
    try {
        using namespace rin::pass::detail;
        Program program;
        if (standard_input) {
            const std::string source{std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>()};
            program = parse(load_source(source, base_directory.c_str()));
        } else {
            Imports imports;
            program = parse(load_file(std::filesystem::u8path(path), imports));
        }
        if (!check) result = evaluate(program, program.rules);
    } catch (const std::exception& error) {
        result = {2, {}, std::string(error.what()) + "\n"};
    }
    std::cout << result.output;
    std::cerr << result.error;
    return result.status;
}
