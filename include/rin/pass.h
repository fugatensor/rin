#ifndef RIN_PASS_H
#define RIN_PASS_H

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

namespace rin { inline constexpr char version[] = "rin 0.3.0"; }

namespace rin::pass {

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
            program.facts.insert(std::move(fact));
        }
        ++i;
    }
    return program;
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
    return predicate == "less" ? *left.number < *right.number : *left.number <= *right.number;
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
    Binding binding(query.variables.size());
    for (const auto* fact : facts.candidates(query.atom, query.pattern, binding, facts.relation(query.atom))) {
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

inline bool prove(Program& program, const std::vector<Rule>& rules, FactIndex& facts,
                  std::vector<bool>& proven) {
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
    std::size_t next_witness = 0;
    for (std::size_t round = 0; round < max_rounds; ++round) {
        bool complete = true;
        for (std::size_t i = 0; i < program.queries.size(); ++i) {
            if (!proven[i]) proven[i] = is_proven(program.queries[i], program, facts);
            if (!proven[i]) { complete = false; break; }
        }
        if (complete) return false;
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
            std::vector<Binding> bindings(1, Binding(rule.variables.size()));
            for (std::size_t p = 0; p < rule.premises.size(); ++p) {
                const auto& premise = rule.premises[p];
                const auto& pattern = rule.patterns[p];
                std::vector<Binding> next;
                next.reserve(bindings.size());
                if (is_builtin(premise.predicate)) {
                    for (auto& binding : bindings) {
                        std::optional<Term> left, right;
                        try {
                            if (compare(premise.predicate,
                                    comparison_term(premise, pattern, 0, binding, left),
                                    comparison_term(premise, pattern, 1, binding, right)))
                                next.push_back(std::move(binding));
                        } catch (const ProofLimit&) { return false; }
                    }
                } else {
                    for (auto& binding : bindings) {
                        for (const auto* fact : facts.candidates(premise, pattern, binding, *state.relations[p])) {
                            const bool matched = match(premise, pattern, *fact, binding);
                            if (matched) {
                                if (next.size() >= max_bindings) return false;
                                if (pattern.introduced.empty()) {
                                    // A fully bound atom has at most one matching
                                    // fact; guards can retain the existing row.
                                    next.push_back(std::move(binding));
                                    break;
                                }
                                next.push_back(binding);
                            }
                            for (auto slot : pattern.introduced) binding[slot] = nullptr;
                        }
                    }
                }
                bindings = std::move(next);
                if (bindings.empty()) break;
            }
            // Joins finish before insertion, so relation/column row vectors are
            // never extended while a candidate range is being traversed.
            for (const auto& binding : bindings) {
                const Binding* arguments = &binding;
                Binding values;
                std::vector<Term> witnesses;
                if (!rule.existentials.empty()) {
                    if (!state.fired.insert(binding).second) continue;
                    values = binding;
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
                    facts.add(*program.facts.insert(position, std::move(conclusion)));
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
    const bool saturated = prove(program, rules, facts, proven);
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

} // namespace rin::pass
#endif
