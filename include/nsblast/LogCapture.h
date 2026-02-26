#pragma once

#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "nsblast/logging.h"

namespace nsblast::logging {

struct CapturedLogLine {
    std::string level;
    std::string line;
};

class LogRingBuffer final {
public:
    static LogRingBuffer& instance() noexcept;

    void append(const logfault::Message& msg) noexcept;
    std::vector<CapturedLogLine> snapshot() const;

private:
    static constexpr std::size_t capacity_ = 1000;

    mutable std::mutex mutex_;
    std::deque<CapturedLogLine> lines_;
};

class RingBufferHandler final : public logfault::Handler {
public:
    explicit RingBufferHandler(logfault::LogLevel level)
        : Handler(level) {}

    void LogMessage(const logfault::Message& msg) LOGFAULT_NOEXCEPT override;
};

logfault::LogLevel parseLogLevel(std::string_view name, logfault::LogLevel fallback = logfault::LogLevel::INFO) noexcept;

} // namespace nsblast::logging
