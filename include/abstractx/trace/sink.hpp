/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX CTF 1.8 Trace Sinks (File, UDP, Shared SRAM, Null)
 * ------------------------------------------------------------
 * Zero-allocation C++ sinks for binary CTF trace packet emission.
 */

#ifndef ABSTRACTX_TRACE_SINK_HPP
#define ABSTRACTX_TRACE_SINK_HPP

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

namespace abstractx::trace {

class ITraceSink {
public:
    virtual ~ITraceSink() = default;
    virtual bool open(const char* target) = 0;
    virtual void write(const uint8_t* data, size_t len) = 0;
    virtual void flush() = 0;
    virtual void close() = 0;
    virtual bool is_open() const noexcept = 0;
};

// Null Sink (No-op)
class NullTraceSink final : public ITraceSink {
public:
    bool open(const char* /*target*/) override { return true; }
    void write(const uint8_t* /*data*/, size_t /*len*/) override {}
    void flush() override {}
    void close() override {}
    bool is_open() const noexcept override { return true; }
};

// File Sink (Babeltrace 2 / Trace Compass binary CTF file sink)
class FileTraceSink final : public ITraceSink {
public:
    FileTraceSink() = default;
    ~FileTraceSink() override { close(); }

    bool open(const char* target) override {
        close();
        const char* path = (target && target[0] != '\0') ? target : "trace.ctf";
        file_ = std::fopen(path, "wb");
        return (file_ != nullptr);
    }

    void write(const uint8_t* data, size_t len) override {
        if (file_ && data && len > 0) {
            std::fwrite(data, 1, len, file_);
        }
    }

    void flush() override {
        if (file_) {
            std::fflush(file_);
        }
    }

    void close() override {
        if (file_) {
            std::fflush(file_);
            std::fclose(file_);
            file_ = nullptr;
        }
    }

    bool is_open() const noexcept override { return file_ != nullptr; }

private:
    std::FILE* file_{nullptr};
};

// UDP Sink (Network stream to Visualizer Studio on UDP port 9870)
class UdpTraceSink final : public ITraceSink {
public:
    UdpTraceSink() = default;
    ~UdpTraceSink() override { close(); }

    bool open(const char* target) override {
        close();
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
        fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (fd_ < 0) return false;

        // Parse target "IP:PORT" or default to "127.0.0.1:9870"
        char ip_str[64] = "127.0.0.1";
        uint16_t port = 9870;

        if (target && target[0] != '\0') {
            const char* colon = std::strchr(target, ':');
            if (colon) {
                size_t ip_len = static_cast<size_t>(colon - target);
                if (ip_len < sizeof(ip_str)) {
                    std::memcpy(ip_str, target, ip_len);
                    ip_str[ip_len] = '\0';
                }
                port = static_cast<uint16_t>(std::atoi(colon + 1));
            } else {
                std::strncpy(ip_str, target, sizeof(ip_str) - 1);
            }
        }

        std::memset(&dest_addr_, 0, sizeof(dest_addr_));
        dest_addr_.sin_family = AF_INET;
        dest_addr_.sin_port = htons(port);
        if (::inet_pton(AF_INET, ip_str, &dest_addr_.sin_addr) <= 0) {
            dest_addr_.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        }
        is_open_ = true;
        return true;
#else
        (void)target;
        return false;
#endif
    }

    void write(const uint8_t* data, size_t len) override {
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
        if (is_open_ && fd_ >= 0 && data && len > 0) {
            ::sendto(fd_, data, len, 0, reinterpret_cast<const struct sockaddr*>(&dest_addr_), sizeof(dest_addr_));
        }
#else
        (void)data;
        (void)len;
#endif
    }

    void flush() override {}

    void close() override {
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
#endif
        is_open_ = false;
    }

    bool is_open() const noexcept override { return is_open_; }

private:
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
    int fd_{-1};
    struct sockaddr_in dest_addr_{};
#endif
    bool is_open_{false};
};

} // namespace abstractx::trace

#endif // ABSTRACTX_TRACE_SINK_HPP
