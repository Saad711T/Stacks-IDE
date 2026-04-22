# Stacks IDE — Native Backend

A lightweight local daemon that turns the Stacks IDE web UI into a **real IDE**.

It exposes an HTTP + WebSocket API on `127.0.0.1:17890` (loopback only):

| Endpoint           | Purpose                                 |
| ------------------ | --------------------------------------- |
| `GET  /api/health` | Health check + issues an access token   |
| `GET  /api/toolchains` | Detect installed compilers/interpreters |
| `GET  /api/fs/read?path=` | Read a file from workspace           |
| `POST /api/fs/write` | Write a file to workspace              |
| `GET  /api/fs/list` | List workspace tree                    |
| `WS   /ws/run`     | Spawn + stream a one-shot process       |
| `WS   /ws/pty`     | Interactive shell via PTY (ConPTY / forkpty) |

## Security

- Binds to `127.0.0.1` only. Never exposed on the network.
- All write / run endpoints require the `X-Stacks-Token` header with the random
  token issued by `/api/health` on first launch. The token is stored in the user's
  profile dir (`%APPDATA%/StacksIDE/token` / `~/.config/stacks-ide/token`).

## Dependencies

- [`cpp-httplib`](https://github.com/yhirose/cpp-httplib) — single header, vendored in `include/`
- [`nlohmann/json`](https://github.com/nlohmann/json) — single header, vendored in `include/`
- OS: Win32 (ConPTY) or POSIX (forkpty)

Vendored headers are fetched automatically by CMake on first configure.

## Build

```bash
cmake -S . -B build
cmake --build build --config Release
```

Run:

```bash
./build/stacksd               # Linux/macOS
.\build\Release\stacksd.exe   # Windows
```

Then open the web UI — it auto-discovers the daemon on port 17890.
