// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later

pub const ASP_SYNC: u8 = 0xA5;
pub const ASP_HEADER_LEN: usize = 8;
pub const ASP_CRC_LEN: usize = 2;

#[derive(Clone, Copy)]
pub enum CrcMode {
    On,
    Off,
}

pub fn crc16_xmodem(data: &[u8], seed: u16) -> u16 {
    let mut crc = seed;
    for &b in data {
        crc ^= (b as u16) << 8;
        for _ in 0..8 {
            if (crc & 0x8000) != 0 {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }
    crc
}

pub fn build_frame(payload: &[u8], seq: u16, crc_mode: CrcMode) -> Vec<u8> {
    let mut out = Vec::with_capacity(ASP_HEADER_LEN + payload.len() + ASP_CRC_LEN);
    out.push(ASP_SYNC);
    out.push(1); // ver
    out.push(0); // flags
    out.push(2); // axid: tun
    out.extend_from_slice(&seq.to_be_bytes());
    out.extend_from_slice(&(payload.len() as u16).to_be_bytes());
    out.extend_from_slice(payload);

    let crc = match crc_mode {
        CrcMode::On => crc16_xmodem(&out, 0x0000),
        CrcMode::Off => 0x0000,
    };
    out.extend_from_slice(&crc.to_be_bytes());
    out
}

pub fn extract_payload(frame: &[u8], crc_mode: CrcMode) -> Option<Vec<u8>> {
    if frame.len() < ASP_HEADER_LEN + ASP_CRC_LEN || frame[0] != ASP_SYNC {
        return None;
    }

    let payload_len = u16::from_be_bytes([frame[6], frame[7]]) as usize;
    let expected = ASP_HEADER_LEN + payload_len + ASP_CRC_LEN;
    if frame.len() < expected {
        return None;
    }

    if let CrcMode::On = crc_mode {
        let rx_crc = u16::from_be_bytes([frame[expected - 2], frame[expected - 1]]);
        let calc = crc16_xmodem(&frame[..expected - ASP_CRC_LEN], 0x0000);
        if rx_crc != calc {
            return None;
        }
    }

    Some(frame[ASP_HEADER_LEN..ASP_HEADER_LEN + payload_len].to_vec())
}
