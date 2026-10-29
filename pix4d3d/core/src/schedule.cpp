#include "asec/schedule.hpp"

namespace asec {

CoalescingWorker::CoalescingWorker() { th_ = std::thread([this] { loop(); }); }

CoalescingWorker::~CoalescingWorker() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        quit_ = true; has_ = false; cancel_ = true;
    }
    cv_.notify_all();
    if (th_.joinable()) th_.join();
}

uint64_t CoalescingWorker::submit(bool final, Job job) {
    uint64_t g;
    {
        std::lock_guard<std::mutex> lk(mu_);
        g = ++gen_;
        if (has_) st_.coalesced++;
        pending_ = Pending{g, final, std::move(job)};
        has_ = true;
        st_.submitted++;
        if (running_ && (final || runningFinal_)) cancel_ = true;
    }
    cv_.notify_all();
    return g;
}

uint64_t CoalescingWorker::cancelAll() {
    std::lock_guard<std::mutex> lk(mu_);
    has_ = false;
    pending_ = Pending();
    if (running_) cancel_ = true;
    return ++gen_;
}

CoalescingWorker::Stats CoalescingWorker::stats() const {
    std::lock_guard<std::mutex> lk(mu_);
    return st_;
}

void CoalescingWorker::waitIdle() {
    std::unique_lock<std::mutex> lk(mu_);
    idleCv_.wait(lk, [&] { return !has_ && !running_; });
}

void CoalescingWorker::loop() {
    std::unique_lock<std::mutex> lk(mu_);
    for (;;) {
        cv_.wait(lk, [&] { return quit_ || has_; });
        if (quit_) return;
        Pending p = std::move(pending_);
        pending_ = Pending();
        has_ = false;
        running_ = true; runningFinal_ = p.final;
        cancel_ = false;
        st_.started++;
        lk.unlock();
        if (p.job) p.job(p.gen, p.final, &cancel_);
        lk.lock();
        if (cancel_.load()) st_.cancelled++;
        running_ = false;
        if (!has_) idleCv_.notify_all();
    }
}

}  // namespace asec
