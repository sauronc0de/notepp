// Private header for the platform-specific PTY backend used by Terminal.
//
// Two implementations live in pty_unix.cpp (forkpty / pty.h) and
// pty_windows.cpp (ConPTY). Both expose the same `PtyBackend` interface
// so that terminal.cpp can stay platform-agnostic.

#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <sys/types.h>

namespace notepp::terminal
{

class PtyBackend
{
public:
  PtyBackend() = default;
  virtual ~PtyBackend() = 0;

  PtyBackend(const PtyBackend &) = delete;
  PtyBackend &operator=(const PtyBackend &) = delete;
  PtyBackend(PtyBackend &&) = delete;
  PtyBackend &operator=(PtyBackend &&) = delete;

  /// Spawn @p shell in @p cwd with the given initial window size.
  /// Returns true on success. On failure, @ref isRunning() is false.
  virtual bool start(const std::filesystem::path &cwd, const std::string &shell, int rows, int cols) = 0;

  /// Interrupt a pending @ref read() without tearing down state that the
  /// reader thread may still be using. Call before joining that thread.
  virtual void interruptRead() = 0;

  /// Tear down the child process group and close all backend resources.
  /// No reader thread may be using the backend when this is called.
  virtual void stop() = 0;

  /// Best-effort write. Returns true if all bytes were written.
  virtual bool write(std::string_view bytes) = 0;

  /// Blocking read of up to @p len bytes into @p buf. Returns the number
  /// of bytes read, 0 on EOF (shell closed slave / pipe broken), or -1
  /// on error. @ref interruptRead() makes a pending call return promptly.
  virtual int read(void *buf, size_t len) = 0;

  /// True iff the underlying file descriptor / pipe handle can be polled
  /// for read-readiness via select/poll. Always false on Windows; the
  /// backend's blocking @ref read() is the only way to consume output.
  virtual bool hasSelectableReadHandle() const noexcept = 0;

  /// File descriptor suitable for poll/select on POSIX. -1 otherwise.
  /// Only valid when @ref hasSelectableReadHandle is true.
  virtual int readHandle() const noexcept = 0;

  /// Notify the kernel of a new window size. Safe to call when stopped.
  virtual void resize(int rows, int cols) = 0;

  /// Toggle non-blocking mode on the read side. Safe to call when stopped.
  virtual void setNonBlocking(bool enabled) = 0;

  /// PID of the shell process. -1 if not running.
  virtual pid_t pid() const noexcept = 0;

  /// True between a successful @ref start() and a successful @ref stop().
  virtual bool isRunning() const noexcept = 0;
};

inline PtyBackend::~PtyBackend() = default;

/// Factory: returns a backend appropriate for the current platform.
PtyBackend *createPtyBackend();

} // namespace notepp::terminal
