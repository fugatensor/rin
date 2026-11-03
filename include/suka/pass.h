#ifndef SUKA_PASS_H
#define SUKA_PASS_H

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace suka { inline constexpr char version[] = "suka 0.3.0"; }

namespace suka::pass {

enum class Verdict { Pass, Fail, Unknown, Error };

struct Decision {
    Verdict verdict = Verdict::Error;
    std::string explanation;
};

namespace detail { struct Witness; }

struct Term {
private:
    std::optional<std::size_t> witness_;
    friend struct detail::Witness;

public:
    std::string name;
    std::vector<Term> arguments;
    std::optional<std::uint64_t> number;

    Term(std::string symbol, std::vector<Term> children = {})
        : name(std::move(symbol)), arguments(std::move(children)) {}
    Term(std::uint64_t value) : number(value) {}

    bool anonymous() const { return witness_.has_value(); }
    bool same_head(const Term& other) const {
        return witness_ == other.witness_ && number == other.number && name == other.name;
    }

    int compare(const Term& other) const {
        if (witness_ != other.witness_)
            return witness_ < other.witness_ ? -1 : 1;
        if (number != other.number)
            return number < other.number ? -1 : 1;
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
};

struct Fact {
    std::string predicate;
    std::vector<Term> arguments;
    bool operator<(const Fact& other) const {
        return std::tie(predicate, arguments) < std::tie(other.predicate, other.arguments);
    }
};

struct Obligation {
    Fact atom;
    std::string name;
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

using Atom = Fact;

inline bool is_builtin(const std::string& predicate) {
    return predicate == "different" || predicate == "less" || predicate == "less_equal";
}

using Variables = std::set<std::string>;
using Binding = std::map<std::string, Term>;

struct Rule {
    std::vector<Atom> premises;
    Atom conclusion;
    Variables variables, existentials;
    Location line;
};
struct Query {
    Atom atom;
    std::string name;
    Variables variables;
};
struct Program {
    std::set<Atom> facts;
    std::vector<Rule> rules;
    std::vector<Query> queries;
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
    std::string variable() {
        auto value = name();
        if (std::all_of(value.begin(), value.end(),
                [](unsigned char ch) { return ch >= '0' && ch <= '9'; }))
            fail(line.location, "a variable must have a symbolic name");
        return value;
    }
    void finish() const {
        if (!done()) fail(line.location, "unexpected '" + peek() + "'");
    }
    Term term(std::size_t depth = 0) {
        if (depth >= max_depth) fail(line.location, "term nesting limit exceeded");
        Term result{name(), {}};
        if (take("(") && !take(")")) {
            do { result.arguments.push_back(term(depth + 1)); } while (take(","));
            expect(")");
        }
        if (result.arguments.empty() &&
            std::all_of(result.name.begin(), result.name.end(),
                [](unsigned char ch) { return ch >= '0' && ch <= '9'; })) {
            std::uint64_t value;
            const auto converted = std::from_chars(result.name.data(), result.name.data() + result.name.size(), value);
            if (converted.ec != std::errc{}) fail(line.location, "integer literal exceeds uint64");
            return Term(value);
        }
        return result;
    }
    Atom atom() {
        const bool call = peek(1) == "(";
        auto value = term();
        if (take(":")) return {name(), {std::move(value)}};
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
            fail(line.location, "expected: import MODULE (without quotes or .suka suffix)");
        const auto& name = line.tokens[1];
        if (name.find_first_of("\"'") != std::string::npos)
            fail(line.location, "remove quotes from import module names; write: import facts");
        if (name.size() >= 5 && name.compare(name.size() - 5, 5, ".suka") == 0)
            fail(line.location, "omit the .suka suffix in imports; write: import facts");
        if (!is_identifier(name) || name.front() == '.' ||
            name.find_first_of("/\\:") != std::string::npos ||
            std::any_of(name.begin(), name.end(), [](unsigned char ch) { return ch <= 32 || ch == 127; }))
            fail(line.location, "imports require a bare module name in the same directory");
        const auto relative = std::filesystem::u8path(name + ".suka");
        const auto target = std::filesystem::weakly_canonical(directory / relative);
        if (target.parent_path() != std::filesystem::weakly_canonical(directory))
            fail(line.location, "imports must stay in the same directory");
        if (imports.active.count(target)) fail(line.location, "cyclic import: " + name);
        if (!std::filesystem::is_regular_file(target)) fail(line.location, "cannot import module: " + name + " (" + name + ".suka)");
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

inline bool is_variable(const Term& term, const Variables& variables) {
    return !term.number && term.arguments.empty() && variables.count(term.name);
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
                const auto type = cursor.name();
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
                rule.premises.insert(rule.premises.end(), guards.begin(), guards.end());
                // A structural comparison needs every universal variable bound.
                // Relational premises and type guards establish those bindings.
                std::stable_partition(rule.premises.begin(), rule.premises.end(),
                    [](const Atom& premise) { return !is_builtin(premise.predicate); });
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
            program.queries.push_back(std::move(query));
        } else {
            auto fact = cursor.atom();
            cursor.finish();
            if (is_builtin(fact.predicate))
                fail(line.location, fact.predicate + " cannot be asserted as a fact");
            program.facts.insert(std::move(fact));
        }
        ++i;
    }
    return program;
}

inline bool match_term(const Term& pattern, const Term& value, const Variables& variables, Binding& binding) {
    if (is_variable(pattern, variables)) {
        const auto inserted = binding.emplace(pattern.name, value);
        return inserted.second || inserted.first->second == value;
    }
    if (!pattern.same_head(value) || pattern.arguments.size() != value.arguments.size()) return false;
    for (std::size_t i = 0; i < pattern.arguments.size(); ++i)
        if (!match_term(pattern.arguments[i], value.arguments[i], variables, binding)) return false;
    return true;
}

inline bool match(const Atom& pattern, const Atom& fact, const Variables& variables, Binding& binding) {
    if (pattern.predicate != fact.predicate || pattern.arguments.size() != fact.arguments.size()) return false;
    for (std::size_t i = 0; i < pattern.arguments.size(); ++i)
        if (!match_term(pattern.arguments[i], fact.arguments[i], variables, binding)) return false;
    return true;
}

struct ProofLimit {};

inline std::size_t node_count(const Term& term, std::size_t limit) {
    std::size_t count = 1;
    if (!limit) return count;
    for (const auto& argument : term.arguments) {
        count += node_count(argument, limit - count);
        if (count > limit) return limit + 1;
    }
    return count;
}

inline Term substitute(const Term& term, const Binding& binding, std::size_t& budget) {
    if (term.number || term.anonymous()) {
        if (!budget) throw ProofLimit{};
        --budget;
        return term;
    }
    const auto value = binding.find(term.name);
    if (term.arguments.empty() && value != binding.end()) {
        const auto count = node_count(value->second, budget);
        if (count > budget) throw ProofLimit{};
        budget -= count;
        return value->second;
    }
    if (!budget) throw ProofLimit{};
    --budget;
    Term result{term.name, {}};
    for (const auto& argument : term.arguments) result.arguments.push_back(substitute(argument, binding, budget));
    return result;
}

inline std::size_t depth(const Term& term) {
    std::size_t result = 1;
    for (const auto& argument : term.arguments) result = std::max(result, 1 + depth(argument));
    return result;
}

inline bool compare(const std::string& predicate, const Term& left, const Term& right) {
    if (predicate == "different") return !(left == right);
    if (!left.number || !right.number) return false;
    return predicate == "less" ? *left.number < *right.number : *left.number <= *right.number;
}

inline bool is_proven(const Query& query, const Program& program);

// Stable fact nodes let each relation and argument value keep a compact row index.
class FactIndex {
    using Rows = std::vector<const Atom*>;
    struct Relation {
        Rows rows;
        std::vector<std::map<Term, Rows>> columns;
    };
    std::map<std::pair<std::string, std::size_t>, Relation> relations_;

