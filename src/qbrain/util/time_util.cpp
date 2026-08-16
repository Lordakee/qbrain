#include "qbrain/util/time_util.hpp"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace qbrain::util {

static std::tm utc_tm() {
  using namespace std::chrono;
  auto t = system_clock::to_time_t(system_clock::now());
  std::tm tm{};
#ifdef _WIN32
  gmtime_s(&tm, &t);
#else
  gmtime_r(&t, &tm);
#endif
  return tm;
}

std::string utc_now() {
  auto tm = utc_tm();
  std::ostringstream oss;
  oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
  return oss.str();
}

std::string utc_date() {
  auto tm = utc_tm();
  std::ostringstream oss;
  oss << std::put_time(&tm, "%Y-%m-%d");
  return oss.str();
}

// N38-B: shared helper for the census datetime('now', ...) -> bound-parameter
// rewrites. Byte-identical output shape to SQLite datetime('now') (UTC,
// whole seconds), so stored TEXT values and lexicographic comparisons keep
// their exact pre-N38 semantics on the SQLite path (plan P2-3).
std::string utc_now_offset(long long delta_seconds) {
  using namespace std::chrono;
  const auto shifted = system_clock::now() + seconds{delta_seconds};
  auto t = system_clock::to_time_t(shifted);
  std::tm tm{};
#ifdef _WIN32
  gmtime_s(&tm, &t);
#else
  gmtime_r(&t, &tm);
#endif
  std::ostringstream oss;
  oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
  return oss.str();
}

std::string utc_seven_day_boundary(
    std::optional<std::chrono::system_clock::time_point> fixed_now) {
  using namespace std::chrono;
  const auto now = fixed_now ? *fixed_now : system_clock::now();
  const year_month_day boundary{floor<days>(now) - days{6}};

  std::ostringstream oss;
  oss << std::setfill('0') << std::setw(4) << static_cast<int>(boundary.year()) << '-'
      << std::setw(2) << static_cast<unsigned>(boundary.month()) << '-' << std::setw(2)
      << static_cast<unsigned>(boundary.day()) << "T00:00:00Z";
  return oss.str();
}

}  // namespace qbrain::util
