#!/usr/bin/env python3
import argparse
import subprocess
import re
from pathlib import Path


SYMBOLS = [
    "dbg_desc_size",
    "dbg_desc_buf",
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


def read_u32(target: str, addr: int) -> int:
    result = subprocess.run(
        ["pyocd", "cmd", "-t", target, "-c", f"read32 0x{addr:08x}"],
        capture_output=True,
        text=True,
        check=True,
    )
    for line in result.stdout.splitlines():
        match = re.match(r"([0-9a-fA-F]+):\s+([0-9a-fA-F]+)", line)
        if match:
            return int(match.group(2), 16)
    raise RuntimeError("read32 failed")


def read_bytes(target: str, addr: int, size: int) -> bytes:
    cmds = [f"read8 0x{addr + i:08x}" for i in range(size)]
    result = subprocess.run(
        ["pyocd", "cmd", "-t", target, "-c", " ; ".join(cmds)],
        capture_output=True,
        text=True,
        check=True,
    )
    values = {}
    for line in result.stdout.splitlines():
        match = re.match(r"([0-9a-fA-F]+):\s+([0-9a-fA-F]+)", line)
        if match:
            values[int(match.group(1), 16)] = int(match.group(2), 16)
    data = bytearray()
    for i in range(size):
        data.append(values.get(addr + i, 0))
    return bytes(data)


def main() -> None:
    parser = argparse.ArgumentParser(description="Read USB config descriptor from target via pyocd.")
    parser.add_argument("--elf", default="build/stm32f4_kernel/release/stm32f4_kernel.elf")
    parser.add_argument("--target", default="stm32f407vg")
    parser.add_argument("--max", type=int, default=512)
    args = parser.parse_args()

    elf_path = Path(args.elf)
    if not elf_path.exists():
        raise SystemExit(f"ELF not found: {elf_path}")

    addrs = resolve_symbols(elf_path)
    missing = [s for s in SYMBOLS if s not in addrs]
    if missing:
        raise SystemExit(f"Missing symbols: {', '.join(missing)}")

    size = read_u32(args.target, addrs["dbg_desc_size"])
    if size > args.max:
        size = args.max

    data = read_bytes(args.target, addrs["dbg_desc_buf"], size)
    for i in range(0, len(data), 16):
        chunk = data[i:i + 16]
        hex_bytes = " ".join(f"{b:02x}" for b in chunk)
        print(f"{i:04x}: {hex_bytes}")


if __name__ == "__main__":
    main()
