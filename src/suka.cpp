#include "suka.h"
#include "suka/pass.h"

struct suka_result : suka::pass::detail::Result {
    suka_result(suka::pass::detail::Result value)
        : suka::pass::detail::Result(std::move(value)) {}
    suka_result(int status, std::string output, std::string error)
        : suka::pass::detail::Result{status, std::move(output), std::move(error)} {}
};

namespace {
using namespace suka::pass::detail;

Result evaluate(const std::vector<Line>& lines, bool check) {
    auto program = parse(lines);
    return check ? Result{0, {}, {}} : suka::pass::detail::evaluate(program, program.rules);
}

template<class Loader>
suka_result* invoke(Loader loader, bool check) noexcept {
    try {
        return new suka_result(evaluate(loader(), check));
    } catch (const std::exception& error) {
        try { return new suka_result{2, {}, std::string(error.what()) + "\n"}; } catch (...) { return nullptr; }
    } catch (...) {
        try { return new suka_result{2, {}, "suka: unexpected error\n"}; } catch (...) { return nullptr; }
    }
}

suka_result* invoke_file(const char* path, bool check) noexcept {
    return invoke([&]() {
        if (!path || !*path) throw std::runtime_error("<source>:1: file path must not be null or empty");
        Imports imports;
        return load_file(std::filesystem::u8path(path), imports);
    }, check);
}

} // namespace

extern "C" {
suka_result* suka_run_file(const char* path) { return invoke_file(path, false); }
suka_result* suka_check_file(const char* path) { return invoke_file(path, true); }
suka_result* suka_run_source(const char* source, const char* directory) {
    return invoke([&]() {
        if (!source) throw std::runtime_error("<source>:1: source must not be null");
        return load_source(source, directory);
    }, false);
}
suka_result* suka_check_source(const char* source, const char* directory) {
    return invoke([&]() {
        if (!source) throw std::runtime_error("<source>:1: source must not be null");
        return load_source(source, directory);
    }, true);
}
int suka_result_status(const suka_result* result) { return result ? result->status : 2; }
const char* suka_result_output(const suka_result* result) { return result ? result->output.c_str() : ""; }
const char* suka_result_error(const suka_result* result) { return result ? result->error.c_str() : "suka: null result"; }
void suka_result_free(suka_result* result) { delete result; }
const char* suka_version(void) { return suka::version; }
}
