// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later

mod asp;
mod dma;
mod tun;

use anyhow::Result;
use clap::Parser;

use crate::asp::{build_frame, extract_payload, CrcMode};
use crate::dma::DmaTransport;
use crate::tun::TunDevice;

#[derive(Parser, Debug)]
#[command(author, version, about = "AbstractX Rust TUN + DMA bridge scaffold")]
struct Args {
    #[arg(long, default_value = "tun0")]
    tun_name: String,

    #[arg(long, default_value = "10.0.0.1/24")]
    tun_cidr: String,

    #[arg(long, default_value = "/dev/xdma0_h2c_0")]
    dma_h2c: String,

    #[arg(long, default_value = "/dev/xdma0_c2h_0")]
    dma_c2h: String,

    #[arg(long, default_value_t = 4096)]
    dma_read_chunk: usize,

    #[arg(long, default_value = "auto")]
    crc_mode: String,

    #[arg(long, default_value_t = 5)]
    loop_sleep_ms: u64,
}

fn parse_crc_mode(mode: &str) -> CrcMode {
    match mode {
        "on" => CrcMode::On,
        "off" => CrcMode::Off,
        _ => CrcMode::Off, // DMA-first default
    }
}

fn main() -> Result<()> {
    let args = Args::parse();
    let mut tun = TunDevice::open(&args.tun_name, &args.tun_cidr)?;
    let mut dma = DmaTransport::open(&args.dma_h2c, &args.dma_c2h, args.dma_read_chunk)?;

    let crc_mode = parse_crc_mode(&args.crc_mode);
    let mut seq: u16 = 0;
    let sleep = std::time::Duration::from_millis(args.loop_sleep_ms);

    loop {
        if let Some(pkt) = tun.try_read_packet()? {
            let frame = build_frame(&pkt, seq, crc_mode);
            dma.write_frame(&frame)?;
            seq = seq.wrapping_add(1);
        }

        if let Some(frame) = dma.try_read_frame()? {
            if let Some(payload) = extract_payload(&frame, crc_mode) {
                tun.write_packet(&payload)?;
            }
        }

        std::thread::sleep(sleep);
    }
}