    static const Term* bound(const Term& term, const Variables& variables, const Binding& binding) {
        if (is_variable(term, variables)) {
            const auto value = binding.find(term.name);
            return value == binding.end() ? nullptr : &value->second;
        }
        for (const auto& argument : term.arguments)
            // A constructed pattern with variables still uses ordinary matching.
            if (bound(argument, variables, {}) == nullptr) return nullptr;
        return &term;
    }

public:
    void add(const Atom& fact) {
        auto& relation = relations_[{fact.predicate, fact.arguments.size()}];
        relation.rows.push_back(&fact);
        relation.columns.resize(fact.arguments.size());
        for (std::size_t i = 0; i < fact.arguments.size(); ++i)
            relation.columns[i][fact.arguments[i]].push_back(&fact);
    }

    const Rows& candidates(const Atom& pattern, const Variables& variables, const Binding& binding) const {
        static const Rows empty;
        const auto found = relations_.find({pattern.predicate, pattern.arguments.size()});
        if (found == relations_.end()) return empty;
        const auto& relation = found->second;
        const Rows* rows = &relation.rows;
        for (std::size_t i = 0; i < pattern.arguments.size(); ++i) {
            const auto* value = bound(pattern.arguments[i], variables, binding);
            if (!value) continue;
            const auto column = relation.columns[i].find(*value);
            if (column == relation.columns[i].end()) return empty;
            if (column->second.size() < rows->size()) rows = &column->second;
        }
        return *rows;
    }
};

inline bool prove(Program& program, const std::vector<Rule>& rules) {
    FactIndex facts;
    for (const auto& fact : program.facts) facts.add(fact);
    std::vector<std::set<Binding>> fired(rules.size());
    std::size_t next_witness = 0;
    for (std::size_t round = 0; round < max_rounds; ++round) {
        // All requested proofs are already established; no need to enumerate successors.
        if (std::all_of(program.queries.begin(), program.queries.end(),
                        [&](const Query& query) { return is_proven(query, program); })) return false;
        bool changed = false;
        for (std::size_t index = 0; index < rules.size(); ++index) {
            const auto& rule = rules[index];
            std::vector<Binding> bindings(1);
            for (const auto& premise : rule.premises) {
                std::vector<Binding> next;
                if (is_builtin(premise.predicate)) {
                    for (auto& binding : bindings) {
                        if (binding.size() != rule.variables.size()) return false;
                        std::size_t left_budget = max_term_nodes, right_budget = max_term_nodes;
                        try {
                            if (compare(premise.predicate,
                                    substitute(premise.arguments[0], binding, left_budget),
                                    substitute(premise.arguments[1], binding, right_budget)))
                                next.push_back(std::move(binding));
                        } catch (const ProofLimit&) { return false; }
                    }
                    bindings = std::move(next);
                    if (bindings.empty()) break;
                    continue;
                }
                for (const auto& binding : bindings) {
                    for (const auto* fact : facts.candidates(premise, rule.variables, binding)) {
                        auto candidate = binding;
                        if (match(premise, *fact, rule.variables, candidate)) {
                            if (next.size() >= max_bindings) return false;
                            next.push_back(std::move(candidate));
                        }
                    }
                }
                bindings = std::move(next);
                if (bindings.empty()) break;
            }
            for (const auto& binding : bindings) {
                auto values = binding;
                if (!rule.existentials.empty()) {
                    if (!fired[index].insert(binding).second) continue;
                    for (const auto& variable : rule.existentials)
                        values.emplace(variable, Witness::make(next_witness++));
                }
                Atom conclusion{rule.conclusion.predicate, {}};
                std::size_t budget = max_term_nodes;
                try {
                    for (const auto& term : rule.conclusion.arguments) {
                        auto value = substitute(term, values, budget);
                        if (depth(value) > max_depth) return false;
                        conclusion.arguments.push_back(std::move(value));
                    }
                } catch (const ProofLimit&) { return false; }
                if (!program.facts.count(conclusion)) {
                    if (program.facts.size() >= max_facts) return false;
                    facts.add(*program.facts.insert(std::move(conclusion)).first);
                    changed = true;
                }
            }
        }
        if (!changed) return true;
    }
    return false;
}

inline bool is_proven(const Query& query, const Program& program) {
    if (is_builtin(query.atom.predicate))
        return compare(query.atom.predicate, query.atom.arguments[0], query.atom.arguments[1]);
    if (query.variables.empty()) return program.facts.count(query.atom) != 0;
    for (const auto& fact : program.facts) {
        Binding binding;
        if (match(query.atom, fact, query.variables, binding)) return true;
    }
    return false;
}

inline Result evaluate(Program& program, const std::vector<Rule>& rules, bool explain_success = true) {
    const bool saturated = prove(program, rules);
    bool failed = false, unknown = false;
    std::ostringstream output;
    for (const auto& query : program.queries) {
        const bool found = is_proven(query, program);
        const bool incomplete = !is_builtin(query.atom.predicate) && !saturated;
        if (!found || explain_success)
            output << "verify " << query.name << (found ? " PASS\n" : incomplete ? " UNKNOWN\n" : " FAIL\n");
        unknown = unknown || (!found && incomplete);
        failed = failed || (!found && !incomplete);
    }
    std::string error;
    if (unknown) error = "suka: inference limit reached; unproved queries are UNKNOWN\n";
    return {unknown ? 3 : failed ? 1 : 0, output.str(), error};
}

} // namespace detail

// Compile fixed rules once. Each check owns its facts and derived state.
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
                if (detail::is_builtin(obligation.atom.predicate) && obligation.atom.arguments.size() != 2)
                    throw std::runtime_error(obligation.atom.predicate + " requires exactly two ground terms");
                program.queries.push_back({obligation.atom, obligation.name, {}});
            }
            return detail::decision(detail::evaluate(program, rules_.rules, false));
        } catch (const std::exception& error) {
            return {Verdict::Error, error.what()};
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

} // namespace suka::pass
#endif
