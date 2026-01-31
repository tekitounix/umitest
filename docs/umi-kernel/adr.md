# UMI Kernel ADR

**目的:** 仕様の設計判断を簡潔に記録する。

---

## ADR-0001: Syscall 番号体系
- コアAPI/FS/拡張のグルーピング方式を採用。
- グループ内は連番で詰め、拡張のために予約帯域を確保する。

## ADR-0002: SharedMemory 優先方針
- 共有メモリで済むものは `syscall` にしない。
- `syscall` は特権操作・スケジューラ操作・ブートストラップに限定。

## ADR-0003: SharedMemory メタ情報統合
- ABI/上限/機能フラグなどのメタ情報は共有メモリ先頭に集約する。

## ADR-0004: SystemTask 優先度
- SystemTask は ControlTask より高優先度とし、OS 生存性を保証する。

## ADR-0005: FS 非同期一本化
- FS syscall は非同期一本化とし、完了通知はイベントで返す。
