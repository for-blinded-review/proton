#include <assert.h>

#include <chrono>
#include <cstdio>
#include <malloc.h>

#include "common/co20executor/coro.h"
#include "common/fibsched/boost-fibersched.h"
#include "common/protonlib/co.h"
#include "common/utility.h"

#include <boost/context/fiber.hpp>

ssize_t total = 0;

constexpr static int ROUND = 10;
constexpr static int CONCURRENT = 10;
constexpr static uint64_t SIZE = 10UL * 1024 * 1024;

// template <int X>
struct socket {
  // 所谓readable就直接走一遭yield好了
  // 由于executor只保留一个任务，所以就等于直接回到executor一轮
  [[gnu::noinline]] Yield wait_for_ready() noexcept { return {}; }
  [[gnu::noinline]] Task<ssize_t> io_some(void *buf, size_t count) {
    // 先wait for readable
    co_await wait_for_ready();
    // 假设读到个随机数字（1～16）
    // auto x = std::min((size_t)(1 + rand() % 16), count);
    auto x = 8;
    total += x;
    co_return x;
  }

  [[gnu::noinline]] Task<ssize_t> io_fully(void *buf, size_t count) {
    size_t size = 0;
    while (count) {
      ssize_t s = co_await io_some(buf, count);
      if (s < 0)
        co_return s;
      else if (s == 0)
        co_return size;
      assert(s <= count);
      count -= s;
      size += s;
      (char *&)buf += s;
    }
    co_return size;
  }
};

FiberSched fsc;

// template <int X>
struct bsocket {
  [[gnu::noinline]] void wait_for_ready() { fsc.yield(); }
  [[gnu::noinline]] ssize_t io_some(void *buf, size_t count) {
    wait_for_ready();
    // auto x = std::min((size_t)(1 + rand() % 16), count);
    auto x = 8;
    total += x;
    return x;
  }
  [[gnu::noinline]] ssize_t io_fully(void *buf, size_t count) {
    size_t size = 0;
    while (count) {
      ssize_t s = io_some(buf, count);
      if (s < 0)
        return s;
      else if (s == 0)
        return size;
      assert(s <= count);
      count -= s;
      size += s;
      (char *&)buf += s;
    }
    return size;
  }
};

// template <int X>
struct psocket {
  [[gnu::noinline]]
#if defined(NEW) || defined(CACSTH)
  __attribute__((preserve_none))
#endif
  void
  wait_for_ready() {
    proton::thread_yield();
  }

  [[gnu::noinline]]
#if defined(NEW) || defined(CACSTH)
  __attribute__((preserve_none))
#endif
  ssize_t
  io_some(void *buf, size_t count) {
    wait_for_ready();
    // auto x = std::min((size_t)(1 + rand() % 16), count);
    auto x = 8;
    total += x;
    return x;
  }

  [[gnu::noinline]] ssize_t io_fully(void *buf, size_t count) {
    size_t size = 0;
    while (count) {
      ssize_t s = io_some(buf, count);
      if (s < 0)
        return s;
      else if (s == 0)
        return size;
      assert(s <= count);
      count -= s;
      size += s;
      (char *&)buf += s;
    }
    return size;
  }
};

// template <int X> void *ptask(void *arg) {
//   auto s = (psocket<X> *)arg;
//   s->io_fully(nullptr, SIZE);
//   return nullptr;
// }

void *ptask(void *arg) {
  auto s = (psocket *)arg;
  s->io_fully(nullptr, SIZE);
  return nullptr;
}

// template <int X, template <int> typename T>
// struct TypeArr : public TypeArr<X - 1, T> {
//   T<X> member;

//   template <typename Func> void foreach (Func &&f) {
//     f(X, member);
//     TypeArr<X - 1, T>::foreach (std::move(f));
//   }
// };

// template <template <int> typename T> struct TypeArr<0, T> {
//   template <typename Func> void foreach (Func &&f) {}
// };

template <int X, typename T> struct TypeArr : public TypeArr<X - 1, T> {
  T member;

  template <typename Func> void foreach (Func &&f) {
    f(X, member);
    TypeArr<X - 1, T>::foreach (std::move(f));
  }
};

template <typename T> struct TypeArr<0, T> {
  template <typename Func> void foreach (Func &&f) {}
};

template <int N, typename Sock, typename RTask, typename Task>
void test(const char *name, RTask &&ready, Task &&task, uint64_t &time_spent) {
  TypeArr<N, Sock> s;
  total = 0;
  ready(s);
  auto start = std::chrono::high_resolution_clock::now();
  task(s);
  auto end = std::chrono::high_resolution_clock::now();
  printf("%s\t%lld\t%ld\n", name,
         std::chrono::duration_cast<std::chrono::microseconds>(end - start)
             .count(),
         total);
  time_spent +=
      std::chrono::duration_cast<std::chrono::microseconds>(end - start)
          .count();
}

template <typename C> struct get_type_tag;

template <template <int> class C, int T> struct get_type_tag<C<T>> {
  constexpr static int tag = T;
};

int main() {
  mallopt(M_TRIM_THRESHOLD, -1);
  uint64_t tc = 0;
  uint64_t tb = 0;
  uint64_t tp = 0;
  for (auto turn = 0; turn < ROUND; turn++) {
    // executor.clear();
    // test<CONCURRENT, socket>(
    //     "Coro20",
    //     [&](auto &s) {
    //       s.foreach ([&](int, auto &sock) {
    //         executor.schedule(sock.io_fully(nullptr, SIZE));
    //       });
    //     },
    //     [&](auto &s) { executor.all(); }, tc);
    // printf("Switch count = %lu\n", executor.switch_count());
    // fsc.clear();
    // test<CONCURRENT, bsocket>(
    //     "Boost",
    //     [&](auto &s) {
    //       s.foreach ([&](int t, auto &sock) {
    //         fsc.schedule([t, &sock]() {
    //           // auto size = 16 * (rand() % 32);
    //           // auto x = alloca(size);
    //           // // (void)(*(char*)x);
    //           // asm volatile("" : "+r"(x) :);
    //           // fsc.yield();
    //           sock.io_fully(nullptr, SIZE);
    //         });
    //       });
    //       // fsc.yield();
    //     },
    //     [&](auto &s) { fsc.all(); }, tb);
    // printf("Switch count = %lu\n", fsc.switch_count());
    std::vector<proton::join_handle *> jhs;
    test<CONCURRENT, psocket>(
        "proton",
        [&](auto &s) {
          s.foreach ([&](int, auto &sock) {
            // jhs.emplace_back(proton::thread_enable_join(proton::thread_create(
            //     ptask<get_type_tag<
            //         typename
            //         std::remove_reference<decltype(sock)>::type>::tag>,
            //     &sock)));
            jhs.emplace_back(proton::thread_enable_join(
                proton::thread_create(&ptask, &sock)));
          });
        },
        [&](auto) {
          for (auto &x : jhs) {
            proton::thread_join(x);
          }
        },
        tp);
    printf("Switch count = %lu\n", fsc.switch_count());
  }

  printf("Average rounds Coro20= %lu us, Boost= %lu us, proton= %lu us\n",
         tc / ROUND, tb / ROUND, tp / ROUND);
  return 0;
}