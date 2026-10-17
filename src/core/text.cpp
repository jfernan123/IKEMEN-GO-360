#include "core/text.h"

#include <cctype>
#include <cmath>

namespace m360::text {

std::string_view trim(std::string_view s)
{
	size_t b = 0, e = s.size();
	while (b < e && std::isspace((unsigned char)s[b]))
		b++;
	while (e > b && std::isspace((unsigned char)s[e - 1]))
		e--;
	return s.substr(b, e - b);
}

std::string toLower(std::string_view s)
{
	std::string out(s);
	for (char &c : out)
		c = (char)std::tolower((unsigned char)c);
	return out;
}

bool startsWith(std::string_view s, std::string_view prefix)
{
	return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

std::vector<std::string> splitLines(std::string_view text)
{
	std::vector<std::string> lines;
	size_t start = 0;
	while (start <= text.size()) {
		size_t end = text.find('\n', start);
		if (end == std::string_view::npos)
			end = text.size();
		lines.emplace_back(trim(text.substr(start, end - start)));
		start = end + 1;
	}
	return lines;
}

std::string_view stripComment(std::string_view line)
{
	const size_t semi = line.find(';');
	return trim(semi == std::string_view::npos ? line : line.substr(0, semi));
}

std::vector<std::string> split(std::string_view s, char sep, size_t max_pieces)
{
	std::vector<std::string> pieces;
	size_t start = 0;
	while (pieces.size() + 1 < max_pieces) {
		const size_t at = s.find(sep, start);
		if (at == std::string_view::npos)
			break;
		pieces.emplace_back(trim(s.substr(start, at - start)));
		start = at + 1;
	}
	pieces.emplace_back(trim(s.substr(start)));
	return pieces;
}

int32_t atoi(std::string_view s)
{
	s = trim(s);
	if (s.empty())
		return 0;
	const bool negative = s[0] == '-';
	if (s[0] == '-' || s[0] == '+')
		s.remove_prefix(1);
	int64_t n = 0;
	const int64_t limit = negative ? 2147483648LL : 2147483647LL;
	for (char c : s) {
		if (c < '0' || c > '9')
			break;
		n = n * 10 + (c - '0');
		if (n > limit)
			return negative ? INT32_MIN : INT32_MAX;
	}
	return (int32_t)(negative ? -n : n);
}

double atof(std::string_view s)
{
	s = trim(s);
	if (s.empty())
		return 0;
	const bool negative = s[0] == '-';
	if (s[0] == '-' || s[0] == '+')
		s.remove_prefix(1);
	double value = 0, scale = 1;
	bool fraction = false;
	size_t i = 0;
	for (; i < s.size(); i++) {
		const char c = s[i];
		if (c >= '0' && c <= '9') {
			if (fraction) {
				scale /= 10;
				value += (c - '0') * scale;
			} else {
				value = value * 10 + (c - '0');
			}
		} else if (c == '.' && !fraction) {
			fraction = true;
		} else {
			break;
		}
	}
	// An exponent, as Go's ParseFloat would take it.
	if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
		const int32_t exp = atoi(s.substr(i + 1));
		value *= std::pow(10.0, exp);
	}
	return negative ? -value : value;
}

void sectionName(std::string_view line, std::string &name, std::string &rest)
{
	name.clear();
	rest.clear();
	if (line.empty() || line[0] != '[')
		return;
	std::string_view sec = stripComment(line);
	if (sec.empty() || sec.back() != ']')
		return;
	sec = sec.substr(1, sec.find(']') - 1);
	const size_t space = sec.find(' ');
	if (space != std::string_view::npos) {
		name = toLower(sec.substr(0, space + 1));
		rest = std::string(sec.substr(space + 1));
	} else {
		name = toLower(sec);
	}
}

} // namespace m360::text
