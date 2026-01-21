#!/usr/bin/env python3
"""
STM32 Debug Utilities for UMI project.

Provides:
- Symbol table parsing from ELF files
- PyOCD target connection management
- Debug variable reading/writing
- USB audio device discovery

Usage as library:
    from stm32_debug import TargetDebugger, find_usb_audio_device

    with TargetDebugger('build/stm32f4_kernel/release/stm32f4_kernel') as dbg:
        value = dbg.read('dbg_streaming')
        dbg.write('dbg_some_var', 42)
"""

import subprocess
import re
from pathlib import Path
from contextlib import contextmanager
from typing import Optional, Dict, Tuple, List, Any


def parse_symbols(elf_path: str) -> Dict[str, int]:
    """
    Parse symbol table from ELF file using arm-none-eabi-nm.

    Args:
        elf_path: Path to ELF file (with or without extension)

    Returns:
        Dictionary mapping symbol names to addresses
    """
    elf = Path(elf_path)
    if not elf.suffix:
        elf = elf.with_suffix('.elf') if elf.with_suffix('.elf').exists() else elf

    if not elf.exists():
        raise FileNotFoundError(f"ELF file not found: {elf}")

    result = subprocess.run(
        ['arm-none-eabi-nm', '-C', str(elf)],
        capture_output=True, text=True, check=True
    )

    symbols = {}
    for line in result.stdout.splitlines():
        match = re.match(r'([0-9a-fA-F]+)\s+\S\s+(.+)', line)
        if match:
            addr, name = match.groups()
            symbols[name] = int(addr, 16)

    return symbols


def find_usb_audio_device(name_pattern: str = 'UMI') -> Tuple[Optional[int], Optional[str], int, int]:
    """
    Find USB audio device by name pattern.

    Args:
        name_pattern: Substring to match in device name

    Returns:
        Tuple of (device_index, device_name, output_channels, input_channels)
        Returns (None, None, 0, 0) if not found
    """
    try:
        import sounddevice as sd
        devices = sd.query_devices()
        for i, dev in enumerate(devices):
            if name_pattern in dev['name']:
                return i, dev['name'], dev['max_output_channels'], dev['max_input_channels']
    except ImportError:
        pass
    return None, None, 0, 0


