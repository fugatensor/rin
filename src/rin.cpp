#include "rin.h"
#include "rin/pass.h"

struct rin_result : rin::pass::detail::Result {
    rin_result(rin::pass::detail::Result value)
        : rin::pass::detail::Result(std::move(value)) {}
    rin_result(int status, std::string output, std::string error)
        : rin::pass::detail::Result{status, std::move(output), std::move(error)} {}
};

namespace {
using namespace rin::pass::detail;

Result evaluate(const std::vector<Line>& lines, bool check) {
    auto program = parse(lines);
    return check ? Result{0, {}, {}} : rin::pass::detail::evaluate(program, program.rules);
}

template<class Loader>
rin_result* invoke(Loader loader, bool check) noexcept {
    try {
        return new rin_result(evaluate(loader(), check));
    } catch (const std::exception& error) {
        try { return new rin_result{2, {}, std::string(error.what()) + "\n"}; } catch (...) { return nullptr; }
    } catch (...) {
        try { return new rin_result{2, {}, "rin: unexpected error\n"}; } catch (...) { return nullptr; }
    }
}

rin_result* invoke_file(const char* path, bool check) noexcept {
    return invoke([&]() {
        if (!path || !*path) throw std::runtime_error("<source>:1: file path must not be null or empty");
        Imports imports;
        return load_file(std::filesystem::u8path(path), imports);
    }, check);
}

} // namespace

extern "C" {
rin_result* rin_run_file(const char* path) { return invoke_file(path, false); }
rin_result* rin_check_file(const char* path) { return invoke_file(path, true); }
rin_result* rin_run_source(const char* source, const char* directory) {
    return invoke([&]() {
        if (!source) throw std::runtime_error("<source>:1: source must not be null");
        return load_source(source, directory);
    }, false);
}
rin_result* rin_check_source(const char* source, const char* directory) {
    return invoke([&]() {
        if (!source) throw std::runtime_error("<source>:1: source must not be null");
        return load_source(source, directory);
    }, true);
}
int rin_result_status(const rin_result* result) { return result ? result->status : 2; }
const char* rin_result_output(const rin_result* result) { return result ? result->output.c_str() : ""; }
const char* rin_result_error(const rin_result* result) { return result ? result->error.c_str() : "rin: null result"; }
void rin_result_free(rin_result* result) { delete result; }
const char* rin_version(void) { return rin::version; }
}
