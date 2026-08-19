/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Generic I/O Dispatcher Pattern (Push, Await, Response, Events)
 * ------------------------------------------------------------------------
 * Demonstrates the universal I/O Dispatcher engine using unified include/asp_coro.hpp:
 * 1. Push-and-Forget: co_await io.async_push(target, payload)
 * 2. Request-Response: auto res = co_await io.async_request(target, cmd)
 * 3. Tag Completion Routing strictly to Main Thread (Rule 4.2)
 */

#include "asp_coro.hpp"
#include "spsc_tlp_ring.hpp"
#include "asp_tlp64.hpp"

#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <atomic>
#include <vector>
#include <unordered_map>
#include <string>

using namespace abstractx;
using namespace abstractx::coro;

// =============================================================================
// GENERIC I/O DISPATCHER ENGINE
// =============================================================================
class GenericIoDispatcher {
public:
    GenericIoDispatcher(SpscTlpRing<64>& tx_ring, SpscTlpRing<64>& rx_ring)
        : tx_(tx_ring), rx_(rx_ring) {}

    // Awaiter for Request-Response (Push command, yield until correlated response returns)
    struct RequestAwaiter {
        GenericIoDispatcher& dispatcher_;
        uint32_t target_addr_;
        uint32_t cmd_;
        uint8_t tag_;
        uint32_t response_val_{0};

        bool await_ready() const noexcept { return false; }
        
        void await_suspend(std::coroutine_handle<> handle) noexcept {
            dispatcher_.register_waiter(tag_, handle, &response_val_);
            Tlp64 req = Tlp64::make_mem_write(target_addr_, cmd_, tag_);
            dispatcher_.tx_.push(req);
        }

        uint32_t await_resume() const noexcept {
            return response_val_;
        }
    };

    // Awaiter for Push-and-Forget
    struct PushAwaiter {
        GenericIoDispatcher& dispatcher_;
        uint32_t target_addr_;
        uint32_t data_;

        bool await_ready() const noexcept { return false; }

        void await_suspend(std::coroutine_handle<> handle) noexcept {
            Tlp64 req = Tlp64::make_mem_write(target_addr_, data_, 0);
            dispatcher_.tx_.push(req);
            handle.resume();
        }

        void await_resume() noexcept {}
    };

    RequestAwaiter async_request_response(uint32_t target, uint32_t cmd, uint8_t tag) {
        return RequestAwaiter{*this, target, cmd, tag};
    }

    PushAwaiter async_push(uint32_t target, uint32_t data) {
        return PushAwaiter{*this, target, data};
    }

    void register_waiter(uint8_t tag, std::coroutine_handle<> h, uint32_t* result_ptr) {
        pending_waiters_[tag] = {h, result_ptr};
    }

    // Called on the Main Thread to process incoming completion packets
    void process_completions() {
        Tlp64 resp;
        while (rx_.pop(resp)) {
            uint8_t tag = resp.tag();
            auto it = pending_waiters_.find(tag);
            if (it != pending_waiters_.end()) {
                if (it->second.result_ptr) {
                    uint32_t val = (static_cast<uint32_t>(resp.wire.payload[0]) << 24) |
                                   (static_cast<uint32_t>(resp.wire.payload[1]) << 16) |
                                   (static_cast<uint32_t>(resp.wire.payload[2]) << 8) |
                                   (static_cast<uint32_t>(resp.wire.payload[3]));
                    *(it->second.result_ptr) = val;
                }
                auto handle = it->second.handle;
                pending_waiters_.erase(it);
                handle.resume(); // Resumed strictly on the Main Thread (Rule 4.2)
            }
        }
    }

private:
    struct WaiterEntry {
        std::coroutine_handle<> handle{nullptr};
        uint32_t* result_ptr{nullptr};
    };

    SpscTlpRing<64>& tx_;
    SpscTlpRing<64>& rx_;
    std::unordered_map<uint8_t, WaiterEntry> pending_waiters_;
};

// =============================================================================
// BACKGROUND I/O THREAD (Simulates Linux Socket / UART / FPGA I/O Worker)
// =============================================================================
class BackgroundIoWorker {
public:
    BackgroundIoWorker(SpscTlpRing<64>& tx, SpscTlpRing<64>& rx)
        : tx_(tx), rx_(rx), running_(false) {}

    void start() {
        running_ = true;
        worker_thread_ = std::thread(&BackgroundIoWorker::loop, this);
    }

    void stop() {
        running_ = false;
        if (worker_thread_.joinable()) worker_thread_.join();
    }

