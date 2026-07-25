#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace qbrain::util {

std::string sha256_hex(std::string_view data);
std::string content_hash(std::string_view title, std::string_view body);

}  // namespace qbrain::util
