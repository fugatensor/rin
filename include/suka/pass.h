#ifndef SUKA_PASS_H
#define SUKA_PASS_H

#include <string>
#include <string_view>

namespace suka::pass {

enum class Verdict { Pass, Fail, Unknown, Error };

struct Decision {
    Verdict verdict = Verdict::Error;
    std::string explanation;
};

// Run a UTF-8 Suka program and return the aggregate result of its verify
// statements. Callers define and enumerate their own proof obligations.
// PASS means all supplied queries were proved; FAIL means at least one was
// not derived by the finite rules. UNKNOWN and ERROR also forbid adoption.
Decision run(std::string_view source);

} // namespace suka::pass

#endif
