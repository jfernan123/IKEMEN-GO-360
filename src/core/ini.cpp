#include "core/ini.h"

#include "core/text.h"

namespace m360 {

std::string IniSection::get(const std::string &key) const
{
	const auto it = values.find(key);
	return it == values.end() ? std::string() : it->second;
}

std::string IniSection::text(const std::string &key) const
{
	std::string v = get(key);
	if (v.size() >= 2 && v.front() == '"') {
		const size_t close = v.find('"', 1);
		return v.substr(1, close == std::string::npos ? std::string::npos : close - 1);
	}
	return v;
}

bool IniSection::floats(const std::string &key, std::vector<float *> out) const
{
	const auto it = values.find(key);
	if (it == values.end())
		return false;
	const std::vector<std::string> parts = text::split(it->second, ',');
	if (parts.empty() || parts[0].empty())
		return false;
	for (size_t i = 0; i < out.size() && i < parts.size(); i++) {
		if (!parts[i].empty())
			*out[i] = (float)text::atof(parts[i]);
	}
	return true;
}

bool IniSection::ints(const std::string &key, std::vector<int32_t *> out) const
{
	const auto it = values.find(key);
	if (it == values.end())
		return false;
	const std::vector<std::string> parts = text::split(it->second, ',');
	if (parts.empty() || parts[0].empty())
		return false;
	for (size_t i = 0; i < out.size() && i < parts.size(); i++) {
		if (!parts[i].empty())
			*out[i] = text::atoi(parts[i]);
	}
	return true;
}

std::vector<IniSection> parseIni(const std::string &body)
{
	std::vector<IniSection> sections;
	for (const std::string &raw : text::splitLines(body)) {
		if (!raw.empty() && raw[0] == '[') {
			const size_t close = raw.find(']');
			if (close == std::string::npos)
				continue;
			IniSection s;
			s.name = std::string(text::trim(std::string_view(raw).substr(1, close - 1)));
			s.lower = text::toLower(s.name);
			sections.push_back(std::move(s));
			continue;
		}
		if (sections.empty())
			continue;
		// A ';' inside quotes isn't a comment.
		std::string_view line = raw;
		bool quoted = false;
		for (size_t i = 0; i < line.size(); i++) {
			if (line[i] == '"')
				quoted = !quoted;
			else if (line[i] == ';' && !quoted) {
				line = line.substr(0, i);
				break;
			}
		}
		const size_t eq = line.find('=');
		if (eq == std::string_view::npos)
			continue;
		const std::string key = text::toLower(text::trim(line.substr(0, eq)));
		if (key.empty())
			continue;
		if (sections.back().values.emplace(key, std::string(text::trim(line.substr(eq + 1)))).second)
			sections.back().keys.push_back(key);
	}
	return sections;
}

const IniSection *findSection(const std::vector<IniSection> &sections, const std::string &name)
{
	const std::string lower = text::toLower(name);
	for (const IniSection &s : sections) {
		if (s.lower == lower)
			return &s;
	}
	return nullptr;
}

} // namespace m360
