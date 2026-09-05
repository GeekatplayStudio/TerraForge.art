#pragma once
#include <atomic>
#include <condition_variable>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace gpx::detail {
// One batch at a time. Nested or concurrent callers execute inline, avoiding
// oversubscription and deadlock while preserving the exact band boundaries.
class ParallelPool {
  std::mutex submit, mutex;
  std::condition_variable ready, done;
  std::vector<std::thread> threads;
  std::function<void(unsigned)> job;
  unsigned generation = 0, count = 0, next = 0, pending = 0;
  bool stopping = false;
  std::exception_ptr error;
  inline static thread_local bool executing = false;

  void execute(unsigned i) {
    executing = true;
    try { job(i); }
    catch (...) {
      std::lock_guard<std::mutex> lk(mutex);
      if (!error) error = std::current_exception();
    }
    executing = false;
    std::lock_guard<std::mutex> lk(mutex);
    if (--pending == 0) done.notify_one();
  }
public:
  ~ParallelPool() {
    { std::lock_guard<std::mutex> lk(mutex); stopping = true; }
    ready.notify_all();
    for (auto &t : threads) t.join();
  }
  void run(unsigned n, const std::function<void(unsigned)> &fn) {
    if (executing || n <= 1) {
      for (unsigned i = 0; i < n; ++i) fn(i);
      return;
    }
    std::unique_lock<std::mutex> serial(submit, std::try_to_lock);
    if (!serial.owns_lock() || n <= 1) {
      for (unsigned i = 0; i < n; ++i) fn(i);
      return;
    }
    {
      std::lock_guard<std::mutex> lk(mutex);
      // New workers start under this lock. Their initial generation must
      // precede the new batch even if the OS starts them after notification.
      while (threads.size() + 1 < n) {
        unsigned seen = generation;
        try {
          threads.emplace_back([this, seen] {
            std::unique_lock<std::mutex> lk(mutex);
            unsigned observed = seen;
            while (true) {
              ready.wait(lk, [&] { return stopping || generation != observed; });
              if (stopping) return;
              observed = generation;
              while (next < count) {
                unsigned i = next++;
                lk.unlock(); execute(i); lk.lock();
              }
            }
          });
        } catch (const std::system_error &) { break; }
      }
      job = fn;
      error = nullptr;
      count = pending = n;
      next = 1; // the calling thread always participates
      ++generation;
    }
    ready.notify_all();
    execute(0);
    std::unique_lock<std::mutex> lk(mutex);
    // Also drain work when the OS refused to create any background workers.
    while (next < count) {
      unsigned i = next++;
      lk.unlock(); execute(i); lk.lock();
    }
    done.wait(lk, [&] { return pending == 0; });
    job = {};
    if (error) std::rethrow_exception(error);
  }
};
inline ParallelPool &parallel_pool() { static ParallelPool pool; return pool; }
} // namespace gpx::detail
