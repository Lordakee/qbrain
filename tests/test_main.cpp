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
  };
  for (auto& t : tests) {
    try {
      t.f();
      std::cout << "[PASS] " << t.n << "\n";
    } catch (const std::exception& e) {
      std::cout << "[FAIL] " << t.n << ": " << e.what() << "\n";
      ++failed;
    }
  }
  return failed ? 1 : 0;
}
