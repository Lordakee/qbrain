// tests/test_n33.cpp — N33 real multimodal ingestion/search (subagent B).
// Covers D3 (embed_image provider contract, redaction, deterministic mock)
// and D4 (put_raw_data/get_raw_data/file_upload metadata, search_by_image
// fail-open + cosine). Valid PNG/JPEG inputs are constructed inline (CRC-32
// correct IHDR / SOF0) so the suite is self-contained; tests/fixtures/img
// from subagent A may land later without affecting these assertions.

#include "qbrain/ai/embed.hpp"
#include "qbrain/core/brain.hpp"
#include "qbrain/ops/registry.hpp"
#include "qbrain/search/vector.hpp"
#include "qbrain/util/paths.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

using json = nlohmann::json;

#define QB_CHECK(cond)                                                          \
  do {                                                                          \
    if (!(cond)) {                                                              \
      throw std::runtime_error(std::string("CHECK failed: ") + #cond + " @ " +  \
                               __FILE__ + ":" + std::to_string(__LINE__));      \
    }                                                                           \
  } while (0)

namespace {

// ---- env scoping (hermetic credential state per section) ----

class N33ScopedEnv {
 public:
  N33ScopedEnv(const char* name, const std::string& value) : name_(name) {
    if (const char* previous = std::getenv(name)) previous_ = previous;
    if (_putenv_s(name, value.c_str()) != 0) {
      throw std::runtime_error("failed to set environment variable");
    }
  }
  ~N33ScopedEnv() { _putenv_s(name_, previous_ ? previous_->c_str() : ""); }
  N33ScopedEnv(const N33ScopedEnv&) = delete;
  N33ScopedEnv& operator=(const N33ScopedEnv&) = delete;

 private:
  const char* name_;
  std::optional<std::string> previous_;
};

class N33ClearCreds {
 public:
  N33ClearCreds() = default;
  ~N33ClearCreds() {
    for (auto it = scoped_.rbegin(); it != scoped_.rend(); ++it) it->reset();
  }
  void clear(const char* name) {
    scoped_.push_back(std::make_unique<N33ScopedEnv>(name, ""));
  }

