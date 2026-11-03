#include "suka.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

struct suka_result {
    int status;
    std::string output, error;
};

namespace {

constexpr std::size_t max_depth = 128;
constexpr std::size_t max_rounds = 256, max_facts = 10000, max_bindings = 10000;
constexpr std::size_t max_term_nodes = 10000;

struct Location {
    std::string path;
    std::size_t number;
};

struct Term {
    std::string name;
    std::vector<Term> arguments;
    int compare(const Term& other) const {
        const int order = name.compare(other.name);
        if (order) return order;
        for (std::size_t i = 0; i < std::min(arguments.size(), other.arguments.size()); ++i) {
            const int child = arguments[i].compare(other.arguments[i]);
            if (child) return child;
        }
        return arguments.size() < other.arguments.size() ? -1 : arguments.size() > other.arguments.size() ? 1 : 0;
    }
    bool operator<(const Term& other) const {
        return compare(other) < 0;
    }
    bool operator==(const Term& other) const {
        return compare(other) == 0;
    }
};

struct Atom {
    std::string predicate;
    std::vector<Term> arguments;
    bool operator<(const Atom& other) const {
        return std::tie(predicate, arguments) < std::tie(other.predicate, other.arguments);
    }
};

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
    std::string name, exists_type;
    Variables variables;
};
struct Constructor {
    std::string name;
    std::vector<std::string> parameters;
};
struct Program {
    std::set<Atom> facts;
    std::vector<Rule> rules;
    std::vector<Query> queries;
    std::map<std::string, std::vector<Constructor>> types;
    std::map<std::string, Location> type_lines;
    std::map<std::string, Term> inhabitants;
    std::set<Term> terms;
    std::set<std::string> partial_predicates;
};
struct Line {
    Location location;
    std::size_t indent;
    std::vector<std::string> tokens;
};

[[noreturn]] void fail(const Location& line, const std::string& message) {
    throw std::runtime_error(line.path + ":" + std::to_string(line.number) + ": " + message);
}

bool is_identifier(const std::string& token) {
    return !token.empty() && token != "->" && token != "=>" &&
           token.find_first_of(":,=()|\"") == std::string::npos;
}

