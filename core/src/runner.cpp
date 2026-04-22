// Stacks IDE — process runner implementation

#include "stacks/runner.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <string>

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
#else
  #include <unistd.h>
  #include <sys/wait.h>
  #include <spawn.h>
  extern char** environ;
#endif

namespace fs = std::filesystem;

namespace stacks {

static std::string which(const std::string& exe) {
#ifdef _WIN32
    const char sep = ';';
    const std::string exts[] = {".exe", ".bat", ".cmd", ""};
#else
    const char sep = ':';
    const std::string exts[] = {""};
#endif
    const char* path = std::getenv("PATH");
    if (!path) return "";
    std::stringstream ss(path);
    std::string dir;
    while (std::getline(ss, dir, sep)) {
        if (dir.empty()) continue;
        for (const auto& ext : exts) {
            fs::path p = fs::path(dir) / (exe + ext);
            std::error_code ec;
            if (fs::exists(p, ec) && fs::is_regular_file(p, ec))
                return p.string();
        }
    }
    return "";
}

Toolchains detect_toolchains() {
    Toolchains t;
    t.python = which("python3"); if (t.python.empty()) t.python = which("python");
    t.node   = which("node");
    t.cpp    = which("g++");     if (t.cpp.empty())    t.cpp    = which("clang++");
    t.c      = which("gcc");     if (t.c.empty())      t.c      = which("clang");
    t.java   = which("java");
    t.javac  = which("javac");
#ifdef _WIN32
    t.shell  = which("powershell"); if (t.shell.empty()) t.shell = which("cmd");
#else
    t.shell  = which("bash");    if (t.shell.empty())  t.shell  = which("sh");
#endif
    return t;
}

std::string build_command(const RunRequest& req) {
    fs::path abs = fs::path(req.workspace_root) / req.path;
    const std::string file = abs.string();
    const std::string stem = abs.stem().string();
    const std::string dir  = abs.parent_path().string();
    auto tc = detect_toolchains();

    auto quote = [](const std::string& s) {
        std::string out = "\"";
        for (char c : s) { if (c == '"') out += "\\"; out += c; }
        out += "\"";
        return out;
    };

    const std::string q = quote(file);
    const std::string ex = quote((fs::path(dir) / (stem + "_stacksbin")).string());
    const std::string args = req.args.empty() ? "" : (" " + req.args);

    if (req.language == "python") {
        if (tc.python.empty()) return "echo \"Python not found\"";
        return quote(tc.python) + " -u " + q + args;
    }
    if (req.language == "node") {
        if (tc.node.empty()) return "echo \"Node.js not found\"";
        return quote(tc.node) + " " + q + args;
    }
    if (req.language == "cpp") {
        if (tc.cpp.empty()) return "echo \"C++ compiler not found\"";
        return quote(tc.cpp) + " -std=c++17 -O2 -Wall " + q + " -o " + ex + " && " + ex + args;
    }
    if (req.language == "c") {
        if (tc.c.empty()) return "echo \"C compiler not found\"";
        return quote(tc.c) + " -std=c11 -O2 -Wall " + q + " -o " + ex + " && " + ex + args;
    }
    if (req.language == "java") {
        if (tc.javac.empty() || tc.java.empty()) return "echo \"Java not found\"";
        return quote(tc.javac) + " " + q + " -d " + quote(dir) + " && " +
               quote(tc.java) + " -cp " + quote(dir) + " " + stem + args;
    }
    if (req.language == "shell") {
        if (tc.shell.empty()) return "echo \"Shell not found\"";
#ifdef _WIN32
        return quote(tc.shell) + " -File " + q + args;
#else
        return quote(tc.shell) + " " + q + args;
#endif
    }
    if (req.language == "rust") {
        std::string rustc = which("rustc");
        if (rustc.empty()) return "echo \"rustc not found\"";
        return quote(rustc) + " " + q + " -o " + ex + " && " + ex + args;
    }
    if (req.language == "go") {
        std::string go = which("go");
        if (go.empty()) return "echo \"go not found\"";
        return quote(go) + " run " + q + args;
    }

    // fallback: treat as shell
    return q + args;
}

RunResult run_sync(const RunRequest& req, const OutputCallback& cb) {
    RunResult r;
    std::string cmd = build_command(req);

#ifdef _WIN32
    // Use _popen — simple, blocks until completion, captures merged stdout+stderr.
    std::string full = cmd + " 2>&1";
    FILE* pipe = _popen(full.c_str(), "r");
#else
    // Redirect stderr to stdout so we can distinguish later via ANSI if needed.
    std::string full = cmd + " 2>&1";
    FILE* pipe = popen(full.c_str(), "r");
#endif
    if (!pipe) {
        r.error = "failed to spawn process";
        return r;
    }
    r.spawned = true;
    std::array<char, 4096> buf;
    while (true) {
        size_t n = std::fread(buf.data(), 1, buf.size(), pipe);
        if (n > 0) cb(std::string(buf.data(), n), false);
        if (n < buf.size()) {
            if (std::feof(pipe)) break;
            if (std::ferror(pipe)) break;
        }
    }

#ifdef _WIN32
    r.exit_code = _pclose(pipe);
#else
    int status = pclose(pipe);
    if (WIFEXITED(status)) r.exit_code = WEXITSTATUS(status);
    else r.exit_code = -1;
#endif
    return r;
}

} // namespace stacks