class TargetDebugger:
    """
    Context manager for debugging STM32 targets via PyOCD.

    Automatically loads symbols from ELF file and provides convenient
    read/write access to debug variables by name.

    Example:
        with TargetDebugger('build/my_target/release/my_target') as dbg:
            isr_count = dbg.read('dbg_isr_count')
            print(f"ISR called {isr_count} times")
    """

    def __init__(self, elf_path: str, target: str = 'stm32f407vg',
                 auto_resume: bool = True):
        """
        Initialize debugger.

        Args:
            elf_path: Path to ELF file (symbols will be loaded from here)
            target: PyOCD target override string
            auto_resume: If True, resume target after each read/write
        """
        self.elf_path = elf_path
        self.target_name = target
        self.auto_resume = auto_resume
        self.symbols: Dict[str, int] = {}
        self.session = None
        self.target = None

    def __enter__(self):
        from pyocd.core.helpers import ConnectHelper

        self.symbols = parse_symbols(self.elf_path)
        self.session = ConnectHelper.session_with_chosen_probe(
            target_override=self.target_name
        )
        self.session.__enter__()
        self.target = self.session.target
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        if self.session:
            self.session.__exit__(exc_type, exc_val, exc_tb)
        return False

    def get_address(self, symbol: str) -> int:
        """Get address of symbol by name."""
        if symbol not in self.symbols:
            raise KeyError(f"Symbol not found: {symbol}")
        return self.symbols[symbol]

    def halt(self):
        """Halt target execution."""
        self.target.halt()

    def resume(self):
        """Resume target execution."""
        self.target.resume()

    def reset(self, halt: bool = False):
        """Reset target, optionally halting after reset."""
        if halt:
            self.target.reset_and_halt()
        else:
            self.target.reset()

    def read(self, symbol: str, size: int = 4, signed: bool = False) -> int:
        """
        Read debug variable by symbol name.

        Args:
            symbol: Symbol name (e.g., 'dbg_streaming')
            size: Size in bytes (1, 2, or 4)
            signed: If True, interpret as signed integer

        Returns:
            Value read from memory
        """
        addr = self.get_address(symbol)
        was_running = not self.target.is_halted()

        if was_running:
            self.target.halt()

        if size == 1:
            value = self.target.read8(addr)
        elif size == 2:
            value = self.target.read16(addr)
        else:
            value = self.target.read32(addr)

        if signed and size == 4 and value >= 0x80000000:
            value -= 0x100000000
        elif signed and size == 2 and value >= 0x8000:
            value -= 0x10000
        elif signed and size == 1 and value >= 0x80:
            value -= 0x100

        if was_running and self.auto_resume:
            self.target.resume()

        return value

    def read_block(self, symbol: str, count: int) -> List[int]:
        """
        Read block of 32-bit values starting at symbol address.

        Args:
            symbol: Symbol name for start address
            count: Number of 32-bit words to read

        Returns:
            List of values
        """
        addr = self.get_address(symbol)
        was_running = not self.target.is_halted()

        if was_running:
            self.target.halt()

        values = self.target.read_memory_block32(addr, count)

        if was_running and self.auto_resume:
            self.target.resume()

        return values

    def write(self, symbol: str, value: int, size: int = 4):
        """
        Write value to debug variable by symbol name.

        Args:
            symbol: Symbol name
            value: Value to write
            size: Size in bytes (1, 2, or 4)
        """
        addr = self.get_address(symbol)
        was_running = not self.target.is_halted()

        if was_running:
            self.target.halt()

        if size == 1:
            self.target.write8(addr, value & 0xFF)
        elif size == 2:
            self.target.write16(addr, value & 0xFFFF)
        else:
            self.target.write32(addr, value & 0xFFFFFFFF)

        if was_running and self.auto_resume:
            self.target.resume()

    def list_debug_symbols(self, prefix: str = 'dbg_') -> Dict[str, int]:
        """
        List all symbols matching prefix.

        Args:
            prefix: Symbol name prefix to filter

        Returns:
            Dictionary of matching symbol names to addresses
        """
        return {k: v for k, v in self.symbols.items() if k.startswith(prefix)}

    def snapshot(self, symbols: Optional[List[str]] = None,
                 prefix: str = 'dbg_') -> Dict[str, int]:
        """
        Read multiple debug variables at once (single halt/resume).

        Args:
            symbols: List of symbol names to read, or None for all with prefix
            prefix: If symbols is None, read all symbols with this prefix

        Returns:
            Dictionary of symbol names to values
        """
        if symbols is None:
            symbols = list(self.list_debug_symbols(prefix).keys())

        was_running = not self.target.is_halted()
        if was_running:
            self.target.halt()

        result = {}
        for sym in symbols:
            addr = self.get_address(sym)
            result[sym] = self.target.read32(addr)

        if was_running and self.auto_resume:
            self.target.resume()

        return result


def flash_firmware(elf_path: str, target: str = 'stm32f407vg',
                   reset_after: bool = True):
    """
    Flash firmware to target.

    Args:
        elf_path: Path to ELF or BIN file
        target: PyOCD target override
        reset_after: If True, reset target after flashing
    """
    from pyocd.core.helpers import ConnectHelper
    from pyocd.flash.file_programmer import FileProgrammer

    path = Path(elf_path)
    bin_path = path.with_suffix('.bin')

    if not bin_path.exists():
        raise FileNotFoundError(f"Binary file not found: {bin_path}")

    with ConnectHelper.session_with_chosen_probe(target_override=target) as session:
        session.target.reset_and_halt()
        FileProgrammer(session).program(str(bin_path), base_address=0x08000000)

        if reset_after:
            session.target.reset()


if __name__ == '__main__':
    # Simple test
    import sys

    if len(sys.argv) < 2:
        print("Usage: stm32_debug.py <elf_path> [symbol]")
        print("  List all debug symbols or read specific symbol")
        sys.exit(1)

    elf_path = sys.argv[1]

    if len(sys.argv) > 2:
        # Read specific symbol
        symbol = sys.argv[2]
        with TargetDebugger(elf_path) as dbg:
            value = dbg.read(symbol)
            print(f"{symbol} = {value} (0x{value:08x})")
    else:
        # List all debug symbols
        symbols = parse_symbols(elf_path)
        dbg_symbols = {k: v for k, v in symbols.items() if k.startswith('dbg_')}

        print(f"Debug symbols in {elf_path}:")
        for name, addr in sorted(dbg_symbols.items(), key=lambda x: x[1]):
            print(f"  0x{addr:08x}  {name}")
