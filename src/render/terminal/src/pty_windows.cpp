// Windows PTY backend (ConPTY).
//
// Uses the Windows 10+ ConPTY API (CreatePseudoConsole) to spawn a real
// console shell (cmd.exe by default). The flow follows the official
// Microsoft sample at
// https://learn.microsoft.com/windows/win32/procthread/creating-a-pseudoconsole-session
//
// One file is compiled twice (Unix and Windows); only the matching
// `#if defined(_WIN32)` branch is real. The Unix branch is an empty stub
// in pty_unix.cpp.

#include "pty.hpp"

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstring>
#include <string>
#include <string_view>

namespace notepp::terminal
{

namespace
{
constexpr HANDLE kNoHandle = nullptr;
constexpr DWORD kNoPid = 0;

// Convert UTF-8 std::string to a wide string the Windows API can consume.
std::wstring widen(const std::string &s)
{
  if(s.empty()) return {};
  const int needed = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring out(static_cast<size_t>(needed), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), needed);
  return out;
}
} // namespace

class PtyBackendConPty final : public PtyBackend
{
public:
  ~PtyBackendConPty() override
  {
    stop();
  }

  bool start(const std::filesystem::path &cwd, const std::string &shell, int rows, int cols) override
  {
    stop();

    const COORD size{static_cast<SHORT>(cols > 0 ? cols : 80), static_cast<SHORT>(rows > 0 ? rows : 24)};

    HANDLE input_read_tmp = kNoHandle;
    HANDLE input_write = kNoHandle;
    HANDLE output_read = kNoHandle;
    HANDLE output_write_tmp = kNoHandle;

    if(!CreatePipe(&input_read_tmp, &input_write, nullptr, 0)) return false;
    if(!CreatePipe(&output_read, &output_write_tmp, nullptr, 0))
    {
      CloseHandle(input_read_tmp);
      CloseHandle(input_write);
      return false;
    }

    HPCON pc = nullptr;
    HRESULT hr = CreatePseudoConsole(size, input_read_tmp, output_write_tmp, 0, &pc);
    // CreatePseudoConsole takes ownership of the read end of input and the
    // write end of output; close our copies either way.
    CloseHandle(input_read_tmp);
    CloseHandle(output_write_tmp);
    if(FAILED(hr))
    {
      CloseHandle(input_write);
      CloseHandle(output_read);
      return false;
    }

    // Set up STARTUPINFOEX with the pseudo console attribute.
    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(si);
    SIZE_T attr_size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attr_size);
    si.lpAttributeList = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(HeapAlloc(GetProcessHeap(), 0, attr_size));
    if(si.lpAttributeList == nullptr)
    {
      ClosePseudoConsole(pc);
      CloseHandle(input_write);
      CloseHandle(output_read);
      return false;
    }
    if(!InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attr_size))
    {
      HeapFree(GetProcessHeap(), 0, si.lpAttributeList);
      ClosePseudoConsole(pc);
      CloseHandle(input_write);
      CloseHandle(output_read);
      return false;
    }
    if(!UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, pc, sizeof(pc), nullptr, nullptr))
    {
      DeleteProcThreadAttributeList(si.lpAttributeList);
      HeapFree(GetProcessHeap(), 0, si.lpAttributeList);
      ClosePseudoConsole(pc);
      CloseHandle(input_write);
      CloseHandle(output_read);
      return false;
    }

    PROCESS_INFORMATION pi{};
    std::wstring shell_w = widen(shell.empty() ? std::string("cmd.exe") : shell);
    std::wstring cwd_w = widen(cwd.string());
    const std::wstring cmdline = shell_w;

    BOOL ok = CreateProcessW(nullptr, const_cast<LPWSTR>(cmdline.data()), nullptr, nullptr, FALSE,
                             EXTENDED_STARTUPINFO_PRESENT, nullptr, cwd_w.empty() ? nullptr : cwd_w.c_str(),
                             &si.StartupInfo, &pi);

    DeleteProcThreadAttributeList(si.lpAttributeList);
    HeapFree(GetProcessHeap(), 0, si.lpAttributeList);

    if(!ok)
    {
      ClosePseudoConsole(pc);
      CloseHandle(input_write);
      CloseHandle(output_read);
      return false;
    }

    input_write_ = input_write;
    output_read_ = output_read;
    hpc_ = pc;
    process_handle_ = pi.hProcess;
    process_id_ = pi.dwProcessId;
    CloseHandle(pi.hThread); // we keep only the process handle
    return true;
  }

  void stop() override
  {
    if(hpc_ != nullptr)
    {
      ClosePseudoConsole(hpc_);
      hpc_ = nullptr;
    }
    if(process_handle_ != kNoHandle)
    {
      // Give the process a chance to exit gracefully, then kill.
      if(WaitForSingleObject(process_handle_, 500) != WAIT_OBJECT_0)
      {
        TerminateProcess(process_handle_, 0);
        WaitForSingleObject(process_handle_, INFINITE);
      }
      CloseHandle(process_handle_);
      process_handle_ = kNoHandle;
      process_id_ = kNoPid;
    }
    if(input_write_ != kNoHandle)
    {
      CloseHandle(input_write_);
      input_write_ = kNoHandle;
    }
    if(output_read_ != kNoHandle)
    {
      CloseHandle(output_read_);
      output_read_ = kNoHandle;
    }
  }

  bool write(std::string_view bytes) override
  {
    if(input_write_ == kNoHandle) return false;
    const char *p = bytes.data();
    size_t left = bytes.size();
    while(left > 0)
    {
      DWORD written = 0;
      const DWORD chunk = static_cast<DWORD>(left > 0xFFFFFFFFULL ? 0xFFFFFFFFULL : left);
      if(!WriteFile(input_write_, p, chunk, &written, nullptr)) return false;
      p += written;
      left -= written;
    }
    return true;
  }

  int read(void *buf, size_t len) override
  {
    if(output_read_ == kNoHandle) return -1;
    DWORD got = 0;
    // Blocking ReadFile: returns when data is available, when the pipe is
    // broken (ClosePseudoConsole or process exit causes this), or when the
    // handle is closed by stop(). The shell never writes more than a few KB
    // between redraws so the 32-bit chunking is fine.
    const DWORD chunk = static_cast<DWORD>(len > 0xFFFFFFFFULL ? 0xFFFFFFFFULL : len);
    if(!ReadFile(output_read_, buf, chunk, &got, nullptr))
    {
      const DWORD err = GetLastError();
      if(err == ERROR_BROKEN_PIPE || err == ERROR_OPERATION_ABORTED) return 0;
      return -1;
    }
    return static_cast<int>(got);
  }

  bool hasSelectableReadHandle() const noexcept override { return false; }

  int readHandle() const noexcept override { return -1; }

  void resize(int rows, int cols) override
  {
    if(hpc_ == nullptr) return;
    const COORD size{static_cast<SHORT>(cols > 0 ? cols : 80), static_cast<SHORT>(rows > 0 ? rows : 24)};
    ResizePseudoConsole(hpc_, size);
  }

  void setNonBlocking(bool) override
  {
    // No-op: Windows pipes are intrinsically readable in blocking mode and
    // shutdown is signalled by closing the handle.
  }

  pid_t pid() const noexcept override
  {
    return static_cast<pid_t>(process_id_);
  }

  bool isRunning() const noexcept override
  {
    if(process_handle_ == kNoHandle) return false;
    return WaitForSingleObject(process_handle_, 0) == WAIT_TIMEOUT;
  }

  HANDLE outputHandle() const noexcept { return output_read_; }

private:
  HANDLE input_write_ = kNoHandle;
  HANDLE output_read_ = kNoHandle;
  HPCON hpc_ = nullptr;
  HANDLE process_handle_ = kNoHandle;
  DWORD process_id_ = kNoPid;
};

PtyBackend *createPtyBackend()
{
  return new PtyBackendConPty();
}

} // namespace notepp::terminal

#endif // _WIN32
