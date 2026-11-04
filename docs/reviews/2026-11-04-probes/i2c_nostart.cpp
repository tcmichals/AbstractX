// Proves: LinuxI2cDriver advertises no_start, write_regs_async builds [write(reg), write(data,NoStart)],
// worker matches neither 1-msg nor write+read form -> issues no ioctl, reports Ok.
#include "abstractx/coro.hpp"
#include "linux_i2c_driver.hpp"
#include <cstdio>
#include <thread>
namespace abstractx::hal { ITimer& get_timer_driver() noexcept; }
using namespace abstractx;
static hal::I2cResult g_res{}; static bool g_done=false;
coro::Task<void> body(hal::LinuxI2cDriver& d) {
    static const uint8_t data[2]={0x11,0x22};
    g_res = co_await d.write_regs_async(uint16_t{0x68}, uint8_t{0x10}, std::span<const uint8_t>(data,2));
    g_done=true;
}
coro::Task<void> body2(hal::LinuxI2cDriver& d) {
    static uint8_t out[2];
    g_res = co_await d.read_regs_async(uint16_t{0x68}, uint8_t{0x10}, std::span<uint8_t>(out,2));
    g_done=true;
}
int main(){
    hal::LinuxI2cDriver d("/dev/null");   // fd >= 0, every real I2C_RDWR ioctl fails with ENOTTY
    hal::I2cConfig c{}; d.init(c);
    std::printf("fd=%d\n", d.worker().fd());
    { auto t = body(d); t.resume(); for(int i=0;i<2000 && !g_done;++i){Dispatcher::process(); std::this_thread::sleep_for(std::chrono::microseconds(500));}
      std::printf("write_regs_async: done=%d status=%d transferred=%zu msgs_done=%u\n", g_done,(int)g_res.status,g_res.transferred_bytes,(unsigned)g_res.msgs_done); }
    g_done=false;
    { auto t = body2(d); t.resume(); for(int i=0;i<2000 && !g_done;++i){Dispatcher::process(); std::this_thread::sleep_for(std::chrono::microseconds(500));}
      std::printf("read_regs_async (control): done=%d status=%d transferred=%zu\n", g_done,(int)g_res.status,g_res.transferred_bytes); }
    d.shutdown();
}
