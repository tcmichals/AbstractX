/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX HAL: bus lock ([SPEC-HAL-14]). Loop-context state only: lock requests and the device
 * transactions of other callers wait in one first-come list; no interrupt touches it.
 */

#ifndef ABSTRACTX_HAL_BUS_LOCK_HPP
#define ABSTRACTX_HAL_BUS_LOCK_HPP

#include <coroutine>
#include <cstdint>
#include <utility>

#include "abstractx/domain_dispatcher.hpp"

namespace abstractx::hal {

// @impl [SPEC-HAL-14] docs/DESIGN_SPECIFICATION.md
class BusLock {
public:
    // A waiting lock request (run == nullptr) or a waiting transaction (run submits or cancels it).
    // Lives in the awaiting coroutine frame until it is granted, submitted or cancelled.
    struct Waiter {
        Waiter*                 next{nullptr};
        void                  (*run)(Waiter* self, bool cancel) noexcept {nullptr};
        void*                   self{nullptr};
        std::coroutine_handle<> handle{};
        uint32_t                granted{0};
    };

    // A transaction made under `token` (0 = none) must wait while someone else holds the bus.
    bool blocks(uint32_t token) const noexcept { return owner_ != 0 && owner_ != token; }
    bool held() const noexcept { return owner_ != 0; }

    // Token when the bus was free (now held), 0 when it is held by someone else.
    uint32_t try_acquire() noexcept {
        if (owner_ != 0) {
            return 0;
        }
        owner_ = next_token();
        return owner_;
    }

    void wait(Waiter* w) noexcept {
        w->next = nullptr;
        if (tail_ != nullptr) {
            tail_->next = w;
        } else {
            head_ = w;
        }
        tail_ = w;
    }

    // Release by the holder: submit waiting transactions in order up to the next lock request,
    // which then holds the bus.
    void release(uint32_t token) noexcept {
        if (token == 0 || token != owner_) {
            return;
        }
        owner_ = 0;
        while (Waiter* w = pop()) {
            if (w->run == nullptr) {
                owner_ = next_token();
                w->granted = owner_;
                (void)Dispatcher::push(w->handle);
                return;
            }
            w->run(w, false);
        }
    }

    // Waiting transactions complete with Cancelled; waiting lock requests get an empty guard.
    void cancel_waiters() noexcept {
        while (Waiter* w = pop()) {
            if (w->run == nullptr) {
                w->granted = 0;
                (void)Dispatcher::push(w->handle);
            } else {
                w->run(w, true);
            }
        }
    }

    bool remove(Waiter* w) noexcept {
        if (w == nullptr || head_ == nullptr) {
            return false;
        }
        if (head_ == w) {
            head_ = w->next;
            if (tail_ == w) {
                tail_ = nullptr;
            }
            w->next = nullptr;
            return true;
        }
        Waiter* curr = head_;
        while (curr->next != nullptr && curr->next != w) {
            curr = curr->next;
        }
        if (curr->next == w) {
            curr->next = w->next;
            if (tail_ == w) {
                tail_ = curr;
            }
            w->next = nullptr;
            return true;
        }
        return false;
    }

    bool has_waiters() const noexcept { return head_ != nullptr; }

private:
    Waiter* pop() noexcept {
        Waiter* w = head_;
        if (w != nullptr) {
            head_ = w->next;
            if (head_ == nullptr) {
                tail_ = nullptr;
            }
            w->next = nullptr;
        }
        return w;
    }

    uint32_t next_token() noexcept {
        if (++last_ == 0) {
            ++last_;
        }
        return last_;
    }

    Waiter*  head_{nullptr};
    Waiter*  tail_{nullptr};
    uint32_t owner_{0};
    uint32_t last_{0};
};

// Holds the bus until destroyed or release()d; move-only.
class BusLockGuard {
public:
    BusLockGuard() noexcept = default;
    BusLockGuard(BusLock* lock, uint32_t token) noexcept : lock_(lock), token_(token) {}
    BusLockGuard(const BusLockGuard&) = delete;
    BusLockGuard& operator=(const BusLockGuard&) = delete;
    BusLockGuard(BusLockGuard&& o) noexcept
        : lock_(std::exchange(o.lock_, nullptr)), token_(std::exchange(o.token_, 0U)) {}
    BusLockGuard& operator=(BusLockGuard&& o) noexcept {
        if (this != &o) {
            release();
            lock_ = std::exchange(o.lock_, nullptr);
            token_ = std::exchange(o.token_, 0U);
        }
        return *this;
    }
    ~BusLockGuard() { release(); }

    void release() noexcept {
        if (lock_ != nullptr) {
            lock_->release(token_);
            lock_ = nullptr;
            token_ = 0;
        }
    }

    bool held() const noexcept { return lock_ != nullptr; }
    uint32_t token() const noexcept { return token_; }

private:
    BusLock* lock_{nullptr};
    uint32_t token_{0};
};

// co_await bus.lock_async(): resumes once no other guard holds the bus.
struct BusLockAwaiter {
    BusLock&        lock;
    BusLock::Waiter node{};
    uint32_t        token{0};

    ~BusLockAwaiter() noexcept {
        if (token == 0 && node.granted == 0) {
            lock.remove(&node);
        }
    }

    bool await_ready() noexcept {
        token = lock.try_acquire();
        return token != 0;
    }
    void await_suspend(std::coroutine_handle<> h) noexcept {
        node.handle = h;
        lock.wait(&node);
    }
    BusLockGuard await_resume() noexcept {
        const uint32_t t = token != 0 ? token : node.granted;
        return t != 0 ? BusLockGuard(&lock, t) : BusLockGuard{};
    }
};

} // namespace abstractx::hal

#endif // ABSTRACTX_HAL_BUS_LOCK_HPP
