#pragma once

#include <cstdint>
#include <memory>

// Source: https://embeddedartistry.com/blog/2017/05/17/creating-a-circular-buffer-in-c-and-c/

template<class T>
class circular_buffer {
public:
    explicit circular_buffer(size_t size) :
            buf_(std::unique_ptr<T[]>(new T[size])),
            max_size_(size) {
    }

    void put(T item) {
        buf_[head_] = item;
        if (full_) {
            tail_ = (tail_ + 1) % max_size_;
        }
        head_ = (head_ + 1) % max_size_;
        full_ = head_ == tail_;
    }

    T get() {
        if (empty()) {
            return T();
        }

        //Read data and advance the tail (we now have a free space)
        auto val = buf_[tail_];
        full_ = false;
        tail_ = (tail_ + 1) % max_size_;

        return val;
    }

    T peek() {
        if (empty()) {
            return T();
        }
        return buf_[tail_];
    }

    // Clears the buffer.
    void reset() {
        head_ = tail_;
        full_ = false;
    }

    bool empty() const {
        //if head and tail are equal, we are empty
        return (!full_ && (head_ == tail_));
    }

    bool full() const {
        //If tail is ahead the head by 1, we are full
        return full_;
    }

    size_t capacity() const {
        return max_size_;
    }

    size_t size() const {
        size_t size = max_size_;

        if (!full_) {
            if (head_ >= tail_) {
                size = head_ - tail_;
            } else {
                size = max_size_ + head_ - tail_;
            }
        }

        return size;
    }

private:
    std::unique_ptr<T[]> buf_;
    size_t head_ = 0;
    size_t tail_ = 0;
    const size_t max_size_;
    bool full_ = 0;
};

template<class T>
class buffered_stream {
public:

    buffered_stream(size_t capacity, size_t threshold)
            : stream{capacity},
              read_threshold{threshold},
              read_threshold_reached{false} {}

    void put(T data) {
        stream.put(data);
        if (!read_threshold_reached && stream.size() > read_threshold) {
            read_threshold_reached = true;
        }
    }

    T get() {
        auto res = stream.get();
        if (stream.empty()) {
            read_threshold_reached = false;
        }
        return res;
    }

    size_t size() const {
        return stream.size();
    }

    bool can_read() const {
        return read_threshold_reached && !stream.empty();
    }

private:
    circular_buffer<T> stream;
    size_t read_threshold;
    bool read_threshold_reached;
};
