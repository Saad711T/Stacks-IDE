// Stacks IDE — POSIX PTY implementation (Linux / macOS)

#ifndef _WIN32

#include "stacks/pty.hpp"

#include <atomic>
#include <cstring>
#include <thread>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#ifdef __linux__
  #include <pty.h>
#else
  #include <util.h>
#endif

namespace stacks {

class PosixPty : public Pty {
public:
    ~PosixPty() override { close(); }

    bool spawn(const std::string& shell, int cols, int rows,
               std::function<void(const std::string&)> onData) override {
        winsize ws{};
        ws.ws_col = static_cast<unsigned short>(cols > 0 ? cols : 80);
        ws.ws_row = static_cast<unsigned short>(rows > 0 ? rows : 24);

        pid_ = forkpty(&master_, nullptr, nullptr, &ws);
        if (pid_ < 0) return false;

        if (pid_ == 0) {
            // child
            setenv("TERM", "xterm-256color", 1);
            execl(shell.c_str(), shell.c_str(), (char*)nullptr);
            _exit(127);
        }

        running_ = true;
        onData_ = std::move(onData);
        reader_ = std::thread([this] {
            char buf[4096];
            while (running_) {
                ssize_t n = ::read(master_, buf, sizeof(buf));
                if (n <= 0) break;
                if (onData_) onData_(std::string(buf, buf + n));
            }
            running_ = false;
        });
        return true;
    }

    void write(const std::string& data) override {
        if (master_ >= 0) ::write(master_, data.data(), data.size());
    }

    void resize(int cols, int rows) override {
        if (master_ < 0) return;
        winsize ws{};
        ws.ws_col = static_cast<unsigned short>(cols);
        ws.ws_row = static_cast<unsigned short>(rows);
        ioctl(master_, TIOCSWINSZ, &ws);
    }

    void close() override {
        running_ = false;
        if (master_ >= 0) { ::close(master_); master_ = -1; }
        if (pid_ > 0) { kill(pid_, SIGTERM); waitpid(pid_, nullptr, 0); pid_ = -1; }
        if (reader_.joinable()) reader_.join();
    }

private:
    int master_ = -1;
    pid_t pid_ = -1;
    std::atomic<bool> running_{false};
    std::thread reader_;
    std::function<void(const std::string&)> onData_;
};

std::unique_ptr<Pty> Pty::create() { return std::make_unique<PosixPty>(); }

} // namespace stacks

#endif // !_WIN32
