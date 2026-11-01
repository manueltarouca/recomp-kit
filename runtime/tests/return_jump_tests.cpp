// A computed return must resume its pending C caller exactly once, even if
// the guest continuation is also callable as an alternate block entry.
#include "../memory.h"
#include <cstdio>

extern "C" void fn_0d02b000(X86 *);
extern "C" void fn_0d02b004(X86 *);
static constexpr uint32_t continuation = 0x0d02b080u, branch = 0x0d02b090u;
static unsigned transfers, checks, failures;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(x)) {                                                                                \
            ++failures;                                                                            \
            std::fprintf(stderr, "%d: %s\n", __LINE__, #x);                                        \
        }                                                                                          \
    } while (0)

extern "C" int recomp_is_call_return(uint32_t target) {
    return target == continuation;
}
extern "C" int32_t recomp_index_of(uint32_t target) {
    return target == continuation || target == branch ? 0 : -1;
}
extern "C" void recomp_call(X86 *c, uint32_t target) {
    ++transfers;
    c->eip = target;
    ++c->r[R_ESI]; // Model the callable continuation running in this C frame.
}
extern "C" void recomp_jump(X86 *c, uint32_t target) {
    recomp_call(c, target); // Entry dispatch precedes the non-entry return fallback.
}

int main() {
    mem_init();
    const uint32_t stack = GUEST_STACK_TOP - 256;
    for (uint32_t count = 0; count < 9; ++count) {
        X86 cpu = {};
        cpu.r[R_ESP] = stack;
        cpu.r[R_EBP] = stack + 128;
        cpu.r[R_EDX] = count;
        wr32(stack, continuation);
        transfers = 0;
        fn_0d02b000(&cpu);
        ++cpu.r[R_ESI]; // The pending caller now resumes at its continuation.
        CHECK(cpu.r[R_ESI] == 1 && transfers == 0);
        CHECK(cpu.r[R_ESP] == stack + 4 + count * 4);
        CHECK(cpu.r[R_EBP] == stack + 128 && cpu.eip == continuation);
    }
    // The interior entry lacks the prologue. Its popped destination is not
    // proven to be its caller's return; it must still dispatch an ordinary jump.
    const uint32_t destinations[] = {branch, continuation};
    for (uint32_t target : destinations) {
        X86 cpu = {};
        cpu.r[R_ESP] = stack;
        wr32(stack, 2); // Saved argument count.
        wr32(stack + 4, 0);
        wr32(stack + 8, target);
        transfers = 0;
        fn_0d02b004(&cpu);
        CHECK(transfers == 1 && cpu.r[R_ESI] == 1 && cpu.eip == target);
        CHECK(cpu.r[R_ESP] == stack + 20);
    }
    mem_shutdown();
    std::printf("Return jumps: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
