#pragma once

// Child process control for the real process death proofs.
//
// The proofs require genuinely separate operating system processes: a worker that
// is killed with an uncatchable termination, and a coordinator that is killed
// mid-life. Nothing here emulates death with an in-memory flag.

#include <string>

namespace pf_process {

/// A spawned child process with its standard output captured through an
/// anonymous pipe.
struct ChildProcess {
  void* process = nullptr;
  void* thread = nullptr;
  void* read_pipe = nullptr;
  unsigned long pid = 0;

  bool valid() const noexcept { return process != nullptr; }
};

/// True when child process control is available on this platform.
bool available() noexcept;

/// Spawns a process, capturing its standard output. The command line is passed
/// exactly as given; no shell is involved.
ChildProcess spawn(const std::string& command_line, std::string& error);

/// Reads one line of captured output. Returns false at end of stream.
bool read_line(ChildProcess& child, std::string& line, std::string& error);

/// Reads lines until one starts with the given prefix or the stream ends.
bool wait_for_line(ChildProcess& child, const std::string& prefix, std::string& line,
                   std::string& error);

/// Terminates the process immediately, without giving it a chance to run any
/// shutdown path. This is the process death the runtime must survive.
void terminate(ChildProcess& child);

/// Waits for the process to exit and closes every handle.
bool wait(ChildProcess& child, unsigned long& exit_code, std::string& error);

/// Closes handles without waiting. Safe to call repeatedly.
void close(ChildProcess& child);

}  // namespace pf_process
