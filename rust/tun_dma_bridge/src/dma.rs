// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later

use std::collections::VecDeque;
use std::fs::File;
use std::io::{Read, Write};
use std::os::fd::AsRawFd;

use anyhow::{Context, Result};

use crate::asp::{ASP_CRC_LEN, ASP_HEADER_LEN, ASP_SYNC};

pub struct DmaTransport {
    h2c: File,
    c2h: File,
    read_chunk: usize,
    buffer: Vec<u8>,
    queue: VecDeque<Vec<u8>>,
}

impl DmaTransport {
    pub fn open(h2c_path: &str, c2h_path: &str, read_chunk: usize) -> Result<Self> {
        let h2c = File::options()
            .write(true)
            .open(h2c_path)
            .with_context(|| format!("open h2c {}", h2c_path))?;

        let c2h = File::options()
            .read(true)
            .open(c2h_path)
            .with_context(|| format!("open c2h {}", c2h_path))?;

        // Best-effort O_NONBLOCK on C2H fd.
        unsafe {
            let flags = libc::fcntl(c2h.as_raw_fd(), libc::F_GETFL);
            if flags >= 0 {
                let _ = libc::fcntl(c2h.as_raw_fd(), libc::F_SETFL, flags | libc::O_NONBLOCK);
            }
        }

        Ok(Self {
            h2c,
            c2h,
            read_chunk,
            buffer: Vec::new(),
            queue: VecDeque::new(),
        })
    }

    pub fn write_frame(&mut self, frame: &[u8]) -> Result<()> {
        self.h2c.write_all(frame)?;
        Ok(())
    }

    pub fn try_read_frame(&mut self) -> Result<Option<Vec<u8>>> {
        self.drain_rx()?;
        Ok(self.queue.pop_front())
    }

    fn drain_rx(&mut self) -> Result<()> {
        let mut temp = vec![0u8; self.read_chunk];
        loop {
            match self.c2h.read(&mut temp) {
                Ok(0) => break,
                Ok(n) => {
                    self.buffer.extend_from_slice(&temp[..n]);
                    while let Some(frame) = pop_one_frame(&mut self.buffer) {
                        self.queue.push_back(frame);
                    }
                }
                Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => break,
                Err(e) => return Err(e.into()),
            }
        }
        Ok(())
    }
}

fn pop_one_frame(buf: &mut Vec<u8>) -> Option<Vec<u8>> {
    if buf.len() < ASP_HEADER_LEN + ASP_CRC_LEN {
        return None;
    }

    if buf[0] != ASP_SYNC {
        if let Some(idx) = buf.iter().position(|b| *b == ASP_SYNC) {
            buf.drain(0..idx);
        } else {
            buf.clear();
            return None;
        }
        if buf.len() < ASP_HEADER_LEN + ASP_CRC_LEN {
            return None;
        }
    }

    let payload_len = u16::from_be_bytes([buf[6], buf[7]]) as usize;
    let frame_len = ASP_HEADER_LEN + payload_len + ASP_CRC_LEN;
    if buf.len() < frame_len {
        return None;
    }

    let frame = buf[0..frame_len].to_vec();
    buf.drain(0..frame_len);
    Some(frame)
}
