#include "qbrain/mcp/auth.hpp"

#include "qbrain/util/hash.hpp"

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace qbrain::mcp {

namespace {

bool is_scope_token(std::string_view s, std::string_view want) {
  return s == want;
}

unsigned parse_scope_list(std::string_view list) {
  unsigned scopes = 0;
  std::string_view rest = list;
  while (!rest.empty()) {
    auto comma = rest.find(',');
    auto item = rest.substr(0, comma == std::string_view::npos ? rest.size() : comma);
    if (is_scope_token(item, "read")) scopes |= static_cast<unsigned>(TokenScope::read);
    else if (is_scope_token(item, "write")) scopes |= static_cast<unsigned>(TokenScope::write);
    else if (is_scope_token(item, "admin")) scopes |= static_cast<unsigned>(TokenScope::admin);
    else return 0;  // unknown scope invalidates the entry
    if (comma == std::string_view::npos) break;
    rest = rest.substr(comma + 1);
  }
  return scopes;
}

}  // namespace

bool constant_time_equal(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  unsigned char diff = 0;
  for (size_t i = 0; i < a.size(); ++i)
    diff |= static_cast<unsigned char>(a[i] ^ b[i]);
  return diff == 0;
}

bool token_shape_valid(std::string_view token) {
  if (token.size() < 16 || token.size() > 256) return false;
  for (char c : token)
    if (static_cast<unsigned char>(c) < 0x21 || static_cast<unsigned char>(c) > 0x7E)
      return false;
  return true;
}

std::vector<ScopedToken> parse_scoped_tokens(std::string_view config_value) {
  std::vector<ScopedToken> out;
  std::string_view rest = config_value;
  while (!rest.empty()) {
    auto semi = rest.find(';');
    auto entry = rest.substr(0, semi == std::string_view::npos ? rest.size() : semi);
    rest = semi == std::string_view::npos ? std::string_view{} : rest.substr(semi + 1);
    if (entry.empty()) continue;
    // name:token:scope[,scope]
    auto c1 = entry.find(':');
    auto c2 = c1 == std::string_view::npos ? std::string_view::npos : entry.find(':', c1 + 1);
    auto c3 = c2 == std::string_view::npos ? std::string_view::npos : entry.find(':', c2 + 1);
    bool ok = c1 != std::string_view::npos && c2 != std::string_view::npos &&
              c3 == std::string_view::npos;
    if (ok) {
      ScopedToken st;
      st.name = std::string(entry.substr(0, c1));
      st.token = std::string(entry.substr(c1 + 1, c2 - c1 - 1));
      st.scopes = parse_scope_list(entry.substr(c2 + 1));
      ok = !st.name.empty() && token_shape_valid(st.token) && st.scopes != 0;
      if (ok) out.push_back(std::move(st));
    }
    if (!ok)
      std::fprintf(stderr, "[qbrain-auth] skipping invalid QBRAIN_MCP_TOKENS entry\n");
  }
  return out;
}

std::string audit_hash_prefix(std::string_view secret) {
  return util::sha256_hex(secret).substr(0, 16);
}

std::optional<TokenAuth> authenticate_bearer(const std::vector<ScopedToken>& tokens,
                                             std::string_view bearer_value) {
  for (const auto& st : tokens) {
    if (bearer_value.size() != st.token.size()) continue;
    if (!constant_time_equal(bearer_value, st.token)) continue;
    TokenAuth auth;
    auth.name = st.name;
    if (st.scopes & static_cast<unsigned>(TokenScope::admin)) auth.capability = "admin";
    else if (st.scopes & static_cast<unsigned>(TokenScope::write)) auth.capability = "write";
    else auth.capability = "";  // read-only: authenticated, no capability
    auth.hash_prefix = audit_hash_prefix(st.token);
    return auth;
  }
  return std::nullopt;
}

}  // namespace qbrain::mcp
