#include "qbrain/util/hash.hpp"
#include <sstream>
#include <iomanip>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#endif

namespace qbrain::util {

std::string sha256_hex(std::string_view data) {
#ifdef _WIN32
  BCRYPT_ALG_HANDLE alg = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  DWORD obj_len = 0, cb = 0, hash_len = 0;
  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
    return {};
  BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&obj_len), sizeof(obj_len),
                    &cb, 0);
  BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hash_len), sizeof(hash_len),
                    &cb, 0);
  std::vector<uint8_t> obj(obj_len), out(hash_len);
  if (BCryptCreateHash(alg, &hash, obj.data(), obj_len, nullptr, 0, 0) < 0) {
    BCryptCloseAlgorithmProvider(alg, 0);
    return {};
  }
  BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())),
                 static_cast<ULONG>(data.size()), 0);
  BCryptFinishHash(hash, out.data(), hash_len, 0);
  BCryptDestroyHash(hash);
  BCryptCloseAlgorithmProvider(alg, 0);
  std::ostringstream oss;
  for (auto b : out)
    oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
  return oss.str();
#else
  (void)data;
  return {};
#endif
}

std::string content_hash(std::string_view title, std::string_view body) {
  std::string s;
  s.reserve(title.size() + body.size() + 1);
  s.append(title);
  s.push_back('\n');
  s.append(body);
  return sha256_hex(s);
}

}  // namespace qbrain::util
