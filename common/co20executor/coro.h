#pragma once

#include <coroutine>

#include "executor.h"

// 标志类型，用于标记Yield动作
struct Yield {};

// 全局executor，懒得搞了
static Executor executor;

// Yield动作的定义
struct YieldAwaiter {
    // 进入必定挂起
    bool await_ready() const noexcept { return false; }
    // 挂起后把caller加入调度器
    void await_suspend(std::coroutine_handle<> handle) noexcept  {
        executor.yield();
    }
    // 没有返回值
    void await_resume() noexcept {}
};

template <typename ValType>
struct Task {
    struct promise_type {
        // 构造了一个promise后，如何构造包装的generator
        // 由于Gen这个类型里只有一个handle，zh鳄梨可以直接朴素构造
        auto get_return_object() noexcept  {
            return Task{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        // 记录调用者
        std::coroutine_handle<> caller = {};
        // 记录返回值
        ValType store;

        // 进门先挂起
        auto initial_suspend() noexcept { return std::suspend_always{}; }

        // 调用里如果co_return了
        template <std::convertible_to<ValType> T>
        std::suspend_never return_value(T &&x) noexcept  {
            // 保存结果
            store = std::move(x);
            // 如果有调用者就恢复调用者
            if (caller) {
                executor.exchange(caller);
            } else {
                executor.done();
            }
            std::atomic_signal_fence(std::memory_order_release);
            // 不挂起（suspend never）
            return {};
        }
        // 协程执行完了析构
        auto final_suspend() noexcept { return std::suspend_never{}; }
        // 异常了直接退
        void unhandled_exception() noexcept  { exit(1); }

        // 如果需要co_await一个Yield类型
        // 把自己也变形成YieldAwaiter
        // YieldAwaiter在await时会把自己塞入executor排队，轮到自己时执行回调由executor唤醒自己
        YieldAwaiter await_transform(Yield) noexcept  { return {}; }

        // 其他情况，co_await一个Task，就是调用了
        template <CoroTask T>
        auto await_transform(T &&expr)  noexcept {
            auto hd = expr.handle;
            expr.handle.promise().caller =
                std::coroutine_handle<promise_type>::from_promise(*this);
            // 需要把新调用加入executor
            executor.exchange(hd);
            return std::move(expr);
        }
    };

    std::coroutine_handle<promise_type> handle;

    // 构造时先挂起
    bool await_ready()  noexcept { return false; }

    // 挂起时啥都不用干……因为有transform
    void await_suspend(std::coroutine_handle<>)  noexcept {}

    // 返回值从promise里拿
    ValType await_resume()  noexcept { return std::move(handle.promise().store); }
};