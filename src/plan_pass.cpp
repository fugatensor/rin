#include "suka/pass.h"
#include "suka.h"
#include "plan_availability_rules.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace suka::pass {
namespace {
using Result = std::unique_ptr<suka_result, decltype(&suka_result_free)>;

std::string buffer(std::size_t index) { return "b" + std::to_string(index); }
std::string step(std::size_t index) { return "s" + std::to_string(index); }

std::string instantiate(std::size_t from) {
    std::string rule(kPlanAvailabilityRule);
    const auto replace = [&](const char* token, const std::string& value) {
        const std::string key(token);
        std::size_t position = 0;
        while ((position = rule.find(key, position)) != std::string::npos) {
            rule.replace(position, key.size(), value);
            position += value.size();
        }
    };
    replace("@FROM@", step(from));
    replace("@TO@", step(from + 1));
    return rule;
}

Decision invalid(const std::string& reason) { return {Verdict::Fail, reason}; }
} // namespace

Decision checkPlan(const Plan& plan) {
    // These are finite structural conditions that the Horn language cannot
    // express without inequality or negation. Read availability is proved below.
    std::vector<bool> produced(plan.buffer_count, false);
    for (const auto index : plan.public_inputs) {
        if (index >= plan.buffer_count) return invalid("public input buffer index is out of range");
        if (produced[index]) return invalid("public input buffer is repeated");
        produced[index] = true;
    }
    for (const auto& operation : plan.steps) {
        for (const auto index : operation.inputs)
            if (index >= plan.buffer_count) return invalid("step input buffer index is out of range");
        for (const auto index : operation.outputs) {
            if (index >= plan.buffer_count) return invalid("step output buffer index is out of range");
            if (produced[index]) return invalid("buffer has multiple writers or aliases a public input");
            produced[index] = true;
        }
    }
    if (plan.public_outputs.empty()) return invalid("plan must have a public output");
    for (const auto index : plan.public_outputs)
        if (index >= plan.buffer_count) return invalid("public output buffer index is out of range");
    for (const bool has_writer : produced)
        if (!has_writer) return invalid("plan contains a buffer without a writer");

    std::string source;
    for (std::size_t index = 0; index < plan.buffer_count; ++index)
        source += buffer(index) + ": buffer\n";
    for (const auto index : plan.public_inputs)
        source += "available(" + buffer(index) + ",s0)\n";
    for (std::size_t index = 0; index < plan.steps.size(); ++index) {
        source += instantiate(index);
        for (const auto output : plan.steps[index].outputs)
            source += "available(" + buffer(output) + "," + step(index + 1) + ")\n";
    }
    for (std::size_t index = 0; index < plan.steps.size(); ++index)
        for (std::size_t port = 0; port < plan.steps[index].inputs.size(); ++port)
            source += "verify READ_" + std::to_string(index) + "_" + std::to_string(port) +
                      " = available(" + buffer(plan.steps[index].inputs[port]) + "," + step(index) + ")\n";
    for (std::size_t port = 0; port < plan.public_outputs.size(); ++port)
        source += "verify OUTPUT_" + std::to_string(port) + " = available(" +
                  buffer(plan.public_outputs[port]) + "," + step(plan.steps.size()) + ")\n";
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
