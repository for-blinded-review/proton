#include <stdio.h>
#include <inttypes.h>
#include <utility>
#include <new>
#include "common/protonlib/gen.h"

__attribute__((noinline))
__attribute__((preserve_none))
uint64_t seq_gen(GCTX* fp, uint64_t c) {
    // uint64_t padding[100] = {0};
    // *(volatile uint64_t*)&padding[c%100] = c;
    GPromise g(fp);
    // while(likely(c >= 8)) {
    //     g.yield(c--); g.yield(c--); g.yield(c--); g.yield(c--);
    //     g.yield(c--); g.yield(c--); g.yield(c--); g.yield(c--);
    // }
    while(c) {
        g.yield(c--);
    }
    return 0;
}

uint64_t make_move(uint64_t n, uint64_t f, uint64_t t) {
    return n + (f << 8) + (t << 16);
}

__attribute__((noinline))
__attribute__((preserve_none))
void _Hanoi(GPromise& g, char n, char f, char t, char a) {
    if (n == 0) return;
    _Hanoi(g, n-1, f, a, t);
    g.yield(make_move(n, f, t));
    _Hanoi(g, n-1, a, t, f);
}

__attribute__((noinline))
__attribute__((preserve_none))
uint64_t Hanoi(GCTX* fp, char n) {
    GPromise g(fp);
    _Hanoi(g, n, 'a', 'b', 'c');
    return 0;
}

/*
__attribute__((noinline))
__attribute__((preserve_none))
void seq_gen2(GPromise g, uint64_t c) {
    seq_gen(g, c);
}


__attribute__((noinline))
__attribute__((preserve_none))
void seq_gen3(GPromise g, uint64_t c) {
    seq_gen2(g, c);
}
*/

__attribute__((noinline))
__attribute__((preserve_none))
uint64_t testy(uint64_t c) {
    uint64_t sum = 0;
    Generator g(&Hanoi, (char)c);
    for (;likely(g); g.resume()) {
        sum++;
    }
    return sum;
    // printf("sum=%llu\n", sum);
}

__attribute__((noinline))
__attribute__((preserve_none))
uint64_t testx(uint64_t c) {
    uint64_t sum = 0;
    Generator g(&seq_gen, c);
    for (;likely(g); g.resume())
        sum += g.value();
    return sum;
}

#ifdef RUN

int main() {
    // auto sum = testx(100000000);
    auto sum = testy(20);
    printf("sum=%llu\n", sum);
    return 0;
}

#endif
