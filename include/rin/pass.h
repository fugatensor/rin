#ifndef RIN_PASS_H
#define RIN_PASS_H

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace rin { inline constexpr char version[] = "rin 0.3.0"; }

namespace rin::pass {

enum class Verdict { Pass, Fail, Unknown, Error };

struct Decision {
    Verdict verdict = Verdict::Error;
    std::string explanation;
};

namespace detail {
struct Witness;
struct Program;
struct Query;
struct ProvenanceRecord;
} // namespace detail

class Number {
    bool negative_ = false;
    bool is_decimal_ = false;
    std::uint64_t magnitude_ = 0;
    std::int64_t scale_ = 0;
    std::string digits_;

    struct RawDecimal {
        bool negative = false;
        std::int64_t scale = 0;
        std::string digits;
    };

    static bool is_digit(char ch) noexcept { return ch >= '0' && ch <= '9'; }

    static Number from_normalized(bool negative, std::int64_t scale, std::string digits) {
        if (digits.empty()) return Number{};
        if (scale > 0 && static_cast<std::size_t>(scale) >= digits.size() && scale <= 20) {
            std::string buf = digits;
            buf.append(static_cast<std::size_t>(scale) - digits.size(), '0');
            std::uint64_t mag = 0;
            const auto converted = std::from_chars(buf.data(), buf.data() + buf.size(), mag);
            if (converted.ec == std::errc{}) {
                if (!negative) {
                    Number n;
                    n.magnitude_ = mag;
                    return n;
                }
                if (mag <= (std::uint64_t{1} << 63)) {
                    Number n;
                    n.negative_ = true;
                    n.magnitude_ = mag;
                    return n;
                }
            }
        }
        Number n;
        n.negative_ = negative;
        n.is_decimal_ = true;
        n.scale_ = scale;
        n.digits_ = std::move(digits);
        return n;
    }

    static std::optional<Number> parse_decimal_impl(std::string_view text, std::string* error_message) {
        auto fail_with = [&](std::string_view message) -> std::optional<Number> {
            if (error_message) *error_message = std::string(message);
            return std::nullopt;
        };
        if (text.empty()) return fail_with("empty numeric literal");
        std::size_t pos = 0;
        bool negative = false;
        if (text[pos] == '+' || text[pos] == '-') {
            negative = text[pos] == '-';
            ++pos;
        }
        const std::size_t int_start = pos;
        while (pos < text.size() && is_digit(text[pos])) ++pos;
        if (pos == int_start) return fail_with("invalid numeric literal");
        const std::string_view int_part = text.substr(int_start, pos - int_start);

        std::string_view frac_part;
        if (pos < text.size() && text[pos] == '.') {
            ++pos;
            const std::size_t frac_start = pos;
            while (pos < text.size() && is_digit(text[pos])) ++pos;
            if (pos == frac_start) return fail_with("invalid decimal fraction");
            frac_part = text.substr(frac_start, pos - frac_start);
        }

        std::int64_t exp = 0;
        if (pos < text.size() && (text[pos] == 'e' || text[pos] == 'E')) {
            ++pos;
            bool exp_neg = false;
            if (pos < text.size() && (text[pos] == '+' || text[pos] == '-')) {
                exp_neg = text[pos] == '-';
                ++pos;
            }
            const std::size_t exp_start = pos;
            while (pos < text.size() && is_digit(text[pos])) ++pos;
            if (pos == exp_start) return fail_with("invalid numeric exponent");
            std::int64_t exp_mag = 0;
            const auto exp_view = text.substr(exp_start, pos - exp_start);
            const auto converted = std::from_chars(exp_view.data(), exp_view.data() + exp_view.size(), exp_mag);
            if (converted.ec != std::errc{} || exp_mag > 1000000)
                return fail_with("numeric literal exponent out of range");
            exp = exp_neg ? -exp_mag : exp_mag;
        }
        if (pos != text.size()) return fail_with("invalid numeric literal");
        if (int_part.size() + frac_part.size() > 10000)
            return fail_with("numeric literal precision limit exceeded");

        std::string combined;
        combined.reserve(int_part.size() + frac_part.size());
        combined.append(int_part);
        combined.append(frac_part);

        std::size_t first_nonzero = 0;
        while (first_nonzero < combined.size() && combined[first_nonzero] == '0') ++first_nonzero;
        if (first_nonzero == combined.size()) return Number{};

        std::size_t last_nonzero = combined.size();
        while (last_nonzero > first_nonzero && combined[last_nonzero - 1] == '0') --last_nonzero;

        const std::int64_t raw_scale = static_cast<std::int64_t>(int_part.size()) + exp;
        const std::int64_t scale = raw_scale - static_cast<std::int64_t>(first_nonzero);
        return from_normalized(negative, scale, combined.substr(first_nonzero, last_nonzero - first_nonzero));
    }

    static int compare_decimal_parts(bool neg1, std::int64_t scale1, std::string_view d1,
                                     bool neg2, std::int64_t scale2, std::string_view d2) noexcept {
        if (d1.empty() && d2.empty()) return 0;
        if (d1.empty()) return neg2 ? 1 : -1;
        if (d2.empty()) return neg1 ? -1 : 1;
        if (neg1 != neg2) return neg1 ? -1 : 1;
        int mag_cmp = 0;
        if (scale1 != scale2) mag_cmp = scale1 < scale2 ? -1 : 1;
        else {
            const int ord = d1.compare(d2);
            mag_cmp = ord < 0 ? -1 : ord > 0 ? 1 : 0;
        }
        return neg1 ? -mag_cmp : mag_cmp;
    }

    void integer_to_parts(char (&buf)[24], std::int64_t& scale, std::string_view& digits) const noexcept {
        if (magnitude_ == 0) {
            scale = 0;
            digits = {};
            return;
        }
        const auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), magnitude_);
        (void)ec;
        std::size_t len = static_cast<std::size_t>(ptr - buf);
        scale = static_cast<std::int64_t>(len);
        while (len > 0 && buf[len - 1] == '0') --len;
        digits = std::string_view(buf, len);
    }

