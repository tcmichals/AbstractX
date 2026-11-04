#include "abstractx/coro.hpp"
#include <cstdio>
using namespace abstractx; using namespace abstractx::coro;

// --- A: CoroutineIoEngine returns stale response on TX-full ---
SpscTlpRing<4> tx4; // not used
SpscTlpRing<> txr, rxr;
CoroutineIoEngine eng{txr, rxr};
Tlp64 g_r1{}, g_r2{}; int g_a=0;
Task<void> reader(uint32_t addr, Tlp64* out){ *out = co_await eng.async_read(addr); ++g_a; }

// --- B: AsyncQueue with two consumers: second overwrites first pop_waiter_ ---
AsyncQueue<int,4> q; int got[2]={-1,-1}; int nb=0;
Task<void> consumer(int i){ got[i] = co_await q.pop(); ++nb; }

// --- C: when_any loser never cancelled: frame stays live forever ---
Event never;
Task<int> fast(){ co_return 1; }
Task<int> hang(){ co_await never; co_return 2; }
int g_c=-1;
Task<void> racer(){ auto v = co_await when_any(hang(), fast()); g_c = (int)v.index(); }

int main(){
    // A: 254 good reads cycle every tag once; each completion carries payload0 = tag
    for (int i=0;i<254;++i){ Tlp64 out{}; auto t = reader(0x100, &out); t.resume();
      Tlp64 tx; txr.pop(tx); uint8_t tag = tx.wire.tag;
      Tlp64 cpl = Tlp64::make_completion_data(tag, 0xA5000000u | tag, Channel::Control);
      rxr.push(cpl); eng.poll_completions(); Dispatcher::process(); }
    std::printf("A1 good reads completed=%d\n", g_a);
    { Tlp64 dummy{}; while (txr.push(dummy)) {} }      // TX ring full: next push_tx fails
    { Tlp64 out{}; auto t = reader(0x200, &out); t.resume();
      std::printf("A2 read with TX ring FULL: done=%d returned type=%u tag=%u payload=%02x %02x %02x %02x (stale CplD of an earlier read, no error)\n",
        t.done(), out.wire.type, out.wire.tag, out.wire.payload[0], out.wire.payload[1], out.wire.payload[2], out.wire.payload[3]); }
    // B
    { auto c0 = consumer(0); auto c1 = consumer(1); c0.resume(); c1.resume();
      q.try_push(7); q.try_push(8); Dispatcher::process(); Dispatcher::process();
      std::printf("B consumers woken=%d got0=%d got1=%d c0.done=%d c1.done=%d\n", nb, got[0], got[1], c0.done(), c1.done()); }
    // C
    { size_t before = coro_pool_used();
      for (int i=0;i<200;++i){ auto r = racer(); r.resume(); Dispatcher::process(); }
      std::printf("C winner=%d pool_used before=%zu after 200 races=%zu (hang() frames never freed)\n", g_c, before, coro_pool_used()); }
}
