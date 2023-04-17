#pragma once

#include "common/list.h"
#include <boost/context/fiber.hpp>
#include <deque>
#include <iostream>

struct fibnode : public intrusive_list_node<fibnode> {
  boost::context::fiber fiber;

  fibnode(boost::context::fiber &&f) : fiber(std::move(f)) {}
  fibnode() = default;

  boost::context::fiber resume() { return std::move(fiber).resume(); }
};

class random_alloc {
private:
  std::size_t size_;
  std::size_t offset_;

public:
  random_alloc(std::size_t size) noexcept
      : size_(size), offset_(16 * (rand() % 32)) {}

  boost::context::stack_context allocate() {
    void *vp = std::malloc(size_);
    if (!vp) {
      throw std::bad_alloc();
    }
    boost::context::stack_context sctx;
    sctx.size = size_;
    sctx.sp = static_cast<char *>(vp) + sctx.size - offset_;
    return sctx;
  }

  void deallocate(boost::context::stack_context &sctx) noexcept {
    void *vp = static_cast<char *>(sctx.sp) - sctx.size + offset_;
    std::free(vp);
  }
};

class FiberSched {
protected:
  using fiber = boost::context::fiber;
  intrusive_list<fibnode> q;
  uint64_t count;

  void set_back(fiber &&t) {
    assert(!q.back()->fiber);
    if (t) {
      q.back()->fiber = std::move(t);
    }
  }
  auto get_next() {
    q.node = q.node->next();
    return q.node;
  }
  fiber && final() {
    // current node is now finished
    auto th = q.pop_front();
    delete th;
    count++;
    return std::move(q.node->fiber);
  }

public:
  FiberSched() { q.push_front(new fibnode()); }

  ~FiberSched() { q.delete_all(); }

  template <typename Func>
  void schedule(Func &&task) {
    // q.push_back(
    //     new fibnode(fiber(std::allocator_arg_t(), random_alloc(128 * 1024),
    //                       [this, task](fiber &&f) {
    //                         set_back(std::move(f));
    //                         task();
    //                         return final();
    //                       })));
    q.push_back(new fibnode(fiber([this, task](fiber &&f) {
      set_back(std::move(f));
      task();
      return final();
    })));
  }
  void yield();
  void all();
  uint64_t switch_count() { return count; }
  void clear() { count = 0; }
};