#include <boost/coroutine2/all.hpp>
#include <stdio.h>
#include <chrono>
#include <ratio>

/// Search for --- to skip over generator code

#include <coroutine>
#include "g.h"

// template <typename T> struct generator {
//   struct promise_type {
//     T current_value;
//     std::suspend_always yield_value(T value) {
//       this->current_value = value;
//       return {};
//     }
//     std::suspend_always initial_suspend() { return {}; }
//     std::suspend_always final_suspend() noexcept { return {}; }
//     generator get_return_object() { return generator{this}; };
//     void unhandled_exception() { std::terminate(); }
//     void return_void() {}
//   };

//   struct iterator {
//     std::coroutine_handle<promise_type> _Coro;
//     bool _Done;

//     iterator(std::coroutine_handle<promise_type> Coro, bool Done)
//         : _Coro(Coro), _Done(Done) {}

//     iterator &operator++() {
//       _Coro.resume();
//       _Done = _Coro.done();
//       return *this;
//     }

//     bool operator==(iterator const &_Right) const {
//       return _Done == _Right._Done;
//     }

//     bool operator!=(iterator const &_Right) const { return !(*this == _Right); }
//     T const &operator*() const { return _Coro.promise().current_value; }
//     T const *operator->() const { return &(operator*()); }
//   };

//   iterator begin() {
//     p.resume();
//     return {p, p.done()};
//   }

//   iterator end() { return {p, true}; }

//   generator(generator const&) = delete;
//   generator(generator &&rhs) : p(rhs.p) { rhs.p = nullptr; }

//   ~generator() {
//     if (p)
//       p.destroy();
//   }

// private:
//   explicit generator(promise_type *p)
//       : p(std::coroutine_handle<promise_type>::from_promise(*p)) {}

//   std::coroutine_handle<promise_type> p;
// };

//// --- stackful coroutines

typedef boost::coroutines2::coroutine<int> coro_t;

void producer(coro_t::push_type &sink, int count) {
  for (; count != 0; --count)
    sink(count);
}

int reduce(coro_t::pull_type &source)
{
  int sum = 0;
  for (auto v: source)
    sum += v;
  return sum;
}

void test1(int iters) {
  puts("testing stackful... ");
  coro_t::pull_type p([iters](auto& out) { producer(out, iters); });
  auto result = reduce(p);
  printf("=> %d\n", result);
}

//// --- stackless coroutines

// // comment out noinline and see what happens
// __attribute__((noinline)) 
// generator<int> producer(int count) {
//   for (; count != 0; --count)
//     co_yield count;
//   // while (count) {
//     // auto x = count --;
//   //   co_yield count;
//   //   count --;
//   // }
// }
int reduce(generator<int> &source)
{
  int sum = 0;
  for (auto v: source)
    sum += v;
  return sum;
}

// comment out noinline and see what happens
__attribute__((noinline)) 
void test2(int iters) {
  puts("testing stackless... ");
  auto p = producer(iters);
  auto result = reduce(p);
  printf("=> %d\n", result);
}

using namespace std::chrono;

using dur = duration<double>;
using hpc = high_resolution_clock;

int main() {
  int count = 100'000;
  int nano = 1'000'000'000;
  try {
    dur elapsed1, elapsed2;
    {
      auto start = hpc::now();
      test1(count);
      auto stop = hpc::now();
      elapsed1 = duration_cast<dur>(stop - start);
      printf("elapsed %g ms\n", elapsed1.count()*1000);
    }
    {
      auto start = hpc::now();
      test2(count);
      auto stop = hpc::now();
      elapsed2 = duration_cast<dur>(stop - start);
      printf("elapsed %g\n", elapsed2.count()*1000);
    }
    printf("stackless are %g times faster\n", elapsed1.count() / elapsed2.count());
    printf("stackful consume %g ns per iteration\n", elapsed1.count() / count * nano);
    printf("stackless consume %g ns per iteration\n", elapsed2.count() / count * nano);
  } catch (...) {
    puts("caught something");
  }
}