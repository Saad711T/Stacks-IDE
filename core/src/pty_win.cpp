// Stacks IDE — Windows ConPTY implementation

#ifdef _WIN32

#include "stacks/pty.hpp"

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace stacks {

class WinPty : public Pty {
public:
    ~WinPty() override { close(); }

    bool spawn(const std::string& shell, int cols, int rows,
               std::function<void(const std::string&)> onData) override {
        HANDLE inRead = nullptr, inWrite = nullptr;
        HANDLE outRead = nullptr, outWrite = nullptr;
        if (!CreatePipe(&inRead, &inWrite, nullptr, 0)) return false;
        if (!CreatePipe(&outRead, &outWrite, nullptr, 0)) {
            CloseHandle(inRead); CloseHandle(inWrite); return false;
        }

        COORD size{ static_cast<SHORT>(cols > 0 ? cols : 80),
                    static_cast<SHORT>(rows > 0 ? rows : 24) };

        HRESULT hr = CreatePseudoConsole(size, inRead, outWrite, 0, &hpc_);
        CloseHandle(inRead); CloseHandle(outWrite);
        if (FAILED(hr)) { CloseHandle(inWrite); CloseHandle(outRead); return false; }

        // Prepare STARTUPINFOEX
        STARTUPINFOEXA si{};
        si.StartupInfo.cb = sizeof(si);
        SIZE_T attrSize = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
        attrList_.resize(attrSize);
        si.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrList_.data());
        if (!InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attrSize)) return false;
        if (!UpdateProcThreadAttribute(si.lpAttributeList, 0,
                PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, hpc_, sizeof(hpc_), nullptr, nullptr))
            return false;

        PROCESS_INFORMATION pi{};
        std::string cmd = shell;
        if (!CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                            EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr,
                            &si.StartupInfo, &pi))
            return false;

        child_ = pi.hProcess;
        CloseHandle(pi.hThread);
        writeHandle_ = inWrite;
        readHandle_ = outRead;
        onData_ = std::move(onData);
        running_ = true;

        reader_ = std::thread([this] {
            char buf[4096]; DWORD n = 0;
            while (running_ && ReadFile(readHandle_, buf, sizeof(buf), &n, nullptr) && n > 0) {
                if (onData_) onData_(std::string(buf, buf + n));
            }
        });
        return true;
    }

    void write(const std::string& data) override {
        if (!writeHandle_) return;
        DWORD w = 0;
        WriteFile(writeHandle_, data.data(), static_cast<DWORD>(data.size()), &w, nullptr);
    }

    void resize(int cols, int rows) override {
        if (!hpc_) return;
        COORD size{ static_cast<SHORT>(cols), static_cast<SHORT>(rows) };
        ResizePseudoConsole(hpc_, size);
    }

    void close() override {
        running_ = false;
        if (hpc_)         { ClosePseudoConsole(hpc_); hpc_ = nullptr; }
        if (writeHandle_) { CloseHandle(writeHandle_); writeHandle_ = nullptr; }
        if (readHandle_)  { CloseHandle(readHandle_); readHandle_ = nullptr; }
        if (child_)       { TerminateProcess(child_, 0); CloseHandle(child_); child_ = nullptr; }
        if (reader_.joinable()) reader_.join();
    }

private:
    HPCON hpc_ = nullptr;
    HANDLE writeHandle_ = nullptr;
    HANDLE readHandle_ = nullptr;
    HANDLE child_ = nullptr;
    std::atomic<bool> running_{false};
    std::thread reader_;
    std::vector<char> attrList_;
    std::function<void(const std::string&)> onData_;
};

std::unique_ptr<Pty> Pty::create() { return std::make_unique<WinPty>(); }

} // namespace stacks

#endif // _WIN32
