/**
 * AlgoForge — include/dashboard/log_buffer.hpp
 *
 * Phase 5 (dashboard) — fixed-capacity in-memory log ring buffer.
 * Python oracle: python/algoforge/dashboard/log_buffer.py :: LogRingBuffer.
 * Header-only; thread-safe push (the dashboard logs from multiple sources).
 */
#pragma once

#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace algoforge::dashboard {

/** One captured log record. Mirrors the Python dict keys ts/level/component/msg. */
struct LogRecord {
    std::string ts;         ///< ISO-8601 UTC, seconds precision ("%Y-%m-%dT%H:%M:%SZ")
    std::string level;      ///< "INFO" / "WARNING" / ...
    std::string component;
    std::string msg;
};

class LogRingBuffer {
public:
    explicit LogRingBuffer(size_t capacity = 500) : capacity_(capacity) {}

    void push(LogRecord rec) {
        std::lock_guard<std::mutex> lk(mtx_);
        buf_.push_back(std::move(rec));
        while (buf_.size() > capacity_) buf_.pop_front();
    }

    /** Last n records in chronological order (all if n >= size). Mirrors Python tail(). */
    std::vector<LogRecord> tail(int n) const {
        std::lock_guard<std::mutex> lk(mtx_);
        const int size = static_cast<int>(buf_.size());
        const int start = (n < size && n > 0) ? size - n : 0;
        return std::vector<LogRecord>(buf_.begin() + start, buf_.end());
    }

    size_t size() const { std::lock_guard<std::mutex> lk(mtx_); return buf_.size(); }
    size_t capacity() const { return capacity_; }

private:
    size_t                  capacity_;
    std::deque<LogRecord>   buf_;
    mutable std::mutex      mtx_;
};

} // namespace algoforge::dashboard
