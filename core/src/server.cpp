// Stacks IDE — daemon entry point.
//
// HTTP API + a tiny WebSocket implementation for /ws/run and /ws/pty.
// We implement WS ourselves (RFC 6455 frames) on top of cpp-httplib's raw
// socket access because httplib does not ship WS support.

#include "httplib.h"
#include "json.hpp"
#include "stacks/runner.hpp"
#include "stacks/pty.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
  #include <winsock2.h>
  #include <ws2tcpip.h>
  using socket_t = SOCKET;
  #define CLOSESOCK closesocket
  #define SHUT_WR SD_SEND
#else
  #include <sys/socket.h>
  #include <unistd.h>
  #include <arpa/inet.h>
  using socket_t = int;
  #define CLOSESOCK ::close
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

static void handle_ws_client(socket_t fd);

/* ---------- configuration ---------- */
constexpr const char* LISTEN_HOST = "127.0.0.1";
constexpr int         LISTEN_PORT = 17890;

/* ---------- globals ---------- */
static std::string g_token;
static fs::path    g_workspace;

/* ---------- helpers ---------- */
static std::string load_or_create_token() {
    fs::path cfg;
#ifdef _WIN32
    const char* base = std::getenv("APPDATA");
    cfg = fs::path(base ? base : ".") / "StacksIDE";
#else
    const char* base = std::getenv("HOME");
    cfg = fs::path(base ? base : ".") / ".config" / "stacks-ide";
#endif
    std::error_code ec;
    fs::create_directories(cfg, ec);
    fs::path tokenFile = cfg / "token";
    if (fs::exists(tokenFile)) {
        std::ifstream f(tokenFile);
        std::string t; std::getline(f, t);
        if (!t.empty()) return t;
    }
    std::mt19937_64 rng{std::random_device{}()};
    std::string t;
    for (int i = 0; i < 32; i++) {
        int v = rng() % 36;
        t += (v < 10) ? char('0' + v) : char('a' + v - 10);
    }
    std::ofstream out(tokenFile); out << t;
    return t;
}

static bool path_safe(const fs::path& p) {
    auto ws = fs::weakly_canonical(g_workspace);
    auto can = fs::weakly_canonical(p);
    auto rel = fs::relative(can, ws);
    auto s = rel.string();
    return !s.empty() && s.find("..") != 0;
}

/* ---------- WebSocket frames (RFC 6455) ----------
   Minimal, single-connection-safe implementation. Supports text + binary,
   fragmented or not, with masked client frames. Control frames are handled
   (ping -> pong, close -> close). */

static std::string sha1_base64(const std::string& s);  // forward

struct WsConn {
    socket_t fd = -1;
    std::atomic<bool> alive{true};
    std::mutex write_mu;

    void close() {
        if (alive.exchange(false)) {
            shutdown(fd, SHUT_WR);
            CLOSESOCK(fd);
        }
    }

    bool send_frame(uint8_t opcode, const std::string& payload) {
        std::lock_guard<std::mutex> lk(write_mu);
        std::vector<uint8_t> f;
        f.push_back(0x80 | (opcode & 0x0F));
        size_t n = payload.size();
        if (n < 126) f.push_back(static_cast<uint8_t>(n));
        else if (n < 65536) {
            f.push_back(126);
            f.push_back((n >> 8) & 0xFF);
            f.push_back(n & 0xFF);
        } else {
            f.push_back(127);
            for (int i = 7; i >= 0; --i) f.push_back((n >> (i * 8)) & 0xFF);
        }
        f.insert(f.end(), payload.begin(), payload.end());
        size_t sent = 0;
        while (sent < f.size()) {
            int s = ::send(fd, reinterpret_cast<const char*>(f.data() + sent),
                           static_cast<int>(f.size() - sent), 0);
            if (s <= 0) return false;
            sent += s;
        }
        return true;
    }

    bool send_text(const std::string& s) { return send_frame(0x1, s); }

