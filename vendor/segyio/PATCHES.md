# vendor/segyio 本地补丁清单

基线：segyio **v2.0.0-alpha.1**（`CMakeLists.txt` 中 `paleo_segyio`）。下列为有意分叉，
升级上游时需逐条核对是否已被上游修复，未修复则重放。源码内以 `paleo local patch`
注释标出。

## S1 · `src/segy.c` — `trace_bsize` / `segy_trsize` 整数溢出

- `segy_collect_metadata`：`samplecount` 可来自 rev2 扩展字段（int32），
  `samplecount * elemsize` 在 int 上溢出属于 UB（审计 03 D3，libFuzzer+UBSan 复现：
  `segy.c:1120: signed integer overflow: 1289994496 * 4`），回绕后的 trace 尺寸
  继续参与 `segy_traces`/`segy_readsubtr` 的偏移与长度计算。改为 64 位求积，
  `samplecount < 0` 或 `bsize > INT_MAX - SEGY_TRACE_HEADER_SIZE` 时返回
  `SEGY_INVALID_FIELD_VALUE`。
- `segy_trsize`：同样 64 位求积，溢出返回 -1（与格式非法时同一错误约定）。
- 回归：`tests/tst_seismic_core.cpp::segyioRejectsOverflowingTraceSize`。

## 独立数据服务使用的上游来源

- 官方仓库：https://github.com/equinor/segyio
- 基线标签：`v2.0.0-alpha.1`，标签引用 `50ed15549a967b98dbde9f4e3122f5b3c5ff0bbf`（2026-10-28 核对）。
- 本服务直接链接 C 库；不要求目标电脑安装 Python。既有 S1 补丁保留。

## S2 · `src/segy.c::segy_traces` — 非法道长及超范围道数

- 道头总字节数先转为64位计算，禁止非正道长/非法道头数量参与取余。
- 道数超过 `INT_MAX` 时返回 `SEGY_INVALID_FIELD_VALUE`，替代仅Debug生效的断言和Release中的窄化转换。
- 服务仍使用64位文件偏移；接口的道数上限明确遵循该版本segyio的int道编号。
- 大文件回归：`tests/segyio_access_tests.cpp` 的Windows稀疏文件尾道读取（逻辑大小超过2GB，实际磁盘分配小于4MB）。
