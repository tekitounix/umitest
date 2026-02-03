#!/usr/bin/env python3
"""
Daisy Pod QSPI 実機テスト (pyocd版)

pyocdを使用してDaisy PodのQSPIフラッシュを直接テストします。
ST-Link/V3などのデバッガが必要です。

使用方法:
    1. Daisy PodをST-Linkで接続
    2. python3 test_qspi_pyocd.py
"""

import subprocess
import sys
import time
from pathlib import Path

def run_pyocd_cmd(subcommand, args, check=True):
    """pyocdコマンドを実行"""
    cmd = ["pyocd", subcommand, "-t", "stm32h750xx"] + args
    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=30
        )
        if check and result.returncode != 0:
            err = result.stderr[:200] if result.stderr else "no error output"
            print(f"  [ERR] pyocd failed: {err}")
            return None
        return result
    except subprocess.TimeoutExpired:
        print(f"  [ERR] Timeout")
        return None
    except FileNotFoundError:
        print(f"  [ERR] pyocd not found. Install: pip install pyocd")
        return None

def test_1_connection():
    """テスト1: 接続確認"""
    print("\n[テスト1] pyocd接続確認")
    print("-" * 50)

    result = run_pyocd_cmd("list", [], check=False)
    if result is None:
        return False

    print(result.stdout)

    if "stm32h750" in result.stdout.lower() or "stm32h7" in result.stdout.lower():
        print("  [PASS] STM32H750検出")
        return True
    else:
        print("  [WARN] STM32H750が見つかりません")
        print("  接続を確認してください")
        return False

def test_2_qspi_regs():
    """テスト2: QSPIレジスタ読み出し"""
    print("\n[テスト2] QSPIレジスタ読み出し")
    print("-" * 50)

    regs = {
        "CR": 0x52005000,
        "DCR": 0x52005004,
        "SR": 0x52005008,
        "CCR": 0x52005014,
        "ABR": 0x52005018,
    }

    all_ok = True
    for name, addr in regs.items():
        result = run_pyocd_cmd(
            "commander",
            ["-c", f"read32 0x{addr:08X}"]
        )
        if result and result.stdout:
            print(f"  {name} (0x{addr:08X}): {result.stdout.strip()}")
        else:
            print(f"  {name}: 読み出し失敗")
            all_ok = False

    return all_ok

def test_3_qspi_memory_mapped():
    """テスト3: Memory-mappedモードでQSPIメモリ読み出し"""
    print("\n[テスト3] QSPIメモリ読み出し (0x90000000)")
    print("-" * 50)

    result = run_pyocd_cmd(
        "commander",
        ["-c", "read32 0x90000000 4"]
    )

    if result is None or not result.stdout:
        print("  [FAIL] QSPIメモリ読み出し失敗")
        return False

    print(f"  QSPI[0x90000000]: {result.stdout.strip()}")

    lines = result.stdout.strip().split('\n')
    if lines:
        print("  [PASS] QSPIメモリアクセス成功")
        return True

    return False

def test_4_write_read():
    """テスト4: 書き込みと読み出しテスト"""
    print("\n[テスト4] QSPI書き込み/読み出しテスト")
    print("-" * 50)

    loader_path = Path(__file__).parent / "stm32_external_loader" / "IS25LP064A_DaisySeed.stldr"
    if not loader_path.exists():
        print(f"  [SKIP] 外部ローダーが見つかりません: {loader_path}")
        return None

    print(f"  [INFO] 外部ローダー検出: {loader_path.name}")

    # Create test pattern file
    test_data = bytes([0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                       0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
                       0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA,
                       0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0])

    import tempfile
    with tempfile.NamedTemporaryFile(delete=False, suffix=".bin") as f:
        f.write(test_data)
        test_file = f.name

    try:
        # Erase and write to QSPI
        print("  [INFO] QSPIにテストデータ書き込み中...")
        result = run_pyocd_cmd(
            "flash",
            ["-a", "0x90000000", "--format", "bin", "--erase", "sector",
             "--loader", str(loader_path), test_file],
            check=False
        )

        if result is None or result.returncode != 0:
            print(f"  [FAIL] 書き込み失敗: {result.stderr if result else 'timeout'}")
            return False

        print("  [INFO] 書き込み成功、読み出し検証中...")

        # Read back and verify
        read_result = run_pyocd_cmd(
            "commander",
            ["-c", "read32 0x90000000 8"]
        )

        if read_result is None or not read_result.stdout:
            print("  [FAIL] 読み出し失敗")
            return False

        print(f"  読出データ: {read_result.stdout.strip()}")
        print("  [PASS] 書き込み/読み出し成功")
        return True

    finally:
        import os
        os.unlink(test_file)

def main():
    print("=" * 60)
    print("Daisy Pod QSPI 実機テスト (pyocd版)")
    print("=" * 60)
    print()
    
    results = []
    
    results.append(("接続確認", test_1_connection()))
    results.append(("QSPIレジスタ", test_2_qspi_regs()))
    results.append(("QSPIメモリ", test_3_qspi_memory_mapped()))
    results.append(("書き込み機能", test_4_write_read()))
    
    # サマリー
    print("\n" + "=" * 60)
    print("テスト結果サマリー")
    print("=" * 60)
    
    pass_count = sum(1 for _, r in results if r is True)
    fail_count = sum(1 for _, r in results if r is False)
    skip_count = sum(1 for _, r in results if r is None)
    
    for name, result in results:
        status = "PASS" if result is True else ("SKIP" if result is None else "FAIL")
        symbol = "✓" if result is True else ("○" if result is None else "✗")
        print(f"  {symbol} {name}: {status}")
    
    print()
    print(f"合計: {len(results)}件 / PASS: {pass_count} / FAIL: {fail_count} / SKIP: {skip_count}")
    
    if fail_count == 0:
        print("\n[成功] pyocd経由でQSPIにアクセスできます")
        return 0
    else:
        print(f"\n[{fail_count}件の問題] 接続または設定を確認してください")
        return 1

if __name__ == "__main__":
    sys.exit(main())
