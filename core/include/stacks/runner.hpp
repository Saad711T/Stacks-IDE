// Stacks IDE — process runner
// Spawns compilers / interpreters and streams their output line-by-line.

#pragma once

#include <string>
#include <vector>
#include <functional>
#include <optional>

namespace stacks {

struct RunRequest {
    std::string workspace_root;    // absolute path
    std::string path;              // path relative to workspace_root
    std::string language;          // python / node / cpp / c / java / shell
    std::string args;              // extra CLI args for the produced binary / script
};

struct RunResult {
    int exit_code = 0;
    bool spawned = false;
    std::string error;             // set when spawned == false
};

using OutputCallback = std::function<void(const std::string& data, bool is_stderr)>;

// Build the actual shell command to run for a given language + file.
// For compiled languages this returns a two-step "compile && run" pipeline.
std::string build_command(const RunRequest& req);

// Spawn a process, stream its stdout/stderr to `cb`, and return its exit code.
// Runs synchronously on the calling thread; the server wraps it in a worker.
RunResult run_sync(const RunRequest& req, const OutputCallback& cb);

// Detect which toolchains are installed on this machine. Values are the
// resolved executable paths, or empty when missing.
struct Toolchains {
    std::string python;
    std::string node;
    std::string cpp;     // g++ or clang++
    std::string c;       // gcc or clang
    std::string java;    // java runtime
    std::string javac;   // java compiler
    std::string shell;   // bash / sh / powershell
};

Toolchains detect_toolchains();

} // namespace stacks
