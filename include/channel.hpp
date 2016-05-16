#ifndef __CHANNEL_HPP__
#define __CHANNEL_HPP__


#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>

using std::queue;
using std::mutex;
using std::condition_variable;
using std::unique_lock;

template<typename T>
class channel {
    queue<T> queue;
    mutex m;
    condition_variable cv;

    bool closed;
    bool sealed;
    size_t buffer_size;
public:
    typedef T value_type;

    channel(size_t buffer_size = 0)
        : closed(false), sealed(false), buffer_size(buffer_size)
    {}

    channel(channel<T> const &) = delete;
    channel(channel<T> &&) = delete;

    ~channel() { close(); }

    void close() {
        unique_lock<mutex> lock(m);
        closed = true;
        lock.unlock();
        cv.notify_all();
    }
    void seal() {
        unique_lock<mutex> lock(m);
        sealed = true;
        lock.unlock();
        cv.notify_all();
    }
    bool is_closed() {
        unique_lock<mutex> lock(m);
        return closed;
    }
    bool is_sealed() {
        unique_lock<mutex> lock(m);
        return sealed;
    }
    bool is_empty() {
        unique_lock<mutex> lock(m);
        return queue.empty();
    }
    size_t size() {
        unique_lock<mutex> lock(m);
        return queue.size();
    }
    bool send(T const & arg) {
        unique_lock<mutex> lock(m);
        if (closed || sealed) {
            return false;
        }

        queue.push(arg);
        if (buffer_size > 0) {
            while(queue.size() > buffer_size) {
                queue.pop();
            }
        }
        lock.unlock();
        cv.notify_one();
        return true;
    }
    template<bool wait = true>
    bool recv(T & arg) {
        unique_lock<mutex> lock(m);
        if (!sealed && wait) {
            cv.wait(lock, [&]() {
                return closed || !queue.empty() || (sealed && queue.empty());
            });
        }
        if (closed) {
            return false;
        } else if (sealed && queue.empty()) {
            closed = true;
            lock.unlock();
            cv.notify_all();
            return false;
        } else if (queue.empty()) {
            return false;
        }

        arg = queue.front();
        queue.pop();

        if (queue.empty()) {
            lock.unlock();
            cv.notify_all();
        }
        return true;
    }
};

#endif