 private:
  std::vector<std::unique_ptr<N33ScopedEnv>> scoped_;
};

// ---- valid inline image construction ----

void put_u32_be(std::string& out, uint32_t v) {
  out.push_back(static_cast<char>((v >> 24) & 0xFF));
  out.push_back(static_cast<char>((v >> 16) & 0xFF));
  out.push_back(static_cast<char>((v >> 8) & 0xFF));
  out.push_back(static_cast<char>(v & 0xFF));
}

void put_u16_be(std::string& out, uint16_t v) {
  out.push_back(static_cast<char>((v >> 8) & 0xFF));
  out.push_back(static_cast<char>(v & 0xFF));
}

uint32_t crc32_of(const std::string& data) {
  uint32_t c = 0xFFFFFFFFu;
  for (char ch : data) {
    c ^= static_cast<unsigned char>(ch);
    for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return c ^ 0xFFFFFFFFu;
}

std::string png_chunk(const char* type, const std::string& data) {
  std::string out;
  put_u32_be(out, static_cast<uint32_t>(data.size()));
  const std::string body(type + data);
  out += body;
  put_u32_be(out, crc32_of(body));
  return out;
}

// PNG: signature + IHDR(123x456, bit depth 8, color type 6 RGBA) + IEND.
std::string n33_png_123x456() {
  std::string ihdr;
  put_u32_be(ihdr, 123);
  put_u32_be(ihdr, 456);
  ihdr.push_back('\x08');  // bit depth
  ihdr.push_back('\x06');  // color type RGBA
  ihdr.push_back('\x00');  // compression
  ihdr.push_back('\x00');  // filter
  ihdr.push_back('\x00');  // interlace
  std::string png = std::string("\x89PNG\r\n\x1a\n", 8);
  png += png_chunk("IHDR", ihdr);
  png += png_chunk("IEND", "");
  return png;
}

// JPEG: SOI + APP0(JFIF) + SOF0(200x100, 3 components) + EOI.
std::string n33_jpeg_200x100() {
  std::string jpg;
  jpg.push_back(static_cast<char>(0xFF));
  jpg.push_back(static_cast<char>(0xD8));  // SOI
  jpg.push_back(static_cast<char>(0xFF));
  jpg.push_back(static_cast<char>(0xE0));  // APP0
  put_u16_be(jpg, 16);
  jpg += std::string("JFIF\0", 5);
  jpg += std::string("\x01\x02\x00\x00\x01\x00\x01\x00\x00", 9);
  jpg.push_back(static_cast<char>(0xFF));
  jpg.push_back(static_cast<char>(0xC0));  // SOF0
  put_u16_be(jpg, 17);
  jpg.push_back('\x08');  // precision
  put_u16_be(jpg, 100);   // height
  put_u16_be(jpg, 200);   // width
  jpg.push_back('\x03');  // components
  jpg += std::string("\x01\x11\x00\x02\x11\x01\x03\x11\x01", 9);
  jpg.push_back(static_cast<char>(0xFF));
  jpg.push_back(static_cast<char>(0xD9));  // EOI
  return jpg;
}

json n33_expected_png_block() {
  // PNG color type 6 (RGBA) surfaces as 4 samples/pixel in ImageMeta.
  return json{{"format", "png"},
              {"mime", "image/png"},
              {"content_based", true},
              {"declared_ext_mismatch", false},
              {"width", 123},
              {"height", 456},
              {"bit_depth", 8},
              {"components", 4}};
}

json n33_expected_jpeg_block() {
  // JPEG SOF sample precision surfaces as bit_depth.
  return json{{"format", "jpeg"},
              {"mime", "image/jpeg"},
              {"content_based", true},
              {"declared_ext_mismatch", false},
              {"width", 200},
              {"height", 100},
              {"bit_depth", 8},
              {"components", 3}};
}

// ---- loopback helpers (failure injection) ----

#ifdef _WIN32

class N33Winsock {
 public:
  N33Winsock() {
    WSADATA data{};
    started_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
  }
  ~N33Winsock() {
    if (started_) WSACleanup();
  }

 private:
  bool started_ = false;
};

unsigned short n33_free_port() {
  SOCKET probe = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (probe == INVALID_SOCKET) throw std::runtime_error("probe socket failed");
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  if (bind(probe, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
    closesocket(probe);
    throw std::runtime_error("probe bind failed");
  }
  int size = sizeof(addr);
  if (getsockname(probe, reinterpret_cast<sockaddr*>(&addr), &size) == SOCKET_ERROR) {
    closesocket(probe);
    throw std::runtime_error("getsockname failed");
  }
  const unsigned short port = ntohs(addr.sin_port);
  closesocket(probe);
  return port;
}

// One-shot raw HTTP server that answers with a 401 whose WWW-Authenticate
// header and body carry URLs plus (fake) credential material: the negative
// fixture for N33 D3 credential isolation.
void n33_serve_401_once(unsigned short port) {
  SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (listener == INVALID_SOCKET) return;
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(port);
  if (bind(listener, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0 ||
      listen(listener, 1) != 0) {
    closesocket(listener);
    return;
  }
  // Detached: worst case the client times out; the caller only asserts
  // redaction of whatever error comes back.
  std::thread([listener]() {
    SOCKET client = accept(listener, nullptr, nullptr);
    closesocket(listener);
    if (client == INVALID_SOCKET) return;
    DWORD timeout = 2000;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    char buf[4096];
    while (recv(client, buf, sizeof(buf), 0) > 0) {
      // drain request until idle
    }
    const std::string body =
        R"({"error":{"message":"key sk-n33b-SECRETKEY rejected for )"
        R"(https://api.n33-redact.example/v1/embeddings","type":"invalid_request_error"}})";
    const std::string response =
        "HTTP/1.1 401 Unauthorized\r\n"
        "Content-Type: application/json\r\n"
        "WWW-Authenticate: Bearer realm=\"https://auth.n33-redact.example/realms/q\", "
        "error=\"invalid_token\"\r\n"
        "Content-Length: " +
        std::to_string(body.size()) +
        "\r\n"
        "Connection: close\r\n"
        "\r\n" +
        body;
    size_t sent = 0;
    while (sent < response.size()) {
      const int n = ::send(client, response.data() + sent,
                           static_cast<int>(response.size() - sent), 0);
      if (n <= 0) break;
      sent += static_cast<size_t>(n);
    }
    shutdown(client, SD_BOTH);
    closesocket(client);
  }).detach();
}

