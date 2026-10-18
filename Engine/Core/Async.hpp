#pragma once

#include <chrono>
#include <condition_variable>
#include <coroutine>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace ld {

class ServiceError : public std::runtime_error {
public:
    explicit ServiceError(const std::string& message) : std::runtime_error(message) {}
    std::optional<std::string> code() const;
    std::string reason() const;
};

class Cancelled : public std::exception {
public:
    const char* what() const noexcept override { return "Operazione annullata"; }
};

std::string describe(std::exception_ptr error);
std::string readable(std::string_view message);
std::optional<std::string> messageCode(std::string_view message);

class Executor {
public:
    virtual ~Executor() = default;
    virtual void post(std::function<void()> job) = 0;
};

class ManualQueue final : public Executor {
public:
    void post(std::function<void()> job) override;
    std::size_t drain();
    bool pump(double seconds);

private:
    std::mutex lock;
    std::condition_variable signal;
    std::deque<std::function<void()>> jobs;
};

class ThreadPool final : public Executor {
public:
    explicit ThreadPool(unsigned threads);
    ~ThreadPool() override;
    void post(std::function<void()> job) override;

private:
    std::mutex lock;
    std::condition_variable signal;
    std::deque<std::function<void()>> jobs;
    std::vector<std::thread> workers;
    bool stopping = false;
};

Executor& mainQueue();
void setMainQueue(Executor* executor);
ManualQueue& fallbackQueue();
Executor& backgroundPool();
void after(double seconds, std::function<void()> job);

template <typename T>
class Async;

namespace detail {

struct PromiseBase {
    std::coroutine_handle<> continuation;
    std::exception_ptr error;
    bool detached = false;
    std::function<void(std::exception_ptr)> finished;

    struct FinalAwaiter {
        bool await_ready() noexcept { return false; }
        template <typename P>
        std::coroutine_handle<> await_suspend(std::coroutine_handle<P> handle) noexcept {
            auto& promise = handle.promise();
            if (promise.continuation) return promise.continuation;
            if (promise.detached) {
                auto finished = std::move(promise.finished);
                auto error = promise.error;
                handle.destroy();
                if (finished) finished(error);
            }
            return std::noop_coroutine();
        }
        void await_resume() noexcept {}
    };

    std::suspend_always initial_suspend() noexcept { return {}; }
    FinalAwaiter final_suspend() noexcept { return {}; }
    void unhandled_exception() { error = std::current_exception(); }
};

}

template <typename T>
class Async {
public:
    struct promise_type : detail::PromiseBase {
        std::optional<T> value;
        Async get_return_object() { return Async(std::coroutine_handle<promise_type>::from_promise(*this)); }
        template <typename V>
        void return_value(V&& v) { value.emplace(std::forward<V>(v)); }
    };

    Async() = default;
    explicit Async(std::coroutine_handle<promise_type> h) : handle(h) {}
    Async(Async&& other) noexcept : handle(std::exchange(other.handle, {})) {}
    Async& operator=(Async&& other) noexcept {
        if (this != &other) {
            if (handle) handle.destroy();
            handle = std::exchange(other.handle, {});
        }
        return *this;
    }
    Async(const Async&) = delete;
    ~Async() {
        if (handle) handle.destroy();
    }

    bool await_ready() const noexcept { return false; }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> caller) noexcept {
        handle.promise().continuation = caller;
        return handle;
    }
    T await_resume() {
        auto& p = handle.promise();
        if (p.error) std::rethrow_exception(p.error);
        return std::move(*p.value);
    }

    void start(std::function<void(std::exception_ptr)> finished = {}) {
        auto h = std::exchange(handle, {});
        h.promise().detached = true;
        h.promise().finished = std::move(finished);
        h.resume();
    }

private:
    std::coroutine_handle<promise_type> handle;
};

template <>
class Async<void> {
public:
    struct promise_type : detail::PromiseBase {
        Async get_return_object() { return Async(std::coroutine_handle<promise_type>::from_promise(*this)); }
        void return_void() {}
    };

    Async() = default;
    explicit Async(std::coroutine_handle<promise_type> h) : handle(h) {}
    Async(Async&& other) noexcept : handle(std::exchange(other.handle, {})) {}
    Async& operator=(Async&& other) noexcept {
        if (this != &other) {
            if (handle) handle.destroy();
            handle = std::exchange(other.handle, {});
        }
        return *this;
    }
    Async(const Async&) = delete;
    ~Async() {
        if (handle) handle.destroy();
    }

    bool await_ready() const noexcept { return false; }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> caller) noexcept {
        handle.promise().continuation = caller;
        return handle;
    }
    void await_resume() {
        if (handle.promise().error) std::rethrow_exception(handle.promise().error);
    }

    void start(std::function<void(std::exception_ptr)> finished = {}) {
        auto h = std::exchange(handle, {});
        h.promise().detached = true;
        h.promise().finished = std::move(finished);
        h.resume();
    }

private:
    std::coroutine_handle<promise_type> handle;
};

struct SwitchTo {
    Executor& executor;
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h) { executor.post([h] { h.resume(); }); }
    void await_resume() const noexcept {}
};

inline SwitchTo onMain() { return {mainQueue()}; }

template <typename F>
struct BackgroundWork {
    using R = std::invoke_result_t<F>;
    F work;
    std::optional<std::conditional_t<std::is_void_v<R>, bool, R>> value;
    std::exception_ptr error;
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h) {
        backgroundPool().post([this, h] {
            try {
                if constexpr (std::is_void_v<R>) {
                    work();
                    value.emplace(true);
                } else value.emplace(work());
            } catch (...) {
                error = std::current_exception();
            }
            mainQueue().post([h] { h.resume(); });
        });
    }
    R await_resume() {
        if (error) std::rethrow_exception(error);
        if constexpr (!std::is_void_v<R>) return std::move(*value);
    }
};

template <typename F>
BackgroundWork<std::decay_t<F>> background(F&& work) {
    return BackgroundWork<std::decay_t<F>>{std::forward<F>(work), std::nullopt, nullptr};
}

struct Sleep {
    double seconds;
    bool await_ready() const noexcept { return seconds <= 0; }
    void await_suspend(std::coroutine_handle<> h) {
        after(seconds, [h] { h.resume(); });
    }
    void await_resume() const noexcept {}
};

inline Sleep sleepFor(double seconds) { return {seconds}; }

void spawnDetached(Async<void> task, std::function<void(std::exception_ptr)> finished = {});

}
