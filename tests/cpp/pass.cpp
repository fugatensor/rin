#include "rin/pass.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <thread>

rin::pass::Decision runFromOtherTranslationUnit(std::string_view source);
rin::pass::Solution solveFromOtherTranslationUnit(std::string_view source);

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

    // General numeric types: signed int64, uint64, double, and exact decimal.
    {
        require(Term{1} == Term{std::uint64_t{1}}, "int and uint64 1 must be equal");
        require(Term{1} == Term{1.0}, "int 1 and double 1.0 must be equal");
        require(Term{1} == Term::decimal("1.00"), "int 1 and decimal 1.00 must be equal");
        require(Term{0} == Term{-0.0} && Term{0} == Term::decimal("-0.00"),
                "zero representations must canonicalize identically");
        require(Term{0.1} == Term::decimal("0.1"),
                "double 0.1 and decimal 0.1 must match cleanly");
        require(Term{std::int64_t{9007199254740992LL}} < Term{std::int64_t{9007199254740993LL}},
                "integers above 2^53 must preserve exact precision");
        require(Term::decimal("-9223372036854775808.5") < Term{std::numeric_limits<std::int64_t>::min()},
                "decimal below INT64_MIN must compare smaller");
        require(Term{std::numeric_limits<std::uint64_t>::max()} < Term::decimal("18446744073709551615.5"),
                "decimal above UINT64_MAX must compare larger");
        require(Term::decimal("0.123456789012345678901") < Term::decimal("0.123456789012345678902"),
                "exact decimals must preserve precision beyond double");

        const Number neg_min{std::numeric_limits<std::int64_t>::min()};
        require(neg_min.is_integer() && neg_min.fits_int64() && !neg_min.fits_uint64(),
                "INT64_MIN classification failed");
        require(neg_min.as_int64() == std::numeric_limits<std::int64_t>::min(), "INT64_MIN value mismatch");
        require(neg_min.to_string() == "-9223372036854775808", "INT64_MIN formatting mismatch");

        const Number dec = Number::decimal("-3.1400");
        require(!dec.is_integer() && !dec.fits_int64() && !dec.fits_uint64(), "decimal integer flags failed");
        require(std::abs(dec.as_double() + 3.14) < 1e-12, "decimal as_double mismatch");
        require(dec.to_string() == "-3.14", "decimal formatting mismatch");

        for (double bad : {std::numeric_limits<double>::quiet_NaN(),
                           std::numeric_limits<double>::infinity(),
                           -std::numeric_limits<double>::infinity()}) {
            bool rejected = false;
            try { Term invalid{bad}; }
            catch (const std::runtime_error&) { rejected = true; }
            require(rejected, "non-finite double must be rejected");
        }

        const std::vector<Term> ordered_terms{
            Term::decimal("-9223372036854775808.5"),
            Term{std::numeric_limits<std::int64_t>::min()},
            Term{std::int64_t{-10}},
            Term{-0.5},
            Term{std::int64_t{0}},
            Term{0.1},
            Term{1.5f},
            Term{std::int64_t{9007199254740992LL}},
            Term{std::int64_t{9007199254740993LL}},
            Term{std::numeric_limits<std::uint64_t>::max()},
            Term::decimal("18446744073709551615.5"),
            Term{1e20},
        };
        for (std::size_t i = 0; i < ordered_terms.size(); ++i) {
            for (std::size_t j = 0; j < ordered_terms.size(); ++j) {
                for (const auto* pred : {"less", "less_equal", "greater", "greater_equal"}) {
                    const std::string_view p(pred);
                    const bool expected = p == "less" ? i < j :
                        p == "less_equal" ? i <= j : p == "greater" ? i > j : i >= j;
                    const auto res = comparisons.check({}, {{{pred, {ordered_terms[i], ordered_terms[j]}}, "NUM"}});
                    require(res.verdict == (expected ? Verdict::Pass : Verdict::Fail),
                            "cross-type numeric comparison mismatch");
                }
            }
        }

        const Program crossMatch(
            "all x: item, d: dose =>\n"
            "    dose_of(x, d) & greater_equal(d, -1.5) & less(d, 2.5) -> safe(x)\n"
            "all x: item =>\n"
            "    dose_of(x, 1.0) -> unit_dose(x)\n");
        const auto mres = crossMatch.solve(
            {
                {"item", {Term{"a"}}}, {"item", {Term{"b"}}}, {"item", {Term{"c"}}},
                {"dose", {Term{1}}}, {"dose", {Term{-1.5}}}, {"dose", {Term{2.5}}},
                {"dose_of", {Term{"a"}, Term{std::int64_t{1}}}},
                {"dose_of", {Term{"b"}, Term{-1.5}}},
                {"dose_of", {Term{"c"}, Term{2.5}}},
            },
            {
                {{"safe", {Term{"a"}}}, "A_SAFE"},
                {{"safe", {Term{"b"}}}, "B_SAFE"},
                {{"unit_dose", {Term{"a"}}}, "A_UNIT"},
                {{"safe", {Term{"c"}}}, "C_UNSAFE"},
            });
        require(mres.verdict == Verdict::Fail, "C_UNSAFE should fail");
        require(mres.find_query("A_SAFE")->verdict == Verdict::Pass, "int 1 should match numeric rule");
        require(mres.find_query("B_SAFE")->verdict == Verdict::Pass, "-1.5 >= -1.5 should pass");
        require(mres.find_query("A_UNIT")->verdict == Verdict::Pass, "int 1 should match rule literal 1.0");
        require(mres.find_query("C_UNSAFE")->verdict == Verdict::Fail, "2.5 < 2.5 should fail");
    }

    // Query Mode: predicate extraction, pattern bindings, existential witnesses, and provenance.
    {
        const Program ddiRules(
            "all a, b: drug, e: effect, d: dose =>\n"
            "    co_prescribed(a, b) & different(a, b) & triggers(a, e, d) & greater(d, 0.5) -> ddi(a, b, e)\n"
            "all a, b: drug, e: effect =>\n"
            "    ddi(a, b, e) -> alert(a, e)\n");
        const std::vector<Fact> ddiFacts{
            {"drug", {Term{"aspirin"}}}, {"drug", {Term{"warfarin"}}}, {"drug", {Term{"ibuprofen"}}},
            {"effect", {Term{"bleeding"}}}, {"effect", {Term{"ulcer"}}},
            {"dose", {Term{0.75}}}, {"dose", {Term{0.25}}},
            {"co_prescribed", {Term{"aspirin"}, Term{"warfarin"}}},
            {"co_prescribed", {Term{"aspirin"}, Term{"aspirin"}}},
            {"co_prescribed", {Term{"ibuprofen"}, Term{"warfarin"}}},
            {"triggers", {Term{"aspirin"}, Term{"bleeding"}, Term{0.75}}},
            {"triggers", {Term{"ibuprofen"}, Term{"ulcer"}, Term{0.25}}},
            {"ddi", {Term{"seedA"}, Term{"seedB"}}}, // 2-ary seed fact to verify arity filter & derived_facts
        };

        // Pure saturation mode (no obligations)
        const auto sat = ddiRules.solve(ddiFacts);
        require(sat.verdict == Verdict::Pass && sat.saturated, "finite DDI rules should saturate");
        const auto ddi3 = sat.query_predicate("ddi", 3);
        require(ddi3.size() == 1 && ddi3[0] == Fact{"ddi", {Term{"aspirin"}, Term{"warfarin"}, Term{"bleeding"}}},
                "should extract only the derived 3-ary ddi instance");
        require(sat.query_predicate("ddi").size() == 2, "predicate without arity returns both 2-ary and 3-ary");
        require(sat.derived_facts("ddi").size() == 1 && sat.is_derived(ddi3[0]),
                "derived_facts should exclude given seed facts");
        require(!sat.is_derived(Fact{"ddi", {Term{"seedA"}, Term{"seedB"}}}),
                "seed fact should not be marked derived");

        const auto bindings = sat.query_bindings("ddi", {"A", "B", "E"});
        require(bindings.size() == 1, "query_bindings should extract 3-ary ddi binding");
        require(bindings[0].bindings.at("A") == Term{"aspirin"} &&
                bindings[0].bindings.at("B") == Term{"warfarin"} &&
                bindings[0].bindings.at("E") == Term{"bleeding"},
                "extracted DDI bindings mismatch");
        require(!bindings[0].trace.steps.empty(), "binding match should include proof trace");

        const auto patternMatches = sat.query(Fact{"ddi", {Term{"aspirin"}, Term{"B"}, Term{"E"}}}, {"B", "E"});
        require(patternMatches.size() == 1 && patternMatches[0].bindings.at("B") == Term{"warfarin"},
                "partial pattern query should bind remaining variables");

        const auto rinQueryMatches = sat.query("exists A, B => ddi(A, B, bleeding)");
        require(rinQueryMatches.size() == 1 && rinQueryMatches[0].bindings.at("A") == Term{"aspirin"},
                "Rin string query should extract matching bindings");

        const auto alertFact = Fact{"alert", {Term{"aspirin"}, Term{"bleeding"}}};
        const auto trace = sat.explain(alertFact);
        require(trace.has_value() && !trace->steps.empty(), "explain should return derivation trace");
        require(trace->steps.back().fact == alertFact &&
                trace->steps.back().origin == ProofOrigin::Rule,
                "trace root step should be derived alert fact");
        const auto formatted = trace->format();
        require(formatted.find("greater(0.75, 0.5)") != std::string::npos &&
                formatted.find("different(aspirin, warfarin)") != std::string::npos,
                "trace should include evaluated builtin conditions");

        // Multi-threaded solve on shared Program
        std::thread t1([&] {
            const auto s = ddiRules.solve(ddiFacts);
            require(s.query_bindings("ddi", {"A", "B", "E"}).size() == 1, "thread 1 solve mismatch");
        });
        std::thread t2([&] {
            const auto s = ddiRules.solve(ddiFacts);
            require(s.query_predicate("alert", 2).size() == 1, "thread 2 solve mismatch");
        });
        t1.join();
        t2.join();
    }

    // Existential witness extraction & concise provenance via solve()
    {
        const auto exSol = solveFromOtherTranslationUnit(
            "alice: human\n"
            "bob: human\n"
            "all x: human =>\n"
            "    exists m, f => parents(m, f, x)\n"
            "verify PARENTS = exists m, f => parents(m, f, alice)\n"
            "verify ALL_PARENTS = exists x, m, f => parents(m, f, x)\n");
        require(exSol.verdict == Verdict::Pass && exSol.saturated, "existential solve should pass");
        const auto* parentsReport = exSol.find_query("PARENTS");
        require(parentsReport && parentsReport->verdict == Verdict::Pass && parentsReport->matches.size() == 1,
                "PARENTS query should find 1 existential match");
        const auto& pm = parentsReport->matches[0];
        require(pm.bindings.at("m").anonymous() && pm.bindings.at("f").anonymous() &&
                pm.bindings.at("m") != pm.bindings.at("f"),
                "existential witnesses must be distinct anonymous terms");
        require(!pm.trace.steps.empty() && !pm.trace.steps.back().witnesses.empty(),
                "existential trace must record introduced witnesses");
        const auto reportText = parentsReport->format();
        require(reportText.find("verify PARENTS PASS") != std::string::npos &&
                reportText.find("witness:") != std::string::npos &&
                reportText.find("trace:") != std::string::npos,
                "QueryReport::format should include witness and trace");

        const auto* allParents = exSol.find_query("ALL_PARENTS");
        require(allParents && allParents->matches.size() == 2,
                "all_matches=true should return all matching tuples across alice and bob");

        // all_matches=false option and trace=false option
        const auto compact = solve(
            "alice: human\n"
            "bob: human\n"
            "all x: human => exists m => mother(m, x)\n"
            "verify ANY = exists x, m => mother(m, x)\n",
            SolveOptions{true, false, false});
        require(compact.verdict == Verdict::Pass, "compact solve should pass");
        require(compact.queries[0].matches.size() == 1, "all_matches=false should return 1 witness");
        require(compact.queries[0].matches[0].trace.steps.empty(), "trace=false should omit trace");
        require(!compact.explain(compact.queries[0].matches[0].fact).has_value(),
                "explain with trace=false should return nullopt");

        // Witness round-trip safety (feeding an extracted witness into a new solve must not collide)
        const Term extractedWitness = pm.bindings.at("m");
        const Program nextGen("all x: node => exists y => next(x, y)");
        const auto nextSol = nextGen.solve(
            {{"node", {extractedWitness}}},
            {{{"next", {extractedWitness, Term{"Y"}}}, "NEXT", {"Y"}}});
        require(nextSol.verdict == Verdict::Pass, "witness round-trip should pass");
        const Term& newlyGenerated = nextSol.queries[0].matches[0].bindings.at("Y");
        require(newlyGenerated.anonymous() && newlyGenerated != extractedWitness,
                "newly generated witness must not collide with input witness");

        // Resource-limited solve behavior
        const auto limitedSol = solve(source("limited"));
        require(limitedSol.verdict == Verdict::Unknown && !limitedSol.saturated,
                "unbounded program should report Unknown and saturated=false");
        const Program limitedNoVerify("0: nat\nall n: nat => succ(n): nat");
        const auto limitedSat = limitedNoVerify.solve({});
        require(limitedSat.verdict == Verdict::Unknown && !limitedSat.saturated,
                "pure saturation hitting limit must return Unknown");
    }

    std::cout << "Rin header-only proof API passed\n";
}
