// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later

use std::ffi::CString;
use std::fs::File;
use std::io::{Read, Write};
use std::mem::MaybeUninit;
use std::os::fd::AsRawFd;

use anyhow::{Context, Result};

const TUNSETIFF: libc::c_ulong = 0x400454ca;
const IFF_TUN: i16 = 0x0001;
const IFF_NO_PI: i16 = 0x1000;

#[repr(C)]
struct IfReq {
    name: [u8; libc::IFNAMSIZ],
    flags: libc::c_short,
}

pub struct TunDevice {
    file: File,
}

impl TunDevice {
    pub fn open(name: &str, cidr: &str) -> Result<Self> {
        let file = File::options()
            .read(true)
            .write(true)
            .open("/dev/net/tun")
            .context("open /dev/net/tun")?;

        let mut ifr = MaybeUninit::<IfReq>::zeroed();
        let mut ifr = unsafe { ifr.assume_init() };
        ifr.flags = (IFF_TUN | IFF_NO_PI) as libc::c_short;

        let name_bytes = name.as_bytes();
        let max = libc::IFNAMSIZ.saturating_sub(1);
        for (i, b) in name_bytes.iter().take(max).enumerate() {
            ifr.name[i] = *b;
        }

        let rc = unsafe { libc::ioctl(file.as_raw_fd(), TUNSETIFF, &ifr) };
        if rc < 0 {
            return Err(std::io::Error::last_os_error()).context("ioctl TUNSETIFF");
        }

        let cmd1 = CString::new(format!("ip link set {} up", name))?;
        let cmd2 = CString::new(format!("ip addr replace {} dev {}", cidr, name))?;
        unsafe {
            libc::system(cmd1.as_ptr());
            libc::system(cmd2.as_ptr());
        }

        // Best-effort nonblocking.
        unsafe {
            let flags = libc::fcntl(file.as_raw_fd(), libc::F_GETFL);
            if flags >= 0 {
                let _ = libc::fcntl(file.as_raw_fd(), libc::F_SETFL, flags | libc::O_NONBLOCK);
            }
        }

        Ok(Self { file })
    }

    pub fn try_read_packet(&mut self) -> Result<Option<Vec<u8>>> {
        let mut buf = vec![0u8; 2048];
        match self.file.read(&mut buf) {
            Ok(0) => Ok(None),
            Ok(n) => {
                buf.truncate(n);
                Ok(Some(buf))
            }
            Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => Ok(None),
            Err(e) => Err(e.into()),
        }
    }

    pub fn write_packet(&mut self, payload: &[u8]) -> Result<()> {
        self.file.write_all(payload)?;
        Ok(())
    }
}