    // Blocking read of a full application message. Returns false when closed.
    bool recv_message(std::string& out, uint8_t& opcode) {
        out.clear();
        bool finished = false;
        bool first = true;
        while (!finished) {
            uint8_t hdr[2];
            if (!read_exact(hdr, 2)) return false;
            bool fin = hdr[0] & 0x80;
            uint8_t op = hdr[0] & 0x0F;
            if (first) { opcode = op; first = false; }
            bool masked = hdr[1] & 0x80;
            uint64_t len = hdr[1] & 0x7F;
            if (len == 126) {
                uint8_t e[2]; if (!read_exact(e, 2)) return false;
                len = (uint64_t(e[0]) << 8) | e[1];
            } else if (len == 127) {
                uint8_t e[8]; if (!read_exact(e, 8)) return false;
                len = 0;
                for (int i = 0; i < 8; i++) len = (len << 8) | e[i];
            }
            uint8_t mask[4] = {0,0,0,0};
            if (masked && !read_exact(mask, 4)) return false;
            std::string payload(len, 0);
            if (len && !read_exact(reinterpret_cast<uint8_t*>(payload.data()), len)) return false;
            if (masked) for (uint64_t i = 0; i < len; i++) payload[i] ^= mask[i % 4];

            if (op == 0x8) { send_frame(0x8, payload); return false; }       // close
            if (op == 0x9) { send_frame(0xA, payload); continue; }            // ping
            if (op == 0xA) { continue; }                                      // pong

            out += payload;
            if (fin) finished = true;
        }
        return true;
    }

private:
    bool read_exact(uint8_t* buf, size_t n) {
        size_t got = 0;
        while (got < n) {
            int r = ::recv(fd, reinterpret_cast<char*>(buf + got),
                           static_cast<int>(n - got), 0);
            if (r <= 0) return false;
            got += r;
        }
        return true;
    }
};

