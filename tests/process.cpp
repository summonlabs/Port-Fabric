#include "process.hpp"

#if defined(_WIN32)
#include <windows.h>

#include <vector>
#endif

namespace pf_process {

#if defined(_WIN32)

namespace {

/// Job object every child is assigned to, with kill-on-close semantics.
///
/// A coordinator or agent must never outlive the proof process that started it,
/// even when that process is killed outright: if the handle closes for any
/// reason, Windows terminates every process still in the job. Creating the job
/// lazily keeps the cost off the paths that never spawn anything.
HANDLE child_job() {
  static HANDLE job = []() -> HANDLE {
    HANDLE created = CreateJobObjectW(nullptr, nullptr);
    if (created == nullptr) {
      return nullptr;
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(created, JobObjectExtendedLimitInformation, &limits,
                                 sizeof(limits))) {
      CloseHandle(created);
      return nullptr;
    }
    return created;
  }();
  return job;
}

}  // namespace

bool available() noexcept { return true; }

ChildProcess spawn(const std::string& command_line, std::string& error) {
  ChildProcess child;
  SECURITY_ATTRIBUTES attributes{};
  attributes.nLength = sizeof(attributes);
  attributes.bInheritHandle = TRUE;
  HANDLE read_handle = nullptr;
  HANDLE write_handle = nullptr;
  if (!CreatePipe(&read_handle, &write_handle, &attributes, 0)) {
    error = "the capture pipe could not be created";
    return child;
  }
  // The read end must not be inherited by the child.
  SetHandleInformation(read_handle, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdOutput = write_handle;
  startup.hStdError = write_handle;
  startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  PROCESS_INFORMATION information{};

  std::wstring command(command_line.begin(), command_line.end());
  std::vector<wchar_t> buffer(command.begin(), command.end());
  buffer.push_back(L'\0');
  const BOOL created = CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, TRUE,
                                      CREATE_NO_WINDOW, nullptr, nullptr, &startup, &information);
  CloseHandle(write_handle);
  if (!created) {
    CloseHandle(read_handle);
    error = "the child process could not be created (windows error " +
            std::to_string(static_cast<unsigned long>(GetLastError())) + ")";
    return child;
  }
  // Assigning the child to the kill-on-close job guarantees that no coordinator or
  // agent survives this process, however this process ends.
  if (const HANDLE job = child_job(); job != nullptr) {
    if (!AssignProcessToJobObject(job, information.hProcess)) {
      // A failed assignment is not fatal for the proof, but it is reported so a
      // leaked child can never be mistaken for a clean run.
      error = "the child could not be assigned to the cleanup job";
    }
  }
  child.process = information.hProcess;
  child.thread = information.hThread;
  child.read_pipe = read_handle;
  child.pid = information.dwProcessId;
  return child;
}

bool read_line(ChildProcess& child, std::string& line, std::string& error) {
  line.clear();
  if (child.read_pipe == nullptr) {
    error = "the process has no captured output";
    return false;
  }
  char buffer[1];
  DWORD read = 0;
  while (true) {
    if (!ReadFile(static_cast<HANDLE>(child.read_pipe), buffer, 1, &read, nullptr) || read == 0) {
      return !line.empty();
    }
    if (buffer[0] == '\n') {
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      return true;
    }
    line.push_back(buffer[0]);
    if (line.size() > 65536) {
      error = "the child produced an implausibly long line";
      return false;
    }
  }
}

bool wait_for_line(ChildProcess& child, const std::string& prefix, std::string& line,
                   std::string& error) {
  while (read_line(child, line, error)) {
    if (line.rfind(prefix, 0) == 0) {
      return true;
    }
  }
  if (error.empty()) {
    error = "the child closed its output before reporting " + prefix;
  }
  return false;
}

void terminate(ChildProcess& child) {
  if (child.process != nullptr) {
    TerminateProcess(static_cast<HANDLE>(child.process), 99);
  }
}

bool wait(ChildProcess& child, unsigned long& exit_code, std::string& error) {
  exit_code = 0;
  if (child.process == nullptr) {
    error = "the process handle is not valid";
    return false;
  }
  const DWORD result = WaitForSingleObject(static_cast<HANDLE>(child.process), INFINITE);
  if (result != WAIT_OBJECT_0) {
    error = "waiting for the process failed";
    return false;
  }
  DWORD code = 0;
  if (!GetExitCodeProcess(static_cast<HANDLE>(child.process), &code)) {
    error = "the process exit code could not be read";
    return false;
  }
  exit_code = code;
  close(child);
  return true;
}

void close(ChildProcess& child) {
  if (child.read_pipe != nullptr) {
    CloseHandle(static_cast<HANDLE>(child.read_pipe));
    child.read_pipe = nullptr;
  }
  if (child.thread != nullptr) {
    CloseHandle(static_cast<HANDLE>(child.thread));
    child.thread = nullptr;
  }
  if (child.process != nullptr) {
    CloseHandle(static_cast<HANDLE>(child.process));
    child.process = nullptr;
  }
}

#else

bool available() noexcept { return false; }

ChildProcess spawn(const std::string& command_line, std::string& error) {
  (void)command_line;
  error = "child process control is UNSUPPORTED on this platform";
  return ChildProcess{};
}

bool read_line(ChildProcess& child, std::string& line, std::string& error) {
  (void)child;
  line.clear();
  error = "child process control is UNSUPPORTED on this platform";
  return false;
}

bool wait_for_line(ChildProcess& child, const std::string& prefix, std::string& line,
                   std::string& error) {
  (void)prefix;
  return read_line(child, line, error);
}

void terminate(ChildProcess& child) { (void)child; }

bool wait(ChildProcess& child, unsigned long& exit_code, std::string& error) {
  (void)child;
  exit_code = 0;
  error = "child process control is UNSUPPORTED on this platform";
  return false;
}

void close(ChildProcess& child) { (void)child; }

#endif

}  // namespace pf_process
