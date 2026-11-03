#include "suka/pass.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}
}

int main() {
    using suka::pass::Verdict;
    using suka::pass::run;

    const auto proven = run(
        "first: node\n"
        "second: node\n"
        "all a: node, b: node =>\n"
        "    different(a, b) -> distinct(a, b)\n"
        "verify PAIR = distinct(first, second)\n");
    require(proven.verdict == Verdict::Pass &&
                proven.explanation.find("verify PAIR PASS") != std::string::npos,
            "generic rule proof should pass its named obligation");

    const auto unproved = run("first: node\nverify MISSING = distinct(first, second)\n");
    require(unproved.verdict == Verdict::Fail &&
                unproved.explanation.find("verify MISSING FAIL") != std::string::npos,
            "finite rules should report an unproved obligation");

    const auto limited = run(
        "seed: item\n"
        "all x: item => succ(x): item\n"
        "verify OPEN = missing(seed)\n");
    require(limited.verdict == Verdict::Unknown &&
                limited.explanation.find("verify OPEN UNKNOWN") != std::string::npos,
            "unbounded derivation should remain unknown at the inference limit");

    const auto malformed = run("verify BAD = broken(\n");
    require(malformed.verdict == Verdict::Error,
            "malformed proof source should report an error");

    std::string nul_source = "verify GOOD = different(a,b)\n";
    nul_source.push_back('\0');
    nul_source += "verify BAD = broken(\n";
    const auto truncated = run(std::string_view(nul_source.data(), nul_source.size()));
    require(truncated.verdict == Verdict::Error &&
                truncated.explanation.find("NUL") != std::string::npos,
            "embedded NUL must not hide an unparsed obligation");

    const std::string wrapped = "verify GOOD = different(a,b)\nverify BAD = broken(\n";
    const auto slice = run(std::string_view(wrapped.data(), wrapped.find("verify BAD")));
    require(slice.verdict == Verdict::Pass,
            "string_view length must define the complete proof source");

    std::cout << "SUKA generic proof API passed\n";
}
