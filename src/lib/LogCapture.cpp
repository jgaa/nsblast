#include <sstream>

#include "nsblast/LogCapture.h"

namespace nsblast::logging {

LogRingBuffer& LogRingBuffer::instance() noexcept {
    static LogRingBuffer instance;
    return instance;
}

void LogRingBuffer::append(const logfault::Message& msg) noexcept {
    try {
        std::ostringstream out;
        logfault::Handler::PrintMessage(out, msg);

        CapturedLogLine line{
            std::string{logfault::Handler::LevelName(msg.level_)},
            out.str()
        };

        std::lock_guard lock{mutex_};
        if (lines_.size() >= capacity_) {
            lines_.pop_front();
        }
        lines_.emplace_back(std::move(line));
    } catch (...) {
        // Never throw from a log handler.
    }
}

std::vector<CapturedLogLine> LogRingBuffer::snapshot() const {
    std::lock_guard lock{mutex_};
    return {lines_.begin(), lines_.end()};
}

void RingBufferHandler::LogMessage(const logfault::Message& msg) LOGFAULT_NOEXCEPT {
    LogRingBuffer::instance().append(msg);
}

logfault::LogLevel parseLogLevel(const std::string_view name, const logfault::LogLevel fallback) noexcept {
    if (name == "trace") {
        return logfault::LogLevel::TRACE;
    }
    if (name == "debug") {
        return logfault::LogLevel::DEBUGGING;
    }
    if (name == "info") {
        return logfault::LogLevel::INFO;
    }
    if (name == "notice") {
        return logfault::LogLevel::NOTICE;
    }
    if (name == "warn" || name == "warning") {
        return logfault::LogLevel::WARN;
    }
    if (name == "error") {
        return logfault::LogLevel::ERROR;
    }
    if (name == "off" || name == "false" || name == "disabled" || name.empty()) {
        return logfault::LogLevel::DISABLED;
    }

    return fallback;
}

} // namespace nsblast::logging
