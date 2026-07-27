// Unix PTY backend (Linux + macOS).
//
// Uses `forkpty(3)` from libutil: it allocates a master/slave pair, forks,
// and in the child sets up a new session with the slave as the controlling
// terminal on stdin/stdout/stderr. The parent receives the master fd which
// we read from and write to.

#include "pty.hpp"

#if !defined(_WIN32)

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <pty.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/prctl.h>
#endif

#include <array>
#include <atomic>
#include <cstdlib>
#include <string>

namespace notepp::terminal
{

namespace
{
constexpr pid_t kNoChild = -1;
constexpr int kNoFd = -1;
constexpr int kGracefulWaitAttempts = 50;
constexpr long kGracefulWaitNanoseconds = 10L * 1000L * 1000L;

std::string queryShell()
{
  if(const char *sh = std::getenv("SHELL"); sh != nullptr && *sh != '\0')
  {
    return std::string(sh);
  }
  return "/bin/sh";
}
} // namespace

class PtyBackendUnix final : public PtyBackend
{
public:
  ~PtyBackendUnix() override
  {
    stop();
  }

  bool start(const std::filesystem::path &cwd, const std::string &shell, int rows, int cols) override
  {
    stop();

    struct winsize ws
    {
    };
    ws.ws_row = static_cast<short>(rows > 0 ? rows : 24);
    ws.ws_col = static_cast<short>(cols > 0 ? cols : 80);
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;

    int master_fd = kNoFd;
    pid_t child = forkpty(&master_fd, nullptr, nullptr, &ws);
    if(child < 0)
    {
      return false;
    }
    if(child == 0)
    {
      // Child: reset signal dispositions that the parent may have customised
      // (e.g. SDL's SIGIO handler) so the shell is not surprised, then exec.
      sigset_t empty{};
      sigemptyset(&empty);
      sigprocmask(SIG_SETMASK, &empty, nullptr);
      signal(SIGPIPE, SIG_DFL);
      signal(SIGCHLD, SIG_DFL);

      if(!cwd.empty())
      {
        if(::chdir(cwd.c_str()) != 0)
        {
          // Non-fatal: shell may still work, just not from the intended dir.
        }
      }

      // Terminate this child if the parent goes away so we don't leak shells.
#if defined(__linux__)
      ::prctl(PR_SET_PDEATHSIG, SIGHUP);
#endif

      std::string path = shell;
      if(path.empty()) path = queryShell();

      // Build argv: shell [shell arg] or -shell arg for login shells.
      const char *base = path.c_str();
      const char *slash = std::strrchr(base, '/');
      const char *name = slash ? slash + 1 : base;

      std::string login_name;
      login_name.reserve(std::strlen(name) + 2);
      login_name.push_back('-');
      login_name.append(name);

      std::array<const char *, 3> argv{
          {login_name.c_str(), "-i", nullptr}};

      // PATH is normally inherited; ensure execve finds a shell in a non-PATH env.
      ::execvp(path.c_str(), const_cast<char *const *>(argv.data()));
      // exec only returns on failure.
      ::_exit(127);
    }

    master_fd_ = master_fd;
    child_pid_ = child;
    read_interrupted_.store(false, std::memory_order_release);
    setNonBlocking(true);
    return true;
  }

  void interruptRead() override
  {
    // Unix reads are non-blocking and retry every 2 ms. A flag interrupts the
    // retry loop without closing the descriptor underneath an active read.
    read_interrupted_.store(true, std::memory_order_release);
  }

  void stop() override
  {
    interruptRead();

    const pid_t child = child_pid_;
    if(child > 0)
    {
      signalChild(child, SIGHUP);

      bool reaped = false;
      for(int attempt = 0; attempt < kGracefulWaitAttempts && !reaped; ++attempt)
      {
        reaped = tryReap(child, WNOHANG);
        if(!reaped)
        {
          const struct timespec pause
          {
            0, kGracefulWaitNanoseconds
          };
          (void)::nanosleep(&pause, nullptr);
        }
      }

      if(!reaped)
      {
        signalChild(child, SIGKILL);
        (void)tryReap(child, 0);
      }
      child_pid_ = kNoChild;
    }
    if(master_fd_ != kNoFd)
    {
      ::close(master_fd_);
      master_fd_ = kNoFd;
    }
  }

  bool write(std::string_view bytes) override
  {
    if(master_fd_ == kNoFd) return false;
    const char *p = bytes.data();
    size_t left = bytes.size();
    while(left > 0)
    {
      const ssize_t n = ::write(master_fd_, p, left);
      if(n < 0)
      {
        if(errno == EINTR) continue;
        return false;
      }
      p += n;
      left -= static_cast<size_t>(n);
    }
    return true;
  }

  int read(void *buf, size_t len) override
  {
    if(master_fd_ == kNoFd) return -1;
    // Loop past EINTR. The descriptor is non-blocking so interruptRead() can
    // end the retry loop without racing a close against this system call.
    for(;;)
    {
      if(read_interrupted_.load(std::memory_order_acquire)) return 0;
      const ssize_t n = ::read(master_fd_, buf, len);
      if(n < 0 && errno == EINTR) continue;
      if(n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
      {
        const struct timespec pause
        {
          0, 2L * 1000L * 1000L
        };
        (void)::nanosleep(&pause, nullptr);
        continue;
      }
      return static_cast<int>(n);
    }
  }

  bool hasSelectableReadHandle() const noexcept override
  {
    return true;
  }

  int readHandle() const noexcept override
  {
    return master_fd_;
  }

  void resize(int rows, int cols) override
  {
    if(master_fd_ == kNoFd) return;
    struct winsize ws
    {
    };
    ws.ws_row = static_cast<short>(rows > 0 ? rows : 24);
    ws.ws_col = static_cast<short>(cols > 0 ? cols : 80);
    ::ioctl(master_fd_, TIOCSWINSZ, &ws);
  }

  void setNonBlocking(bool enabled) override
  {
    if(master_fd_ == kNoFd) return;
    int flags = ::fcntl(master_fd_, F_GETFL, 0);
    if(flags < 0) return;
    if(enabled)
      flags |= O_NONBLOCK;
    else
      flags &= ~O_NONBLOCK;
    ::fcntl(master_fd_, F_SETFL, flags);
  }

  pid_t pid() const noexcept override
  {
    return child_pid_;
  }

  bool isRunning() const noexcept override
  {
    return child_pid_ > kNoChild;
  }

private:
  static void signalChild(pid_t child, int signalNumber)
  {
    // forkpty() creates a new session in the child, but the parent can race
    // that setup if stop() follows start() immediately. Never signal a
    // process group unless it is confirmed to be owned by the child.
    const pid_t childGroup = ::getpgid(child);
    if(childGroup == child)
      (void)::killpg(childGroup, signalNumber);
    else
      (void)::kill(child, signalNumber);
  }

  static bool tryReap(pid_t child, int options)
  {
    int status = 0;
    for(;;)
    {
      const pid_t result = ::waitpid(child, &status, options);
      if(result == child) return true;
      if(result == 0) return false;
      if(errno == EINTR) continue;
      return errno == ECHILD;
    }
  }

  int master_fd_ = kNoFd;
  pid_t child_pid_ = kNoChild;
  std::atomic<bool> read_interrupted_{false};
};

PtyBackend *createPtyBackend()
{
  return new PtyBackendUnix();
}

} // namespace notepp::terminal

#endif // !_WIN32
