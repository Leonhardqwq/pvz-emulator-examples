#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#ifdef _WIN32
#include <codecvt>
#include <locale>
#endif
#include <cstdlib>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip> // std::put_time
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#ifdef __APPLE__
#include <pthread.h>
#endif
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

#ifdef __APPLE__
class test_thread {
public:
    test_thread() = default;

    template <typename F>
    explicit test_thread(F&& f)
        : state_(std::make_shared<State>())
        , joined_(false)
    {
        state_->fn = std::forward<F>(f);

        pthread_attr_t attr;
        int rc = pthread_attr_init(&attr);
        if (rc != 0) {
            state_.reset();
            joined_ = true;
            throw std::runtime_error("创建线程失败");
        }

        rc = pthread_attr_setstacksize(&attr, 8 * 1024 * 1024);
        if (rc == 0) {
            rc = pthread_create(&thread_, &attr, run, state_.get());
        }
        pthread_attr_destroy(&attr);

        if (rc != 0) {
            state_.reset();
            joined_ = true;
            throw std::runtime_error("创建线程失败");
        }
    }

    test_thread(const test_thread&) = delete;
    test_thread& operator=(const test_thread&) = delete;

    test_thread(test_thread&& other) noexcept
        : thread_(other.thread_)
        , state_(std::move(other.state_))
        , joined_(other.joined_)
    {
        other.joined_ = true;
        other.thread_ = pthread_t();
    }

    test_thread& operator=(test_thread&& other) noexcept
    {
        if (this != &other) {
            if (joinable()) {
                std::terminate();
            }
            thread_ = other.thread_;
            state_ = std::move(other.state_);
            joined_ = other.joined_;
            other.joined_ = true;
            other.thread_ = pthread_t();
        }
        return *this;
    }

    ~test_thread()
    {
        if (joinable()) {
            std::terminate();
        }
    }

    [[nodiscard]] bool joinable() const
    {
        return state_ != nullptr && !joined_;
    }

    void join()
    {
        if (!joinable()) {
            return;
        }
        pthread_join(thread_, nullptr);
        joined_ = true;
        auto exception = state_->exception;
        state_.reset();
        if (exception) {
            std::rethrow_exception(exception);
        }
    }

private:
    struct State {
        std::function<void()> fn;
        std::exception_ptr exception;
    };

    static void* run(void* arg) noexcept
    {
        auto* state = static_cast<State*>(arg);
        try {
            state->fn();
        } catch (...) {
            state->exception = std::current_exception();
        }
        return nullptr;
    }

    pthread_t thread_ {};
    std::shared_ptr<State> state_;
    bool joined_ {true};
};
#else
using test_thread = std::thread;
#endif

#ifdef _WIN32
std::wstring utf8_to_wstring(const std::string& str)
{
    std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
    return converter.from_bytes(str);
}
#endif

// format: 2009.12.25_21.41.37
[[nodiscard]] std::string get_timestamp()
{
    std::time_t t = std::time(nullptr);
    std::tm* tm = std::localtime(&t);

    std::ostringstream os;
    os << std::put_time(tm, "%Y.%m.%d_%H.%M.%S");

    return os.str();
}

[[nodiscard]] std::pair<std::ofstream, std::string> open_csv(const std::string& filename)
{
    std::string full_filename = filename + " (" + get_timestamp() + ") .csv";
    std::filesystem::path path = std::filesystem::u8path(full_filename.c_str());
    std::ofstream file(path, std::ios::binary);

    if (!file) {
        std::cerr << "打开文件失败: " << full_filename << std::endl;
        throw std::runtime_error("打开文件失败");
    }

    file << "\xEF\xBB\xBF"; // UTF-8 BOM
    return {std::move(file), full_filename};
}

void setup_console_encoding()
{
#ifdef _WIN32
    ::system("chcp 65001 > nul");
#endif
}

std::vector<std::string> parse_cmd_line(int argc, char* argv[])
{
#ifdef _WIN32
    static_cast<void>(argc);
    static_cast<void>(argv);

    std::wstring cmd_line = GetCommandLineW();
    int wide_argc;
    LPWSTR* wide_argv = CommandLineToArgvW(cmd_line.c_str(), &wide_argc);
    std::vector<std::wstring> args(wide_argv, wide_argv + wide_argc);
    LocalFree(wide_argv);

    std::vector<std::string> cmd_args;
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    for (const auto& arg : args) {
        cmd_args.push_back(converter.to_bytes(arg));
    }
    return cmd_args;
#else
    std::vector<std::string> cmd_args;
    cmd_args.reserve(static_cast<size_t>(argc));
    for (int i = 0; i < argc; i++) {
        cmd_args.emplace_back(argv[i] == nullptr ? "" : argv[i]);
    }
    return cmd_args;
#endif
}

[[nodiscard]] std::string get_cmd_arg(const std::vector<std::string>& args,
    const std::string& option, const std::optional<std::string>& default_value = std::nullopt)
{
    auto it = std::find(args.begin(), args.end(), "-" + option);
    if (it != args.end() && ++it != args.end()) {
        return *it;
    }
    if (default_value.has_value()) {
        return *default_value;
    } else {
        std::cerr << "请提供参数: " << option << std::endl;
        exit(1);
        return "";
    }
}

[[nodiscard]] bool get_cmd_flag(const std::vector<std::string>& args, const std::string& option)
{
    return std::find(args.begin(), args.end(), "-" + option) != args.end();
}

[[nodiscard]] std::vector<int> assign_repeat(int total_repeat_num, unsigned int thread_count)
{
    if (total_repeat_num <= 0) {
        return {};
    }

    int thread_num = static_cast<int>(std::max(1U, thread_count));
    if (total_repeat_num < thread_num) {
        thread_num = total_repeat_num;
    }

    int base = total_repeat_num / thread_num;
    int extra = total_repeat_num % thread_num;

    std::vector<int> repeat_per_thread(thread_num, base);

    for (int i = 0; i < extra; ++i) {
        repeat_per_thread[i]++;
    }
    return repeat_per_thread;
}

[[nodiscard]] std::vector<std::string> split(const std::string& s, char delim)
{
    std::vector<std::string> tokens;
    std::istringstream iss(s);
    std::string token;

    while (std::getline(iss, token, delim)) {
        tokens.push_back(token);
    }
    return tokens;
}
