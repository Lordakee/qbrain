#pragma once
#include <chrono>
#include <optional>
#include <string>

namespace qbrain::util {

std::string utc_now();  // "YYYY-MM-DD HH:MM:SS"
std::string utc_date(); // "YYYY-MM-DD"
// N38-B (SQL census "datetime('now', ...)" rewrites): same "YYYY-MM-DD HH:MM:SS"
// UTC shape as utc_now()/SQLite datetime('now'), shifted by delta_seconds
// (negative = past). Dialect-neutral replacement for SQL-side
// datetime('now', '+/-N seconds|hours') modifiers: call sites bind the
// computed string as a parameter instead of embedding a SQLite-only function.
std::string utc_now_offset(long long delta_seconds);
std::string utc_seven_day_boundary(
    std::optional<std::chrono::system_clock::time_point> fixed_now = std::nullopt);

}  // namespace qbrain::util
