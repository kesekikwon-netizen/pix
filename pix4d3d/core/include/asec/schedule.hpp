// 단면 계산 예약(Qt 없음): 끌기 중 미리보기 요청을 합치고(coalescing), 최종 계산은 우선한다.
// 규칙
//  - 새 요청은 항상 대기 중인 요청을 대체한다(마지막 것만 남음)
//  - 미리보기는 실행 중인 미리보기를 취소하지 않는다 → 끄는 동안에도 결과가 계속 갱신됨(취소만 반복되어 아무것도 안 보이는 일 없음)
//  - 최종(final) 요청이나, 실행 중인 것이 최종일 때의 새 요청은 실행 중인 계산을 취소한다
#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>

namespace asec {

class CoalescingWorker {
public:
    using Job = std::function<void(uint64_t gen, bool final, const std::atomic<bool>* cancel)>;
    CoalescingWorker();
    ~CoalescingWorker();
    CoalescingWorker(const CoalescingWorker&) = delete;
    CoalescingWorker& operator=(const CoalescingWorker&) = delete;

    /// 요청을 넣는다. 돌려준 세대 번호(gen)는 단조 증가
    uint64_t submit(bool final, Job job);
    /// 대기 중 요청을 버리고 실행 중인 계산을 취소(새 장면 열기 등). 이후 세대 번호를 돌려줌
    uint64_t cancelAll();
    uint64_t latestGen() const { return gen_.load(); }
    struct Stats { size_t submitted = 0, started = 0, cancelled = 0, coalesced = 0; };
    Stats stats() const;
    /// 대기·실행이 모두 끝날 때까지 기다림(시험용)
    void waitIdle();

private:
    struct Pending { uint64_t gen = 0; bool final = false; Job job; };
    mutable std::mutex mu_;
    std::condition_variable cv_, idleCv_;
    bool has_ = false, quit_ = false, running_ = false, runningFinal_ = false;
    Pending pending_;
    std::atomic<bool> cancel_{false};
    std::atomic<uint64_t> gen_{0};
    Stats st_;
    std::thread th_;
    void loop();
};

/// GUI 쪽 결과 받기 규칙: 이미 보여준 것보다 새 세대의 결과만 받는다(오래된 미리보기가 새 결과를 덮지 않음)
struct ResultGate {
    uint64_t shownGen = 0;
    bool accept(uint64_t gen) { if (gen <= shownGen) return false; shownGen = gen; return true; }
};

}  // namespace asec
