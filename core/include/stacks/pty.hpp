// Stacks IDE — PTY abstraction
// Windows: ConPTY; POSIX: forkpty
#pragma once

#include <string>
#include <functional>
#include <memory>

namespace stacks {

class Pty {
public:
    virtual ~Pty() = default;

    // Spawn a shell and begin a read loop; each chunk is delivered to `onData`.
    virtual bool spawn(const std::string& shell, int cols, int rows,
                       std::function<void(const std::string&)> onData) = 0;

    virtual void write(const std::string& data) = 0;
    virtual void resize(int cols, int rows)     = 0;
    virtual void close()                        = 0;

    static std::unique_ptr<Pty> create();
};

} // namespace stacks
