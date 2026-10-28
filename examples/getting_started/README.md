# 九个 C++ 入门案例

每个目录的 `main.cpp` 都是完整程序，只通过公开 `DataService` 获取或修改对象。测试数据不在仓库中；可先在仓库外通过 `create_api_demo_project` 生成合成工区，或使用自己已经导入的工区。

## 编译

先构建主工程并安装 SDK，再从仓库根目录配置案例工程。以下路径应替换为自己的安装位置：

```powershell
cmake --install build --prefix sdk
cmake -S examples/getting_started -B build-examples -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="<SDK绝对目录>;<Qt6目录>" -DZLIB_ROOT="<Zlib目录>"
cmake --build build-examples --parallel 8
```

`#include <paleo_data/service.h>` 引入类型声明；`find_package` 和 `target_link_libraries` 找到并链接实际库，两个步骤都需要。程序内部建立 `QCoreApplication`；运行时须能找到 Qt、SQLite 插件及相关运行库。

以下命令省略 Windows `.exe` 后缀。多配置构建器的程序可能在 `Release` 子目录。`<...>` 是需要替换的参数说明，不能直接照输；资料编号先通过目录查询获得，不固定假定某个数字。

## 案例目录

| 案例 | 调用方式 | 学习内容 |
|---|---|---|
| [01 井清单](01_list_wells/main.cpp) | `example_01_list_wells <工区>` | `openProject`、`listWells`，了解每口井的资料覆盖 |
| [02 按名取曲线](02_curve_by_name/main.cpp) | `example_02_curve_by_name <工区> <井名> <曲线名> [曲线组编号]` | 名称定位到稳定编号，再取得 `CurveSlice` |
| [03 曲线统计](03_curve_statistics/main.cpp) | `example_03_curve_statistics <工区> <曲线编号> <顶深> <底深>` | 按深度读取，按质量过滤，再计算统计量 |
| [04 分层](04_read_intervals/main.cpp) | `example_04_read_intervals <工区> [分层道编号 [顶深 底深]]` | 先列道，再读取 `IntervalRecord` 和深度约定 |
| [05 时深](05_read_time_depth/main.cpp) | `example_05_read_time_depth <工区> [时深表编号]` | 取得完整深度—时间数组及原有约定 |
| [06 特征表](06_feature_table/main.cpp) | `example_06_feature_table <工区> <井编号> <曲线编号1> <曲线编号2>` | 组合共同深度轴的曲线，供后续算法调用 |
| [07 JSON 导出](07_export_curve_json/main.cpp) | `example_07_export_curve_json <工区> <曲线编号> <新JSON路径> [顶深 底深]` | 显式序列化完整返回对象，不覆盖已有文件 |
| [08 修改闭环](08_edit_temporary_project/main.cpp) | `example_08_edit_temporary_project` | 自动生成临时输入，导入、清理、改值、重开验证 |
| [09 SEG-Y](09_segyio/main.cpp) | `example_09_segyio <工区> [地震体编号]` | 检查布局，按需读取单道/剖面/切片，处理与统计 |

前六个案例主要读取数据；07 写入调用方明确指定的新导出文件；08 的修改只发生在自动清理的临时工区；09 只读访问外部地震体。

## 先看目录，再读数值

```cpp
paleo::data::CurveSlice result;
std::string error;
bool success = service.readCurveSlice(
    curveId,                      // 来自当前工区的曲线编号。
    paleo::data::Missing,          // 不限制顶深。
    paleo::data::Missing,          // 不限制底深。
    true,                         // 采用处理值；原值仍保留。
    result,
    error);
```

`success` 表示调用是否成功。实际数据在 `result.depths`、`rawValues`、`values` 和 `quality` 中。只向屏幕打印少量样点不表示返回对象只有这些点。

`DataEntry` 是目录卡片。曲线名可能重复，应进一步明确曲线组或直接用编号，不随意取第一条。质量 1 表示来源空值，即使原始哨兵是有限数也应排除；质量 2、4 的使用策略由业务决定。合法负值保留。

## 合成工区练习

```powershell
.\build\create_api_demo_project.exe "D:/PaleoDemo/synthetic-project"
.\build-examples\example_01_list_wells.exe "D:/PaleoDemo/synthetic-project"
.\build-examples\example_02_curve_by_name.exe "D:/PaleoDemo/synthetic-project" DEMO-1 GR
.\build-examples\example_09_segyio.exe "D:/PaleoDemo/synthetic-project"
```

合成生成器的井名、坐标和数值均由程序生成。它不是实际测区，不能用作地质推论。输入工区路径应包含 `data.sqlite`，而不是原始 LAS 或 SEG-Y 所在目录。

## 结果的使用边界

- 分层范围查询保留原层段边界，不自动裁剪为请求区间。深度类型、单位和未钻穿状态需一并解释。
- 时深表保留正负时间和未知约定，不自动插值、换单位或把单程换为双程。
- 特征表目前要求同井、同一曲线组的公共深度轴，不隐式重采样；它是算法输入，不代表已经执行地质分类。
- JSON 导出是 `CurveSlice` 的后续处理。非有限值写为 `null`，质量与原值保留；大地震体不应整体转成 JSON。
- 修改点的 `sampleIndex` 是原曲线从 0 开始的采样索引，不是深度或筛选后的行号。

若返回 `false`，先处理 `error`。成功返回空数组可能表示范围内没有数据。更改公开结构体后应重新编译消费程序，避免继续使用旧的二进制布局。

更多说明见 [调用阅读指南](../../docs/cpp-reading-guide.md) 和 [地震指南](../../docs/segyio-guide.md)。
