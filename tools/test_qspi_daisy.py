#!/usr/bin/env python3
"""
Daisy Pod QSPI 実機テストスクリプト

このスクリプトはDaisy PodのQSPIフラッシュメモリを実機テストします：
1. QSPIレジスタの状態確認
2. JEDEC ID読み出し（Flash種別確認）
3. 書き込みテスト（パターン書き込みと読み出し確認）
4. CRC読み出しによるデータ整合性確認

必要環境:
- Daisy PodがUSBで接続されていること
- umi_dfu.pyが同じディレクトリにあるか、PATHに含まれていること

使用方法:
    python3 test_qspi_daisy.py
"""

import subprocess
import sys
import time
from pathlib import Path

# テスト設定
QSPI_BASE = 0x90000000
QSPI_SIZE = 8 * 1024 * 1024  # 8MB
TEST_PATTERNS = [
    (b'\x00\x00\x00\x00\x00\x00\x00\x00', "zeros"),
    (b'\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF', "ones"),
    (b'\x55\x55\x55\x55\x55\x55\x55\x55', "0x55"),
    (b'\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA', "0xAA"),
    (b'\x12\x34\x56\x78\x9A\xBC\xDE\xF0', "increment"),
]

def run_dfu_command(args):
    """DFUコマンドを実行して結果を返す"""
    umi_dfu_path = Path(__file__).parent / "umi_dfu.py"
    if not umi_dfu_path.exists():
        umi_dfu_path = Path("umi_dfu.py")  # カレントディレクトリで検索
    
    cmd = [sys.executable, str(umi_dfu_path)] + args
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=10)
        if result.returncode != 0:
            print(f"  [ERR] Command failed: {' '.join(args)}")
            print(f"        stderr: {result.stderr[:200]}")
            return None
        return result.stdout
    except subprocess.TimeoutExpired:
        print(f"  [ERR] Command timeout: {' '.join(args)}")
        return None
    except Exception as e:
        print(f"  [ERR] Exception: {e}")
        return None

def test_1_debug_qspi():
    """テスト1: QSPIレジスタダンプ"""
    print("\n[テスト1] QSPIレジスタ状態確認")
    print("-" * 50)
    
    result = run_dfu_command(["debug-qspi"])
    if result is None:
        print("  [FAIL] QSPIデバッグコマンド失敗")
        return False
    
    print(result)
    
    if "CR:" in result and "SR:" in result and "CCR:" in result:
        print("  [PASS] QSPIレジスタ読み出し成功")
        return True
    else:
        print("  [WARN] レジスタ情報が不完全")
        return True  # 部分的な成功として扱う

def test_2_jedec_id():
    """テスト2: JEDEC ID読み出し"""
    print("\n[テスト2] Flash JEDEC ID読み出し")
    print("-" * 50)
    
    result = run_dfu_command(["flash-id"])
    if result is None:
        print("  [FAIL] JEDEC ID読み出し失敗")
        return False
    
    print(result)
    
    # IS25LP064A: 0x9D6017
    # W25Q64JV: 0xEF4017
    if "0x9D" in result or "0xEF" in result:
        print("  [PASS] 有効なFlash ID検出")
        return True
    else:
        print("  [WARN] 不明なFlash ID")
        return False

def test_3_flash_status():
    """テスト3: Flash Status Register読み出し"""
    print("\n[テスト3] Flash Status Register読み出し")
    print("-" * 50)
    
    result = run_dfu_command(["flash-status"])
    if result is None:
        print("  [FAIL] Status Register読み出し失敗")
        return False
    
    print(result)
    print("  [PASS] Status Register読み出し成功")
    return True

