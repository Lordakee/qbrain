namespace math {

class Calculator {
 public:
  Calculator();
  Calculator(int seed);
  ~Calculator();
  int add(int a, int b);
  int add(int a);
  static Calculator* create();
  void clear() noexcept override;

 private:
  int total_ = 0;
};

struct Point {
  int x;
  int y;
};

class Nested {
 public:
  class Inner {
   public:
    void ping();
  };
  Inner inner_;
};

template <typename T>
T clamp_value(T v, T lo, T hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

template <typename T>
class Box {
 public:
  T value;
};

int Calculator::add(int a, int b) {
  return a + b;
}

void demo(Point p, Calculator c) {
  int sum = c.add(p.x, p.y);
  clamp_value(sum, 0, 10);
  Calculator::create();
}

void demo() {
  demo(Point{1, 2}, Calculator(3));
}

}  // namespace math