std::vector<Line> read_lines(std::istream& input, const std::string& path) {
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
            } else if (std::string(":,=()|").find(text[pos]) != std::string::npos) {
                line.tokens.push_back(text.substr(pos++, 1));
            } else {
                const auto start = pos;
                while (pos < text.size() &&
                       !std::isspace(static_cast<unsigned char>(text[pos])) &&
                       text[pos] != '#' &&
                       std::string(":,=()|\"").find(text[pos]) == std::string::npos &&
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
        return result;
    }
    Atom atom() {
        const bool call = peek(1) == "(";
        auto value = term();
        if (take(":")) return {name(), {std::move(value)}};
        if (!call) fail(line.location, "expected OBJECT : TYPE or RELATION(arguments)");
        return {std::move(value.name), std::move(value.arguments)};
    }
    Atom exists(Variables& variables) {
        expect("exists");
        do {
            const auto variable = name();
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

std::vector<Line> load_file(const std::filesystem::path& path, Imports& imports);

std::vector<Line> expand_imports(const std::vector<Line>& lines,
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

std::vector<Line> load_file(const std::filesystem::path& input, Imports& imports) {
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

std::vector<Line> load_source(const char* text, const char* base_directory) {
    if (!text) throw std::runtime_error("<source>:1: source must not be null");
    const auto directory = base_directory && *base_directory
        ? std::filesystem::absolute(std::filesystem::u8path(base_directory)) : std::filesystem::current_path();
    if (!std::filesystem::is_directory(directory))
        throw std::runtime_error("<source>:1: base directory does not exist");
    std::istringstream input(text);
    Imports imports;
    return expand_imports(read_lines(input, "<source>"), directory, imports);
}

bool is_variable(const Term& term, const Variables& variables) {
    return term.arguments.empty() && variables.count(term.name);
}

bool collect_terms(const Term& term, const Variables& variables, std::set<Term>& terms) {
    bool ground = !is_variable(term, variables);
    for (const auto& argument : term.arguments)
        if (!collect_terms(argument, variables, terms)) ground = false;
    if (ground) terms.insert(term);
    return ground;
}

bool has_type(const Term& term, const std::string& type, const Program& program) {
    const auto definition = program.types.find(type);
    if (definition == program.types.end()) return false;
    for (const auto& constructor : definition->second) {
        if (constructor.name != term.name || constructor.parameters.size() != term.arguments.size()) continue;
        bool valid = true;
        for (std::size_t i = 0; i < term.arguments.size(); ++i)
            if (!has_type(term.arguments[i], constructor.parameters[i], program)) valid = false;
        if (valid) return true;
    }
    return false;
}

void prepare_types(Program& program) {
    std::set<std::string> constructors;
    std::map<std::string, std::pair<std::size_t, std::size_t>> sizes;
    for (const auto& entry : program.types) {
        for (const auto& constructor : entry.second) {
            if (!constructors.insert(constructor.name).second)
                fail(program.type_lines.at(entry.first), "duplicate constructor '" + constructor.name + "'");
            for (const auto& parameter : constructor.parameters)
                if (!program.types.count(parameter))
                    fail(program.type_lines.at(entry.first), "undefined constructor parameter type '" + parameter + "'");
        }
    }
    // Find a finite example of each inhabited type without enumerating its domain.
    bool changed;
    do {
        changed = false;
        for (const auto& entry : program.types) {
            if (program.inhabitants.count(entry.first)) continue;
            for (const auto& constructor : entry.second) {
                Term value{constructor.name, {}};
                bool ready = true;
                std::size_t nodes = 1, height = 1;
                for (const auto& parameter : constructor.parameters) {
                    const auto example = program.inhabitants.find(parameter);
                    if (example == program.inhabitants.end()) { ready = false; break; }
                    const auto size = sizes.at(parameter);
                    if (nodes > max_term_nodes - size.first || size.second >= max_depth)
                        fail(program.type_lines.at(entry.first), "constructor example limit exceeded");
                    nodes += size.first;
                    height = std::max(height, size.second + 1);
                    value.arguments.push_back(example->second);
                }
                if (ready) {
                    program.inhabitants.emplace(entry.first, value);
                    sizes[entry.first] = {nodes, height};
                    collect_terms(value, {}, program.terms);
                    changed = true;
                    break;
                }
            }
        }
    } while (changed);
    // Every nullary alternative is a useful finite candidate, including e.g. true/false.
    for (const auto& entry : program.types)
        for (const auto& constructor : entry.second)
            if (constructor.parameters.empty()) program.terms.insert({constructor.name, {}});
}

Program parse(const std::vector<Line>& lines) {
    Program program;
    std::vector<std::pair<Atom, Location>> input_facts;
    for (std::size_t i = 0; i < lines.size();) {
        const auto& line = lines[i];
        Cursor cursor(line);
        if (line.indent) fail(line.location, "unexpected indentation");
        if (cursor.keyword("type") && cursor.take("type")) {
            const auto type = cursor.name();
            if (program.types.count(type)) fail(line.location, "duplicate type '" + type + "'");
            cursor.expect("=");
            std::vector<Constructor> constructors;
            do {
                Constructor constructor{cursor.name(), {}};
                if (cursor.take("(") && !cursor.take(")")) {
                    do { constructor.parameters.push_back(cursor.name()); } while (cursor.take(","));
                    cursor.expect(")");
                }
                constructors.push_back(std::move(constructor));
            } while (cursor.take("|"));
            cursor.finish();
            program.types.emplace(type, std::move(constructors));
            program.type_lines[type] = line.location;
        } else if (cursor.keyword("all") && cursor.take("all")) {
            Variables variables;
            std::vector<Atom> guards;
            do {
                std::vector<std::string> group;
                do {
                    const auto variable = cursor.name();
                    if (!variables.insert(variable).second) fail(line.location, "duplicate variable '" + variable + "'");
                    group.push_back(variable);
                } while (cursor.take(","));
                cursor.expect(":");
                const auto type = cursor.name();
                for (const auto& variable : group) guards.push_back({type, {{variable, {}}}});
            } while (cursor.take(","));
            cursor.expect("=>");
            auto add_rule = [&](Cursor& rule_cursor) {
                Rule rule{{}, {}, variables, {}, rule_cursor.line.location};
                if (!rule_cursor.keyword("exists")) {
                    auto first = rule_cursor.atom();
                    if (rule_cursor.take("->")) rule.premises.push_back(std::move(first));
                    else rule.conclusion = std::move(first);
                }
                if (rule.conclusion.predicate.empty()) {
                    if (rule_cursor.keyword("exists"))
                        rule.conclusion = rule_cursor.exists(rule.existentials);
                    else rule.conclusion = rule_cursor.atom();
                }
                rule_cursor.finish();
                for (const auto& variable : rule.existentials)
                    if (variables.count(variable)) fail(rule.line, "existential variable shadows '" + variable + "'");
                rule.premises.insert(rule.premises.end(), guards.begin(), guards.end());
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
            Query query{{}, label, {}, {}};
            if (query_cursor.peek() == "exists" && query_cursor.peek(2) == "->") {
                query_cursor.expect("exists");
                query.exists_type = query_cursor.name();
                query_cursor.expect("->");
                query.atom.predicate = query_cursor.name();
            } else if (query_cursor.keyword("exists")) {
                query.atom = query_cursor.exists(query.variables);
            } else query.atom = query_cursor.atom();
            query_cursor.finish();
            program.queries.push_back(std::move(query));
        } else {
            auto fact = cursor.atom();
            cursor.finish();
            input_facts.push_back({fact, line.location});
            program.facts.insert(std::move(fact));
        }
        ++i;
    }
    prepare_types(program);
    for (const auto& fact : input_facts) {
        if (program.types.count(fact.first.predicate) &&
            (fact.first.arguments.size() != 1 || !has_type(fact.first.arguments[0], fact.first.predicate, program)))
            fail(fact.second, "fact does not match the declared constructor type '" + fact.first.predicate + "'");
        for (const auto& term : fact.first.arguments) collect_terms(term, {}, program.terms);
    }
    for (const auto& rule : program.rules) {
        if (program.types.count(rule.conclusion.predicate))
            fail(rule.line, "a declared constructor type cannot be a rule conclusion");
        Variables variables = rule.variables;
        variables.insert(rule.existentials.begin(), rule.existentials.end());
        for (const auto& premise : rule.premises) {
            for (const auto& term : premise.arguments) collect_terms(term, rule.variables, program.terms);
            if (program.types.count(premise.predicate))
                program.partial_predicates.insert(rule.conclusion.predicate);
        }
        for (const auto& term : rule.conclusion.arguments) collect_terms(term, variables, program.terms);
    }
    for (const auto& query : program.queries)
        for (const auto& term : query.atom.arguments) collect_terms(term, query.variables, program.terms);
    bool changed;
    do {
        changed = false;
        for (const auto& rule : program.rules)
            for (const auto& premise : rule.premises)
                if (program.partial_predicates.count(premise.predicate) &&
                    program.partial_predicates.insert(rule.conclusion.predicate).second) changed = true;
    } while (changed);
    return program;
}

bool match_term(const Term& pattern, const Term& value, const Variables& variables, Binding& binding) {
    if (is_variable(pattern, variables)) {
        const auto inserted = binding.emplace(pattern.name, value);
        return inserted.second || inserted.first->second == value;
    }
    if (pattern.name != value.name || pattern.arguments.size() != value.arguments.size()) return false;
    for (std::size_t i = 0; i < pattern.arguments.size(); ++i)
        if (!match_term(pattern.arguments[i], value.arguments[i], variables, binding)) return false;
    return true;
}

bool match(const Atom& pattern, const Atom& fact, const Variables& variables, Binding& binding) {
    if (pattern.predicate != fact.predicate || pattern.arguments.size() != fact.arguments.size()) return false;
    for (std::size_t i = 0; i < pattern.arguments.size(); ++i)
        if (!match_term(pattern.arguments[i], fact.arguments[i], variables, binding)) return false;
    return true;
}

struct ProofLimit {};

std::size_t node_count(const Term& term, std::size_t limit) {
    std::size_t count = 1;
    if (!limit) return count;
    for (const auto& argument : term.arguments) {
        count += node_count(argument, limit - count);
        if (count > limit) return limit + 1;
    }
    return count;
}

Term substitute(const Term& term, const Binding& binding, std::size_t& budget) {
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

std::size_t depth(const Term& term) {
    std::size_t result = 1;
    for (const auto& argument : term.arguments) result = std::max(result, 1 + depth(argument));
    return result;
}

bool is_proven(const Query& query, const Program& program);

bool prove(Program& program) {
    std::vector<std::set<Binding>> fired(program.rules.size());
    std::set<Term> checked_terms;
    std::size_t next_witness = 0;
    for (std::size_t round = 0; round < max_rounds; ++round) {
        // All requested proofs are already established; no need to enumerate successors.
        if (std::all_of(program.queries.begin(), program.queries.end(),
                        [&](const Query& query) { return is_proven(query, program); })) return false;
        bool changed = false;
        // Materialize only the finite terms actually encountered, never all successors.
        for (const auto& term : program.terms) {
            if (!checked_terms.insert(term).second) continue;
            for (const auto& type : program.types) {
                if (!has_type(term, type.first, program)) continue;
                Atom fact{type.first, {term}};
                if (!program.facts.count(fact)) {
                    if (program.facts.size() >= max_facts) return false;
                    program.facts.insert(std::move(fact));
                    changed = true;
                }
            }
        }
        for (std::size_t index = 0; index < program.rules.size(); ++index) {
            const auto& rule = program.rules[index];
            std::vector<Binding> bindings(1);
            for (const auto& premise : rule.premises) {
                std::vector<Binding> next;
                for (const auto& binding : bindings) {
                    for (const auto& fact : program.facts) {
                        auto candidate = binding;
                        if (match(premise, fact, rule.variables, candidate)) {
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
                        values.emplace(variable, Term{":w" + std::to_string(next_witness++), {}});
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
                    for (const auto& term : conclusion.arguments) collect_terms(term, {}, program.terms);
                    program.facts.insert(std::move(conclusion));
                    changed = true;
                }
            }
        }
        if (!changed) return true;
    }
    return false;
}

// Solve a constructor-type query structurally, including existential patterns.
bool type_pattern(const Term& pattern, const std::string& type, const Variables& variables,
                  Binding& binding, const Program& program) {
    if (is_variable(pattern, variables)) {
        const auto value = binding.find(pattern.name);
        if (value != binding.end()) return has_type(value->second, type, program);
        const auto example = program.inhabitants.find(type);
        if (example == program.inhabitants.end()) return false;
        binding.emplace(pattern.name, example->second);
        return true;
    }
    for (const auto& constructor : program.types.at(type)) {
        if (constructor.name != pattern.name || constructor.parameters.size() != pattern.arguments.size()) continue;
        for (std::size_t i = 0; i < pattern.arguments.size(); ++i)
            if (!type_pattern(pattern.arguments[i], constructor.parameters[i], variables, binding, program)) return false;
        return true;
    }
    return false;
}

bool is_proven(const Query& query, const Program& program) {
    if (!query.exists_type.empty()) {
        for (const auto& fact : program.facts) {
            if (fact.predicate == query.exists_type && fact.arguments.size() == 1 &&
                (program.facts.count({query.atom.predicate, fact.arguments}) ||
                 has_type(fact.arguments[0], query.atom.predicate, program))) return true;
        }
        return false;
    }
    if (program.types.count(query.atom.predicate)) {
        if (query.atom.arguments.size() != 1) return false;
        Binding binding;
        return type_pattern(query.atom.arguments[0], query.atom.predicate, query.variables, binding, program);
    }
    if (query.variables.empty()) return program.facts.count(query.atom) != 0;
    for (const auto& fact : program.facts) {
        Binding binding;
        if (match(query.atom, fact, query.variables, binding)) return true;
    }
    return false;
}

suka_result evaluate(const std::vector<Line>& lines, bool check) {
    auto program = parse(lines);
    if (check) return {0, {}, {}};
    const bool saturated = prove(program);
    bool failed = false, unknown = false;
    std::ostringstream output;
    for (const auto& query : program.queries) {
        const bool found = is_proven(query, program);
        const bool structural = query.exists_type.empty() && program.types.count(query.atom.predicate);
        const bool incomplete = !structural && (!saturated ||
            program.partial_predicates.count(query.atom.predicate) ||
            program.partial_predicates.count(query.exists_type));
        output << "verify " << query.name << (found ? " PASS\n" : incomplete ? " UNKNOWN\n" : " FAIL\n");
        unknown = unknown || (!found && incomplete);
        failed = failed || (!found && !incomplete);
    }
    std::string error;
    if (unknown) error = saturated ? "suka: constructor-domain search is limited; unproved queries are UNKNOWN\n"
                                   : "suka: inference limit reached; unproved queries are UNKNOWN\n";
    return {unknown ? 3 : failed ? 1 : 0, output.str(), error};
}

template<class Loader>
suka_result* invoke(Loader loader, bool check) noexcept {
    try {
        return new suka_result(evaluate(loader(), check));
    } catch (const std::exception& error) {
        try { return new suka_result{2, {}, std::string(error.what()) + "\n"}; } catch (...) { return nullptr; }
    } catch (...) {
        try { return new suka_result{2, {}, "suka: unexpected error\n"}; } catch (...) { return nullptr; }
    }
}

suka_result* invoke_file(const char* path, bool check) noexcept {
    return invoke([&]() {
        if (!path || !*path) throw std::runtime_error("<source>:1: file path must not be null or empty");
        Imports imports;
        return load_file(std::filesystem::u8path(path), imports);
    }, check);
}

} // namespace

extern "C" {
suka_result* suka_run_file(const char* path) { return invoke_file(path, false); }
suka_result* suka_check_file(const char* path) { return invoke_file(path, true); }
suka_result* suka_run_source(const char* source, const char* directory) {
    return invoke([&]() { return load_source(source, directory); }, false);
}
suka_result* suka_check_source(const char* source, const char* directory) {
    return invoke([&]() { return load_source(source, directory); }, true);
}
int suka_result_status(const suka_result* result) { return result ? result->status : 2; }
const char* suka_result_output(const suka_result* result) { return result ? result->output.c_str() : ""; }
const char* suka_result_error(const suka_result* result) { return result ? result->error.c_str() : "suka: null result"; }
void suka_result_free(suka_result* result) { delete result; }
const char* suka_version(void) { return "suka 0.2.0"; }
}
