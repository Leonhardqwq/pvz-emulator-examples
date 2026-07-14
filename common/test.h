#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <cmath>
#include <codecvt>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip> // std::put_time
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>
#include <windows.h>

std::wstring utf8_to_wstring(const std::string& str)
{
    std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
    return converter.from_bytes(str);
}

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

std::vector<std::string> parse_cmd_line()
{
    std::wstring cmd_line = GetCommandLineW();
    int argc;
    LPWSTR* argv = CommandLineToArgvW(cmd_line.c_str(), &argc);
    std::vector<std::wstring> args(argv, argv + argc);
    LocalFree(argv);

    std::vector<std::string> cmd_args;
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    for (const auto& arg : args) {
        cmd_args.push_back(converter.to_bytes(arg));
    }
    return cmd_args;
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

[[nodiscard]] std::vector<int> assign_repeat(int total_repeat_num, int thread_num)
{
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

// Standard error of the mean, given the sum, sum-of-squares, and count of samples.
[[nodiscard]] inline double sample_standard_error(double sum, double sum_sq, long long count)
{
    if (count < 2) {
        return 0.0;
    }
    double n = static_cast<double>(count);
    double mean = sum / n;
    double variance = (sum_sq - n * mean * mean) / (n - 1.0);
    return variance > 0.0 ? std::sqrt(variance / n) : 0.0;
}

// Formats "mean ± std": std is rounded to 2 significant figures and mean to the
// same number of decimal places. `suffix` (e.g. "%") is appended to both numbers.
// When std is non-positive (no meaningful spread), `fallback_precision` decimal
// places are used for the mean; callers should pass the precision they use on
// the std-disabled path so both outputs match.
[[nodiscard]] std::string format_mean_std(
    double mean, double sd, const std::string& suffix = "", int fallback_precision = 2)
{
    if (!std::isfinite(sd) || sd <= 0.0) {
        std::ostringstream os;
        os << std::fixed << std::setprecision(fallback_precision) << mean << suffix << " ± 0"
           << suffix;
        return os.str();
    }

    auto round_to = [](double value, int dp) {
        double scale = std::pow(10.0, dp);
        return std::round(value * scale) / scale;
    };

    int mag = static_cast<int>(std::floor(std::log10(sd)));
    int decimals = 1 - mag; // decimal places that keep 2 significant figures

    double sd_rounded = round_to(sd, decimals);
    // Rounding may bump the magnitude up (e.g. 0.0999 -> 0.10): re-fit decimals.
    if (sd_rounded > 0.0 && static_cast<int>(std::floor(std::log10(sd_rounded))) > mag) {
        decimals -= 1;
        sd_rounded = round_to(sd, decimals);
    }

    double mean_rounded = round_to(mean, decimals);

    std::ostringstream os;
    os << std::fixed << std::setprecision(std::max(decimals, 0)) << mean_rounded << suffix
       << " ± " << sd_rounded << suffix;
    return os.str();
}