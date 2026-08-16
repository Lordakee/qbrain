// N37: packaging & data-root closeout tests (single registration
// `n37_packaging`). Unit groups: version constants (single source of truth
// include/qbrain/version.hpp must equal the 2.0.0 release, matching the
// CMakeLists.txt project VERSION), data-root path resolution under an
// isolated LOCALAPPDATA override (%LOCALAPPDATA%\Qbrain\brains\<id>\
// brain.db structure), and brain-id normalization (case folding, length
// bound, charset allow-list, Windows reserved device names, and rejection of
// hostile traversal/drive/slash ids — asserting the ACTUAL behavior of
// src/qbrain/util/paths.cpp: hostile ids throw std::runtime_error).
#include "qbrain/util/paths.hpp"
#include "qbrain/version.hpp"

#include <cstdio>
#include <filesystem>
#include <functional>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

namespace fs = std::filesystem;

int n37_failures = 0;
#define N37_CHECK(cond)                                                                \
  do {                                                                                 \
    if (!(cond)) {                                                                     \
      std::printf("[FAIL] n37: CHECK failed: %s @ %s:%d\n", #cond, __FILE__, __LINE__); \
      ++n37_failures;                                                                  \
    }                                                                                  \
  } while (0)

template <typename F>
bool n37_throws(F&& f) {
  try {
    f();
    return false;
  } catch (const std::exception&) {
    return true;
  }
}

// Scoped LOCALAPPDATA override so path resolution is deterministic and the
// test never touches the real %LOCALAPPDATA%\Qbrain data root. Paths under
// test are computed, never created, so the override target needs no
// filesystem existence.
struct N37ScopedLocalAppData {
  explicit N37ScopedLocalAppData(const std::string& value) {
#ifdef _WIN32
    if (const char* prev = std::getenv("LOCALAPPDATA")) {
      previous_ = prev;
      had_ = true;
    }
    _putenv_s("LOCALAPPDATA", value.c_str());
#else
    (void)value;
#endif
  }
  ~N37ScopedLocalAppData() {
#ifdef _WIN32
    _putenv_s("LOCALAPPDATA", had_ ? previous_.c_str() : "");
#endif
  }

 private:
  std::string previous_;
  bool had_ = false;
};

std::string n37_to_generic(const fs::path& p) {
  // Compare structure without depending on the separator spelling.
  return p.generic_string();
}

}  // namespace

void test_n37_packaging() {
  // --- Version constants (D2): single source of truth include/qbrain/version.hpp
  // must carry 2.0.0, synchronized with the CMakeLists.txt project VERSION
  // (consistency with CMake is additionally asserted by the N37 hard audit).
  N37_CHECK(QBRAIN_VERSION_MAJOR == 2);
  N37_CHECK(QBRAIN_VERSION_MINOR == 0);
  N37_CHECK(QBRAIN_VERSION_PATCH == 0);

  // --- Data-root resolution structure (D3) under an isolated LOCALAPPDATA
  // override (src/qbrain/util/paths.cpp honors the env override first).
  {
    const fs::path isolated_root = fs::temp_directory_path() / "qbrain-n37-isolated-root";
    N37ScopedLocalAppData guard(qbrain::util::path_to_utf8(isolated_root));
    const fs::path qroot = qbrain::util::qbrain_root();
    N37_CHECK(qroot == isolated_root / "Qbrain");
    N37_CHECK(qbrain::util::brains_root() == qroot / "brains");
    N37_CHECK(qbrain::util::config_path() == qroot / "config.json");
    N37_CHECK(qbrain::util::audit_dir() == qroot / "audit");

    // A known brain id maps under the data root with the documented structure:
    // %LOCALAPPDATA%\Qbrain\brains\<normalized-id>\brain.db
    const fs::path dir = qbrain::util::brain_dir("ProjX");
    N37_CHECK(dir == qroot / "brains" / "projx");  // id lowercased
    N37_CHECK(qbrain::util::brain_db_path("ProjX") == dir / "brain.db");
    N37_CHECK(n37_to_generic(qbrain::util::brain_db_path("ProjX")) ==
              n37_to_generic(isolated_root / "Qbrain" / "brains" / "projx" / "brain.db"));
  }

  // --- Brain-id normalization: accepted forms (charset [a-z0-9_-] after
  // A-Z folding; 64 bytes max).
  N37_CHECK(qbrain::util::normalize_brain_id("Proj-X_2") == "proj-x_2");
  N37_CHECK(qbrain::util::normalize_brain_id("ALLCAPS") == "allcaps");
  N37_CHECK(qbrain::util::normalize_brain_id("0123456789") == "0123456789");
  N37_CHECK(qbrain::util::normalize_brain_id(std::string(64, 'a')).size() == 64);

  // --- Hostile ids: ACTUAL implemented behavior is rejection via
  // std::runtime_error("invalid brain id") — never silent sanitization or
  // path escape.
  N37_CHECK(n37_throws([] { qbrain::util::normalize_brain_id(""); }));
  N37_CHECK(n37_throws([] { qbrain::util::normalize_brain_id(std::string(65, 'a')); }));
  N37_CHECK(n37_throws([] { qbrain::util::normalize_brain_id(".."); }));            // traversal
  N37_CHECK(n37_throws([] { qbrain::util::normalize_brain_id("c:"); }));            // drive letter
  N37_CHECK(n37_throws([] { qbrain::util::normalize_brain_id("C:\\evil"); }));      // drive + path
  N37_CHECK(n37_throws([] { qbrain::util::normalize_brain_id("a/b"); }));           // posix slash
  N37_CHECK(n37_throws([] { qbrain::util::normalize_brain_id("a\\b"); }));          // backslash
  N37_CHECK(n37_throws([] { qbrain::util::normalize_brain_id("a b"); }));           // space
  N37_CHECK(n37_throws([] { qbrain::util::normalize_brain_id("a.b"); }));           // dot
  N37_CHECK(n37_throws([] { qbrain::util::normalize_brain_id("a%b"); }));           // percent
  N37_CHECK(n37_throws([] { qbrain::util::normalize_brain_id("con"); }));           // reserved device
  N37_CHECK(n37_throws([] { qbrain::util::normalize_brain_id("COM1"); }));
  N37_CHECK(n37_throws([] { qbrain::util::normalize_brain_id("lpt9"); }));
  // Hostile ids must not escape the brains root via brain_dir either.
  N37_CHECK(n37_throws([] { (void)qbrain::util::brain_dir(".."); }));
  N37_CHECK(n37_throws([] { (void)qbrain::util::brain_db_path("C:\\evil"); }));

  // utf8<->path round trip is lossless for ASCII (used for env overrides).
  N37_CHECK(qbrain::util::utf8_to_path(qbrain::util::path_to_utf8(fs::path("Qbrain")))
                == fs::path("Qbrain"));

  if (n37_failures == 0) std::printf("[n37] packaging assertions all passed\n");
}
