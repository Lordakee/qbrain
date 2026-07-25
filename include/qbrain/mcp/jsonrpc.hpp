#pragma once
#include <optional>
#include <string>
#include <string_view>

namespace qbrain::mcp {

// LSP-style Content-Length framing (MCP default)
bool write_framed_message(const std::string& body);
// Read one framed message from stdin; nullopt on EOF
std::optional<std::string> read_framed_message();

// For tests: parse headers + body from a buffer
std::optional<std::string> parse_framed_buffer(std::string_view buf, size_t* consumed);

std::string make_framed(const std::string& body);

}  // namespace qbrain::mcp