/* ---------- SHA-1 + base64 (tiny, only used for WS handshake) ---------- */
static uint32_t leftrotate(uint32_t v, int c) { return (v << c) | (v >> (32 - c)); }
static std::string sha1(const std::string& input) {
    std::vector<uint8_t> msg(input.begin(), input.end());
    uint64_t bits = msg.size() * 8;
    msg.push_back(0x80);
    while (msg.size() % 64 != 56) msg.push_back(0);
    for (int i = 7; i >= 0; --i) msg.push_back((bits >> (i * 8)) & 0xFF);
    uint32_t h0=0x67452301, h1=0xEFCDAB89, h2=0x98BADCFE, h3=0x10325476, h4=0xC3D2E1F0;
    for (size_t chunk = 0; chunk < msg.size(); chunk += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++)
            w[i] = (msg[chunk+i*4]<<24) | (msg[chunk+i*4+1]<<16) | (msg[chunk+i*4+2]<<8) | msg[chunk+i*4+3];
        for (int i = 16; i < 80; i++)
            w[i] = leftrotate(w[i-3]^w[i-8]^w[i-14]^w[i-16], 1);
        uint32_t a=h0,b=h1,c=h2,d=h3,e=h4;
        for (int i = 0; i < 80; i++) {
            uint32_t f,k;
            if (i<20)      { f=(b&c)|((~b)&d); k=0x5A827999; }
            else if (i<40) { f=b^c^d;          k=0x6ED9EBA1; }
            else if (i<60) { f=(b&c)|(b&d)|(c&d); k=0x8F1BBCDC; }
            else           { f=b^c^d;          k=0xCA62C1D6; }
            uint32_t tmp = leftrotate(a,5)+f+e+k+w[i];
            e=d; d=c; c=leftrotate(b,30); b=a; a=tmp;
        }
        h0+=a; h1+=b; h2+=c; h3+=d; h4+=e;
    }
    std::string out(20, 0);
    auto put = [&](int offs, uint32_t v){ for (int i=0;i<4;i++) out[offs+i] = (v >> (24-i*8)) & 0xFF; };
    put(0,h0); put(4,h1); put(8,h2); put(12,h3); put(16,h4);
    return out;
}
static std::string base64(const std::string& in) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0, valb = -6;
    for (unsigned char c : in) {
        val = (val << 8) | c;
        valb += 8;
        while (valb >= 0) { out.push_back(T[(val >> valb) & 0x3F]); valb -= 6; }
    }
    if (valb > -6) out.push_back(T[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}
static std::string ws_accept_key(const std::string& key) {
    return base64(sha1(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"));
}

/* ---------- CORS + auth helpers ---------- */
static void add_cors(httplib::Response& res) {
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Headers", "Content-Type, X-Stacks-Token");
    res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
}
static bool check_token(const httplib::Request& req) {
    auto t = req.get_header_value("X-Stacks-Token");
    // Allow if running in same-origin dev mode OR token matches.
    return t == g_token;
}

/* ---------- main ---------- */
int main(int argc, char** argv) {
#if defined(_WIN32)
    WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    g_token = load_or_create_token();
    g_workspace = (argc > 1) ? fs::path(argv[1]) : fs::current_path() / "workspace";
    fs::create_directories(g_workspace);

    std::cout << "Stacks IDE daemon\n"
              << "  workspace: " << g_workspace << "\n"
              << "  listening: http://" << LISTEN_HOST << ":" << LISTEN_PORT << "\n"
              << "  token:     " << g_token << "\n";

    httplib::Server svr;

    svr.Options(R"(.*)", [](const httplib::Request&, httplib::Response& res) {
        add_cors(res); res.status = 204;
    });

    svr.Get("/api/health", [](const httplib::Request&, httplib::Response& res) {
        add_cors(res);
        json j; j["ok"] = true; j["version"] = "0.2.0"; j["token"] = g_token;
        res.set_content(j.dump(), "application/json");
    });

    svr.Get("/api/toolchains", [](const httplib::Request& req, httplib::Response& res) {
        add_cors(res);
        auto tc = stacks::detect_toolchains();
        json j;
        j["python"] = !tc.python.empty();
        j["node"]   = !tc.node.empty();
        j["cpp"]    = !tc.cpp.empty();
        j["c"]      = !tc.c.empty();
        j["java"]   = !tc.java.empty() && !tc.javac.empty();
        j["shell"]  = !tc.shell.empty();
        j["paths"]  = { {"python", tc.python}, {"node", tc.node}, {"cpp", tc.cpp},
                        {"c", tc.c}, {"java", tc.java}, {"shell", tc.shell} };
        res.set_content(j.dump(), "application/json");
    });

    svr.Get("/api/fs/list", [](const httplib::Request& req, httplib::Response& res) {
        add_cors(res);
        if (!check_token(req)) { res.status = 401; return; }
        json out = json::array();
        for (auto& e : fs::recursive_directory_iterator(g_workspace)) {
            auto rel = fs::relative(e.path(), g_workspace).generic_string();
            out.push_back({ {"path", rel}, {"type", e.is_directory() ? "dir" : "file"} });
        }
        res.set_content(out.dump(), "application/json");
    });

    svr.Get("/api/fs/read", [](const httplib::Request& req, httplib::Response& res) {
        add_cors(res);
        if (!check_token(req)) { res.status = 401; return; }
        auto path = req.get_param_value("path");
        auto full = g_workspace / path;
        if (!path_safe(full) || !fs::exists(full)) { res.status = 404; return; }
        std::ifstream f(full, std::ios::binary);
        std::stringstream ss; ss << f.rdbuf();
        json j; j["content"] = ss.str();
        res.set_content(j.dump(), "application/json");
    });

    svr.Post("/api/fs/write", [](const httplib::Request& req, httplib::Response& res) {
        add_cors(res);
        if (!check_token(req)) { res.status = 401; return; }
        try {
            auto j = json::parse(req.body);
            auto path = j.at("path").get<std::string>();
            auto content = j.at("content").get<std::string>();
            auto full = g_workspace / path;
            if (!path_safe(full)) { res.status = 400; return; }
            fs::create_directories(full.parent_path());
            std::ofstream f(full, std::ios::binary);
            f.write(content.data(), content.size());
            res.set_content("{\"ok\":true}", "application/json");
        } catch (std::exception& e) {
            res.status = 400;
            res.set_content(std::string("{\"error\":\"") + e.what() + "\"}", "application/json");
        }
    });

    // Upgrade handler for /ws/run and /ws/pty. httplib exposes the raw socket
    // through set_pre_routing_handler + custom upgrade logic.
    svr.set_pre_routing_handler([](const httplib::Request& req, httplib::Response& res) {
        auto upgrade = req.get_header_value("Upgrade");
        if (upgrade != "websocket") return httplib::Server::HandlerResponse::Unhandled;

        auto key = req.get_header_value("Sec-WebSocket-Key");
        auto accept = ws_accept_key(key);

        std::string target = req.path;  // captured for thread

        // cpp-httplib does not let us hijack the socket directly. We send the
        // upgrade response as HTTP and mark the connection to be taken over by
        // hijacking the socket in a post handler using content_receiver tricks.
        // Simpler: we write the handshake then dispatch on a background thread
        // using the raw file descriptor stashed by httplib.
        res.status = 101;
        res.set_header("Upgrade", "websocket");
        res.set_header("Connection", "Upgrade");
        res.set_header("Sec-WebSocket-Accept", accept);
        // httplib will close the socket after handling this response, so we
        // instead route WebSockets via a dedicated listening thread below.
        res.set_header("X-Stacks-WS", target);
        return httplib::Server::HandlerResponse::Handled;
    });

    // --- Dedicated WS listener on a side port is simpler than hijacking httplib.
    // We run the HTTP server on LISTEN_PORT and a raw WS acceptor on LISTEN_PORT+1.
    std::thread ws_thread([] {
        socket_t lst = ::socket(AF_INET, SOCK_STREAM, 0);
        int yes = 1;
        setsockopt(lst, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
        sockaddr_in addr{}; addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(0x7F000001); // 127.0.0.1
        addr.sin_port = htons(LISTEN_PORT + 1);
        if (::bind(lst, (sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); return; }
        listen(lst, 8);

        while (true) {
            sockaddr_in ca{}; socklen_t cal = sizeof(ca);
            socket_t c = ::accept(lst, (sockaddr*)&ca, &cal);
            if (c < 0) break;
            std::thread([c] { handle_ws_client(c); }).detach();
        }
    });

    std::cout << "  ws:        ws://" << LISTEN_HOST << ":" << (LISTEN_PORT + 1) << "\n"
              << "Ready.\n";

    svr.listen(LISTEN_HOST, LISTEN_PORT);

    ws_thread.join();
#if defined(_WIN32)
    WSACleanup();
#endif
    return 0;
}

/* ---------- WebSocket client handler ----------
   Implements a minimalist HTTP-style handshake on the raw socket, then
   dispatches to run/pty handlers based on the requested path. */

static void ws_handshake(socket_t fd, const std::string& key) {
    std::string resp =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + ws_accept_key(key) + "\r\n\r\n";
    ::send(fd, resp.data(), (int)resp.size(), 0);
}

static void handle_run_ws(WsConn& c) {
    std::string msg; uint8_t op = 0;
    if (!c.recv_message(msg, op)) return;
    json req;
    try { req = json::parse(msg); } catch (...) {
        c.send_text("{\"type\":\"exit\",\"code\":-1}"); c.close(); return;
    }
    stacks::RunRequest r;
    r.workspace_root = g_workspace.string();
    r.path     = req.value("path", "");
    r.language = req.value("language", "shell");
    r.args     = req.value("args", "");

    auto result = stacks::run_sync(r, [&](const std::string& data, bool is_err) {
        json out = { {"type", is_err ? "stderr" : "stdout"}, {"data", data} };
        c.send_text(out.dump());
    });
    json ex = { {"type", "exit"}, {"code", result.exit_code} };
    c.send_text(ex.dump());
    c.close();
}

static void handle_pty_ws(WsConn& c) {
    auto pty = stacks::Pty::create();
#ifdef _WIN32
    std::string shell = "cmd.exe";
#else
    std::string shell = std::getenv("SHELL") ? std::getenv("SHELL") : "/bin/bash";
#endif
    if (!pty->spawn(shell, 80, 24, [&](const std::string& data) {
        json out = { {"type", "stdout"}, {"data", data} };
        c.send_text(out.dump());
    })) {
        c.send_text("{\"type\":\"stderr\",\"data\":\"failed to spawn pty\"}");
        c.close(); return;
    }
    while (true) {
        std::string msg; uint8_t op = 0;
        if (!c.recv_message(msg, op)) break;
        try {
            auto j = json::parse(msg);
            auto type = j.value("type", "");
            if (type == "stdin") pty->write(j.value("data", ""));
            else if (type == "resize") pty->resize(j.value("cols", 80), j.value("rows", 24));
        } catch (...) {}
    }
    pty->close();
    c.close();
}

static void handle_ws_client(socket_t fd) {
    // Read request line + headers (up to 8KB)
    std::string buf; buf.resize(8192);
    int n = ::recv(fd, buf.data(), (int)buf.size(), 0);
    if (n <= 0) { CLOSESOCK(fd); return; }
    buf.resize(n);

    // Parse request line
    auto eol = buf.find("\r\n");
    std::string line = buf.substr(0, eol);
    std::string method, path, proto;
    {
        std::istringstream iss(line);
        iss >> method >> path >> proto;
    }
    // Parse headers (simple case-insensitive)
    std::string key, token;
    size_t pos = eol + 2;
    while (pos < buf.size()) {
        auto nxt = buf.find("\r\n", pos);
        if (nxt == std::string::npos || nxt == pos) break;
        std::string h = buf.substr(pos, nxt - pos);
        auto colon = h.find(':');
        if (colon != std::string::npos) {
            std::string name = h.substr(0, colon);
            std::string val  = h.substr(colon + 1);
            while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) val.erase(val.begin());
            for (auto& ch : name) ch = std::tolower(ch);
            if (name == "sec-websocket-key") key = val;
            if (name == "x-stacks-token") token = val;
        }
        pos = nxt + 2;
    }
    if (key.empty()) { CLOSESOCK(fd); return; }

    // Extract token from query (?token=) if header absent (browsers can't set headers on WS easily).
    if (token.empty()) {
        auto q = path.find("?token=");
        if (q != std::string::npos) token = path.substr(q + 7);
    }
    if (token != g_token) {
        std::string r = "HTTP/1.1 401 Unauthorized\r\nContent-Length: 0\r\n\r\n";
        ::send(fd, r.data(), (int)r.size(), 0);
        CLOSESOCK(fd);
        return;
    }

    ws_handshake(fd, key);

    WsConn c; c.fd = fd;
    if (path.rfind("/ws/run", 0) == 0)      handle_run_ws(c);
    else if (path.rfind("/ws/pty", 0) == 0) handle_pty_ws(c);
    else c.close();
}
