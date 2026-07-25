#pragma once
#include "qbrain/core/types.hpp"
#include <string>
#include <vector>

namespace qbrain::ai {

struct ChatMessage {
  std::string role;  // system | user | assistant
  std::string content;
};

struct ChatResult {
  bool ok = false;
  std::string error;
  std::string content;
};

ChatResult chat_complete(const Config& cfg, const std::vector<ChatMessage>& messages,
                         double temperature = 0.2);

}  // namespace qbrain::ai