public:
    Number() = default;

    template <class T, std::enable_if_t<
        std::is_arithmetic_v<T> &&
        !std::is_same_v<T, bool> &&
        !std::is_same_v<T, char> &&
        !std::is_same_v<T, signed char> &&
        !std::is_same_v<T, unsigned char> &&
        !std::is_same_v<T, wchar_t> &&
        !std::is_same_v<T, char16_t> &&
        !std::is_same_v<T, char32_t>, int> = 0>
    Number(T value) {
        if constexpr (std::is_floating_point_v<T>) {
            const double d = static_cast<double>(value);
            if (!std::isfinite(d))
                throw std::runtime_error("non-finite floating-point value is not supported");
            if (d == 0.0) return;
            if (d >= -0x1p63 && d < 0x1p64 && std::trunc(d) == d) {
                if (d < 0.0) {
                    negative_ = true;
                    magnitude_ = (d == -0x1p63) ? (std::uint64_t{1} << 63) : static_cast<std::uint64_t>(-d);
                } else {
                    magnitude_ = static_cast<std::uint64_t>(d);
                }
                return;
            }
            char buf[64];
            const auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), d);
            if (ec != std::errc{})
                throw std::runtime_error("cannot format floating-point value");
            std::string err;
            auto parsed = parse_decimal_impl(std::string_view(buf, static_cast<std::size_t>(ptr - buf)), &err);
            if (!parsed) throw std::runtime_error(err);
            *this = std::move(*parsed);
        } else if constexpr (std::is_signed_v<T>) {
            const std::int64_t signed_value = static_cast<std::int64_t>(value);
            if (signed_value < 0) {
                negative_ = true;
                magnitude_ = std::uint64_t{0} - static_cast<std::uint64_t>(signed_value);
            } else {
                magnitude_ = static_cast<std::uint64_t>(signed_value);
            }
        } else {
            magnitude_ = static_cast<std::uint64_t>(value);
        }
    }

    static Number from_negative_magnitude(std::uint64_t magnitude) noexcept {
        Number n;
        if (magnitude != 0) {
            n.negative_ = true;
            n.magnitude_ = magnitude;
        }
        return n;
    }

    static Number decimal(std::string_view text) {
        std::string err;
        auto parsed = parse_decimal_impl(text, &err);
        if (!parsed) throw std::runtime_error(err);
        return *parsed;
    }

    static std::optional<Number> try_parse_decimal(std::string_view text, std::string* error_message = nullptr) {
        return parse_decimal_impl(text, error_message);
    }

    bool is_integer() const noexcept {
        return !is_decimal_ || (scale_ > 0 && static_cast<std::size_t>(scale_) >= digits_.size());
    }

    bool fits_int64() const noexcept {
        if (is_decimal_) return false;
        return negative_ ? magnitude_ <= (std::uint64_t{1} << 63)
                         : magnitude_ <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    }

    bool fits_uint64() const noexcept {
        return !is_decimal_ && !negative_;
    }

    std::optional<std::int64_t> as_int64() const noexcept {
        if (!fits_int64()) return std::nullopt;
        if (!negative_) return static_cast<std::int64_t>(magnitude_);
        if (magnitude_ == (std::uint64_t{1} << 63)) return std::numeric_limits<std::int64_t>::min();
        return -static_cast<std::int64_t>(magnitude_);
    }

    std::optional<std::uint64_t> as_uint64() const noexcept {
        if (!fits_uint64()) return std::nullopt;
        return magnitude_;
    }

    double as_double() const noexcept {
        if (!is_decimal_) {
            const double d = static_cast<double>(magnitude_);
            return negative_ ? -d : d;
        }
        const std::string text = to_string();
        double out = 0.0;
        const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
        (void)ptr;
        if (ec == std::errc::result_out_of_range) {
            if (scale_ > 0)
                return negative_ ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity();
            return negative_ ? -0.0 : 0.0;
        }
        return out;
    }

    std::string to_string() const {
        if (!is_decimal_) {
            return (negative_ ? "-" : "") + std::to_string(magnitude_);
        }
        std::string out;
        if (negative_) out.push_back('-');
        if (scale_ <= 0 && scale_ >= -6) {
            out += "0.";
            out.append(static_cast<std::size_t>(-scale_), '0');
            out += digits_;
        } else if (scale_ > 0 && static_cast<std::size_t>(scale_) < digits_.size()) {
            out.append(digits_, 0, static_cast<std::size_t>(scale_));
            out.push_back('.');
            out.append(digits_, static_cast<std::size_t>(scale_), std::string::npos);
        } else if (scale_ > 0 && scale_ <= 20) {
            out += digits_;
            out.append(static_cast<std::size_t>(scale_) - digits_.size(), '0');
        } else {
            out.push_back(digits_[0]);
            if (digits_.size() > 1) {
                out.push_back('.');
                out.append(digits_, 1, std::string::npos);
            }
            out.push_back('e');
            const std::int64_t exp = scale_ - 1;
            if (exp >= 0) out.push_back('+');
            out += std::to_string(exp);
        }
        return out;
    }

    int compare(const Number& other) const noexcept {
        if (!is_decimal_ && !other.is_decimal_) {
            if (negative_ != other.negative_) return negative_ ? -1 : 1;
            if (magnitude_ == other.magnitude_) return 0;
            if (negative_) return magnitude_ > other.magnitude_ ? -1 : 1;
            return magnitude_ < other.magnitude_ ? -1 : 1;
        }
        if (!is_decimal_ && magnitude_ == 0) return other.negative_ ? 1 : -1;
        if (!other.is_decimal_ && other.magnitude_ == 0) return negative_ ? -1 : 1;

        char buf1[24], buf2[24];
        std::int64_t s1 = scale_, s2 = other.scale_;
        std::string_view d1 = digits_, d2 = other.digits_;
        if (!is_decimal_) integer_to_parts(buf1, s1, d1);
        if (!other.is_decimal_) other.integer_to_parts(buf2, s2, d2);
        return compare_decimal_parts(negative_, s1, d1, other.negative_, s2, d2);
    }

    bool operator<(const Number& other) const noexcept { return compare(other) < 0; }
    bool operator<=(const Number& other) const noexcept { return compare(other) <= 0; }
    bool operator>(const Number& other) const noexcept { return compare(other) > 0; }
    bool operator>=(const Number& other) const noexcept { return compare(other) >= 0; }
    bool operator==(const Number& other) const noexcept { return compare(other) == 0; }
    bool operator!=(const Number& other) const noexcept { return compare(other) != 0; }
};

struct Term {
private:
    std::optional<std::size_t> witness_;
    friend struct detail::Witness;

public:
    std::string name;
    std::vector<Term> arguments;
    std::optional<Number> number;

    Term(std::string symbol, std::vector<Term> children = {})
        : name(std::move(symbol)), arguments(std::move(children)) {}
    Term(Number value) : number(std::move(value)) {}
    template <class T, std::enable_if_t<
        std::is_arithmetic_v<T> &&
        !std::is_same_v<T, bool> &&
        !std::is_same_v<T, char> &&
        !std::is_same_v<T, signed char> &&
        !std::is_same_v<T, unsigned char> &&
        !std::is_same_v<T, wchar_t> &&
        !std::is_same_v<T, char16_t> &&
        !std::is_same_v<T, char32_t>, int> = 0>
    Term(T value) : number(Number(value)) {}

    static Term decimal(std::string_view text) { return Term(Number::decimal(text)); }

    bool anonymous() const { return witness_.has_value(); }
    std::optional<std::size_t> witness_id() const { return witness_; }
    bool is_number() const { return number.has_value(); }
    bool same_head(const Term& other) const {
        return witness_ == other.witness_ && number == other.number && name == other.name;
    }

    int compare(const Term& other) const {
        if (witness_ != other.witness_)
            return witness_ < other.witness_ ? -1 : 1;
        if (number.has_value() != other.number.has_value())
            return number.has_value() < other.number.has_value() ? -1 : 1;
        if (number.has_value()) {
            if (const int order = number->compare(*other.number)) return order;
        }
        const int order = name.compare(other.name);
        if (order) return order;
        for (std::size_t i = 0; i < std::min(arguments.size(), other.arguments.size()); ++i) {
            const int child = arguments[i].compare(other.arguments[i]);
            if (child) return child;
        }
        return arguments.size() < other.arguments.size() ? -1 : arguments.size() > other.arguments.size() ? 1 : 0;
    }
    bool operator<(const Term& other) const { return compare(other) < 0; }
    bool operator==(const Term& other) const { return compare(other) == 0; }
    bool operator!=(const Term& other) const { return compare(other) != 0; }

    std::string format() const {
        if (witness_.has_value()) return ":w" + std::to_string(*witness_);
        if (number.has_value()) return number->to_string();
        if (arguments.empty()) return name;
        std::string out = name + "(";
        for (std::size_t i = 0; i < arguments.size(); ++i) {
            if (i) out += ", ";
            out += arguments[i].format();
        }
        out.push_back(')');
        return out;
    }
};

struct Fact {
    std::string predicate;
    std::vector<Term> arguments;
    bool operator<(const Fact& other) const {
        return std::tie(predicate, arguments) < std::tie(other.predicate, other.arguments);
    }
    bool operator==(const Fact& other) const {
        return predicate == other.predicate && arguments == other.arguments;
    }
    bool operator!=(const Fact& other) const { return !(*this == other); }
    std::string format() const {
        std::string out = predicate + "(";
        for (std::size_t i = 0; i < arguments.size(); ++i) {
            if (i) out += ", ";
            out += arguments[i].format();
        }
        out.push_back(')');
        return out;
    }
};

struct Obligation {
    Fact atom;
    std::string name;
    std::vector<std::string> variables = {};
};

enum class ProofOrigin { Given, Rule, Builtin };

struct ProofStep {
    std::size_t id = 0;
    Fact fact;
    ProofOrigin origin = ProofOrigin::Given;
    std::optional<std::size_t> rule_index;
    std::string rule_location;
    std::string rule_text;
    std::vector<std::size_t> premise_steps;
    std::vector<Fact> builtin_conditions;
    std::map<std::string, Term> witnesses;
};

struct ProofTrace {
    std::vector<ProofStep> steps;

    std::string format(std::string_view indent = "") const {
        std::ostringstream out;
        for (const auto& step : steps) {
            out << indent << "[#" << step.id << "] ";
            if (step.origin == ProofOrigin::Given) {
                out << "given: " << step.fact.format();
                if (!step.rule_location.empty()) out << " (" << step.rule_location << ")";
            } else if (step.origin == ProofOrigin::Builtin) {
                out << "builtin: " << step.fact.format();
            } else {
                out << "derived: " << step.fact.format() << " <= ";
                out << (step.rule_text.empty() ? "rule" : step.rule_text);
                if (!step.rule_location.empty()) out << " (" << step.rule_location << ")";
                if (!step.premise_steps.empty()) {
                    out << " from [";
                    for (std::size_t i = 0; i < step.premise_steps.size(); ++i) {
                        if (i) out << ", ";
                        out << '#' << step.premise_steps[i];
                    }
                    out << ']';
                }
                if (!step.builtin_conditions.empty()) {
                    out << " where ";
                    for (std::size_t i = 0; i < step.builtin_conditions.size(); ++i) {
                        if (i) out << " & ";
                        out << step.builtin_conditions[i].format();
                    }
                }
                if (!step.witnesses.empty()) {
                    out << " [witness: ";
                    bool first = true;
                    for (const auto& [var, term] : step.witnesses) {
                        if (!first) out << ", ";
                        first = false;
                        out << var << " = " << term.format();
                    }
                    out << ']';
                }
            }
            out << '\n';
        }
        return out.str();
    }
};

struct WitnessMatch {
    Fact fact;
    std::map<std::string, Term> bindings;
    ProofTrace trace;
};

