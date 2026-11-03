#include "suka/pass.h"
#include "suka.h"

#include <memory>
#include <string>
#include <utility>

namespace suka::pass {

Decision run(std::string_view source) {
    if (source.find('\0') != std::string_view::npos)
        return {Verdict::Error, "SUKA source contains an embedded NUL byte"};
    const std::string text(source);
    using Result = std::unique_ptr<suka_result, decltype(&suka_result_free)>;
    Result result(suka_run_source(text.c_str(), nullptr), &suka_result_free);
    if (!result) return {Verdict::Error, "SUKA could not allocate a proof result"};
    const int status = suka_result_status(result.get());
    std::string explanation = suka_result_output(result.get());
    explanation += suka_result_error(result.get());
    switch (status) {
    case 0: return {Verdict::Pass, std::move(explanation)};
    case 1: return {Verdict::Fail, std::move(explanation)};
    case 3: return {Verdict::Unknown, std::move(explanation)};
    default: return {Verdict::Error, std::move(explanation)};
    }
}

} // namespace suka::pass
