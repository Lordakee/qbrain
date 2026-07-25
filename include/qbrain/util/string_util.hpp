#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace qbrain::util {

std::string trim(std::string_view s);
std::vector<std::string> split(std::string_view s, char delim);
std::string to_lower(std::string s);
std::string slugify(std::string_view title);
bool starts_with(std::string_view s, std::string_view prefix);
bool ends_with(std::string_view s, std::string_view suffix);
std::string replace_all(std::string s, std::string_view from, std::string_view to);

}  // namespace qbrain::util
