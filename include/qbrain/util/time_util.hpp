#pragma once
#include <chrono>
#include <optional>
#include <string>

namespace qbrain::util {

std::string utc_now();  // "YYYY-MM-DD HH:MM:SS"
std::string utc_date(); // "YYYY-MM-DD"
std::string utc_seven_day_boundary(
    std::optional<std::chrono::system_clock::time_point> fixed_now = std::nullopt);

}  // namespace qbrain::util
