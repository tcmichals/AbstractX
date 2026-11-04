// Backend completes on a worker thread (as LinuxIoBackend does via SpiWorker); main thread fires
// on_trigger like poll_events. Checks whether auto_done (worker) and on_trigger (main) race on AutoState.
#include "abstractx/hal/io_engine.hpp"
#include <thread>
#include <cstdio>
using namespace abstractx; using namespace abstractx::hal;
struct Be : IIoBackend {
    std::atomic<bool> has{false}; IoTransfer x{};
    bool init_channel(const AutoChannelConfig&) noexcept override { return true; }
    bool start_transfer(const IoTransfer& t) noexcept override {
        if (has.load(std::memory_order_acquire)) return false;
        x = t; has.store(true, std::memory_order_release); return true; }
};
MpscTlpRing<> eg, in;
int main(){
    Be be; IoEngine e(be);
    AutoChannelConfig ch{}; ch.trigger_pin=5; ch.tx_len=1; ch.rx_len=14;
    IoProcessorSetup s{}; s.channels = std::span<const AutoChannelConfig>(&ch,1); s.egress_tx_ring=&eg; s.ingress_rx_ring=&in;
    e.configure(s); e.start();
    std::atomic<bool> stop{false};
    std::thread w([&]{ while(!stop.load()){ if(be.has.load(std::memory_order_acquire)){ IoTransfer t=be.x; be.has.store(false,std::memory_order_release);
        IoResult r{}; r.ok=true; r.transferred=t.rx.size(); t.done(r);} } });
    uint32_t ts=0; size_t got=0; Tlp64 tlp;
    for (int i=0;i<200000;++i){ { InterruptGuard g; e.on_trigger(5, ++ts); } while(in.pop(tlp)) ++got; }
    stop=true; w.join();
    std::printf("emitted=%zu overruns=%u\n", got, e.auto_overruns());
}