#endif  // _WIN32

// No URL / credential material may survive redaction.
bool n33_leaks_secrets(const std::string& text, const std::string& base_url,
                       const std::string& api_key) {
  if (text.find("http://") != std::string::npos) return true;
  if (text.find("https://") != std::string::npos) return true;
  if (text.find("://") != std::string::npos) return true;
  if (!base_url.empty() && text.find(base_url) != std::string::npos) return true;
  if (!api_key.empty() && text.find(api_key) != std::string::npos) return true;
  if (text.find("n33-redact") != std::string::npos) return true;
  if (text.find("SECRETKEY") != std::string::npos) return true;
  return false;
}

}  // namespace

void test_n33_multimodal() {
  namespace fs = std::filesystem;
  qbrain::ops::register_builtin_ops();

  const fs::path dir = fs::temp_directory_path() / "qbrain_n33";
  fs::remove_all(dir);
  fs::create_directories(dir);
  // Isolate from any real %LOCALAPPDATA%\Qbrain\config.json so credential
  // state is fully controlled by this test.
  const std::string local_root = qbrain::util::path_to_utf8(dir / "localappdata");
  N33ScopedEnv local_app_data("LOCALAPPDATA", local_root);

  const std::string png_bytes = n33_png_123x456();
  const std::string jpeg_bytes = n33_jpeg_200x100();

  qbrain::Brain brain("n33");
  brain.open_at(qbrain::util::path_to_utf8(dir / "brain.db"));
  qbrain::ops::OpContext ctx;
  ctx.brain = &brain;
  ctx.allow_write = true;

  // ================= 1. metadata exact match (raw_data path) =================
  {
    ctx.args = {{"key", "img/n33_a.png"}, {"content", png_bytes}};
    auto put = qbrain::ops::global_registry().call("put_raw_data", ctx);
    QB_CHECK(put.ok);
    QB_CHECK(json::parse(put.json)["image"] == n33_expected_png_block());

    ctx.args = {{"key", "img/n33_b.jpg"}, {"content", jpeg_bytes}};
    put = qbrain::ops::global_registry().call("put_raw_data", ctx);
    QB_CHECK(put.ok);
    QB_CHECK(json::parse(put.json)["image"] == n33_expected_jpeg_block());

    // get_raw_data surfaces the stored block, additively.
    ctx.args = {{"key", "img/n33_a.png"}};
    auto got = qbrain::ops::global_registry().call("get_raw_data", ctx);
    QB_CHECK(got.ok);
    const auto payload = json::parse(got.json);
    QB_CHECK(payload["image"] == n33_expected_png_block());
    const auto stored_meta = json::parse(payload["meta_json"].get<std::string>());
    QB_CHECK(stored_meta["image"] == n33_expected_png_block());

    // user-provided meta keys survive the merge.
    ctx.args = {{"key", "img/n33_c.png"},
                {"content", png_bytes},
                {"meta_json", R"({"src":"n33-test","k":7})"}};
    put = qbrain::ops::global_registry().call("put_raw_data", ctx);
    QB_CHECK(put.ok);
    ctx.args = {{"key", "img/n33_c.png"}};
    got = qbrain::ops::global_registry().call("get_raw_data", ctx);
    QB_CHECK(got.ok);
    const auto merged = json::parse(json::parse(got.json)["meta_json"].get<std::string>());
    QB_CHECK(merged["src"] == "n33-test");
    QB_CHECK(merged["k"] == 7);
    QB_CHECK(merged["image"] == n33_expected_png_block());
  }

  // ================= 2. spoof detection + ext mismatch =================
  {
    // PNG content under a .jpg key: classified by content, mismatch flagged.
    ctx.args = {{"key", "img/spoof.png_actually.jpg"}, {"content", png_bytes}};
    auto put = qbrain::ops::global_registry().call("put_raw_data", ctx);
    QB_CHECK(put.ok);
    const auto block = json::parse(put.json)["image"];
    QB_CHECK(block["mime"] == "image/png");
    QB_CHECK(block["content_based"] == true);
    QB_CHECK(block["declared_ext_mismatch"] == true);
    QB_CHECK(block["format"] == "png");

    // Text content under an image name (file_upload path): content wins
    // (text/plain, content_based), mismatch observable, metadata is
    // response-only.
    const fs::path fake = dir / "n33_fake.png";
    {
      std::ofstream out(fake, std::ios::binary);
      out << "this is definitely not a png, just text bytes\n";
    }
    ctx.args = {{"path", qbrain::util::path_to_utf8(fake)}};
    auto up = qbrain::ops::global_registry().call("file_upload", ctx);
    QB_CHECK(up.ok);
    const auto up_payload = json::parse(up.json);
    QB_CHECK(up_payload["id"] > 0);
    QB_CHECK(up_payload.contains("url"));
    QB_CHECK(up_payload["image"]["format"] == "unknown");
    QB_CHECK(up_payload["image"]["mime"] == "text/plain");
    QB_CHECK(up_payload["image"]["content_based"] == true);
    QB_CHECK(up_payload["image"]["declared_ext_mismatch"] == true);

    // Genuine PNG through file_upload: exact block in the response.
    const fs::path good = dir / "n33_good.png";
    {
      std::ofstream out(good, std::ios::binary);
      out.write(png_bytes.data(), static_cast<std::streamsize>(png_bytes.size()));
    }
    ctx.args = {{"path", qbrain::util::path_to_utf8(good)}};
    up = qbrain::ops::global_registry().call("file_upload", ctx);
    QB_CHECK(up.ok);
    QB_CHECK(json::parse(up.json)["image"] == n33_expected_png_block());
  }

  // ================= 3. malformed / truncated bounded =================
  {
    // PNG magic + partial IHDR: no crash, bounded outcome (no dimensions).
    std::string truncated = png_bytes.substr(0, 16);
    ctx.args = {{"key", "img/trunc.png"}, {"content", truncated}};
    auto put = qbrain::ops::global_registry().call("put_raw_data", ctx);
    QB_CHECK(put.ok);
    const auto block = json::parse(put.json);
    QB_CHECK(block.contains("image"));
    QB_CHECK(!block["image"].contains("width"));
    QB_CHECK(!block["image"].contains("height"));

    // JPEG SOI then garbage: bounded, never throws.
    std::string bad_jpeg = std::string("\xFF\xD8\xFF", 3) + std::string(24, '\x7F');
    ctx.args = {{"key", "img/bad.jpg"}, {"content", bad_jpeg}};
    put = qbrain::ops::global_registry().call("put_raw_data", ctx);
    QB_CHECK(put.ok);

    // empty content under an image name: no content classification, ext
    // fallback mime, no mismatch claim — spoof marker shape, never a crash.
    ctx.args = {{"key", "img/empty.png"}, {"content", ""}};
    put = qbrain::ops::global_registry().call("put_raw_data", ctx);
    QB_CHECK(put.ok);
    const auto empty_block = json::parse(put.json)["image"];
    QB_CHECK(empty_block["format"] == "unknown");
    QB_CHECK(empty_block["content_based"] == false);
    QB_CHECK(empty_block["declared_ext_mismatch"] == false);
    ctx.args = {{"key", "img/empty.png"}};
    auto got = qbrain::ops::global_registry().call("get_raw_data", ctx);
    QB_CHECK(got.ok);
    QB_CHECK(json::parse(got.json)["image"] == empty_block);
  }

  // ================= 4. mock determinism =================
  {
    N33ScopedEnv mock_on("QBRAIN_EMBED_MOCK", "1");
    const auto& cfg = brain.config();

    auto e1 = qbrain::ai::embed_image(cfg, png_bytes);
    auto e2 = qbrain::ai::embed_image(cfg, png_bytes);
    QB_CHECK(e1.ok && e1.mock);
    QB_CHECK(e1.vector == e2.vector);            // same content = identical vector
    auto e3 = qbrain::ai::embed_image(cfg, jpeg_bytes);
    QB_CHECK(e3.ok && e3.vector != e1.vector);   // different content differs
    QB_CHECK(e1.vector.size() == 64);

    // search_by_image: byte-identical query_vector across calls (mock).
    const fs::path qa = dir / "n33_mock_a.png";
    const fs::path qb = dir / "n33_mock_b.png";
    {
      std::ofstream(qa, std::ios::binary)
          .write(png_bytes.data(), static_cast<std::streamsize>(png_bytes.size()));
      std::ofstream(qb, std::ios::binary)
          .write(jpeg_bytes.data(), static_cast<std::streamsize>(jpeg_bytes.size()));
    }
    ctx.args = {{"path", qbrain::util::path_to_utf8(qa)}, {"limit", "5"}};
    auto s1 = qbrain::ops::global_registry().call("search_by_image", ctx);
    QB_CHECK(s1.ok);
    auto s2 = qbrain::ops::global_registry().call("search_by_image", ctx);
    QB_CHECK(s2.ok);
    const auto j1 = json::parse(s1.json);
    const auto j2 = json::parse(s2.json);
    QB_CHECK(j1["mode"] == "mock");
    QB_CHECK(j1["query_vector"] == j2["query_vector"]);  // byte-identical floats
    QB_CHECK(j1["query_vector"].is_array() && !j1["query_vector"].empty());

    ctx.args = {{"path", qbrain::util::path_to_utf8(qb)}, {"limit", "5"}};
    auto s3 = qbrain::ops::global_registry().call("search_by_image", ctx);
    QB_CHECK(s3.ok);
    QB_CHECK(json::parse(s3.json)["query_vector"] != j1["query_vector"]);

    // The identical uploaded copy of the query scores highest (~1.0 cosine).
    QB_CHECK(!j1["results"].empty());
    QB_CHECK(j1["results"][0]["score"].get<double>() > 0.999);
    // Ranked descending.
    bool descending = true;
    for (size_t i = 1; i < j1["results"].size(); ++i)
      if (j1["results"][i]["score"].get<double>() > j1["results"][i - 1]["score"].get<double>())
        descending = false;
    QB_CHECK(descending);
  }

  // ================= 5. no credentials: fail-open + text search intact ====
  {
    N33ClearCreds creds;
    creds.clear("OPENAI_API_KEY");
    creds.clear("QBRAIN_API_KEY");
    creds.clear("QBRAIN_EMBED_MOCK");
    brain.config().embedding_api_key.clear();
    brain.config().chat_api_key.clear();
    brain.config().embedding_base_url = "https://api.openai.com/v1";

    ctx.args = {{"path", qbrain::util::path_to_utf8(dir / "n33_good.png")}};
    auto res = qbrain::ops::global_registry().call("search_by_image", ctx);
    QB_CHECK(res.ok);
    QB_CHECK(res.exit_code == 0);
    const auto payload = json::parse(res.json);
    QB_CHECK(payload["results"].is_array() && payload["results"].empty());
    QB_CHECK(payload["mode"] == "unavailable");
    QB_CHECK(payload["reason"] == "no provider credentials");

    // embed_image contract: immediate unavailable, no network attempt.
    auto e = qbrain::ai::embed_image(brain.config(), png_bytes);
    QB_CHECK(!e.ok && e.unavailable && e.no_credentials);
    QB_CHECK(e.error == "no provider credentials");

    // Text search on the same brain is unaffected.
    qbrain::ops::OpContext page_ctx;
    page_ctx.brain = &brain;
    page_ctx.allow_write = true;
    page_ctx.args = {{"slug", "n33-text-page"}, {"body", "quantum ferret binderulon notes"}};
    auto page = qbrain::ops::global_registry().call("put_page", page_ctx);
    QB_CHECK(page.ok);
    qbrain::ops::OpContext search_ctx;
    search_ctx.brain = &brain;
    search_ctx.args = {{"query", "binderulon"}, {"limit", "5"}};
    auto text = qbrain::ops::global_registry().call("search", search_ctx);
    QB_CHECK(text.ok);
    QB_CHECK(text.json.find("n33-text-page") != std::string::npos);
  }

#ifdef _WIN32
  // ================= 6. provider failure injection =================
  {
    N33Winsock winsock;
    N33ClearCreds creds;
    creds.clear("OPENAI_API_KEY");
    creds.clear("QBRAIN_API_KEY");
    creds.clear("QBRAIN_EMBED_MOCK");

    // (a) closed loopback port: connection refused -> unavailable, no leak.
    const unsigned short closed_port = n33_free_port();
    brain.config().embedding_base_url = "http://127.0.0.1:" + std::to_string(closed_port);
    brain.config().embedding_api_key = "sk-n33b-closed-port-key";
    auto e = qbrain::ai::embed_image(brain.config(), png_bytes);
    QB_CHECK(!e.ok && e.unavailable);
    QB_CHECK(!e.no_credentials);
    QB_CHECK(!n33_leaks_secrets(e.error, brain.config().embedding_base_url,
                                brain.config().embedding_api_key));
    QB_CHECK(e.error.size() <= 200);

    // The op surface fails open with the same redaction guarantee.
    ctx.args = {{"path", qbrain::util::path_to_utf8(dir / "n33_good.png")}};
    auto res = qbrain::ops::global_registry().call("search_by_image", ctx);
    QB_CHECK(res.ok && res.exit_code == 0);
    const auto payload = json::parse(res.json);
    QB_CHECK(payload["mode"] == "unavailable");
    QB_CHECK(payload["reason"] != "no provider credentials");
    QB_CHECK(!n33_leaks_secrets(res.json + res.text, brain.config().embedding_base_url,
                                brain.config().embedding_api_key));

    // (b) live 401 with WWW-Authenticate realm/token material (negative
    // fixture from the plan): output carries no URL/credential material.
    const unsigned short serve_port = n33_free_port();
    brain.config().embedding_base_url = "http://127.0.0.1:" + std::to_string(serve_port);
    brain.config().embedding_api_key = "sk-n33b-SECRETKEY";
    n33_serve_401_once(serve_port);
    auto e401 = qbrain::ai::embed_image(brain.config(), png_bytes);
    QB_CHECK(!e401.ok && e401.unavailable);
    QB_CHECK(!e401.no_credentials);
    QB_CHECK(!n33_leaks_secrets(e401.error, brain.config().embedding_base_url,
                                brain.config().embedding_api_key));
    QB_CHECK(e401.error.size() <= 200);

    // oversized input: bounded before any network attempt.
    std::string huge = png_bytes;
    huge.resize(32u * 1024 * 1024 + 10, '\0');
    brain.config().embedding_base_url = "http://127.0.0.1:" + std::to_string(closed_port);
    auto eh = qbrain::ai::embed_image(brain.config(), huge);
    QB_CHECK(!eh.ok && eh.unavailable);
    QB_CHECK(eh.error == "image exceeds size limit");
  }
#endif  // _WIN32

  // ================= 7. 32MiB pre-write bound (ingestion paths) ===========
  {
    // put_raw_data: oversized declared length with image magic prefix ->
    // metadata extraction refused (no image block), write still succeeds.
    std::string huge = png_bytes;
    huge.resize(32u * 1024 * 1024 + 10, '\0');
    ctx.args = {{"key", "img/huge.png"}, {"content", huge}};
    auto put = qbrain::ops::global_registry().call("put_raw_data", ctx);
    QB_CHECK(put.ok);
    QB_CHECK(!json::parse(put.json).contains("image"));
    ctx.args = {{"key", "img/huge.png"}};
    auto got = qbrain::ops::global_registry().call("get_raw_data", ctx);
    QB_CHECK(got.ok);
    const auto payload = json::parse(got.json);
    QB_CHECK(!payload.contains("image"));
    QB_CHECK(payload["meta_json"] == "{}");

    // file_upload: oversized declared file size (sparse extension, cheap) ->
    // extraction skipped at handler entry, upload itself unaffected.
    const fs::path big = dir / "n33_big.png";
    {
      std::ofstream out(big, std::ios::binary);
      out.write(png_bytes.data(), static_cast<std::streamsize>(png_bytes.size()));
    }
    std::error_code ec;
    fs::resize_file(big, 33LL * 1024 * 1024, ec);
    QB_CHECK(!ec);
    ctx.args = {{"path", qbrain::util::path_to_utf8(big)}};
    auto up = qbrain::ops::global_registry().call("file_upload", ctx);
    QB_CHECK(up.ok);
    const auto up_payload = json::parse(up.json);
    QB_CHECK(up_payload["id"] > 0);
    QB_CHECK(up_payload.contains("url"));
    QB_CHECK(!up_payload.contains("image"));
  }

  // ================= 8. backward compatibility =================
  {
    // Legacy row written directly (pre-N33 shape): reads fine, no image key.
    QB_CHECK(brain.put_raw_data("legacy/row", "legacy text", R"({"src":"n33-legacy"})"));
    ctx.args = {{"key", "legacy/row"}};
    auto got = qbrain::ops::global_registry().call("get_raw_data", ctx);
    QB_CHECK(got.ok);
    const auto payload = json::parse(got.json);
    QB_CHECK(payload["content"] == "legacy text");
    QB_CHECK(payload["meta_json"] == R"({"src":"n33-legacy"})");
    QB_CHECK(!payload.contains("image"));

    // Plain text through the handler: meta passes through untouched.
    ctx.args = {{"key", "notes/plain"}, {"content", "hello world"},
                {"meta_json", R"({"kind":"note"})"}};
    auto put = qbrain::ops::global_registry().call("put_raw_data", ctx);
    QB_CHECK(put.ok);
    QB_CHECK(!json::parse(put.json).contains("image"));
    ctx.args = {{"key", "notes/plain"}};
    got = qbrain::ops::global_registry().call("get_raw_data", ctx);
    QB_CHECK(got.ok);
    const auto plain = json::parse(got.json);
    QB_CHECK(!plain.contains("image"));
    QB_CHECK(json::parse(plain["meta_json"].get<std::string>()) ==
             json::parse(R"({"kind":"note"})"));

    // Non-image upload keeps the classic response shape.
    const fs::path txt = dir / "n33_note.txt";
    {
      std::ofstream out(txt, std::ios::binary);
      out << "plain text upload\n";
    }
    ctx.args = {{"path", qbrain::util::path_to_utf8(txt)}};
    auto up = qbrain::ops::global_registry().call("file_upload", ctx);
    QB_CHECK(up.ok);
    QB_CHECK(!json::parse(up.json).contains("image"));
  }

  brain.close();
  fs::remove_all(dir);
}
