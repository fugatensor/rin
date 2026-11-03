#include "suka/pass.h"

suka::pass::Decision runFromOtherTranslationUnit(std::string_view source) {
    return suka::pass::run(source);
}
