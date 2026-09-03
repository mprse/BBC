#include "bbc/app/password_reader.hpp"

#include <iostream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

namespace bbc::app {
namespace {

#ifdef _WIN32
class EchoGuard final {
public:
    EchoGuard() : input_(GetStdHandle(STD_INPUT_HANDLE)) {
        if (input_ == INVALID_HANDLE_VALUE || input_ == nullptr ||
            GetConsoleMode(input_, &original_mode_) == 0) {
            return;
        }

        active_ = SetConsoleMode(input_, original_mode_ & ~ENABLE_ECHO_INPUT) != 0;
    }

    EchoGuard(const EchoGuard&) = delete;
    EchoGuard& operator=(const EchoGuard&) = delete;

    ~EchoGuard() {
        if (active_) {
            SetConsoleMode(input_, original_mode_);
        }
    }

    [[nodiscard]] bool active() const noexcept {
        return active_;
    }

private:
    HANDLE input_ = INVALID_HANDLE_VALUE;
    DWORD original_mode_ = 0;
    bool active_ = false;
};
#else
class EchoGuard final {
public:
    EchoGuard() {
        if (isatty(STDIN_FILENO) == 0 || tcgetattr(STDIN_FILENO, &original_) != 0) {
            return;
        }

        termios hidden = original_;
        hidden.c_lflag &= static_cast<tcflag_t>(~ECHO);
        active_ = tcsetattr(STDIN_FILENO, TCSAFLUSH, &hidden) == 0;
    }

    EchoGuard(const EchoGuard&) = delete;
    EchoGuard& operator=(const EchoGuard&) = delete;

    ~EchoGuard() {
        if (active_) {
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_);
        }
    }

    [[nodiscard]] bool active() const noexcept {
        return active_;
    }

private:
    termios original_{};
    bool active_ = false;
};
#endif

}  // namespace

bool read_password_from_terminal(
    const std::string_view prompt,
    std::string& password,
    std::ostream& prompt_output
) {
    EchoGuard echo_guard;
    if (!echo_guard.active()) {
        return false;
    }

    prompt_output << prompt << std::flush;
    const bool read_succeeded = static_cast<bool>(std::getline(std::cin, password));
    prompt_output << '\n';
    return read_succeeded;
}

}  // namespace bbc::app