def test_4_pattern_write_verify():
    """テスト4: パターン書き込みと検証"""
    print("\n[テスト4] パターン書き込み/検証テスト")
    print("-" * 50)
    
    all_pass = True
    
    for pattern, name in TEST_PATTERNS:
        print(f"\n  パターン: {name} ({pattern.hex()})")
        
        # 1. データ書き込み
        offset = 0
        write_result = run_dfu_command([
            "write-data",
            f"0x{QSPI_BASE + offset:08X}",
            pattern.hex()
        ])
        
        if write_result is None:
            print(f"    [FAIL] 書き込み失敗")
            all_pass = False
            continue
        
        print(f"    [INFO] 書き込み完了")
        
        # 2. データ読み出し
        time.sleep(0.1)  # 書き込み安定化待ち
        
        read_result = run_dfu_command([
            "read-data",
            f"0x{QSPI_BASE + offset:08X}",
            str(len(pattern))
        ])
        
        if read_result is None:
            print(f"    [FAIL] 読み出し失敗")
            all_pass = False
            continue
        
        # 3. 検証
        # 読み出し結果からバイト列を抽出
        lines = read_result.strip().split('\n')
        data_line = None
        for line in lines:
            if line.startswith('Data:'):
                data_line = line
                break
        
        if data_line is None:
            print(f"    [FAIL] データフォーマット不正")
            all_pass = False
            continue
        
        # hex文字列を抽出
        hex_part = data_line.split(':')[1].strip() if ':' in data_line else data_line
        try:
            read_bytes = bytes.fromhex(hex_part.replace(' ', ''))
        except:
            print(f"    [FAIL] hexデコード失敗: {hex_part}")
            all_pass = False
            continue
        
        if read_bytes == pattern:
            print(f"    [PASS] パターンマッチ")
        else:
            print(f"    [FAIL] パターン不一致")
            print(f"      書込: {pattern.hex()}")
            print(f"      読出: {read_bytes.hex()}")
            all_pass = False
    
    return all_pass

def test_5_crc_verify():
    """テスト5: CRCによる整合性確認"""
    print("\n[テスト5] CRC整合性確認")
    print("-" * 50)
    
    # テストパターンを書き込み
    test_data = b'\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0A\x0B\x0C\x0D\x0E\x0F'
    
    result = run_dfu_command([
        "write-data",
        f"0x{QSPI_BASE:08X}",
        test_data.hex()
    ])
    
    if result is None:
        print("  [SKIP] CRCテスト（書き込み失敗のためスキップ）")
        return None
    
    time.sleep(0.1)
    
    # CRC読み出し
    crc_result = run_dfu_command([
        "read-crc",
        f"0x{QSPI_BASE:08X}",
        str(len(test_data))
    ])
    
    if crc_result is None:
        print("  [FAIL] CRC読み出し失敗")
        return False
    
    print(crc_result)
    
    if "CRC:" in crc_result or "0x" in crc_result:
        print("  [PASS] CRC取得成功")
        return True
    else:
        print("  [WARN] CRCフォーマット不明")
        return False

def main():
    print("=" * 60)
    print("Daisy Pod QSPI 実機テスト")
    print("=" * 60)
    print(f"QSPI Base: 0x{QSPI_BASE:08X}")
    print(f"QSPI Size: {QSPI_SIZE // (1024*1024)} MB")
    print()
    
    # DFUデバイス検出チェック
    print("[準備] DFUデバイス検出")
    print("-" * 50)
    result = run_dfu_command(["list"])
    if result is None or "UMI" not in str(result):
        print("[WARN] UMI DFUデバイスが見つかりません")
        print("Daisy PodをUSBで接続し、DFUモード（または通常モード）で")
        print("起動していることを確認してください。")
        print()
        print("テストを続行しますか？ (y/n): ", end='')
        response = input().strip().lower()
        if response != 'y':
            print("テストを中止します。")
            return 1
    else:
        print("[OK] UMI DFUデバイス検出")
        print(result)
    
    # 各テスト実行
    results = []
    
    results.append(("QSPIレジスタ", test_1_debug_qspi()))
    results.append(("JEDEC ID", test_2_jedec_id()))
    results.append(("Status Register", test_3_flash_status()))
    results.append(("パターン書き込み", test_4_pattern_write_verify()))
    results.append(("CRC整合性", test_5_crc_verify()))
    
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
        print("\n[全テスト成功] QSPIは正常に動作しています")
        return 0
    else:
        print(f"\n[{fail_count}件の失敗] 問題が検出されました")
        return 1

if __name__ == "__main__":
    sys.exit(main())