struct QueryReport {
    std::string name;
    Fact pattern;
    std::vector<std::string> variables;
    Verdict verdict = Verdict::Error;
    std::vector<WitnessMatch> matches;

    std::string format() const {
        std::ostringstream out;
        out << "verify " << name << ' ';
        if (verdict == Verdict::Pass) out << "PASS";
        else if (verdict == Verdict::Fail) out << "FAIL";
        else if (verdict == Verdict::Unknown) out << "UNKNOWN";
        else out << "ERROR";
        if (verdict == Verdict::Pass && matches.size() > 1)
            out << " (" << matches.size() << " matches)";
        out << '\n';
        if (verdict == Verdict::Pass) {
            for (std::size_t i = 0; i < matches.size(); ++i) {
                const auto& match = matches[i];
                if (!match.bindings.empty()) {
                    out << "  witness";
                    if (matches.size() > 1) out << '[' << i << ']';
                    out << ": ";
                    bool first = true;
                    for (const auto& [var, term] : match.bindings) {
                        if (!first) out << ", ";
                        first = false;
                        out << var << " = " << term.format();
                    }
                    out << " (" << match.fact.format() << ")\n";
                } else {
                    out << "  fact";
                    if (matches.size() > 1) out << '[' << i << ']';
                    out << ": " << match.fact.format() << '\n';
                }
                if (!match.trace.steps.empty()) {
                    out << "  trace:\n" << match.trace.format("    ");
                }
            }
        }
        return out.str();
    }
};

struct SolveOptions {
    bool saturate = true;
    bool all_matches = true;
    bool trace = true;
};

struct Solution {
    Verdict verdict = Verdict::Error;
    bool saturated = false;
    std::string explanation;
    std::vector<QueryReport> queries;
    std::vector<Fact> facts;

    struct DerivationData {
        ProofOrigin origin = ProofOrigin::Given;
        std::size_t rule_index = 0;
        std::string location;
        std::string rule_text;
        std::vector<std::size_t> premise_indices;
        std::vector<Fact> builtin_conditions;
        std::map<std::string, Term> witnesses;
    };

    std::vector<DerivationData> derivations_;

    bool is_derived(const Fact& fact) const;
    std::optional<ProofTrace> explain(const Fact& fact) const;
    std::vector<Fact> query_predicate(std::string_view predicate) const;
    std::vector<Fact> query_predicate(std::string_view predicate, std::size_t arity) const;
    std::vector<Fact> derived_facts() const;
    std::vector<Fact> derived_facts(std::string_view predicate) const;
    std::vector<Fact> derived_facts(std::string_view predicate, std::size_t arity) const;
    std::vector<WitnessMatch> query_bindings(std::string_view predicate,
                                             const std::vector<std::string>& variable_names) const;
    std::vector<WitnessMatch> query(const Fact& pattern,
                                    const std::vector<std::string>& variables = {}) const;
    std::vector<WitnessMatch> query(std::string_view rin_query) const;
    const QueryReport* find_query(std::string_view name) const;
    std::string format() const;
    ProofTrace build_trace_for_index(std::size_t root_index) const;
};

