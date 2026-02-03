#!/usr/bin/env python3
import argparse
import subprocess
import time
import re
from pathlib import Path


SYMBOLS = [
    "dbg_streaming",
    "dbg_usb_rx_count",
    "dbg_underrun",
    "dbg_overrun",
    "dbg_out_buf_level",
    "dbg_out_buf_min",
    "dbg_out_buf_max",
    "dbg_pll_ppm",
    "dbg_asrc_rate",
    "dbg_feedback",
    "dbg_fb_count",
    "dbg_fb_xfrc",
    "dbg_fb_int_count",
    "dbg_fb_int_last",
    "dbg_fb_actual",
    "dbg_fb_sent",
    "dbg_fb_diepctl",
    "dbg_fb_diepint",
    "dbg_fb_fifo_before",
    "dbg_fb_fifo_after",
    "dbg_iepint_count",
    "dbg_last_daint",
    "dbg_last_daintmsk",
    "dbg_set_iface_val",
    "dbg_out_iface_num",
    "dbg_out_rx_last_len",
    "dbg_out_rx_min_len",
    "dbg_out_rx_max_len",
    "dbg_out_rx_short",
    "dbg_out_rx_disc_count",
    "dbg_out_rx_disc_max",
    "dbg_out_rx_disc_last",
    "dbg_out_rx_disc_threshold",
    "dbg_out_rx_intra_count",
    "dbg_out_rx_intra_max",
    "dbg_out_rx_intra_last",
    "dbg_fb_override_enable",
    "dbg_fb_override_value",
    "dbg_fb_test_mode",
    "dbg_buf_sample0",
    "dbg_buf_sample1",
    "dbg_sample_l",
    "dbg_sample_r",
    "dbg_dma_spike_count",
    "dbg_dma_spike_max",
    "dbg_dma_spike_last",
    "dbg_dma_spike_threshold",
    "dbg_current_sample_rate",
    "dbg_actual_rate",
    "dbg_audio_in_write",
    "dbg_in_buf_level",
]


def resolve_symbols(elf_path: Path) -> dict:
    result = subprocess.run(
        ["arm-none-eabi-nm", "-C", str(elf_path)],
        capture_output=True,
        text=True,
        check=True,
    )
    addrs = {}
    for line in result.stdout.splitlines():
        match = re.match(r"([0-9a-fA-F]+)\s+\S\s+(.+)", line)
        if not match:
            continue
        addr, name = match.groups()
        if name in SYMBOLS:
            addrs[name] = int(addr, 16)
    return addrs


def read_once(target: str, addrs: dict) -> dict:
    cmds = [f"read32 0x{addrs[name]:08x}" for name in SYMBOLS if name in addrs]
    cmd = " ; ".join(cmds)
    result = subprocess.run(
        ["pyocd", "cmd", "-t", target, "-c", cmd],
        capture_output=True,
        text=True,
        check=True,
    )
    values = {}
    for line in result.stdout.splitlines():
        match = re.match(r"([0-9a-fA-F]+):\s+([0-9a-fA-F]+)", line)
        if not match:
            continue
        addr, value = match.groups()
        values[int(addr, 16)] = int(value, 16)
    return values


def main() -> None:
    parser = argparse.ArgumentParser(description="Read USB audio debug counters via pyocd.")
    parser.add_argument("--elf", default="build/stm32f4_kernel/release/stm32f4_kernel.elf")
    parser.add_argument("--target", default="stm32f407vg")
    parser.add_argument("--interval", type=float, default=2.0)
    parser.add_argument("--count", type=int, default=10)
    args = parser.parse_args()

    elf_path = Path(args.elf)
    if not elf_path.exists():
        raise SystemExit(f"ELF not found: {elf_path}")

    addrs = resolve_symbols(elf_path)
    missing = [s for s in SYMBOLS if s not in addrs]
    if missing:
        print("Missing symbols:", ", ".join(missing))

    for i in range(args.count):
        values = read_once(args.target, addrs)
        print(f"t={i * args.interval:.1f}s")
        for name in SYMBOLS:
            if name not in addrs:
                print(f"{name}=<missing>")
                continue
            addr = addrs[name]
            val = values.get(addr)
            if val is None:
                print(f"{name}=<no read>")
            else:
                print(f"{name}=0x{val:08x} ({val})")
        if i + 1 < args.count:
            time.sleep(args.interval)


if __name__ == "__main__":
    main()
