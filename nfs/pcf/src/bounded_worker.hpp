// One background thread running queued jobs in order, with a bounded queue (REVIEW-5ae0ef1 SUS-02
// class: no unbounded threads or queues). Used for the PCF's outbound V2X provisioning calls
// (ADR-0551) so a slow UDM or AMF never holds an SBI server thread.
#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <stop_token>
#include <thread>
#include <utility>

namespace pcf {

class BoundedWorker {
public:
    explicit BoundedWorker(std::size_t capacity)
        : capacity_(capacity), thread_([this](std::stop_token st) { run(st); }) {}

    BoundedWorker(const BoundedWorker&) = delete;
    BoundedWorker& operator=(const BoundedWorker&) = delete;

    // False, with the job dropped, when the queue already holds `capacity` jobs.
    bool try_post(std::function<void()> job) {
        {
            std::lock_guard lock(mutex_);
            if (jobs_.size() >= capacity_) {
                return false;
            }
            jobs_.push_back(std::move(job));
        }
        cv_.notify_one();
        return true;
    }

private:
    void run(std::stop_token st) {
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock lock(mutex_);
                if (!cv_.wait(lock, st, [this] { return !jobs_.empty(); })) {
                    return; // stop requested; queued jobs are dropped
                }
                job = std::move(jobs_.front());
                jobs_.pop_front();
            }
            job();
        }
    }

    const std::size_t capacity_;
    std::mutex mutex_;
    std::condition_variable_any cv_;
    std::deque<std::function<void()>> jobs_;
    std::jthread thread_; // last member: started after, and joined before, the others
};

} // namespace pcf
