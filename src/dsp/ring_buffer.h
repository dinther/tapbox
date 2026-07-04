#pragma once
#include <atomic>
#include <cstddef>
#include <cstring>
#include <algorithm>

namespace audio_dsp {

/// Lock-free single-producer single-consumer ring buffer.
/// Producer calls write(), consumer calls peek()/advance()/read().
/// Thread safety is provided by atomic operations on size_.
template <typename T, size_t N>
class RingBuffer {
 public:
    size_t write(const T *data, size_t count) {
        size_t sz = size_.load(std::memory_order_acquire);
        size_t space = N - sz;
        size_t to_write = std::min(count, space);
        if (to_write == 0) return 0;

        // Split at wraparound boundary for memcpy optimization
        size_t first = std::min(to_write, N - write_pos_);
        std::memcpy(&buf_[write_pos_], data, first * sizeof(T));
        if (to_write > first) {
            std::memcpy(&buf_[0], data + first, (to_write - first) * sizeof(T));
        }
        write_pos_ = (write_pos_ + to_write) % N;
        size_.store(sz + to_write, std::memory_order_release);
        return to_write;
    }

    size_t read(T *out, size_t count) {
        size_t sz = size_.load(std::memory_order_acquire);
        size_t to_read = std::min(count, sz);
        if (to_read == 0) return 0;

        // Split at wraparound boundary for memcpy optimization
        size_t first = std::min(to_read, N - read_pos_);
        std::memcpy(out, &buf_[read_pos_], first * sizeof(T));
        if (to_read > first) {
            std::memcpy(out + first, &buf_[0], (to_read - first) * sizeof(T));
        }
        read_pos_ = (read_pos_ + to_read) % N;
        size_.store(sz - to_read, std::memory_order_release);
        return to_read;
    }

    size_t peek(T *out, size_t count) const {
        size_t sz = size_.load(std::memory_order_acquire);
        size_t to_read = std::min(count, sz);
        if (to_read == 0) return 0;

        // Split at wraparound boundary for memcpy optimization
        size_t first = std::min(to_read, N - read_pos_);
        std::memcpy(out, &buf_[read_pos_], first * sizeof(T));
        if (to_read > first) {
            std::memcpy(out + first, &buf_[0], (to_read - first) * sizeof(T));
        }
        return to_read;
    }

    void advance(size_t count) {
        size_t sz = size_.load(std::memory_order_acquire);
        size_t to_advance = std::min(count, sz);
        read_pos_ = (read_pos_ + to_advance) % N;
        size_.store(sz - to_advance, std::memory_order_release);
    }

    size_t available() const { return size_.load(std::memory_order_acquire); }
    size_t capacity() const { return N; }
    void clear() {
        read_pos_ = 0;
        write_pos_ = 0;
        size_.store(static_cast<size_t>(0), std::memory_order_release);
    }

 private:
    T buf_[N]{};
    size_t read_pos_{0};
    size_t write_pos_{0};
    std::atomic<size_t> size_{0};
};

}  // namespace audio_dsp