namespace detail {

struct Witness {
    static Term make(std::size_t id) {
        Term term{std::string{}};
        term.witness_ = id;
        return term;
    }
};

struct Result {
    int status;
    std::string output, error;
};

inline Decision decision(Result result) {
    const auto verdict = result.status == 0 ? Verdict::Pass :
        result.status == 1 ? Verdict::Fail : result.status == 3 ? Verdict::Unknown : Verdict::Error;
    result.output += result.error;
    return {verdict, std::move(result.output)};
}

constexpr std::size_t max_depth = 128;
constexpr std::size_t max_rounds = 256, max_facts = 10000, max_bindings = 10000;
constexpr std::size_t max_term_nodes = 10000;

struct Location {
    std::string path;
    std::size_t number;
};

inline std::string format_location(const Location& loc) {
    if (loc.path.empty()) return {};
    return loc.path + ":" + std::to_string(loc.number);
}

using Atom = Fact;

inline bool is_builtin(const std::string& predicate) {
    return predicate == "different" ||
           predicate == "less" ||
           predicate == "less_equal" ||
           predicate == "greater" ||
           predicate == "greater_equal";
}

using Variables = std::set<std::string>;
using Binding = std::vector<const Term*>;

constexpr std::size_t no_slot = std::size_t(-1);
struct PatternNode {
    std::size_t slot = no_slot, end = 0;
    bool ground = true, indexed = false;
};
// Preorder metadata refers to the owning atom by offsets, so copying a compiled
// Program preserves its patterns. Variable slots follow declaration-name order.
struct Pattern {
    std::vector<PatternNode> nodes;
    std::vector<std::size_t> arguments, introduced;
};

struct Rule {
    std::vector<Atom> premises;
    Atom conclusion;
    Variables variables, existentials;
    Location line;
    std::vector<Pattern> patterns = {};
    Pattern result = {};
    std::string description = {};
};
struct Query {
    Atom atom;
    std::string name;
    Variables variables;
    Pattern pattern = {};
};
struct Program {
    std::set<Atom> facts;
    std::vector<Rule> rules;
    std::vector<Query> queries;
    std::map<Atom, Location> fact_locations = {};
};
struct Line {
    Location location;
    std::size_t indent;
    std::vector<std::string> tokens;
};

[[noreturn]] inline void fail(const Location& line, const std::string& message) {
    throw std::runtime_error(line.path + ":" + std::to_string(line.number) + ": " + message);
}

inline bool is_identifier(const std::string& token) {
    return !token.empty() && token != "->" && token != "=>" &&
           token.find_first_of(":,=()|&\"") == std::string::npos;
}

inline bool looks_like_number(std::string_view token) noexcept {
    if (token.empty()) return false;
    std::size_t pos = 0;
    if (token[pos] == '+' || token[pos] == '-') ++pos;
    const std::size_t int_start = pos;
    while (pos < token.size() && token[pos] >= '0' && token[pos] <= '9') ++pos;
    if (pos == int_start) return false;
    if (pos < token.size() && token[pos] == '.') {
        ++pos;
        const std::size_t frac_start = pos;
        while (pos < token.size() && token[pos] >= '0' && token[pos] <= '9') ++pos;
        if (pos == frac_start) return false;
    }
    if (pos < token.size() && (token[pos] == 'e' || token[pos] == 'E')) {
        ++pos;
        if (pos < token.size() && (token[pos] == '+' || token[pos] == '-')) ++pos;
        const std::size_t exp_start = pos;
        while (pos < token.size() && token[pos] >= '0' && token[pos] <= '9') ++pos;
        if (pos == exp_start) return false;
    }
    return pos == token.size();
}

inline std::optional<Number> parse_number_token(std::string_view token, const Location& line) {
    if (!looks_like_number(token)) return std::nullopt;
    const bool has_decimal_or_exp = token.find_first_of(".eE") != std::string_view::npos;
    if (!has_decimal_or_exp) {
        std::size_t pos = 0;
        bool negative = false;
        if (token[pos] == '+' || token[pos] == '-') {
            negative = token[pos] == '-';
            ++pos;
        }
        while (pos < token.size() && token[pos] == '0') ++pos;
        if (pos == token.size()) return Number{std::int64_t{0}};
        std::uint64_t mag = 0;
        const auto digits = token.substr(pos);
        const auto converted = std::from_chars(digits.data(), digits.data() + digits.size(), mag);
        if (negative) {
            if (converted.ec != std::errc{} || mag > (std::uint64_t{1} << 63))
                fail(line, "signed integer literal exceeds 64-bit int64 range");
            return Number::from_negative_magnitude(mag);
        }
        if (converted.ec != std::errc{})
            fail(line, "integer literal exceeds 64-bit uint64 range");
        return Number{mag};
    }
    std::string err;
    auto parsed = Number::try_parse_decimal(token, &err);
    if (!parsed) fail(line, err);
    return parsed;
}

inline std::vector<Line> read_lines(std::istream& input, const std::string& path) {
    std::vector<Line> lines;
    std::string text;
    std::size_t number = 0;
    while (std::getline(input, text)) {
        ++number;
        if (number == 1 && text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);
        Line line{{path, number}, 0, {}};
        std::size_t pos = 0;
        while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t'))
            line.indent += text[pos++] == '\t' ? 4 : 1;
        while (pos < text.size() && text[pos] != '#') {
            if (std::isspace(static_cast<unsigned char>(text[pos]))) {
                ++pos;
            } else if (text[pos] == '"') {
                const auto start = pos++;
                while (pos < text.size() && text[pos] != '"') ++pos;
                if (pos == text.size()) fail(line.location, "unterminated import filename");
                line.tokens.push_back(text.substr(start, ++pos - start));
            } else if (text.compare(pos, 2, "->") == 0 || text.compare(pos, 2, "=>") == 0) {
                line.tokens.push_back(text.substr(pos, 2));
                pos += 2;
            } else if (std::string(":,=()|&").find(text[pos]) != std::string::npos) {
                line.tokens.push_back(text.substr(pos++, 1));
            } else {
                const auto start = pos;
                while (pos < text.size() &&
                       !std::isspace(static_cast<unsigned char>(text[pos])) &&
                       text[pos] != '#' &&
                       std::string(":,=()|&\"").find(text[pos]) == std::string::npos &&
                       text.compare(pos, 2, "->") != 0 && text.compare(pos, 2, "=>") != 0) ++pos;
                line.tokens.push_back(text.substr(start, pos - start));
            }
        }
        if (!line.tokens.empty()) lines.push_back(std::move(line));
    }
    if (input.bad()) throw std::runtime_error("cannot read input file");
    return lines;
}

struct Cursor {
    const Line& line;
    std::size_t pos, end;
    explicit Cursor(const Line& source, std::size_t begin = 0,
                    std::size_t limit = std::string::npos)
        : line(source), pos(begin), end(std::min(limit, source.tokens.size())) {}
    bool done() const { return pos == end; }
    const std::string& peek(std::size_t offset = 0) const {
        static const std::string empty;
        return pos + offset < end ? line.tokens[pos + offset] : empty;
    }
    bool take(const std::string& token) {
        if (peek() != token || done()) return false;
        ++pos;
        return true;
    }
    bool keyword(const std::string& token) const {
        return peek() == token && peek(1) != "(" && peek(1) != ":";
    }
    void expect(const std::string& token) {
        if (!take(token)) fail(line.location, "expected '" + token + "'");
    }
    std::string name() {
        if (!is_identifier(peek())) fail(line.location, "expected a name");
        return line.tokens[pos++];
    }
    std::string symbolic_name(const char* message) {
        auto value = name();
        if (looks_like_number(value)) fail(line.location, message);
        return value;
    }
    std::string variable() {
        return symbolic_name("a variable must have a symbolic name");
    }
    void finish() const {
        if (!done()) fail(line.location, "unexpected '" + peek() + "'");
    }
    Term term(std::size_t depth = 0) {
        if (depth >= max_depth) fail(line.location, "term nesting limit exceeded");
        auto head = name();
        if (take("(")) {
            if (looks_like_number(head))
                fail(line.location, "expected a symbolic constructor or relation name");
            Term result{std::move(head), {}};
            if (!take(")")) {
                do { result.arguments.push_back(term(depth + 1)); } while (take(","));
                expect(")");
            }
            return result;
        }
        if (auto number = parse_number_token(head, line.location))
            return Term(std::move(*number));
        return Term{std::move(head), {}};
    }
    Atom atom() {
        const bool call = peek(1) == "(";
        auto value = term();
        if (take(":")) {
            auto type = symbolic_name("a type must have a symbolic name");
            return {std::move(type), {std::move(value)}};
        }
        if (!call || value.number) fail(line.location, "expected OBJECT : TYPE or RELATION(arguments)");
        return {std::move(value.name), std::move(value.arguments)};
    }
    Atom exists(Variables& variables) {
        expect("exists");
        do {
            const auto variable = this->variable();
            if (!variables.insert(variable).second)
                fail(line.location, "duplicate existential variable '" + variable + "'");
        } while (take(","));
        expect("=>");
        return atom();
    }
};

struct Imports {
    std::set<std::filesystem::path> active, loaded;
};

inline std::vector<Line> load_file(const std::filesystem::path& path, Imports& imports);

inline std::vector<Line> expand_imports(const std::vector<Line>& lines,
                               const std::filesystem::path& directory, Imports& imports) {
    std::vector<Line> expanded;
    for (const auto& line : lines) {
        if (line.tokens[0] != "import") { expanded.push_back(line); continue; }
        if (line.indent || line.tokens.size() != 2)
            fail(line.location, "expected: import MODULE (without quotes or .rin suffix)");
        const auto& name = line.tokens[1];
        if (name.find_first_of("\"'") != std::string::npos)
            fail(line.location, "remove quotes from import module names; write: import facts");
        constexpr std::string_view suffix = ".rin";
        if (name.size() >= suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
            fail(line.location, "omit the .rin suffix in imports; write: import facts");
        if (!is_identifier(name) || name.front() == '.' ||
            name.find_first_of("/\\:") != std::string::npos ||
            std::any_of(name.begin(), name.end(), [](unsigned char ch) { return ch <= 32 || ch == 127; }))
            fail(line.location, "imports require a bare module name in the same directory");
        const auto relative = std::filesystem::u8path(name + ".rin");
        const auto target = std::filesystem::weakly_canonical(directory / relative);
        if (target.parent_path() != std::filesystem::weakly_canonical(directory))
            fail(line.location, "imports must stay in the same directory");
        if (imports.active.count(target)) fail(line.location, "cyclic import: " + name);
        if (!std::filesystem::is_regular_file(target)) fail(line.location, "cannot import module: " + name + " (" + name + ".rin)");
        auto imported = load_file(target, imports);
        expanded.insert(expanded.end(), imported.begin(), imported.end());
    }
    return expanded;
}

inline std::vector<Line> load_file(const std::filesystem::path& input, Imports& imports) {
    const auto path = std::filesystem::weakly_canonical(std::filesystem::absolute(input));
    if (imports.loaded.count(path)) return {};
    if (imports.active.size() >= 64 || imports.loaded.size() >= 256)
        fail({path.u8string(), 1}, "import limit exceeded");
    std::ifstream file(path);
    if (!file || !std::filesystem::is_regular_file(path)) fail({path.u8string(), 1}, "cannot open input file");
    imports.active.insert(path);
    auto lines = expand_imports(read_lines(file, path.u8string()), path.parent_path(), imports);
    imports.active.erase(path);
    imports.loaded.insert(path);
    return lines;
}

inline std::vector<Line> load_source(std::string_view text, const char* base_directory) {
    if (text.find('\0') != std::string_view::npos)
        throw std::runtime_error("<source>:1: source contains an embedded NUL byte");
    const auto directory = base_directory && *base_directory
        ? std::filesystem::absolute(std::filesystem::u8path(base_directory)) : std::filesystem::current_path();
    if (!std::filesystem::is_directory(directory))
        throw std::runtime_error("<source>:1: base directory does not exist");
    std::istringstream input{std::string(text)};
    Imports imports;
    return expand_imports(read_lines(input, "<source>"), directory, imports);
}

inline std::size_t node_count(const Term& term, std::size_t limit) {
    std::size_t count = 1;
    if (!limit) return count;
    for (const auto& argument : term.arguments) {
        count += node_count(argument, limit - count);
        if (count > limit) return limit + 1;
    }
    return count;
}

using Slots = std::vector<std::pair<std::string_view, std::size_t>>;

inline Pattern compile_pattern(const Atom& atom, const Slots& slots, std::vector<bool>& bound) {
    Pattern result;
    std::size_t nodes = 0;
    for (const auto& term : atom.arguments) nodes += node_count(term, std::size_t(-1));
    result.nodes.reserve(nodes);
    result.arguments.reserve(atom.arguments.size());
    result.introduced.reserve(std::min(atom.arguments.size(),
        std::size_t(std::count(bound.begin(), bound.end(), false))));
    auto compile = [&](const Term& term, const auto& self) -> std::size_t {
        const auto position = result.nodes.size();
        result.nodes.push_back({});
        if (!term.number && term.arguments.empty()) {
            const auto variable = std::lower_bound(slots.begin(), slots.end(), std::string_view(term.name),
                [](const auto& entry, std::string_view name) { return entry.first < name; });
            if (variable != slots.end() && variable->first == term.name) {
                const auto slot = variable->second;
                result.nodes[position].slot = slot;
                result.nodes[position].ground = false;
                if (!bound[slot]) result.introduced.push_back(slot);
                bound[slot] = true;
            }
        }
        for (const auto& child : term.arguments) {
            const auto child_position = self(child, self);
            result.nodes[position].ground = result.nodes[position].ground && result.nodes[child_position].ground;
        }
        result.nodes[position].end = result.nodes.size();
        return position;
    };
    for (const auto& term : atom.arguments) result.arguments.push_back(compile(term, compile));
    for (auto position : result.arguments) {
        auto& node = result.nodes[position];
        node.indexed = node.ground || (node.slot != no_slot &&
            std::find(result.introduced.begin(), result.introduced.end(), node.slot) == result.introduced.end());
    }
    return result;
}

inline void compile_rule(Rule& rule) {
    Slots slots;
    slots.reserve(rule.variables.size() + rule.existentials.size());
    for (const auto& variable : rule.variables) slots.emplace_back(variable, slots.size());
    std::vector<bool> bound(slots.size());
    rule.patterns.reserve(rule.premises.size());
    for (const auto& premise : rule.premises)
        rule.patterns.push_back(compile_pattern(premise, slots, bound));
    for (const auto& variable : rule.existentials) slots.emplace_back(variable, slots.size());
    std::sort(slots.begin(), slots.end());
    bound.resize(slots.size(), true);
    rule.result = compile_pattern(rule.conclusion, slots, bound);
}

inline void compile_query(Query& query) {
    if (query.variables.empty()) return;
    Slots slots;
    slots.reserve(query.variables.size());
    for (const auto& variable : query.variables) slots.emplace_back(variable, slots.size());
    std::vector<bool> bound(slots.size());
    query.pattern = compile_pattern(query.atom, slots, bound);
}

inline std::string format_rule_description(const std::vector<Atom>& guards,
                                           const std::vector<Atom>& explicit_premises,
                                           const Variables& existentials,
                                           const Atom& conclusion) {
    std::ostringstream out;
    out << "all ";
    for (std::size_t i = 0; i < guards.size(); ++i) {
        if (i) out << ", ";
        out << guards[i].arguments[0].format() << ": " << guards[i].predicate;
    }
    out << " => ";
    if (!explicit_premises.empty()) {
        for (std::size_t i = 0; i < explicit_premises.size(); ++i) {
            if (i) out << " & ";
            out << explicit_premises[i].format();
        }
        out << " -> ";
    }
    if (!existentials.empty()) {
        out << "exists ";
        bool first = true;
        for (const auto& variable : existentials) {
            if (!first) out << ", ";
            first = false;
            out << variable;
        }
        out << " => ";
    }
    out << conclusion.format();
    return out.str();
}

inline Program parse(const std::vector<Line>& lines) {
    Program program;
    for (std::size_t i = 0; i < lines.size();) {
        const auto& line = lines[i];
        Cursor cursor(line);
        if (line.indent) fail(line.location, "unexpected indentation");
        if (cursor.keyword("all") && cursor.take("all")) {
            Variables variables;
            std::vector<Atom> guards;
            do {
                std::vector<std::string> group;
                do {
                    const auto variable = cursor.variable();
                    if (!variables.insert(variable).second) fail(line.location, "duplicate variable '" + variable + "'");
                    group.push_back(variable);
                } while (cursor.take(","));
                cursor.expect(":");
                const auto type = cursor.symbolic_name("a type must have a symbolic name");
                if (is_builtin(type)) fail(line.location, type + " is a reserved term comparison");
                for (const auto& variable : group) guards.push_back({type, {{variable, {}}}});
            } while (cursor.take(","));
            cursor.expect("=>");
            auto add_rule = [&](Cursor& rule_cursor) {
                Rule rule{{}, {}, variables, {}, rule_cursor.line.location};
                if (!rule_cursor.keyword("exists")) {
                    auto first = rule_cursor.atom();
                    if (rule_cursor.take("&")) {
                        rule.premises.push_back(std::move(first));
                        do { rule.premises.push_back(rule_cursor.atom()); }
                        while (rule_cursor.take("&"));
                        rule_cursor.expect("->");
                    } else if (rule_cursor.take("->")) {
                        rule.premises.push_back(std::move(first));
                    } else rule.conclusion = std::move(first);
                }
                if (rule.conclusion.predicate.empty()) {
                    if (rule_cursor.keyword("exists"))
                        rule.conclusion = rule_cursor.exists(rule.existentials);
                    else rule.conclusion = rule_cursor.atom();
                }
                rule_cursor.finish();
                if (is_builtin(rule.conclusion.predicate))
                    fail(rule.line, rule.conclusion.predicate + " is only allowed as a rule premise");
                for (const auto& premise : rule.premises)
                    if (is_builtin(premise.predicate) && premise.arguments.size() != 2)
                        fail(rule.line, premise.predicate + " requires exactly two terms");
                for (const auto& variable : rule.existentials)
                    if (variables.count(variable)) fail(rule.line, "existential variable shadows '" + variable + "'");
                rule.description = format_rule_description(guards, rule.premises, rule.existentials, rule.conclusion);
                rule.premises.insert(rule.premises.end(), guards.begin(), guards.end());
                // A structural comparison needs every universal variable bound.
                // Relational premises and type guards establish those bindings.
                std::stable_partition(rule.premises.begin(), rule.premises.end(),
                    [](const Atom& premise) { return !is_builtin(premise.predicate); });
                compile_rule(rule);
                program.rules.push_back(std::move(rule));
            };
            if (!cursor.done()) {
                add_rule(cursor);
                ++i;
                continue;
            }
            ++i;
            if (i == lines.size() || !lines[i].indent) fail(line.location, "expected an indented rule after '=>'");
            const auto indent = lines[i].indent;
            while (i < lines.size() && lines[i].indent) {
                if (lines[i].indent != indent) fail(lines[i].location, "inconsistent rule indentation");
                Cursor rule_cursor(lines[i]);
                add_rule(rule_cursor);
                ++i;
            }
            continue;
        } else if (cursor.keyword("verify") && cursor.take("verify")) {
            const auto label = cursor.name();
            cursor.expect("=");
            Cursor query_cursor(line, cursor.pos);
            Query query{{}, label, {}};
            if (query_cursor.keyword("exists")) {
                query.atom = query_cursor.exists(query.variables);
            } else query.atom = query_cursor.atom();
            query_cursor.finish();
            if (is_builtin(query.atom.predicate) &&
                (query.atom.arguments.size() != 2 || !query.variables.empty()))
                fail(line.location, query.atom.predicate + " verification requires exactly two ground terms");
            compile_query(query);
            program.queries.push_back(std::move(query));
        } else {
            auto fact = cursor.atom();
            cursor.finish();
            if (is_builtin(fact.predicate))
                fail(line.location, fact.predicate + " cannot be asserted as a fact");
            const auto inserted = program.facts.insert(std::move(fact));
            if (inserted.second) program.fact_locations.emplace(*inserted.first, line.location);
        }
        ++i;
    }
    return program;
}

inline Query compile_query_spec(const Fact& atom, std::string name,
                                const std::vector<std::string>& variable_names) {
    Query query{atom, std::move(name), {}};
    for (const auto& variable : variable_names) {
        if (variable.empty() || !is_identifier(variable) || looks_like_number(variable))
            throw std::runtime_error("query variable '" + variable + "' must have a symbolic name");
        if (!query.variables.insert(variable).second)
            throw std::runtime_error("duplicate query variable '" + variable + "'");
    }
    if (is_builtin(query.atom.predicate) &&
        (query.atom.arguments.size() != 2 || !query.variables.empty()))
        throw std::runtime_error(query.atom.predicate + " verification requires exactly two ground terms");
    compile_query(query);
    return query;
}

inline Query parse_single_query(std::string_view text) {
    const auto lines = load_source(text, nullptr);
    if (lines.size() != 1 || lines[0].indent != 0)
        throw std::runtime_error("expected a single query expression");
    Cursor cursor(lines[0]);
    Query query{{}, {}, {}};
    if (cursor.keyword("exists")) query.atom = cursor.exists(query.variables);
    else query.atom = cursor.atom();
    cursor.finish();
    if (is_builtin(query.atom.predicate) &&
        (query.atom.arguments.size() != 2 || !query.variables.empty()))
        throw std::runtime_error(query.atom.predicate + " query requires exactly two ground terms");
    compile_query(query);
    return query;
}

inline bool match_term(const Term& term, const Pattern& pattern, std::size_t position,
                       const Term& value, Binding& binding) {
    const auto& node = pattern.nodes[position];
    if (node.slot != no_slot) {
        auto& bound = binding[node.slot];
        if (bound) return *bound == value;
        bound = &value;
        return true;
    }
    if (node.ground) return term == value;
    if (!term.same_head(value) || term.arguments.size() != value.arguments.size()) return false;
    auto child = position + 1;
    for (std::size_t i = 0; i < term.arguments.size(); ++i) {
        if (!match_term(term.arguments[i], pattern, child, value.arguments[i], binding)) return false;
        child = pattern.nodes[child].end;
    }
    return true;
}

// The relation index already establishes the predicate and arity.
inline bool match(const Atom& atom, const Pattern& pattern, const Atom& fact, Binding& binding) {
    for (std::size_t i = 0; i < atom.arguments.size(); ++i)
        if (!match_term(atom.arguments[i], pattern, pattern.arguments[i], fact.arguments[i], binding)) return false;
    return true;
}

struct ProofLimit {};

inline Term substitute(const Term& term, const Pattern& pattern, std::size_t position,
                       const Binding& binding, std::size_t& budget) {
    const auto& node = pattern.nodes[position];
    if (node.slot != no_slot || node.ground) {
        const auto& value = node.slot != no_slot ? *binding[node.slot] : term;
        const auto count = node.slot != no_slot ? node_count(value, budget) : node.end - position;
        if (count > budget) throw ProofLimit{};
        budget -= count;
        return value;
    }
    if (!budget) throw ProofLimit{};
    --budget;
    Term result{term.name, {}};
    result.arguments.reserve(term.arguments.size());
    auto child = position + 1;
    for (const auto& argument : term.arguments) {
        result.arguments.push_back(substitute(argument, pattern, child, binding, budget));
        child = pattern.nodes[child].end;
    }
    return result;
}

inline const Term& comparison_term(const Atom& atom, const Pattern& pattern, std::size_t argument,
                                   const Binding& binding, std::optional<Term>& temporary) {
    const auto position = pattern.arguments[argument];
    const auto& node = pattern.nodes[position];
    if (node.slot != no_slot) {
        const auto& value = *binding[node.slot];
        if (node_count(value, max_term_nodes) > max_term_nodes) throw ProofLimit{};
        return value;
    }
    if (node.ground) {
        if (node.end - position > max_term_nodes) throw ProofLimit{};
        return atom.arguments[argument];
    }
    auto budget = max_term_nodes;
    temporary = substitute(atom.arguments[argument], pattern, position, binding, budget);
    return *temporary;
}

inline std::size_t depth(const Term& term) {
    std::size_t result = 1;
    for (const auto& argument : term.arguments) result = std::max(result, 1 + depth(argument));
    return result;
}

inline bool compare(const std::string& predicate, const Term& left, const Term& right) {
    if (predicate == "different") return !(left == right);
    if (!left.number || !right.number) return false;
    const int order = left.number->compare(*right.number);
    if (predicate == "less") return order < 0;
    if (predicate == "less_equal") return order <= 0;
    if (predicate == "greater") return order > 0;
    return order >= 0;
}

// Fact terms remain owned by the immutable nodes of Program::facts. Column keys
// and join bindings borrow those terms for this proof only.
class FactIndex {
public:
    using Rows = std::vector<const Atom*>;
    struct TermLess {
        bool operator()(const Term* left, const Term* right) const { return *left < *right; }
    };
    struct Relation {
        Rows rows;
        std::vector<std::map<const Term*, Rows, TermLess>> columns;
        std::vector<std::size_t> indexed;
    };

private:
    std::map<std::pair<std::string, std::size_t>, Relation> relations_;

public:
    void require(const Atom& atom, const Pattern& pattern) {
        auto& relation = relations_[{atom.predicate, atom.arguments.size()}];
        for (std::size_t i = 0; i < atom.arguments.size(); ++i) {
            if (!pattern.nodes[pattern.arguments[i]].indexed) continue;
            if (relation.columns.empty()) relation.columns.resize(atom.arguments.size());
            if (std::find(relation.indexed.begin(), relation.indexed.end(), i) == relation.indexed.end())
                relation.indexed.push_back(i);
        }
    }

    const Relation* find_relation(const Atom& atom) const {
        const auto found = relations_.find({atom.predicate, atom.arguments.size()});
        return found == relations_.end() ? nullptr : &found->second;
    }

    const Relation& relation(const Atom& atom) const {
        return relations_.find({atom.predicate, atom.arguments.size()})->second;
    }

    void add(const Atom& fact) {
        const auto found = relations_.find({fact.predicate, fact.arguments.size()});
        if (found == relations_.end()) return;
        auto& indexed = found->second;
        indexed.rows.push_back(&fact);
        for (auto i : indexed.indexed)
            indexed.columns[i][&fact.arguments[i]].push_back(&fact);
    }

    static const Rows& candidates(const Atom& atom, const Pattern& pattern,
                                  const Binding& binding, const Relation& relation) {
        static const Rows empty;
        const Rows* rows = &relation.rows;
        if (rows->empty()) return empty;
        for (std::size_t i = 0; i < atom.arguments.size(); ++i) {
            const auto& node = pattern.nodes[pattern.arguments[i]];
            if (!node.indexed) continue;
            const auto* value = node.slot != no_slot ? binding[node.slot]
                                                     : &atom.arguments[i];
            const auto column = relation.columns[i].find(value);
            if (column == relation.columns[i].end()) return empty;
            if (column->second.size() < rows->size()) rows = &column->second;
        }
        return *rows;
    }
};

inline bool is_proven(const Query& query, const Program& program, FactIndex& facts) {
    if (is_builtin(query.atom.predicate))
        return compare(query.atom.predicate, query.atom.arguments[0], query.atom.arguments[1]);
    if (query.variables.empty()) return program.facts.count(query.atom) != 0;
    const auto* relation = facts.find_relation(query.atom);
    if (!relation) return false;
    Binding binding(query.variables.size());
    for (const auto* fact : facts.candidates(query.atom, query.pattern, binding, *relation)) {
        if (match(query.atom, query.pattern, *fact, binding)) return true;
        std::fill(binding.begin(), binding.end(), nullptr);
    }
    return false;
}

struct BindingLess {
    bool operator()(const Binding& left, const Binding& right) const {
        for (std::size_t i = 0; i < left.size(); ++i) {
            const auto order = left[i]->compare(*right[i]);
            if (order) return order < 0;
        }
        return false;
    }
};

struct ProvenanceRecord {
    ProofOrigin origin = ProofOrigin::Given;
    std::size_t rule_index = 0;
    std::string location;
    std::string rule_text;
    std::vector<const Atom*> premises;
    std::vector<Atom> builtin_conditions;
    std::map<std::string, Term> witnesses;
};
using ProvenanceMap = std::map<const Atom*, ProvenanceRecord>;

inline void scan_max_witness(const Term& term, std::size_t& next_witness) {
    if (const auto id = term.witness_id(); id && *id >= next_witness)
        next_witness = *id + 1;
    for (const auto& argument : term.arguments) scan_max_witness(argument, next_witness);
}

inline std::size_t initial_next_witness(const Program& program) {
    std::size_t next_witness = 0;
    for (const auto& fact : program.facts)
        for (const auto& argument : fact.arguments) scan_max_witness(argument, next_witness);
    for (const auto& query : program.queries)
        for (const auto& argument : query.atom.arguments) scan_max_witness(argument, next_witness);
    return next_witness;
}

template <bool Trace>
struct ProofRow {
    Binding slots;
    explicit ProofRow(std::size_t width) : slots(width) {}
};

template <>
struct ProofRow<true> {
    Binding slots;
    std::vector<const Atom*> matched_facts;
    std::vector<Atom> builtin_conditions;
    explicit ProofRow(std::size_t width) : slots(width) {}
};

template <bool Trace>
inline bool prove_impl(Program& program, const std::vector<Rule>& rules, FactIndex& facts,
                       std::vector<bool>& proven, bool early_stop_on_queries,
                       ProvenanceMap* provenance) {
    struct RuleState {
        std::vector<const FactIndex::Relation*> relations;
        std::vector<std::size_t> sizes;
        std::set<Binding, BindingLess> fired;
        bool initialized = false;
    };
    std::vector<RuleState> states(rules.size());
    for (std::size_t i = 0; i < rules.size(); ++i)
        for (const auto& premise : rules[i].premises) {
            states[i].relations.push_back(is_builtin(premise.predicate) ? nullptr : &facts.relation(premise));
            states[i].sizes.push_back(0);
        }
    if constexpr (Trace) {
        if (provenance) {
            for (const auto& fact : program.facts) {
                ProvenanceRecord record;
                record.origin = ProofOrigin::Given;
                const auto loc = program.fact_locations.find(fact);
                if (loc != program.fact_locations.end()) record.location = format_location(loc->second);
                provenance->emplace(&fact, std::move(record));
            }
        }
    }
    std::size_t next_witness = initial_next_witness(program);
    for (std::size_t round = 0; round < max_rounds; ++round) {
        if (early_stop_on_queries && !program.queries.empty()) {
            bool complete = true;
            for (std::size_t i = 0; i < program.queries.size(); ++i) {
                if (!proven[i]) proven[i] = is_proven(program.queries[i], program, facts);
                if (!proven[i]) { complete = false; break; }
            }
            if (complete) return false;
        }
        bool changed = false;
        for (std::size_t index = 0; index < rules.size(); ++index) {
            const auto& rule = rules[index];
            auto& state = states[index];
            bool dirty = !state.initialized;
            for (std::size_t i = 0; i < state.relations.size(); ++i)
                if (state.relations[i] && state.sizes[i] != state.relations[i]->rows.size()) dirty = true;
            if (!dirty) continue;
            state.initialized = true;
            // Capture versions before deriving conclusions, including recursive
            // rules whose own output will make an input relation grow.
            for (std::size_t i = 0; i < state.relations.size(); ++i)
                if (state.relations[i]) state.sizes[i] = state.relations[i]->rows.size();
            std::vector<ProofRow<Trace>> bindings(1, ProofRow<Trace>(rule.variables.size()));
            for (std::size_t p = 0; p < rule.premises.size(); ++p) {
                const auto& premise = rule.premises[p];
                const auto& pattern = rule.patterns[p];
                std::vector<ProofRow<Trace>> next;
                next.reserve(bindings.size());
                if (is_builtin(premise.predicate)) {
                    for (auto& row : bindings) {
                        std::optional<Term> left, right;
                        try {
                            const Term& lhs = comparison_term(premise, pattern, 0, row.slots, left);
                            const Term& rhs = comparison_term(premise, pattern, 1, row.slots, right);
                            if (compare(premise.predicate, lhs, rhs)) {
                                if constexpr (Trace)
                                    row.builtin_conditions.push_back(Atom{premise.predicate, {lhs, rhs}});
                                next.push_back(std::move(row));
                            }
                        } catch (const ProofLimit&) { return false; }
                    }
                } else {
                    for (auto& row : bindings) {
                        for (const auto* fact : facts.candidates(premise, pattern, row.slots, *state.relations[p])) {
                            const bool matched = match(premise, pattern, *fact, row.slots);
                            if (matched) {
                                if (next.size() >= max_bindings) return false;
                                if (pattern.introduced.empty()) {
                                    // A fully bound atom has at most one matching
                                    // fact; guards can retain the existing row.
                                    if constexpr (Trace) row.matched_facts.push_back(fact);
                                    next.push_back(std::move(row));
                                    break;
                                }
                                ProofRow<Trace> copy = row;
                                if constexpr (Trace) copy.matched_facts.push_back(fact);
                                next.push_back(std::move(copy));
                            }
                            for (auto slot : pattern.introduced) row.slots[slot] = nullptr;
                        }
                    }
                }
                bindings = std::move(next);
                if (bindings.empty()) break;
            }
            // Joins finish before insertion, so relation/column row vectors are
            // never extended while a candidate range is being traversed.
            for (const auto& row : bindings) {
                const Binding* arguments = &row.slots;
                Binding values;
                std::vector<Term> witnesses;
                if (!rule.existentials.empty()) {
                    if (!state.fired.insert(row.slots).second) continue;
                    values = row.slots;
                    witnesses.reserve(rule.existentials.size());
                    for (std::size_t i = 0; i < rule.existentials.size(); ++i) {
                        witnesses.push_back(Witness::make(next_witness++));
                        values.push_back(&witnesses.back());
                    }
                    arguments = &values;
                }
                Atom conclusion{rule.conclusion.predicate, {}};
                conclusion.arguments.reserve(rule.conclusion.arguments.size());
                std::size_t budget = max_term_nodes;
                try {
                    for (std::size_t i = 0; i < rule.conclusion.arguments.size(); ++i) {
                        auto value = substitute(rule.conclusion.arguments[i], rule.result,
                                                rule.result.arguments[i], *arguments, budget);
                        if (depth(value) > max_depth) return false;
                        conclusion.arguments.push_back(std::move(value));
                    }
                } catch (const ProofLimit&) { return false; }
                const auto position = program.facts.lower_bound(conclusion);
                if (position == program.facts.end() || conclusion < *position) {
                    if (program.facts.size() >= max_facts) return false;
                    const Atom* inserted = &*program.facts.insert(position, std::move(conclusion));
                    facts.add(*inserted);
                    if constexpr (Trace) {
                        if (provenance) {
                            ProvenanceRecord record;
                            record.origin = ProofOrigin::Rule;
                            record.rule_index = index;
                            record.location = format_location(rule.line);
                            record.rule_text = rule.description;
                            record.premises = row.matched_facts;
                            record.builtin_conditions = row.builtin_conditions;
                            std::size_t w = 0;
                            for (const auto& var : rule.existentials)
                                record.witnesses.emplace(var, witnesses[w++]);
                            provenance->emplace(inserted, std::move(record));
                        }
                    }
                    changed = true;
                }
            }
        }
        if (!changed) return true;
    }
    return false;
}

inline Result evaluate(Program& program, const std::vector<Rule>& rules, bool explain_success = true) {
    FactIndex facts;
    for (const auto& rule : rules)
        for (std::size_t p = 0; p < rule.premises.size(); ++p)
            if (!is_builtin(rule.premises[p].predicate)) facts.require(rule.premises[p], rule.patterns[p]);
    for (const auto& query : program.queries)
        if (!query.variables.empty()) facts.require(query.atom, query.pattern);
    for (const auto& fact : program.facts) facts.add(fact);
    std::vector<bool> proven(program.queries.size());
    const bool saturated = prove_impl<false>(program, rules, facts, proven, !program.queries.empty(), nullptr);
    bool failed = false, unknown = false;
    std::ostringstream output;
    for (std::size_t i = 0; i < program.queries.size(); ++i) {
        const auto& query = program.queries[i];
        const bool found = proven[i] || is_proven(query, program, facts);
        const bool incomplete = !is_builtin(query.atom.predicate) && !saturated;
        if (!found || explain_success)
            output << "verify " << query.name << (found ? " PASS\n" : incomplete ? " UNKNOWN\n" : " FAIL\n");
        unknown = unknown || (!found && incomplete);
        failed = failed || (!found && !incomplete);
    }
    std::string error;
    if (unknown) error = "rin: inference limit reached; unproved queries are UNKNOWN\n";
    return {unknown ? 3 : failed ? 1 : 0, output.str(), error};
}

inline std::vector<WitnessMatch> match_solution_query(const Solution& solution, const Query& query,
                                                      bool all_matches, bool include_trace) {
    std::vector<WitnessMatch> matches;
    if (is_builtin(query.atom.predicate)) {
        if (compare(query.atom.predicate, query.atom.arguments[0], query.atom.arguments[1])) {
            WitnessMatch wm;
            wm.fact = query.atom;
            if (include_trace && !solution.derivations_.empty()) {
                wm.trace.steps.push_back(ProofStep{0, query.atom, ProofOrigin::Builtin, std::nullopt, {}, {}, {}, {}, {}});
            }
            matches.push_back(std::move(wm));
        }
        return matches;
    }
    if (query.variables.empty()) {
        const auto it = std::lower_bound(solution.facts.begin(), solution.facts.end(), query.atom);
        if (it != solution.facts.end() && *it == query.atom) {
            const auto idx = static_cast<std::size_t>(it - solution.facts.begin());
            WitnessMatch wm;
            wm.fact = *it;
            if (include_trace && !solution.derivations_.empty())
                wm.trace = solution.build_trace_for_index(idx);
            matches.push_back(std::move(wm));
        }
        return matches;
    }
    const Fact probe{query.atom.predicate, {}};
    auto it = std::lower_bound(solution.facts.begin(), solution.facts.end(), probe);
    Binding binding(query.variables.size());
    for (; it != solution.facts.end() && it->predicate == query.atom.predicate; ++it) {
        if (it->arguments.size() != query.atom.arguments.size()) continue;
        if (match(query.atom, query.pattern, *it, binding)) {
            const auto idx = static_cast<std::size_t>(it - solution.facts.begin());
            WitnessMatch wm;
            wm.fact = *it;
            std::size_t slot = 0;
            for (const auto& var : query.variables) {
                if (binding[slot]) wm.bindings.emplace(var, *binding[slot]);
                ++slot;
            }
            if (include_trace && !solution.derivations_.empty())
                wm.trace = solution.build_trace_for_index(idx);
            matches.push_back(std::move(wm));
            std::fill(binding.begin(), binding.end(), nullptr);
            if (!all_matches) break;
        } else {
            std::fill(binding.begin(), binding.end(), nullptr);
        }
    }
    return matches;
}

inline Solution evaluate_solution(Program& program, const std::vector<Rule>& rules, const SolveOptions& options) {
    FactIndex facts;
    for (const auto& rule : rules)
        for (std::size_t p = 0; p < rule.premises.size(); ++p)
            if (!is_builtin(rule.premises[p].predicate)) facts.require(rule.premises[p], rule.patterns[p]);
    if (!options.saturate) {
        for (const auto& query : program.queries)
            if (!query.variables.empty()) facts.require(query.atom, query.pattern);
    }
    for (const auto& fact : program.facts) facts.add(fact);

    std::vector<bool> proven(program.queries.size());
    ProvenanceMap provenance;
    const bool early_stop = !options.saturate && !program.queries.empty();
    const bool saturated = options.trace
        ? prove_impl<true>(program, rules, facts, proven, early_stop, &provenance)
        : prove_impl<false>(program, rules, facts, proven, early_stop, nullptr);

    Solution sol;
    sol.saturated = saturated;
    sol.facts.reserve(program.facts.size());
    std::map<const Atom*, std::size_t> atom_to_index;
    for (const auto& fact : program.facts) {
        if (options.trace) atom_to_index.emplace(&fact, sol.facts.size());
        sol.facts.push_back(fact);
    }
    if (options.trace) {
        sol.derivations_.resize(sol.facts.size());
        std::size_t idx = 0;
        for (const auto& fact : program.facts) {
            const auto found = provenance.find(&fact);
            if (found != provenance.end()) {
                auto& dst = sol.derivations_[idx];
                dst.origin = found->second.origin;
                dst.rule_index = found->second.rule_index;
                dst.location = found->second.location;
                dst.rule_text = found->second.rule_text;
                dst.builtin_conditions = found->second.builtin_conditions;
                dst.witnesses = found->second.witnesses;
                dst.premise_indices.reserve(found->second.premises.size());
                for (const Atom* premise : found->second.premises) {
                    dst.premise_indices.push_back(atom_to_index.at(premise));
                }
            }
            ++idx;
        }
    }

    bool failed = false, unknown = false;
    std::ostringstream explanation;
    sol.queries.reserve(program.queries.size());
    for (const auto& query : program.queries) {
        QueryReport report;
        report.name = query.name;
        report.pattern = query.atom;
        report.variables.assign(query.variables.begin(), query.variables.end());
        report.matches = match_solution_query(sol, query, options.all_matches, options.trace);
        const bool found = !report.matches.empty();
        const bool incomplete = !is_builtin(query.atom.predicate) && !saturated;
        report.verdict = found ? Verdict::Pass : incomplete ? Verdict::Unknown : Verdict::Fail;
        unknown = unknown || (!found && incomplete);
        failed = failed || (!found && !incomplete);
        explanation << report.format();
        sol.queries.push_back(std::move(report));
    }
    if (program.queries.empty()) {
        sol.verdict = saturated ? Verdict::Pass : Verdict::Unknown;
        if (!saturated) explanation << "rin: inference limit reached; saturation is incomplete\n";
    } else {
        sol.verdict = unknown ? Verdict::Unknown : failed ? Verdict::Fail : Verdict::Pass;
        if (unknown) explanation << "rin: inference limit reached; unproved queries are UNKNOWN\n";
    }
    sol.explanation = explanation.str();
    return sol;
}

} // namespace detail

inline ProofTrace Solution::build_trace_for_index(std::size_t root_index) const {
    ProofTrace trace;
    if (root_index >= facts.size() || derivations_.empty()) return trace;
    std::map<std::size_t, std::size_t> visited;
    auto visit = [&](std::size_t index, const auto& self) -> std::size_t {
        const auto existing = visited.find(index);
        if (existing != visited.end()) return existing->second;
        const auto& deriv = derivations_[index];
        ProofStep step;
        step.fact = facts[index];
        step.origin = deriv.origin;
        if (deriv.origin == ProofOrigin::Given) {
            step.rule_location = deriv.location;
        } else if (deriv.origin == ProofOrigin::Rule) {
            step.rule_index = deriv.rule_index;
            step.rule_location = deriv.location;
            step.rule_text = deriv.rule_text;
            step.builtin_conditions = deriv.builtin_conditions;
            step.witnesses = deriv.witnesses;
            step.premise_steps.reserve(deriv.premise_indices.size());
            for (const auto p_idx : deriv.premise_indices)
                step.premise_steps.push_back(self(p_idx, self));
        }
        step.id = trace.steps.size();
        trace.steps.push_back(std::move(step));
        visited.emplace(index, trace.steps.back().id);
        return trace.steps.back().id;
    };
    visit(root_index, visit);
    return trace;
}

inline bool Solution::is_derived(const Fact& fact) const {
    const auto it = std::lower_bound(facts.begin(), facts.end(), fact);
    if (it == facts.end() || !(*it == fact) || derivations_.empty()) return false;
    return derivations_[static_cast<std::size_t>(it - facts.begin())].origin == ProofOrigin::Rule;
}

inline std::optional<ProofTrace> Solution::explain(const Fact& fact) const {
    const auto it = std::lower_bound(facts.begin(), facts.end(), fact);
    if (it == facts.end() || !(*it == fact) || derivations_.empty()) return std::nullopt;
    return build_trace_for_index(static_cast<std::size_t>(it - facts.begin()));
}

inline std::vector<Fact> Solution::query_predicate(std::string_view predicate) const {
    std::vector<Fact> result;
    const Fact probe{std::string(predicate), {}};
    for (auto it = std::lower_bound(facts.begin(), facts.end(), probe);
         it != facts.end() && it->predicate == predicate; ++it) {
        result.push_back(*it);
    }
    return result;
}

inline std::vector<Fact> Solution::query_predicate(std::string_view predicate, std::size_t arity) const {
    std::vector<Fact> result;
    const Fact probe{std::string(predicate), {}};
    for (auto it = std::lower_bound(facts.begin(), facts.end(), probe);
         it != facts.end() && it->predicate == predicate; ++it) {
        if (it->arguments.size() == arity) result.push_back(*it);
    }
    return result;
}

inline std::vector<Fact> Solution::derived_facts() const {
    std::vector<Fact> result;
    if (derivations_.empty()) return result;
    for (std::size_t i = 0; i < facts.size(); ++i) {
        if (derivations_[i].origin == ProofOrigin::Rule) result.push_back(facts[i]);
    }
    return result;
}

inline std::vector<Fact> Solution::derived_facts(std::string_view predicate) const {
    std::vector<Fact> result;
    if (derivations_.empty()) return result;
    const Fact probe{std::string(predicate), {}};
    for (auto it = std::lower_bound(facts.begin(), facts.end(), probe);
         it != facts.end() && it->predicate == predicate; ++it) {
        if (derivations_[static_cast<std::size_t>(it - facts.begin())].origin == ProofOrigin::Rule)
            result.push_back(*it);
    }
    return result;
}

inline std::vector<Fact> Solution::derived_facts(std::string_view predicate, std::size_t arity) const {
    std::vector<Fact> result;
    if (derivations_.empty()) return result;
    const Fact probe{std::string(predicate), {}};
    for (auto it = std::lower_bound(facts.begin(), facts.end(), probe);
         it != facts.end() && it->predicate == predicate; ++it) {
        if (it->arguments.size() == arity &&
            derivations_[static_cast<std::size_t>(it - facts.begin())].origin == ProofOrigin::Rule)
            result.push_back(*it);
    }
    return result;
}

inline std::vector<WitnessMatch> Solution::query_bindings(std::string_view predicate,
                                                          const std::vector<std::string>& variable_names) const {
    Fact pattern{std::string(predicate), {}};
    pattern.arguments.reserve(variable_names.size());
    for (const auto& name : variable_names) pattern.arguments.emplace_back(name);
    return query(pattern, variable_names);
}

inline std::vector<WitnessMatch> Solution::query(const Fact& pattern,
                                                 const std::vector<std::string>& variables) const {
    const auto compiled = detail::compile_query_spec(pattern, {}, variables);
    return detail::match_solution_query(*this, compiled, true, !derivations_.empty());
}

inline std::vector<WitnessMatch> Solution::query(std::string_view rin_query) const {
    const auto compiled = detail::parse_single_query(rin_query);
    return detail::match_solution_query(*this, compiled, true, !derivations_.empty());
}

inline const QueryReport* Solution::find_query(std::string_view name) const {
    for (const auto& q : queries)
        if (q.name == name) return &q;
    return nullptr;
}

inline std::string Solution::format() const {
    if (verdict == Verdict::Error) return explanation;
    return explanation;
}

// Compile fixed rules once. Each check/solve owns its facts and derived state.
class Program {
    detail::Program rules_;

public:
    explicit Program(std::string_view rules)
        : rules_(detail::parse(detail::load_source(rules, nullptr))) {}

