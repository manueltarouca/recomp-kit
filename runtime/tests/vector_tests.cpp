// Floating conversion edges checked against architectural integer-indefinite
// values, including exception status Unicorn does not faithfully expose.
#include "../x86.h"
#include <cstdio>
#include <limits>
#include <initializer_list>

static int checks, failures;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(x)) {                                                                                \
            ++failures;                                                                            \
            std::fprintf(stderr, "%d: %s\n", __LINE__, #x);                                        \
        }                                                                                          \
    } while (0)
int main() {
    // Model an ARM-generated positive NaN independently of the host CPU.
    const float positive_nan = sse_float(0x7fc00000u);
    CHECK(sse_arithmetic_bits(0.0f, -0.0f, positive_nan) == 0xffc00000u);
    CHECK(sse_arithmetic_bits(sse_float(0x7f800000u), sse_float(0x7f800000u), positive_nan) ==
          0xffc00000u);
    CHECK(sse_arithmetic_bits(sse_float(0x7fc01234u), 1.0f, sse_float(0x7fc01234u)) == 0x7fc01234u);
    CHECK(sse_arithmetic_bits(1.0f, sse_float(0xff801234u), sse_float(0xffc01234u)) == 0xffc01234u);
    CHECK(sse_arithmetic_bits(1.0f, 2.0f, 0.5f) == 0x3f000000u);
    CHECK(sse_arithmetic_bits(-0.0f, 1.0f, -0.0f) == 0x80000000u);
    X86 c{};
    CHECK(sse_mxcsr(&c) == 0x1f80);
    const int32_t positive[] = {2, 1, 2, 1}, negative[] = {-2, -2, -1, -1};
    for (uint32_t rc = 0; rc < 4; ++rc) {
        c.mxcsr_xor = rc << 13;
        CHECK(sse_i32(&c, 1.5, 0) == uint32_t(positive[rc]));
        CHECK(sse_i32(&c, -1.5, 0) == uint32_t(negative[rc]));
        CHECK(sse_i32(&c, 1.5, 1) == 1);
        CHECK(sse_i32(&c, -1.5, 1) == uint32_t(-1));
        CHECK(sse_mxcsr(&c) == (0x1fa0u | rc << 13)); // precision/inexact
    }
    c.mxcsr_xor = 0;
    CHECK(sse_i32(&c, 2.5, 0) == 2 && sse_i32(&c, 3.5, 0) == 4);
    for (double invalid :
         {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
          -std::numeric_limits<double>::infinity(), 2147483648.0, -2147483649.0}) {
        c.mxcsr_xor = 0;
        CHECK(sse_i32(&c, invalid, 1) == 0x80000000u);
        CHECK(sse_mxcsr(&c) == 0x1f81); // masked invalid
    }
    c.mxcsr_xor = 0;
    CHECK(sse_i32(&c, -2147483648.0, 1) == 0x80000000u && sse_mxcsr(&c) == 0x1f80);
    c.fpu_cw = 0x0b7f;
    c.fpu_top = 3;
    c.fpu_tag = 0xffff;
    fset(&c, 0, -2.75);
    CHECK(fisttp_i16(&c) == -2 && fisttp_i32(&c) == -2 && fisttp_i64(&c) == -2);
    CHECK(c.fpu_cw == 0x0b7f && c.fpu_top == 3);
    fset(&c, 0, std::numeric_limits<double>::quiet_NaN());
    CHECK(fisttp_i16(&c) == INT16_MIN && fisttp_i32(&c) == INT32_MIN &&
          fisttp_i64(&c) == INT64_MIN);
    CHECK(c.fpu_sw & 1);
    fset(&c, 0, 9223372036854775808.0);
    CHECK(fisttp_i64(&c) == INT64_MIN);
    c.st_exact[c.fpu_top] = 1;
    c.st_bits[c.fpu_top] = 0x7fffffffffffffffull;
    CHECK(fisttp_i64(&c) == INT64_MAX);
    std::printf("vector: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
