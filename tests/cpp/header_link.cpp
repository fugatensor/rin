#include "rin/pass.h"

rin::pass::Decision runFromOtherTranslationUnit(std::string_view source) {
    return rin::pass::run(source);
}

rin::pass::Solution solveFromOtherTranslationUnit(std::string_view source) {
    return rin::pass::solve(source);
}
