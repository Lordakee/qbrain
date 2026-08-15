#include "qbrain/codeintel/scan.hpp"
#include "qbrain/core/brain.hpp"
#include "qbrain/ops/registry.hpp"
#include "qbrain/schema/packs.hpp"
#include "qbrain/util/paths.hpp"
#include "qbrain/util/time_util.hpp"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <process.h>
#include <random>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#define QB_CHECK(cond)                                                  \
  do {                                                                  \
    if (!(cond)) {                                                      \
      throw std::runtime_error(std::string("CHECK failed: ") + #cond);  \
    }                                                                   \
  } while (0)

namespace {

class ScopedTestDirectory {
 public:
  ScopedTestDirectory()
      : temp_parent_(resolve_temp_parent()), path_(create_unique(temp_parent_)) {
    if (!is_valid_test_root(path_, temp_parent_)) {
      throw std::runtime_error("created test directory failed validation");
    }
  }

  ~ScopedTestDirectory() noexcept {
    if (!is_valid_test_root(path_, temp_parent_)) return;
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }

  ScopedTestDirectory(const ScopedTestDirectory&) = delete;
  ScopedTestDirectory& operator=(const ScopedTestDirectory&) = delete;

  const std::filesystem::path& path() const noexcept { return path_; }

 private:
  static constexpr const char* kDirectoryPrefix = "qbrain_n2023_";

  static std::filesystem::path resolve_temp_parent() {
    namespace fs = std::filesystem;
    std::error_code error;
    const auto parent = fs::temp_directory_path(error);
    if (error) {
      throw std::runtime_error("failed to resolve test temporary directory");
    }

    const auto canonical_parent = fs::weakly_canonical(parent, error);
    if (error || !fs::is_directory(canonical_parent, error) || error) {
      throw std::runtime_error("test temporary directory is not a directory");
    }
    return canonical_parent;
  }

  static std::filesystem::path create_unique(const std::filesystem::path& parent) {
    namespace fs = std::filesystem;
    const auto process_id = static_cast<unsigned int>(_getpid());
    std::random_device random;

    for (std::uint32_t attempt = 0; attempt < 64; ++attempt) {
      const auto timestamp = static_cast<std::uint64_t>(
          std::chrono::high_resolution_clock::now().time_since_epoch().count());
      const auto random_high = static_cast<std::uint64_t>(random());
      const auto random_low = static_cast<std::uint64_t>(random());
      const auto nonce = (random_high << 32U) ^ random_low ^ attempt;
      const auto candidate =
          parent / (std::string(kDirectoryPrefix) + std::to_string(process_id) + "_" +
                    std::to_string(timestamp) + "_" + std::to_string(nonce));
      std::error_code create_error;
      if (fs::create_directory(candidate, create_error)) {
        return candidate;
      }
      if (create_error) {
        throw std::runtime_error("failed to create unique test directory");
      }
    }
    throw std::runtime_error("failed to allocate unique test directory");
  }

  static bool is_valid_test_root(const std::filesystem::path& path,
                                 const std::filesystem::path& temp_parent) noexcept {
    try {
      namespace fs = std::filesystem;
      std::error_code error;
      const auto canonical_parent = fs::weakly_canonical(path.parent_path(), error);
      if (error || canonical_parent != temp_parent) return false;

      const auto name = path.filename().string();
      const std::string prefix(kDirectoryPrefix);
      if (name.size() <= prefix.size() || name.compare(0, prefix.size(), prefix) != 0) {
        return false;
      }

      const auto status = fs::symlink_status(path, error);
      return !error && fs::is_directory(status) && !fs::is_symlink(status);
    } catch (...) {
      return false;
    }
  }

  std::filesystem::path temp_parent_;
  std::filesystem::path path_;
};

class ScopedEnvironmentVariable {
 public:
  ScopedEnvironmentVariable(std::string name, const std::string& value)
      : name_(std::move(name)) {
    char* previous = nullptr;
    std::size_t previous_size = 0;
    if (_dupenv_s(&previous, &previous_size, name_.c_str()) != 0) {
      throw std::runtime_error("failed to read test environment variable");
    }
    if (previous != nullptr) {
      previous_ = previous;
      std::free(previous);
    }
    if (_putenv_s(name_.c_str(), value.c_str()) != 0) {
      throw std::runtime_error("failed to set test environment variable");
    }
  }

  ~ScopedEnvironmentVariable() {
    _putenv_s(name_.c_str(), previous_ ? previous_->c_str() : "");
  }

  ScopedEnvironmentVariable(const ScopedEnvironmentVariable&) = delete;
  ScopedEnvironmentVariable& operator=(const ScopedEnvironmentVariable&) = delete;

 private:
  std::string name_;
  std::optional<std::string> previous_;
};

}  // namespace

void test_n20_23() {
  ScopedTestDirectory test_directory;
  const auto& dir = test_directory.path();
  ScopedEnvironmentVariable local_app_data(
      "LOCALAPPDATA", qbrain::util::path_to_utf8(dir / "localappdata"));
  auto dbp = dir / "brain.db";

  qbrain::ops::register_builtin_ops();
  qbrain::Brain b("n2023");
  b.open_at(qbrain::util::path_to_utf8(dbp));

  // N20 packs
  qbrain::schema::ensure_default_pack();
  auto packs = qbrain::schema::list_packs(b);
  QB_CHECK(!packs.empty());
  QB_CHECK(qbrain::schema::active_pack_id(b) == "default");
  auto raw = qbrain::schema::load_pack_json(b, "default");
  QB_CHECK(raw.find("default") != std::string::npos || raw.find("types") != std::string::npos);

  qbrain::ops::OpContext ctx;
  ctx.brain = &b;
  ctx.allow_write = true;
  auto lsp = qbrain::ops::global_registry().call("list_schema_packs", ctx);
  QB_CHECK(lsp.ok);
  auto ss = qbrain::ops::global_registry().call("schema_stats", ctx);
  QB_CHECK(ss.ok);

  // N21 takes
  QB_CHECK(b.put_take("entity/x", "X is important", "fact", 1.0) > 0);
  auto tl = b.takes_list("entity/x", 10);
  QB_CHECK(!tl.empty());
  auto ts = b.takes_search("important", 10);
  QB_CHECK(!ts.empty());
  auto tcal = qbrain::ops::global_registry().call("takes_list", ctx);
  QB_CHECK(tcal.ok);

  // seed code page for N22
  qbrain::PageInput in;
  in.slug = "code/foo";
  in.title = "Foo";
  in.type = "meeting";
  in.body = "void foo() {\n  bar();\n  baz();\n}\nvoid bar() {\n  baz();\n}\nvoid baz() { }\n";
  const auto code_page = b.put_page(in);
  const auto prior_year = std::stoi(qbrain::util::utc_date().substr(0, 4)) - 1;
  const auto history_timestamp = std::to_string(prior_year) + "-03-01 00:00:00";
  {
    auto set_history = b.db().prepare(
        "UPDATE pages SET created_at=?,updated_at=? WHERE id=?");
    set_history.bind_text(1, history_timestamp);
    set_history.bind_text(2, history_timestamp);
    set_history.bind_int(3, code_page.id);
    set_history.step_done();
    QB_CHECK(b.db().changes() == 1);
  }
  auto callees = qbrain::codeintel::find_callees(b, "foo", 20, 50);
  QB_CHECK(!callees.empty());
  bool saw_bar = false;
  bool saw_baz = false;
  for (auto& c : callees) {
    if (c.kind.find("bar") != std::string::npos) saw_bar = true;
    if (c.kind.find("baz") != std::string::npos) saw_baz = true;
  }
  QB_CHECK(saw_bar && saw_baz);

  auto flow = qbrain::codeintel::find_flow(b, "foo", 2, 20, 50);
  QB_CHECK(!flow.empty());
  bool flow_has_bar_or_baz = false;
  for (auto& f : flow) {
    if (f.kind.find("flow:") != std::string::npos &&
        (f.kind.find("bar") != std::string::npos || f.kind.find("baz") != std::string::npos)) {
      flow_has_bar_or_baz = true;
    }
  }
  QB_CHECK(flow_has_bar_or_baz);

  auto blast = qbrain::codeintel::find_blast(b, "foo", 40, 50);
  QB_CHECK(!blast.empty());
  bool blast_has_def = false;
  bool blast_has_callee = false;
  for (auto& h : blast) {
    if (h.kind == "def" || h.kind.find("def") != std::string::npos) blast_has_def = true;
    if (h.kind.find("callee:") != std::string::npos) blast_has_callee = true;
  }
  QB_CHECK(blast_has_def || blast_has_callee);
  qbrain::codeintel::clear_traversal_cache();

  // N23 chronicle
  auto otd = b.chronicle_on_this_day("03-01", 50);
  QB_CHECK(!otd.empty());
  for (auto& h : otd) {
    QB_CHECK(h.updated_at.size() >= 10 || h.created_at.size() >= 10);
    if (h.updated_at.size() >= 10) {
      QB_CHECK(h.updated_at[4] == '-' && h.updated_at[7] == '-');
    }
  }
  auto none = b.chronicle_on_this_day("01-01", 50);
  // may or may not be empty depending on run date; if non-empty every hit must match MM-DD
  for (auto& h : none) {
    bool match = false;
    if (h.updated_at.size() >= 10 && h.updated_at.substr(5, 5) == "01-01") match = true;
    if (h.created_at.size() >= 10 && h.created_at.substr(5, 5) == "01-01") match = true;
    QB_CHECK(match);
  }
  auto ls = b.chronicle_last_seen("code/foo");
  QB_CHECK(!ls.empty());
  QB_CHECK(ls.size() >= 10);
  QB_CHECK(ls[4] == '-' && ls[7] == '-');
  int bf = b.chronicle_backfill(10);
  QB_CHECK(bf >= 1);

  auto snap = b.status_snapshot();
  QB_CHECK(snap.schema_version >= 9);

  b.close();
}
