#pragma once

#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace easywork {

enum class RuntimeLogLevel {
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warn = 3,
    Error = 4,
};

enum class RuntimeLogFormat {
    Text = 0,
    Json = 1,
};

class RuntimeLogger {
public:
    static RuntimeLogger& Instance() {
        static RuntimeLogger logger;
        return logger;
    }

    void Configure(RuntimeLogLevel level,
                   RuntimeLogFormat format,
                   std::string file_path = {},
                   std::string trace_id = {}) {
        std::lock_guard<std::mutex> lock(mutex_);
        level_ = level;
        format_ = format;
        if (trace_id.empty()) {
            trace_id_ = BuildDefaultTraceId();
        } else {
            trace_id_ = std::move(trace_id);
        }
        file_.close();
        file_path_ = std::move(file_path);
        if (!file_path_.empty()) {
            file_.open(file_path_, std::ios::out | std::ios::app);
            if (!file_) {
                throw std::runtime_error("Failed to open log file: " + file_path_);
            }
        }
    }

    RuntimeLogLevel Level() const {
        return level_;
    }

    RuntimeLogFormat Format() const {
        return format_;
    }

    void Log(RuntimeLogLevel level,
             std::string_view message,
             std::unordered_map<std::string, std::string> fields = {}) {
        if (level < level_) {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        fields.emplace("trace_id", trace_id_);
        fields.emplace("message", std::string(message));
        const std::string line = format_ == RuntimeLogFormat::Json
                                     ? ToJson(level, fields)
                                     : ToText(level, fields);
        std::ostream& out = file_.is_open() ? file_ : std::cerr;
        out << line << '\n';
        out.flush();
    }

private:
    RuntimeLogger() = default;

    static std::string BuildTimestamp() {
        const auto now = std::chrono::system_clock::now();
        const auto now_time = std::chrono::system_clock::to_time_t(now);
        const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                                now.time_since_epoch()) %
                            1000;
        std::tm tm_buf{};
#if defined(_WIN32)
        gmtime_s(&tm_buf, &now_time);
#else
        gmtime_r(&now_time, &tm_buf);
#endif
        std::ostringstream oss;
        oss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%S")
            << '.' << std::setw(3) << std::setfill('0') << millis.count() << 'Z';
        return oss.str();
    }

    static std::string BuildDefaultTraceId() {
        static std::atomic<uint64_t> seq{0};
        const uint64_t id = ++seq;
        return "ew-" + std::to_string(id);
    }

    static std::string EscapeJson(std::string_view input) {
        std::ostringstream oss;
        for (char ch : input) {
            switch (ch) {
                case '\"':
                    oss << "\\\"";
                    break;
                case '\\':
                    oss << "\\\\";
                    break;
                case '\b':
                    oss << "\\b";
                    break;
                case '\f':
                    oss << "\\f";
                    break;
                case '\n':
                    oss << "\\n";
                    break;
                case '\r':
                    oss << "\\r";
                    break;
                case '\t':
                    oss << "\\t";
                    break;
                default:
                    if (static_cast<unsigned char>(ch) < 0x20) {
                        oss << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                            << static_cast<int>(static_cast<unsigned char>(ch)) << std::dec;
                    } else {
                        oss << ch;
                    }
            }
        }
        return oss.str();
    }

    static const char* LevelName(RuntimeLogLevel level) {
        switch (level) {
            case RuntimeLogLevel::Trace:
                return "trace";
            case RuntimeLogLevel::Debug:
                return "debug";
            case RuntimeLogLevel::Info:
                return "info";
            case RuntimeLogLevel::Warn:
                return "warn";
            case RuntimeLogLevel::Error:
                return "error";
        }
        return "info";
    }

    static std::string ToText(RuntimeLogLevel level,
                              const std::unordered_map<std::string, std::string>& fields) {
        std::ostringstream oss;
        oss << "time=" << BuildTimestamp() << " level=" << LevelName(level);
        for (const auto& [k, v] : fields) {
            oss << ' ' << k << '=' << v;
        }
        return oss.str();
    }

    static std::string ToJson(RuntimeLogLevel level,
                              const std::unordered_map<std::string, std::string>& fields) {
        std::ostringstream oss;
        oss << "{\"time\":\"" << EscapeJson(BuildTimestamp()) << "\","
            << "\"level\":\"" << EscapeJson(LevelName(level)) << "\"";
        for (const auto& [k, v] : fields) {
            oss << ",\"" << EscapeJson(k) << "\":\"" << EscapeJson(v) << "\"";
        }
        oss << "}";
        return oss.str();
    }

    mutable std::mutex mutex_;
    RuntimeLogLevel level_{RuntimeLogLevel::Info};
    RuntimeLogFormat format_{RuntimeLogFormat::Text};
    std::string file_path_;
    std::string trace_id_{"ew-0"};
    std::ofstream file_;
};

inline RuntimeLogLevel ParseRuntimeLogLevel(std::string value) {
    for (char& c : value) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (value == "trace") {
        return RuntimeLogLevel::Trace;
    }
    if (value == "debug") {
        return RuntimeLogLevel::Debug;
    }
    if (value == "info") {
        return RuntimeLogLevel::Info;
    }
    if (value == "warn" || value == "warning") {
        return RuntimeLogLevel::Warn;
    }
    if (value == "error") {
        return RuntimeLogLevel::Error;
    }
    throw std::runtime_error("Unknown log level: " + value);
}

inline RuntimeLogFormat ParseRuntimeLogFormat(std::string value) {
    for (char& c : value) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (value == "text") {
        return RuntimeLogFormat::Text;
    }
    if (value == "json") {
        return RuntimeLogFormat::Json;
    }
    throw std::runtime_error("Unknown log format: " + value);
}

inline void LogRuntime(RuntimeLogLevel level,
                       std::string_view message,
                       std::unordered_map<std::string, std::string> fields = {}) {
    RuntimeLogger::Instance().Log(level, message, std::move(fields));
}

} // namespace easywork
