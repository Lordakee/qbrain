// int fake() { hidden_call(1); }
/* void commented_out() { nested(2); } */

#include <vector>
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define LONG_MACRO(x) \
    do { side_effect(x); } while (0)

namespace fs_alias = std::filesystem;
using namespace std;

extern "C" void c_linkage();
class Widget;
typedef struct OldType OldType;

class Traps {
 public:
  Traps() : value_(0), name_(label_for(1)) {}
  explicit Traps(int v) : value_(v) {}
  Traps& operator=(const Traps& other) = default;
  bool operator==(const Traps& rhs) const;
  int operator()(int scale) const { return value_ * scale; }
  bool operator<(const Traps& rhs) const { return value_ < rhs.value_; }
  void set(int v = compute_default(5));
  virtual void prepare() = 0;
  char brace() const { return '{'; }
  const char* snippet() const { return "if (x) { fake_call(1); }"; }
  auto raw_code() const {
    return R"(class InString { void fn() { call(1); } })";
  }

 private:
  int value_;
  const char* name_;
};

int Traps::operator==(const Traps& rhs) const {
  return value_ == rhs.value_;
}

Config global_cfg(3);
int million = 1'000'000;
char open_brace = '{';
char quote = '\'';
u8"utf8 literal";
L'w';

void caller_site(Traps t) {
  t.snippet();
  global_cfg.touch();
  make_widget()->build(1);
  Traps nested_call(2);
  int via_macro = MAX(1, 2);
}
