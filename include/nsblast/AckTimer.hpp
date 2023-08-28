#pragma once

#include <chrono>
#include <boost/asio.hpp>

#include "nsblast/logging.h"

namespace nsblast::lib {

template <typename ctxT, typename fnT> class AckTimer {
    public:

    AckTimer(ctxT& ctx, fnT fn)
            : timer_{ctx}, fn_{std::move(fn)} {}

    auto startIfIdle(uint64_t millisec) {
        std::lock_guard lock{mutex_};
        if (active_) {
            return;
        }

        timer_.expires_from_now(boost::posix_time::millisec{millisec});
        active_ = true;
        LOG_TRACE_N << "Starting timer with " << millisec
                    << " milliseconds time-out";

        timer_.async_wait([this](boost::system::error_code ec) {
            {
                std::lock_guard lock{mutex_};
                assert(active_);
                active_ = false;
            }
            if (ec) {
                if (ec == boost::asio::error::operation_aborted) {
                    LOG_TRACE << "AckTimer::fn - Timer aborted.";
                    return;
                } else {
                    LOG_WARN << "AckTimer::fn - Timer failed with error: " << ec.message();
                }
            }

            LOG_TRACE << "AckTimer::fn - calling fn.";
            fn_();
        });
    }

    void cancel() {
        boost::system::error_code ec;
        std::lock_guard lock{mutex_};
        active_ = false;
        timer_.cancel(ec);
    }

    private:
        fnT fn_;
        bool active_ = false;
        boost::asio::deadline_timer timer_;
        std::mutex mutex_;
};
} // ns
