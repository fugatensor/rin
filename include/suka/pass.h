#ifndef SUKA_PASS_H
#define SUKA_PASS_H

#include <cstddef>
#include <string>
#include <vector>

namespace suka::pass {

enum class Verdict { Pass, Fail, Unknown, Error };

// ASK supplies observations about one candidate. Step indices describe an
// ordered submission; completion_finished is an observed event state.
struct ReuseCandidate {
    std::size_t prior_last_use_step = 0;
    std::size_t next_first_use_step = 0;
    std::size_t prior_capacity_bytes = 0;
    std::size_t next_size_bytes = 0;
    bool same_submission = false;
    bool prior_completion_finished = false;
    bool same_device = false;
    bool output_released = false;
    bool same_allocation_domain = false;
    bool allocator_owned = false;
};

// Buffer indices are the caller's logical buffer table. Steps are ordered by
// the caller's validated schedule. Each step reads its inputs before writing
// its outputs; a step's output becomes available at the next boundary.
struct PlanStep {
    std::vector<std::size_t> inputs;
    std::vector<std::size_t> outputs;
};

struct Plan {
    std::size_t buffer_count = 0;
    std::vector<std::size_t> public_inputs;
    std::vector<std::size_t> public_outputs;
    std::vector<PlanStep> steps;
};

struct Decision {
    Verdict verdict = Verdict::Error;
    std::string explanation;
};

// A candidate is reusable only when SUKA proves every obligation. FAIL means
// the finite facts do not prove reuse; UNKNOWN/ERROR also forbid reuse.
Decision checkReuse(const ReuseCandidate& candidate);

// Checks references and single-writer structure, then proves each read and
// public output is available at its ordered step boundary.
Decision checkPlan(const Plan& plan);

} // namespace suka::pass

#endif
