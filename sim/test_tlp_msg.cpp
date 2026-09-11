#include "asp_tlp_msg.hpp"
#include "abstractx/trace/tracer.hpp"
#include "abstractx/drivers/imu/icm42688p.hpp"
#include "abstractx/drivers/gps/ublox_gps.hpp"
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

    std::cout << "\n[SUCCESS] Multi-Target TLP Transport & CTF Abstractions Verified!\n";
    return 0;
}
