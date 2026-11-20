#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Text helpers that read MUGEN files the way Ikemen-GO does (src/common.go), so odd files parse the same.
namespace m360::text {

std::string_view trim(std::string_view s);
std::string toLower(std::string_view s);
bool startsWith(std::string_view s, std::string_view prefix);
// Splits on '\n' and trims each line ('\r' included), as Ikemen's SplitAndTrim(text, "\n").
std::vector<std::string> splitLines(std::string_view text);
// The part before the first ';', trimmed: MUGEN's comments.
std::string_view stripComment(std::string_view line);
// Splits on sep, trimming each piece.
std::vector<std::string> split(std::string_view s, char sep, size_t max_pieces = SIZE_MAX);

// Ikemen's Atoi: optional sign, then digits up to the first non-digit; "" and junk give 0.
int32_t atoi(std::string_view s);
// Ikemen's Atof: a decimal number, stopping at the first character that can't continue it.
double atof(std::string_view s);

// "[Begin Action 200]" -> name "begin " (lowercase, with its space), rest "Action 200"; not a section -> both empty.
void sectionName(std::string_view line, std::string &name, std::string &rest);

} // namespace m360::text
