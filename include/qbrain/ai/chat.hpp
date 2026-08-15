#pragma once
#include "qbrain/core/types.hpp"
#include <string>
#include <vector>

namespace qbrain::ai {

enum class ChatFailureKind {
  none,
  configuration,
  transport_error,
  transport_timeout,
  http_status,
  malformed_response,
};

struct ChatMessage {
  std::string role;  // system | user | assistant
  std::string content;
};

struct ChatResult {
  bool ok = false;
  std::string error;
  std::string content;
  ChatFailureKind failure_kind = ChatFailureKind::none;
};

ChatResult chat_complete(const Config& cfg, const std::vector<ChatMessage>& messages,
                         double temperature = 0.2, int timeout_ms = 120000);

}  // namespace qbrain::ai
