#include "suka/pass.h"
#include "suka.h"
#include "buffer_reuse_rules.h"

#include <memory>
#include <string>
#include <utility>

namespace suka::pass {
namespace {
using Result = std::unique_ptr<suka_result, decltype(&suka_result_free)>;

void emit(std::string& source, const char* predicate, bool present) {
    if (present) source += std::string(predicate) + "(candidate)\n";
}
} // namespace

Decision checkReuse(const ReuseCandidate& candidate) {
    std::string source(kBufferReuseRules);
    source += '\n';
    emit(source, "same_submission", candidate.same_submission);
    emit(source, "different_submission", !candidate.same_submission);
    emit(source, "strictly_after_last_use",
         candidate.prior_last_use_step < candidate.next_first_use_step);
    emit(source, "prior_completion_finished", candidate.prior_completion_finished);
    emit(source, "same_allocation_domain", candidate.same_allocation_domain);
    emit(source, "allocator_owned", candidate.allocator_owned);
    emit(source, "same_device", candidate.same_device);
    emit(source, "capacity_sufficient",
         candidate.prior_capacity_bytes >= candidate.next_size_bytes);
    emit(source, "output_released", candidate.output_released);
    Result result(suka_run_source(source.c_str(), nullptr), &suka_result_free);
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
