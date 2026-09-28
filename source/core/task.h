// Minimal C++20 coroutine runtime standing in for C# async/await.
//
// Task<T> is lazy: nothing runs until it is co_awaited or handed to
// Scheduler::spawn. A default-constructed Task<void> is an already-completed
// task, so "return {};" is the equivalent of C#'s Task.CompletedTask and costs
// no allocation.
#pragma once
#include <coroutine>
#include <cstdlib>
#include <optional>
#include <utility>
#include <vector>

namespace sts {

template <class T = void> class Task;

namespace detail {
struct FinalAwaiter {
  bool await_ready() noexcept { return false; }
  template <class P>
  std::coroutine_handle<> await_suspend(std::coroutine_handle<P> h) noexcept {
    auto c = h.promise().continuation;
    return c ? c : std::noop_coroutine();
  }
  void await_resume() noexcept {}
};

struct PromiseBase {
  std::coroutine_handle<> continuation;
  std::suspend_always initial_suspend() noexcept { return {}; }
  FinalAwaiter final_suspend() noexcept { return {}; }
  void unhandled_exception() noexcept { std::abort(); }
};
}  // namespace detail

template <class T> class Task {
 public:
  struct promise_type : detail::PromiseBase {
    std::optional<T> value;
    Task get_return_object() { return Task(std::coroutine_handle<promise_type>::from_promise(*this)); }
    template <class U> void return_value(U&& v) { value.emplace(std::forward<U>(v)); }
  };
  using Handle = std::coroutine_handle<promise_type>;

  Task() = default;
  explicit Task(Handle h) : h_(h) {}
  Task(Task&& o) noexcept : h_(std::exchange(o.h_, {})) {}
  Task& operator=(Task&& o) noexcept { if (this != &o) { reset(); h_ = std::exchange(o.h_, {}); } return *this; }
  ~Task() { reset(); }

  bool await_ready() const noexcept { return !h_ || h_.done(); }
  std::coroutine_handle<> await_suspend(std::coroutine_handle<> caller) noexcept {
    h_.promise().continuation = caller;
    return h_;
  }
  T await_resume() { return std::move(*h_.promise().value); }

 private:
  void reset() { if (h_) { h_.destroy(); h_ = {}; } }
  Handle h_;
};

template <> class Task<void> {
 public:
  struct promise_type : detail::PromiseBase {
    Task get_return_object() { return Task(std::coroutine_handle<promise_type>::from_promise(*this)); }
    void return_void() noexcept {}
  };
  using Handle = std::coroutine_handle<promise_type>;

  Task() = default;
  explicit Task(Handle h) : h_(h) {}
  Task(Task&& o) noexcept : h_(std::exchange(o.h_, {})) {}
  Task& operator=(Task&& o) noexcept { if (this != &o) { reset(); h_ = std::exchange(o.h_, {}); } return *this; }
  ~Task() { reset(); }

  bool await_ready() const noexcept { return !h_ || h_.done(); }
  std::coroutine_handle<> await_suspend(std::coroutine_handle<> caller) noexcept {
    h_.promise().continuation = caller;
    return h_;
  }
  void await_resume() noexcept {}

  bool done() const { return !h_ || h_.done(); }
  Handle handle() const { return h_; }

 private:
  void reset() { if (h_) { h_.destroy(); h_ = {}; } }
  Handle h_;
};

// Drives top-level tasks and timed waits from the frame loop.
class Scheduler {
 public:
  static Scheduler& get() { static Scheduler s; return s; }

  void spawn(Task<> t) {
    auto h = t.handle();
    roots_.push_back(std::move(t));
    if (h) ready_.push_back(h);
  }

  void resumeLater(std::coroutine_handle<> h) { ready_.push_back(h); }
  void resumeAt(double t, std::coroutine_handle<> h) { timers_.push_back({t, h}); }

  // Called once per frame. Game time only advances here, so waits are frame-accurate.
  void update(double dt) {
    now_ += dt * speed;
    for (size_t i = 0; i < timers_.size();) {
      if (timers_[i].at <= now_) {
        ready_.push_back(timers_[i].h);
        timers_[i] = timers_.back();
        timers_.pop_back();
      } else {
        ++i;
      }
    }
    // Resuming may enqueue more work (a signal fired inside a coroutine); run it this frame.
    for (size_t guard = 0; !ready_.empty() && guard < 64; ++guard) {
      auto batch = std::move(ready_);
      ready_.clear();
      for (auto h : batch) h.resume();
    }
    for (size_t i = 0; i < roots_.size();) {
      if (roots_[i].done()) { roots_.erase(roots_.begin() + i); } else { ++i; }
    }
  }

  double now() const { return now_; }
  bool idle() const { return timers_.empty() && ready_.empty(); }
  // Called between frame updates when leaving a run. No suspended coroutine may
  // keep pointers into the discarded Run after this.
  void clear() {
    timers_.clear();
    ready_.clear();
    roots_.clear();
  }
  double speed = 1.0;

 private:
  struct Timer { double at; std::coroutine_handle<> h; };
  std::vector<Task<>> roots_;
  std::vector<std::coroutine_handle<>> ready_;
  std::vector<Timer> timers_;
  double now_ = 0;
};

// co_await wait(0.25) — Cmd.Wait / Cmd.CustomScaledWait.
struct WaitFor {
  double seconds;
  bool await_ready() const noexcept { return seconds <= 0; }
  void await_suspend(std::coroutine_handle<> h) {
    auto& s = Scheduler::get();
    s.resumeAt(s.now() + seconds, h);
  }
  void await_resume() const noexcept {}
};
inline WaitFor wait(double seconds) { return {seconds}; }

// One-shot channel from UI to logic: logic co_awaits next(), UI calls fire().
template <class T> class Signal {
 public:
  struct Awaiter {
    Signal* s;
    bool await_ready() const noexcept { return s->value_.has_value(); }
    void await_suspend(std::coroutine_handle<> h) { s->waiter_ = h; }
    T await_resume() { T v = std::move(*s->value_); s->value_.reset(); return v; }
  };
  Awaiter next() { return {this}; }
  bool waiting() const { return (bool)waiter_; }
  void fire(T v) {
    value_.emplace(std::move(v));
    if (waiter_) { auto h = std::exchange(waiter_, {}); Scheduler::get().resumeLater(h); }
  }

 private:
  std::optional<T> value_;
  std::coroutine_handle<> waiter_;
};

}  // namespace sts
