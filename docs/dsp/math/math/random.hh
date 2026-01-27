#pragma once

#include <cstdint>

namespace mo {
inline namespace math {

class XorShift32 {
 public:
  XorShift32() : x(2463534242u) {}

  std::uint32_t process() {
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
  }

 private:
  std::uint32_t x;
};

}  // namespace math
}  // namespace mo
