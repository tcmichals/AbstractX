#include "asp_tlp_msg.hpp"
#include "abstractx/trace/tracer.hpp"
#include "abstractx/drivers/imu/icm42688p.hpp"
#include "abstractx/drivers/gps/ublox_gps.hpp"
#include "abstractx/hal/spi.hpp"
#include "abstractx/hal/i2c.hpp"
#include "eventfd.hpp"
#include "epoll_reactor.hpp"
#include "spi_worker.hpp"
#include "i2c_worker.hpp"
#include "gpio_gpiod.hpp"
#include <iostream>
#include <cassert>

using namespace abstractx;

int main() {
    std::cout << "=================================================================\n";
    std::cout << " AbstractX Multi-Target TLP Transport & CTF Payload Verification \n";
    std::cout << "=================================================================\n";

    // 1. Verify FPGA Wire Container (64 Bytes)
    std::cout << "[+] FPGA Wire Container (Tlp64 / 512-bit vector): " << sizeof(Tlp64) << " bytes\n";
    assert(sizeof(Tlp64) == 64);

    // 2. Verify Common Header (20 Bytes)
    std::cout << "[+] Universal TLP Header: " << sizeof(TlpHeader) << " bytes\n";
    assert(sizeof(TlpHeader) == 20);

    // 3. Verify Processor-Optimized Short TLP (24 Bytes)
    std::cout << "[+] Processor Short TLP (32-bit Reg R/W): " << sizeof(TlpShort) << " bytes\n";
    assert(sizeof(TlpShort) == 24);

    // 4. Verify Variable Payload Containers
    TlpVar<14> imu_msg{}; // Exact 14B IMU Burst (Accel[6] + Gyro[6] + Temp[2])
    std::cout << "[+] RP2350 / ESP32-P4 IMU Message Container: " << sizeof(imu_msg) << " bytes\n";
    assert(sizeof(imu_msg) == 34); // 20B Header + 14B Payload

    // 5. Verify Zero-Copy Pointer Descriptor for Linux SMP
    std::cout << "[+] Linux SMP Zero-Copy Descriptor: " << sizeof(TlpDescriptor) << " bytes\n";

    // 6. Verify CTF 1.8 Payloads Encapsulated Inside 64-Byte TLPs
    std::cout << "[+] Testing CTF 1.8 Standard Payloads inside Tlp64...\n";
    
    // IMU CTF in TLP
    drivers::imu::ImuSample sample{};
    sample.accel_g[0] = 1.0f;
    sample.accel_g[1] = -0.5f;
    sample.accel_g[2] = 0.25f;
    sample.gyro_dps[0] = 15.0f;
    sample.gyro_dps[1] = -30.0f;
    sample.gyro_dps[2] = 45.0f;
    sample.temp_deg_c = 28.5f;
    sample.timestamp_us = 12345678ULL;
    sample.valid = true;

    Tlp64 imu_tlp = drivers::imu::Icm42688p::to_tlp(sample, 42);
    assert(imu_tlp.channel() == Channel::Telemetry);
    assert(imu_tlp.tag() == 0x01);
    const auto* imu_ctf = imu_tlp.as_ctf<trace::ImuSamplePayload>();
    assert(imu_ctf != nullptr);
    assert(imu_ctf->event_id == 1);
    assert(imu_ctf->sample_seq == 42);
    assert(imu_ctf->accel_x_mg == 1000);
    assert(imu_ctf->accel_y_mg == -500);
    assert(imu_ctf->accel_z_mg == 250);
    assert(imu_ctf->gyro_x_dps == 150);
    assert(imu_ctf->gyro_y_dps == -300);
    assert(imu_ctf->gyro_z_dps == 450);
    assert(imu_ctf->temp_c_1e2 == 2850);
    std::cout << "    [v] IMU CTF 1.8 (27B) packed in Tlp64 correctly\n";

    // GPS CTF in TLP
    drivers::gps::GpsFix fix{};
    fix.lat_1e7 = 377749000;
    fix.lon_1e7 = -1224194000;
    fix.alt_msl_mm = 142500;
    fix.ground_speed_mm_s = 12400;
    fix.heading_1e5 = 18000000;
    fix.satellites = 18;
    fix.fix_type = drivers::gps::GpsFixType::Fix3D;
    fix.itow_ms = 4567890;
    fix.timestamp_us = 12345679ULL;
    fix.valid = true;

    Tlp64 gps_tlp = drivers::gps::UbloxGps::to_tlp(fix);
    assert(gps_tlp.channel() == Channel::Telemetry);
    assert(gps_tlp.tag() == 0x02);
    const auto* gps_ctf = gps_tlp.as_ctf<trace::GpsFixPayload>();
    assert(gps_ctf != nullptr);
    assert(gps_ctf->event_id == 2);
    assert(gps_ctf->itow_ms == 4567890);
    assert(gps_ctf->lat_1e7 == 377749000);
    assert(gps_ctf->lon_1e7 == -1224194000);
    assert(gps_ctf->alt_mm == 142500);
    assert(gps_ctf->ground_speed_mm_s == 12400);
    assert(gps_ctf->heading_1e5 == 18000000);
    assert(gps_ctf->sats == 18);
    assert(gps_ctf->fix_type == 3);
    std::cout << "    [v] GPS CTF 1.8 (35B) packed in Tlp64 correctly\n";

    // Coroutine CTF in TLP
    Tlp64 coro_tlp = trace::CtfTraceEngine<>::make_coro_tlp(7, 0x20001000, trace::CoroState::Resume, 2, 9999ULL);
    assert(coro_tlp.channel() == Channel::Debug);
    const auto* coro_ctf = coro_tlp.as_ctf<trace::CoroEventPayload>();
    assert(coro_ctf != nullptr);
    assert(coro_ctf->event_id == 1);
    assert(coro_ctf->task_id == 7);
    assert(coro_ctf->handle_addr == 0x20001000);
    assert(coro_ctf->state == static_cast<uint8_t>(trace::CoroState::Resume));
    assert(coro_ctf->reason == 2);
    std::cout << "    [v] Coro CTF 1.8 (19B) packed in Tlp64 correctly\n";

    // 7. Verify SPI TLP API Packet Structure & Strongly Typed View
    std::cout << "[+] Testing SPI TLP API Packet Structure & View...\n";
    uint8_t spi_tx_buf[] = { 0x9F, 0x00, 0x00, 0x00 }; // Read WHO_AM_I + 3 dummy bytes
    Tlp64 spi_req = Tlp64::make_spi_transfer(1, 13, spi_tx_buf, 4, ASP_SPI_FLAG_AUTO_CS | ASP_SPI_FLAG_FULL_DUPLEX, 0x2A);
    assert(spi_req.channel() == Channel::Control);
    assert(spi_req.tag() == 0x2A);
    assert(spi_req.wire.type == static_cast<uint8_t>(TlpType::MemWrite));
    assert(spi_req.wire.flags == (ASP_SPI_FLAG_AUTO_CS | ASP_SPI_FLAG_FULL_DUPLEX));
    assert(spi_req.wire.target_address == (ASP_ADDR_SPI_BASE | ASP_REG_XFER));

    TlpSpiView spi_view(spi_req);
    assert(spi_view.req_header()->bus_id == 1);
    assert(spi_view.req_header()->cs_pin == 13);
    assert(spi_view.req_header()->tx_len == 4);
    assert(spi_view.req_header()->rx_len == 4);
    assert(spi_view.tx_data().size() == 4);
    assert(spi_view.tx_data()[0] == 0x9F);
    std::cout << "    [v] SPI Request TLP (bus 1, cs 13, len 4) packed and parsed correctly\n";

    // SPI Completion TLP
    uint8_t spi_rx_buf[] = { 0x00, 0x47, 0x00, 0x00 }; // ICM-42688 WHO_AM_I = 0x47
    Tlp64 spi_cpl = Tlp64::make_spi_cpl(0x2A, 0, spi_rx_buf, 25, 987654321ULL);
    assert(spi_cpl.tag() == 0x2A);
    assert(spi_cpl.wire.type == static_cast<uint8_t>(TlpType::Completion));
    assert(spi_cpl.timestamp_ns() == 987654321ULL);
    TlpSpiView spi_cpl_view(spi_cpl);
    assert(spi_cpl_view.cpl_header()->status == 0);
    assert(spi_cpl_view.cpl_header()->transferred_len == 4);
    assert(spi_cpl_view.cpl_header()->bus_duration_us == 25);
    assert(spi_cpl_view.rx_data().size() == 4);
    assert(spi_cpl_view.rx_data()[1] == 0x47);
    std::cout << "    [v] SPI Completion TLP parsed correctly with status=0, duration=25us\n";

    // 8. Verify I2C TLP API Packet Structure & Strongly Typed View
    std::cout << "[+] Testing I2C TLP API Packet Structure & View...\n";
    Tlp64 i2c_req = Tlp64::make_i2c_read_reg(0, 0x76, 0xF7, 6, 0x3B);
    assert(i2c_req.channel() == Channel::Control);
    assert(i2c_req.tag() == 0x3B);
    assert(i2c_req.wire.type == static_cast<uint8_t>(TlpType::MemRead));
    assert((i2c_req.wire.flags & ASP_I2C_FLAG_REPEATED_START) != 0);

    TlpI2cView i2c_view(i2c_req);
    assert(i2c_view.req_header()->bus_id == 0);
    assert(i2c_view.req_header()->slave_addr == 0x76);
    assert(i2c_view.req_header()->reg_offset == 0xF7);
    assert(i2c_view.req_header()->rx_len == 6);
    std::cout << "    [v] I2C Repeated-Start Read TLP packed and parsed correctly\n";

    uint8_t baro_rx_data[] = { 0x50, 0x64, 0x00, 0x80, 0x1A, 0x00 }; // 6 bytes baro data
    Tlp64 i2c_cpl = Tlp64::make_i2c_cpl(0x3B, 0, baro_rx_data, 120, 11223344ULL);
    assert(i2c_cpl.tag() == 0x3B);
    TlpI2cView i2c_cpl_view(i2c_cpl);
    assert(i2c_cpl_view.cpl_header()->status == 0);
    assert(i2c_cpl_view.cpl_header()->transferred_len == 6);
    assert(i2c_cpl_view.cpl_header()->bus_duration_us == 120);
    assert(i2c_cpl_view.rx_data().size() == 6);
    assert(i2c_cpl_view.rx_data()[0] == 0x50);
    std::cout << "    [v] I2C Completion TLP parsed correctly with 6 bytes data\n";

    // 9. Verify HAL Driver RX & TX Callback Invariants
    std::cout << "[+] Testing HAL SPI & I2C RX/TX Dedicated Callbacks...\n";
    struct CallbackTracker {
        bool spi_tx_called{false};
        bool spi_rx_called{false};
        bool i2c_tx_called{false};
        bool i2c_rx_called{false};

        void on_spi_tx(const hal::SpiResult& res) {
            spi_tx_called = true;
            assert(res.status == hal::SpiStatus::Ok);
        }
        void on_spi_rx(const hal::SpiResult& res) {
            spi_rx_called = true;
            assert(res.status == hal::SpiStatus::Ok);
        }
        void on_i2c_tx(const hal::I2cResult& res) {
            i2c_tx_called = true;
            assert(res.status == hal::I2cStatus::Ok);
        }
        void on_i2c_rx(const hal::I2cResult& res) {
            i2c_rx_called = true;
            assert(res.status == hal::I2cStatus::Ok);
        }
    };

    CallbackTracker tracker{};
    hal::SpiRequest spi_hal_req{};
    spi_hal_req.on_tx_complete = etl::delegate<void(const hal::SpiResult&)>::create<CallbackTracker, &CallbackTracker::on_spi_tx>(tracker);
    spi_hal_req.on_rx_complete = etl::delegate<void(const hal::SpiResult&)>::create<CallbackTracker, &CallbackTracker::on_spi_rx>(tracker);

    hal::SpiResult dummy_spi_res{};
    dummy_spi_res.status = hal::SpiStatus::Ok;
    dummy_spi_res.transferred_bytes = 4;
    spi_hal_req.notify_tx_completion(dummy_spi_res);
    spi_hal_req.notify_rx_completion(dummy_spi_res);
    assert(tracker.spi_tx_called && tracker.spi_rx_called);
    std::cout << "    [v] SPI HAL dedicated on_tx_complete and on_rx_complete delegates verified\n";

    hal::I2cRequest i2c_hal_req{};
    i2c_hal_req.on_tx_complete = etl::delegate<void(const hal::I2cResult&)>::create<CallbackTracker, &CallbackTracker::on_i2c_tx>(tracker);
    i2c_hal_req.on_rx_complete = etl::delegate<void(const hal::I2cResult&)>::create<CallbackTracker, &CallbackTracker::on_i2c_rx>(tracker);

    hal::I2cResult dummy_i2c_res{};
    dummy_i2c_res.status = hal::I2cStatus::Ok;
    dummy_i2c_res.transferred_bytes = 6;
    i2c_hal_req.notify_tx_completion(dummy_i2c_res);
    i2c_hal_req.notify_rx_completion(dummy_i2c_res);
    assert(tracker.i2c_tx_called && tracker.i2c_rx_called);
    std::cout << "    [v] I2C HAL dedicated on_tx_complete and on_rx_complete delegates verified\n";

    // 10. Verify Linux Target EventFd, EpollReactor, SpiWorker & I2cWorker
    std::cout << "[+] Testing Linux Target EventFd, EpollReactor, SpiWorker & I2cWorker...\n";
    target::EventFd efd(true);
    assert(efd.is_valid());
    assert(efd.notify(5));
    assert(efd.drain() == 5);
    std::cout << "    [v] EventFd notify and drain verified\n";

    target::EpollReactor reactor{};
    assert(reactor.is_valid());
    assert(reactor.add_fd_u64(efd.fd(), EPOLLIN, 42));
    assert(efd.notify(1));
    std::array<struct epoll_event, 4> ep_events{};
    int ep_n = reactor.wait(ep_events, 100);
    assert(ep_n == 1);
    assert(ep_events[0].data.u64 == 42);
    assert(efd.drain() == 1);
    std::cout << "    [v] EpollReactor wake on EventFd verified\n";

    // Verify SpiWorker
    target::SpiWorker spi_w{};
    assert(spi_w.start("", 10'000'000, 0)); // Start in mock mode
    hal::SpiRequest sw_req{};
    uint8_t sw_tx[] = { 0x75, 0x00 };
    uint8_t sw_rx[] = { 0x00, 0x00 };
    sw_req.tx_data = sw_tx;
    sw_req.rx_data = sw_rx;
    struct WorkerTracker {
        bool sw_done{false};
        bool iw_done{false};
        void on_spi(const hal::SpiResult& r) {
            sw_done = true;
            assert(r.status == hal::SpiStatus::Ok);
        }
        void on_i2c(const hal::I2cResult& r) {
            iw_done = true;
            assert(r.status == hal::I2cStatus::Ok);
        }
    } wt{};

    sw_req.on_rx_complete = etl::delegate<void(const hal::SpiResult&)>::create<WorkerTracker, &WorkerTracker::on_spi>(wt);
    assert(spi_w.submit(sw_req));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    assert(wt.sw_done);
    assert(sw_rx[1] == 0x47); // Simulated ICM-42688 WHO_AM_I
    spi_w.stop();
    std::cout << "    [v] SpiWorker async execution via EventFd verified (WHO_AM_I=0x47)\n";

    // Verify I2cWorker
    target::I2cWorker i2c_w{};
    assert(i2c_w.start("", 400'000));
    hal::I2cRequest iw_req{};
    uint8_t iw_rx[] = { 0x00 };
    iw_req.slave_addr = 0x76;
    iw_req.use_register = true;
    iw_req.register_offset = 0xD0; // BMP280 chip id register
    iw_req.rx_data = iw_rx;
    iw_req.on_rx_complete = etl::delegate<void(const hal::I2cResult&)>::create<WorkerTracker, &WorkerTracker::on_i2c>(wt);
    assert(i2c_w.submit(iw_req));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    assert(wt.iw_done);
    assert(iw_rx[0] == 0x58); // Simulated BMP280 chip ID
    i2c_w.stop();

    std::cout << "    [v] I2cWorker async execution via EventFd verified (BMP280=0x58)\n";

    // Verify GpiodV2Monitor
    target::GpiodV2Monitor gpio_mon{};
    assert(gpio_mon.open_pin_interrupt("/dev/gpiochip_nonexistent", 3, true, false));
    assert(gpio_mon.is_simulated());
    gpio_mon.trigger_simulated_edge();
    bool gpio_edge_detected = false;
    gpio_mon.process_events([&](unsigned int line, uint64_t ts) {
        (void)ts;
        if (line == 3) gpio_edge_detected = true;
    });
    assert(gpio_edge_detected);
    std::cout << "    [v] GpiodV2Monitor simulated Pin 3 edge dispatch verified\n";

    std::cout << "\n[SUCCESS] Multi-Target TLP Transport, CTF & Unified Linux Target Verified!\n";
    return 0;
}
