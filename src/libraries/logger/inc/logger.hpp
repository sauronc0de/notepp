#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <ctime>
#include <deque>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>

#ifndef _WIN32
#include <unistd.h> // isatty
#endif

// ---------- Levels (unchanged) ----------
enum LogLevel
{
  DEBUG = 0x01,
  INFO = 0x02,
  WARNING = 0x04,
  ERROR = 0x08,
  ED_TRANSITION = 0x10,
  ED_ACTIVITY = 0x20,
  ED_EVENT = 0x40
};

class Logger
{
public:
  std::atomic<bool> shutting_down_{false};

  struct Options
  {
    std::size_t queue_capacity = 4096; // bounded queue, producers wait when full
    bool color = true;                 // ANSI colors if TTY
  };

  Logger() = default;

  // Optional: configure and start explicitly (otherwise we lazy-start)
  void start()
  {
    start(Options{});
  }
  void start(const Options &opt)
  {
    std::lock_guard<std::mutex> lk(state_m_);
    if(running_.load(std::memory_order_acquire))
      return;

    opts_ = opt;
#ifndef _WIN32
    if(opts_.color)
      opts_.color = ::isatty(fileno(stdout));
#endif

    // reset state
    {
      std::lock_guard<std::mutex> qlk(q_m_);
      queue_.clear();
    }
    stop_.store(false, std::memory_order_release);
    worker_ = std::thread([this] { consume_loop_(); });
    running_.store(true, std::memory_order_release);
  }

  void stop()
  {
    std::unique_lock<std::mutex> lk(state_m_);
    if(!running_.load(std::memory_order_acquire))
      return;

    // Announce shutdown to all producers ASAP
    shutting_down_.store(true, std::memory_order_release);
    stop_.store(true, std::memory_order_release);
    lk.unlock();

    // Wake everyone so nothing stays blocked
    cv_not_empty_.notify_all(); // wake consumer
    cv_not_full_.notify_all();  // wake producers waiting for space

    if(worker_.joinable())
      worker_.join();

    lk.lock();
    running_.store(false, std::memory_order_release);
  }

  ~Logger()
  {
    try
    {
      stop();
    }
    catch(...)
    {
    }
  }

  void set_level_mask(std::size_t mask)
  {
    level_mask_.store(mask, std::memory_order_relaxed);
  }
  std::size_t level_mask() const
  {
    return level_mask_.load(std::memory_order_relaxed);
  }

  // -------- Public API (signature kept, still const) --------
  template <typename... Args>
  void log(LogLevel level, const std::string_view &fileName, const int &fileLine, Args &&...args) const
  {
    // Not enter if we are closing
    if(shutting_down_.load(std::memory_order_acquire))
      return;

    if((level_mask_.load(std::memory_order_relaxed) & level) == 0)
      return;
    ensure_started_();

    // format message (no I/O)
    std::ostringstream oss;
    (oss << ... << std::forward<Args>(args));

    // timestamp (thread-safe)
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    char ts[16];
    std::snprintf(ts, sizeof(ts), "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);

    Item item;
    item.level = level;
    item.file = std::string(getFileName_(fileName));
    item.line = fileLine;
    item.time = ts;
    item.text = oss.str();

    // enqueue (lossless: wait if full)
    {
      std::unique_lock<std::mutex> lk(q_m_);
      cv_not_full_.wait(lk, [this] {
        return queue_.size() < opts_.queue_capacity || stop_.load(std::memory_order_acquire);
      });
      if(stop_.load(std::memory_order_acquire))
        return;
      queue_.push_back(std::move(item));
    }
    cv_not_empty_.notify_one();
  }

private:
  struct Item
  {
    LogLevel level{};
    std::string file;
    int line = 0;
    std::string time; // "HH:MM:SS"
    std::string text;
  };

  void consume_loop_()
  {
    for(;;)
    {
      Item it;
      {
        std::unique_lock<std::mutex> lk(q_m_);
        cv_not_empty_.wait(lk, [this] {
          return !queue_.empty() || stop_.load(std::memory_order_acquire);
        });
        if(queue_.empty())
        {
          if(stop_.load(std::memory_order_acquire))
            break;
          continue;
        }
        it = std::move(queue_.front());
        queue_.pop_front();
      }
      cv_not_full_.notify_one();
      print_item_(it);
    }
    // drain any residual items
    for(;;)
    {
      Item it;
      {
        std::lock_guard<std::mutex> lk(q_m_);
        if(queue_.empty())
          break;
        it = std::move(queue_.front());
        queue_.pop_front();
      }
      print_item_(it);
    }
    std::cout.flush();
    std::cerr.flush();
  }

  void print_item_(const Item &m) const
  {
    const char *color = color_for_(m.level);
    const char *reset = opts_.color ? "\033[0m" : "";
    const char *icon = icon_for_(m.level);

    std::ostream &os = std::cout; // keep behavior aligned with your previous code
    os << icon << (opts_.color ? color : "")
       << m.file << "#" << m.line << "\033[30G"
       << "[" << m.time << "]"
       << " - " << m.text
       << reset << "\n";
  }

  static constexpr std::string_view getFileName_(std::string_view path)
  {
    size_t pos = path.find_last_of("/\\");
    return (pos != std::string_view::npos) ? path.substr(pos + 1) : path;
  }

  const char *icon_for_(LogLevel l) const
  {
    switch(l)
    {
    case DEBUG:
      return "🐞 ";
    case INFO:
      return "ℹ️  ";
    case WARNING:
      return "☢️  ";
    case ERROR:
      return "🛑 ";
    case ED_TRANSITION:
      return "🚀 ";
    case ED_ACTIVITY:
      return "⚡ ";
    case ED_EVENT:
      return "🔔 ";
    default:
      return "🔮 ";
    }
  }

  const char *color_for_(LogLevel l) const
  {
    if(!opts_.color)
      return "";
    switch(l)
    {
    case DEBUG:
      return "\033[32m"; // green
    case INFO:
      return "\033[90m"; // gray
    case WARNING:
      return "\033[33m"; // yellow
    case ERROR:
      return "\033[31m"; // red
    case ED_TRANSITION:
      return "\033[34m"; // blue
    case ED_ACTIVITY:
      return "\033[36m"; // cyan
    case ED_EVENT:
      return "\033[34m"; // blue
    default:
      return "\033[35m"; // magenta
    }
  }

  void ensure_started_() const
  {
    if(running_.load(std::memory_order_acquire))
      return;
    std::lock_guard<std::mutex> lk(state_m_);
    if(!running_.load(std::memory_order_relaxed))
    {
      const_cast<Logger *>(this)->start(); // lazy start
    }
  }

  // ---- state ----
  std::atomic<std::size_t> level_mask_{0xFF};
  Options opts_{};

  // queue & sync (must be mutable because log() is const)
  mutable std::mutex q_m_;
  mutable std::condition_variable cv_not_empty_;
  mutable std::condition_variable cv_not_full_;
  mutable std::deque<Item> queue_;

  // worker state
  mutable std::mutex state_m_;
  std::thread worker_;
  std::atomic<bool> running_{false};
  std::atomic<bool> stop_{false};
};
