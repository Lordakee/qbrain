#include "qbrain/util/log.hpp"
#include <iostream>

namespace qbrain::util {
namespace {
Level g_level = Level::Info;
}

void set_log_level(Level l) { g_level = l; }

void log(Level l, std::string_view msg) {
  if (static_cast<int>(l) < static_cast<int>(g_level)) return;
  const char* tag = "INFO";
  if (l == Level::Debug) tag = "DEBUG";
  else if (l == Level::Warn) tag = "WARN";
  else if (l == Level::Error) tag = "ERROR";
  auto& out = (l == Level::Error || l == Level::Warn) ? std::cerr : std::cout;
  out << "[" << tag << "] " << msg << "\n";
}

}  // namespace qbrain::util
