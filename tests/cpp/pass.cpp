#include "rin/pass.h"

#include <array>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <thread>

rin::pass::Decision runFromOtherTranslationUnit(std::string_view source);

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}

std::string source(const char* name) {
    std::ifstream file(std::string(RIN_TEST_FIXTURES) + "/" + name + ".rin");
    require(bool(file), "missing PASS fixture");
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
}

int main() {
    using namespace rin::pass;
    const auto provenSource = source("proven");
    const auto proven = runFromOtherTranslationUnit(provenSource);
    require(proven.verdict == Verdict::Pass &&
                proven.explanation.find("verify PAIR PASS") != std::string::npos,
            "generic rule proof should pass its named obligation");
    require(run(source("unproved")).verdict == Verdict::Fail,
            "finite rules should report an unproved obligation");
    require(run(source("limited")).verdict == Verdict::Unknown,
            "unbounded derivation should remain unknown at the inference limit");
    const auto malformedSource = source("malformed");
    require(run(malformedSource).verdict == Verdict::Error,
            "malformed proof source should report an error");
    const auto truncated = run(provenSource + '\0' + malformedSource);
    require(truncated.verdict == Verdict::Error && truncated.explanation.find("NUL") != std::string::npos,
            "embedded NUL must not hide an unparsed obligation");
    const auto wrapped = provenSource + malformedSource;
    require(run(std::string_view(wrapped.data(), provenSource.size())).verdict == Verdict::Pass,
            "string_view length must define the complete proof source");

    const Program comparisons("");
    const std::array<std::uint64_t, 5> values{0, 1, 2, (std::uint64_t{1} << 63),
                                            std::numeric_limits<std::uint64_t>::max()};
    for (auto left : values)
        for (auto right : values)
            for (const auto* predicate : {"less", "less_equal"}) {
                const bool expected = std::string_view(predicate) == "less" ? left < right : left <= right;
                const auto decision = comparisons.check({}, {{{predicate, {Term{left}, Term{right}}}, "ORDER"}});
                require(decision.verdict == (expected ? Verdict::Pass : Verdict::Fail),
                        "unsigned comparisons must agree with native uint64 ordering");
            }
    require(comparisons.check({{"less", {Term{std::uint64_t{2}}, Term{std::uint64_t{1}}}}}, {}).verdict == Verdict::Error,
            "callers cannot assert a builtin relation");

    const Program rules(source("bounds"));
    const auto checkBounds = [&](const Program& activeRules) {
        for (auto lower : values)
            for (auto upper : values) {
                // Numeric identities can occur in separate relation domains.
                const std::vector<Fact> facts{
                    {"item", {Term{lower}}}, {"value", {Term{lower}}}, {"value", {Term{upper}}},
                    {"bounds", {Term{lower}, Term{lower}, Term{upper}}}};
                const auto decision = activeRules.check(facts, {{{"ordered", {Term{lower}}}, "BOUND"}});
                require(decision.verdict == (lower <= upper ? Verdict::Pass : Verdict::Fail),
                        "compiled rule must derive each fresh fact set independently");
                require(decision.verdict != Verdict::Pass || decision.explanation.empty(),
                        "successful typed proof should avoid formatted output");
            }
    };
    std::thread first([&] { checkBounds(rules); }), second([&] { checkBounds(rules); });
    first.join();
    second.join();
    require(rules.check({}, {{{"ordered", {Term{std::uint64_t{0}}}}, "ISOLATED"}}).verdict == Verdict::Fail,
            "completed checks must not retain inferred facts");
    std::array<Program, 4> retained{Program(""), Program(""), Program(""), Program("")};
    {
        Program original(source("bounds") + source("existentials"));
        retained[0] = original;
    }
    {
        Program original(source("bounds") + source("existentials"));
        retained[1] = Program(original);
    }
    {
        Program original(source("bounds") + source("existentials"));
        retained[2] = std::move(original);
    }
    {
        Program movable(source("bounds") + source("existentials"));
        retained[3] = Program(std::move(movable));
    }
    for (const auto& retainedRules : retained) checkBounds(retainedRules);
    std::thread copiedFirst([&] { checkBounds(retained[0]); });
    std::thread copiedSecond([&] { checkBounds(retained[1]); });
    copiedFirst.join();
    copiedSecond.join();
    const Program existentials(source("existentials"));
    require(existentials.check(
                {{"dangerous", {Term{":w0"}}}},
                {{{"accepted", {Term{"a"}}}, "NO_CAPTURE"}}).verdict == Verdict::Fail,
            "fresh existential witnesses must be distinct from caller-provided symbols");
    bool malformedRules = false;
    try { const Program invalid(malformedSource); }
    catch (const std::runtime_error&) { malformedRules = true; }
    require(malformedRules, "invalid fixed rules must fail during compilation");
    std::cout << "Rin header-only proof API passed\n";
}
