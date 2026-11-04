#include "abstractx/coro.hpp"
#include "linux_spi_driver.hpp"
#include <cstdio>
#include <thread>
using namespace abstractx;
hal::SpiResult g_r{}; bool g_done=false;
struct Slow : hal::LinuxSpiDriver { using LinuxSpiDriver::LinuxSpiDriver; };
coro::Task<void> body(hal::LinuxSpiDriver& d){ static uint8_t tx[4]={1,2,3,4}, rx[4]; g_r = co_await d.transfer_async(tx, rx); g_done=true; }
int main(){
    hal::LinuxSpiDriver d(""); hal::SpiConfig c{}; d.init(c);
    auto t = body(d); t.resume();                 // request handed to the worker
    size_t np = d.cancel_pending(); bool ca = d.cancel_active();
    std::printf("cancel_pending()=%zu cancel_active()=%d has_pending=%d\n", np, ca, d.has_pending_requests());
    for(int i=0;i<2000 && !g_done;++i){ Dispatcher::process(); std::this_thread::sleep_for(std::chrono::microseconds(200)); }
    std::printf("completed anyway: done=%d status=%d (0=Ok, 7=Cancelled)\n", g_done, (int)g_r.status);
    d.shutdown();
}
