#include "common/protonlib/co.h"
#include "utility.h"
#include <chrono>
#include <random>

static constexpr uint64_t THNUM = 1'000'000;
static constexpr uint64_t ROUND = 1;
static constexpr uint64_t TURN = 10;

template <uint64_t ROUND, typename Func> void test(Func &&func) {
  uint64_t sleep = 0;
  uint64_t interrupt = 0;
  uint64_t maxs = 0;
  uint64_t mins = -1;
  uint64_t maxi = 0;
  uint64_t mini = -1;
  for (int i = 0; i < ROUND; i++) {
    auto [x, y] = func();
    sleep += x;
    interrupt += y;
    if (x > maxs)
      maxs = x;
    if (x < mins)
      mins = x;
    if (y > maxi)
      maxi = y;
    if (y < mini)
      mini = y;
  }
  printf("Average yield %lf loop %lf\n",
         (sleep - maxs - mins) * 1.0 / (ROUND - 2) / THNUM,
         (interrupt - maxi - mini) * 1.0 / (ROUND - 2) / THNUM);
}

void *yielder(void *arg) {
  for (int i = 0; i < INT_MAX; i++) {
    proton::thread_yield();
  }
  return 0;
}

int main() {
  proton::init();
  DEFER(proton::fini());
  for (int i = 0; i < THNUM; i++)
    proton::thread_create(yielder, nullptr);
  test<TURN>([&]() -> std::tuple<uint64_t, uint64_t> {
    uint64_t total_sleep = 0;
    uint64_t total_interrupt = 0;
    auto start_sleep = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ROUND; i++) {
      proton::thread_yield();
    }
    auto start_interrupt = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ROUND * THNUM; i++) {
      asm volatile(""
                   :
                   :
                   : "rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rbp", "r8",
                     "r9", "r10", "r11", "r12", "r13", "r14", "r15");
    }
    auto emptyloop = std::chrono::high_resolution_clock::now();
    total_sleep += std::chrono::duration_cast<std::chrono::nanoseconds>(
                       (start_interrupt - start_sleep))
                       .count();

    total_interrupt += std::chrono::duration_cast<std::chrono::nanoseconds>(
                           (emptyloop - start_interrupt))
                           .count();

    printf("yield %lf loop %lf\n", total_sleep * 1.0 / ROUND,
           total_interrupt * 1.0 / ROUND);
    return {(total_sleep - total_interrupt) / ROUND, total_interrupt / ROUND};
  });
  return 0;
}