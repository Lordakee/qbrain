#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace qbrain::mcp {

// N36: bounded static token-scoped authentication for the loopback HTTP MCP.
// Configuration (env QBRAIN_MCP_TOKENS, ';'-separated entries):
//   name:token:scope[,scope]
// scope ∈ {read, write, admin}. Tokens are ASCII printable [0x21-0x7E],
// 16..256 chars. Malformed entries are skipped with a startup warning.
// The legacy single-token env (QBRAIN_MCP_TOKEN) remains transport
// authentication only — it never populates a capability (N30 semantics).
//
// Explicitly deferred (Phase-3): TLS (loopback-only boundary), OAuth flows,
// dynamic user stores, token rotation, per-token brain/source restrictions.

enum class TokenScope : unsigned { none = 0, read = 1, write = 2, admin = 4 };

struct ScopedToken {
  std::string name;
  std::string token;
  unsigned scopes = 0;  // TokenScope bitmask
};

struct TokenAuth {
  std::string name;
  // nullptr-equative: empty means authenticated read-only (no capability).
  std::string capability;  // "", "write", or "admin"
  std::string hash_prefix;  // sha256(token) first 16 hex chars, for audit logs
};

// Constant-time equality for equal-length secret comparisons; safe when
// lengths differ only via the public size check the caller performs first.
bool constant_time_equal(std::string_view a, std::string_view b);

// Parses the QBRAIN_MCP_TOKENS payload (the env value, ';' separated).
// Invalid entries (bad field count, bad charset, length out of 16..256,
// unknown scope) are skipped with a warning on stderr; never throws.
std::vector<ScopedToken> parse_scoped_tokens(std::string_view config_value);

// True when the token string satisfies the documented charset/length bounds.
bool token_shape_valid(std::string_view token);

// Authenticates a Bearer value against the configured tokens.
// Returns nullopt on no match (callers treat malformed headers the same way).
std::optional<TokenAuth> authenticate_bearer(const std::vector<ScopedToken>& tokens,
                                             std::string_view bearer_value);

// SHA-256 of the input, first 16 hex chars (audit prefix; no token material).
std::string audit_hash_prefix(std::string_view secret);

}  // namespace qbrain::mcp
