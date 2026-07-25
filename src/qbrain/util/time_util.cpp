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

}  // namespace qbrain::util
