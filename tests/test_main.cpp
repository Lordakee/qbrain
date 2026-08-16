#include <iostream>
#include <vector>
#include <functional>
#include <string>

using TestFn = void (*)();
static std::vector<std::pair<const char*, TestFn>>& registry() {
  static std::vector<std::pair<const char*, TestFn>> r;
  return r;
}

struct Reg {
  Reg(const char* n, TestFn f) { registry().push_back({n, f}); }
};

#define QB_TEST(name)                         \
  void name();                                \
  static Reg reg_##name(#name, name);         \
  void name()

#define QB_CHECK(cond)                                                  \
  do {                                                                  \
    if (!(cond)) {                                                      \
      throw std::runtime_error(std::string("CHECK failed: ") + #cond +  \
                               " @ " + __FILE__ + ":" + std::to_string(__LINE__)); \
    }                                                                   \
  } while (0)

// declarations
void test_rrf();
void test_vector();
void test_chunker();
void test_extract();
void test_storage();
void test_mcp();
void test_rerank();
void test_minions();
void test_migration_v6();
void test_n12_dream();
void test_live_sync();
void test_n13();
void test_codeintel();
void test_analytics();
void test_n19();
void test_n20();
void test_n22();
void test_n23();
void test_n20_23();
void test_n24_25();
void test_n26_27();
void test_wave4();
void test_wave5();
void test_doctor();
void test_n14();
void test_n15();
void test_n16();
void test_n17();
void test_n18();
void test_n30_c_routing_storage();
void test_n30_b_auth_redaction();
void test_n31_c_negatives();
void test_n31_a_counts_mapping();
void test_n32_scan_integration();
void test_n34();
void test_n33_multimodal();
void test_n35_contract_suite();
void test_n36_token_scope();
void test_n37_packaging();
void test_n38_pg_backend();
void test_n39_rerank_config();

int main() {
  int failed = 0;
  struct T {
    const char* n;
    void (*f)();
  } tests[] = {
      {"rrf", test_rrf},
      {"vector", test_vector},
      {"chunker", test_chunker},
      {"extract", test_extract},
      {"storage", test_storage},
      {"mcp", test_mcp},
      {"rerank", test_rerank},
      {"minions", test_minions},
      {"migration_v6", test_migration_v6},
      {"n12_dream", test_n12_dream},
      {"live_sync", test_live_sync},
      {"n13", test_n13},
      {"codeintel", test_codeintel},
      {"analytics", test_analytics},
      {"n19", test_n19},
      {"n20", test_n20},
      {"n22", test_n22},
      {"n23", test_n23},
      {"n20_23", test_n20_23},
      {"n24_25", test_n24_25},
      {"n26_27", test_n26_27},
      {"wave4", test_wave4},
      {"wave5", test_wave5},
      {"doctor", test_doctor},
      {"n14", test_n14},
      {"n15", test_n15},
      {"n16", test_n16},
      {"n17", test_n17},
      {"n18", test_n18},
      {"n30_c_routing_storage", test_n30_c_routing_storage},
      {"n30_b_auth_redaction", test_n30_b_auth_redaction},
      {"n31_c_negatives", test_n31_c_negatives},
      {"n31_a_counts_mapping", test_n31_a_counts_mapping},
      {"n32_scan_integration", test_n32_scan_integration},
      {"n34", test_n34},
      {"n33_multimodal", test_n33_multimodal},
      {"n35_contract_suite", test_n35_contract_suite},
      {"n36_token_scope", test_n36_token_scope},
      {"n37_packaging", test_n37_packaging},
      {"n38_pg_backend", test_n38_pg_backend},
      {"n39_rerank_config", test_n39_rerank_config},
  };
  for (auto& t : tests) {
    try {
      t.f();
      std::cout << "[PASS] " << t.n << "\n" << std::flush;
    } catch (const std::exception& e) {
      std::cout << "[FAIL] " << t.n << ": " << e.what() << "\n" << std::flush;
      ++failed;
    }
  }
  return failed ? 1 : 0;
}
