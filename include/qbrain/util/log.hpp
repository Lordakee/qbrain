#pragma once
#include <string_view>

namespace qbrain::util {

enum class Level { Debug, Info, Warn, Error };

void set_log_level(Level l);
void log(Level l, std::string_view msg);

inline void info(std::string_view m) { log(Level::Info, m); }
inline void warn(std::string_view m) { log(Level::Warn, m); }
inline void error(std::string_view m) { log(Level::Error, m); }
inline void debug(std::string_view m) { log(Level::Debug, m); }

}  // namespace qbrain::util
