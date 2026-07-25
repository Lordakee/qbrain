#include "qbrain/mcp/jsonrpc.hpp"
#include <iostream>
#include <sstream>

namespace qbrain::mcp {

// Official MCP TypeScript SDK uses NDJSON on stdio:
//   serializeMessage = JSON.stringify(msg) + "\n"
//   readMessage splits on '\n' (optional trailing \r stripped)
// Content-Length framing is NOT used by @modelcontextprotocol/sdk stdio transport.

std::string make_framed(const std::string& body) {
  // body is already a full JSON object string
  return body + "\n";
}

bool write_framed_message(const std::string& body) {
  auto line = make_framed(body);
  std::cout.write(line.data(), static_cast<std::streamsize>(line.size()));
  std::cout.flush();
  return static_cast<bool>(std::cout);
}

std::optional<std::string> parse_framed_buffer(std::string_view buf, size_t* consumed) {
  // NDJSON primary
  auto nl = buf.find('\n');
  if (nl == std::string_view::npos) return std::nullopt;
  auto line = buf.substr(0, nl);
  if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
  // skip empty lines
  if (line.empty()) {
    if (consumed) *consumed = nl + 1;
    return std::nullopt;  // caller should skip and continue — signal empty by special?
  }
  // skip Content-Length style header lines if a client still sends them
  if (line.find('{') == std::string_view::npos && line.find('[') == std::string_view::npos) {
    if (consumed) *consumed = nl + 1;
    // return empty optional but advance — use a loop in read
    // Encode skip by returning nullopt with consumed set — read_framed handles
    if (consumed) *consumed = nl + 1;
    return std::string();  // empty string means "skip this line"
  }
  if (consumed) *consumed = nl + 1;
  return std::string(line);
}

std::optional<std::string> read_framed_message() {
  std::string acc;
  // prepend leftover
  static std::string leftover;
  if (!leftover.empty()) {
    acc.swap(leftover);
  }
  char ch;
  while (true) {
    size_t consumed = 0;
    auto msg = parse_framed_buffer(acc, &consumed);
    if (msg.has_value()) {
      if (msg->empty()) {
        // skip non-JSON line (headers)
        acc.erase(0, consumed);
        continue;
      }
      if (consumed < acc.size()) {
        leftover.assign(acc.begin() + static_cast<std::ptrdiff_t>(consumed), acc.end());
      } else {
        leftover.clear();
      }
      return msg;
    }
    if (!std::cin.get(ch)) {
      leftover.clear();
      return std::nullopt;
    }
    acc.push_back(ch);
    if (acc.size() > 16 * 1024 * 1024) {
      std::cerr << "[qbrain-serve] message too large\n";
      leftover.clear();
      return std::nullopt;
    }
  }
}

}  // namespace qbrain::mcp
