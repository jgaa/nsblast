#pragma once

#include <boost/type_index.hpp>
#include <boost/type_index/runtime_cast/register_runtime_class.hpp>


namespace nsblast::logging {

template <typename T>
std::string_view logName(const T *self) noexcept {
    static const auto name = boost::typeindex::type_id_runtime(*self).pretty_name();
    return name;
}
} // ns

#define LOGFAULT_USE_TID_AS_NAME 1

#include "logfault/logfault.h"

#define LOG_ERROR   LFLOG_ERROR
#define LOG_WARN    LFLOG_WARN
#define LOG_INFO    LFLOG_INFO
#define LOG_DEBUG   LFLOG_DEBUG
#define LOG_TRACE   LFLOG_TRACE

#define LOG_ERROR_N   LFLOG_ERROR_EX
#define LOG_WARN_N    LFLOG_WARN_EX
#define LOG_INFO_N    LFLOG_INFO_EX
#define LOG_DEBUG_N   LFLOG_DEBUG_EX
#define LOG_TRACE_N   LFLOG_TRACE_EX