    ~BackgroundIoWorker() { stop(); }

private:
    void loop() {
        while (running_) {
            Tlp64 req;
            if (tx_.pop(req)) {
                if (req.tag() != 0) { // Request-response requiring completion
                    uint32_t input_val = (static_cast<uint32_t>(req.wire.payload[0]) << 24) |
                                         (static_cast<uint32_t>(req.wire.payload[1]) << 16) |
                                         (static_cast<uint32_t>(req.wire.payload[2]) << 8) |
                                         (static_cast<uint32_t>(req.wire.payload[3]));

                    uint32_t response_val = input_val * 2;
                    Tlp64 resp = Tlp64::make_mem_write(req.target_address(), response_val, req.tag());
                    resp.wire.type = static_cast<uint8_t>(TlpType::Completion);
                    rx_.push(resp);
                }
            }
            std::this_thread::yield();
        }
    }

    SpscTlpRing<64>& tx_;
    SpscTlpRing<64>& rx_;
    std::atomic<bool> running_{false};
    std::thread worker_thread_;
};

// =============================================================================
// CONCURRENT APPLICATION COROUTINES USING THE GENERIC DISPATCHER
// =============================================================================

// Client Coroutine 1: Fire-and-Forget Push Stream
Task<void> client_push_stream_task(GenericIoDispatcher& io, uint32_t count, uint32_t& pushed_count) {
    for (uint32_t i = 1; i <= count; ++i) {
        co_await io.async_push(0x40000A00, i * 100);
        pushed_count++;
    }
}

// Client Coroutine 2: Request-Response Messaging
Task<void> client_request_response_task(GenericIoDispatcher& io, uint32_t req_val, uint8_t tag, uint32_t& out_resp) {
    out_resp = co_await io.async_request_response(0x40000B00, req_val, tag);
}

// =============================================================================
// MAIN ENTRY POINT
// =============================================================================
int main() {
    std::cout << "====================================================================================\n";
    std::cout << " ABSTRACTX GENERIC I/O DISPATCHER PATTERN (PUSH, AWAIT, RESPONSE, COMPLETION)       \n";
    std::cout << "====================================================================================\n";
    std::cout << " Demonstrating generic asynchronous messaging dispatch using include/asp_coro.hpp:\n";
    std::cout << " 1. Push-and-Forget Stream (co_await io.async_push)\n";
    std::cout << " 2. Request-Response Transaction (co_await io.async_request_response)\n";
    std::cout << " 3. Lock-Free Tag Routing & Single-Thread Coroutine Resume (Rule 4.2)\n\n";

    SpscTlpRing<64> host_tx;
    SpscTlpRing<64> host_rx;

    BackgroundIoWorker io_worker{host_tx, host_rx};
    io_worker.start();

    GenericIoDispatcher dispatcher{host_tx, host_rx};

    uint32_t total_pushed = 0;
    Task<void> t_push = client_push_stream_task(dispatcher, 5, total_pushed);
    t_push.resume();

    uint32_t resp_1 = 0;
    uint32_t resp_2 = 0;
    Task<void> t_req1 = client_request_response_task(dispatcher, 42, 1, resp_1);
    Task<void> t_req2 = client_request_response_task(dispatcher, 100, 2, resp_2);
    t_req1.resume();
    t_req2.resume();

    auto start_time = std::chrono::high_resolution_clock::now();
    while ((resp_1 == 0 || resp_2 == 0) &&
           std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - start_time).count() < 100.0)
    {
        dispatcher.process_completions();
        std::this_thread::yield();
    }

    io_worker.stop();

    std::cout << "====================================================================================\n";
    std::cout << " DISPATCHER EXECUTION REPORT                                                        \n";
    std::cout << "====================================================================================\n";
    std::cout << " Client 1 Push Messages Dispatched : " << total_pushed << " / 5 packets\n";
    std::cout << " Client 2 Request-Response (Tag 1) : Request 42  -> Received " << resp_1 << " (Expected: 84)\n";
    std::cout << " Client 3 Request-Response (Tag 2) : Request 100 -> Received " << resp_2 << " (Expected: 200)\n";
    std::cout << " Cross-Thread Mutexes Used         : 0 (100% Lock-Free SPSC)\n";
    std::cout << " Coroutine Resumption Thread       : Main Thread Only (Zero ISR/Thread-Hopping)\n";
    std::cout << "====================================================================================\n\n";

    std::cout << "ARCHITECTURAL CONCLUSION:\n";
    std::cout << "The Generic I/O Dispatcher decouples any high-level coroutine workflow from\n";
    std::cout << "underlying physical I/O transports (IPC, Sockets, CAN, UART, SPI, DMA) seamlessly.\n";
    std::cout << "====================================================================================\n";

    return 0;
}
