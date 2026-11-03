#include "suka/pass.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}
}

int main() {
    using suka::pass::Plan;
    using suka::pass::PlanStep;
    using suka::pass::ReuseCandidate;
    using suka::pass::Verdict;
    using suka::pass::checkPlan;
    using suka::pass::checkReuse;

    Plan chain{3, {0}, {2}, {{{0}, {1}}, {{1}, {2}}}};
    require(checkPlan(chain).verdict == Verdict::Pass, "producer chain should prove all reads and final output");
    Plan premature = chain;
    premature.steps[0].inputs = {1};
    auto decision = checkPlan(premature);
    require(decision.verdict == Verdict::Fail && decision.explanation.find("READ_0_0 FAIL") != std::string::npos,
            "a read before its write should fail its named proof obligation");
    Plan missing = chain;
    missing.public_outputs = {0, 2};
    require(checkPlan(missing).verdict == Verdict::Pass, "a public input remains available as a public output");
    missing.public_outputs = {0, 1, 2};
    require(checkPlan(missing).verdict == Verdict::Pass, "all produced buffers remain available");
    Plan unwritten = chain;
    unwritten.buffer_count = 4;
    unwritten.public_outputs = {3};
    require(checkPlan(unwritten).verdict == Verdict::Fail, "an unwritten public output must fail");
    Plan repeated = chain;
    repeated.steps[1].outputs = {1};
    require(checkPlan(repeated).verdict == Verdict::Fail, "two writes to one logical buffer must fail");
    Plan bad_port = chain;
    bad_port.steps[0].inputs = {3};
    require(checkPlan(bad_port).verdict == Verdict::Fail, "out of range port must fail");
    Plan unproduced{2, {0}, {0}, {}};
    require(checkPlan(unproduced).verdict == Verdict::Fail, "unreferenced unproduced buffer must fail");
    Plan vacuous{};
    require(checkPlan(vacuous).verdict == Verdict::Fail, "empty plan must fail");
    Plan identity{1, {0}, {0}, {}};
    require(checkPlan(identity).verdict == Verdict::Pass, "a public input may be returned directly");

    Plan long_plan{1, {0}, {0}, std::vector<PlanStep>(300)};
    long_plan.steps.back().inputs = {0};
    require(checkPlan(long_plan).verdict == Verdict::Pass,
            "a long finite schedule should prove availability without exceeding inference rounds");

    ReuseCandidate reuse{1, 2, 128, 64, true, false, true, true, true, true};
    require(checkReuse(reuse).verdict == Verdict::Pass, "ordered local reuse should prove");
    reuse.next_first_use_step = 1;
    require(checkReuse(reuse).verdict == Verdict::Fail, "overlapping local intervals must fail");
    reuse.same_submission = false;
    require(checkReuse(reuse).verdict == Verdict::Fail, "unfinished prior submission must fail");
    reuse.prior_completion_finished = true;
    require(checkReuse(reuse).verdict == Verdict::Pass, "completed prior submission should prove");
    reuse.output_released = false;
    require(checkReuse(reuse).verdict == Verdict::Fail, "held public output must fail");
    reuse.output_released = true;
    reuse.same_allocation_domain = false;
    require(checkReuse(reuse).verdict == Verdict::Fail, "foreign allocation domain must fail");
    reuse.same_allocation_domain = true;
    reuse.allocator_owned = false;
    require(checkReuse(reuse).verdict == Verdict::Fail, "foreign allocator storage must fail");
    reuse.allocator_owned = true;
    reuse.next_size_bytes = 256;
    require(checkReuse(reuse).verdict == Verdict::Fail, "insufficient capacity must fail");
    std::cout << "SUKA pass legality proofs passed\n";
}