    Decision check(const std::vector<Fact>& facts,
                   const std::vector<Obligation>& obligations) const {
        try {
            detail::Program program{rules_.facts, {}, rules_.queries};
            for (const auto& fact : facts) {
                if (detail::is_builtin(fact.predicate))
                    throw std::runtime_error(fact.predicate + " cannot be asserted as a fact");
                program.facts.insert(fact);
            }
            for (const auto& obligation : obligations) {
                program.queries.push_back(detail::compile_query_spec(
                    obligation.atom, obligation.name, obligation.variables));
            }
            return detail::decision(detail::evaluate(program, rules_.rules, false));
        } catch (const std::exception& error) {
            return {Verdict::Error, error.what()};
        }
    }

    Solution solve(const std::vector<Fact>& facts,
                   const std::vector<Obligation>& obligations = {},
                   const SolveOptions& options = {}) const {
        try {
            detail::Program program{rules_.facts, {}, rules_.queries, rules_.fact_locations};
            for (const auto& fact : facts) {
                if (detail::is_builtin(fact.predicate))
                    throw std::runtime_error(fact.predicate + " cannot be asserted as a fact");
                program.facts.insert(fact);
            }
            for (const auto& obligation : obligations) {
                program.queries.push_back(detail::compile_query_spec(
                    obligation.atom, obligation.name, obligation.variables));
            }
            return detail::evaluate_solution(program, rules_.rules, options);
        } catch (const std::exception& error) {
            return Solution{Verdict::Error, false, error.what(), {}, {}, {}};
        }
    }
};

inline Decision run(std::string_view source) {
    try {
        auto program = detail::parse(detail::load_source(source, nullptr));
        return detail::decision(detail::evaluate(program, program.rules));
    } catch (const std::exception& error) {
        return {Verdict::Error, error.what()};
    }
}

inline Solution solve(std::string_view source, const SolveOptions& options = {}) {
    try {
        auto program = detail::parse(detail::load_source(source, nullptr));
        return detail::evaluate_solution(program, program.rules, options);
    } catch (const std::exception& error) {
        return Solution{Verdict::Error, false, error.what(), {}, {}, {}};
    }
}

} // namespace rin::pass
#endif
