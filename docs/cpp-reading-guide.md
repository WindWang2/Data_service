# 看懂一次 C++ 数据调用

先通过 CMake 链接 SDK，再包含 `paleo_data/service.h`。宿主建立 `QCoreApplication` 后创建 `DataService`，打开工区，选择资料编号，最后读取数值。

## 调用返回什么

以下是已经打开工区、选定曲线编号后的片段；完整程序见 [案例 02](../examples/getting_started/02_curve_by_name/main.cpp)。

```cpp
paleo::data::CurveSlice result; // 接收深度、原值、处理值、质量数组。
std::string error;             // 接收失败原因。
bool success = service.readCurveSlice(
    curveId,                  // 当前工区中已选定曲线的稳定编号。
    paleo::data::Missing,      // 不限制顶深。
    paleo::data::Missing,      // 不限制底深。
    true,                     // 使用处理值，原值仍保存在 rawValues。
    result,
    error);
if (!success) {
    std::cerr << error << '\n';
    return 1;
}
for (size_t i = 0; i < result.depths.size(); ++i) {
    // 同一个 i 的深度、数值和质量描述同一个返回样点。
    if (std::isfinite(result.values[i]) && result.quality[i] != 1) {
        // 根据业务的质量策略进一步计算或绘图。
    }
}
```

`bool` 只说明是否成功，实际数据写入 `result`。`DataEntry` 是供选择的资料卡片；`CurveSlice` 包含可以直接计算的数组。需要跨程序传输时再做 JSON 序列化，不必先把 C++ 对象变成字符串。

## 四个数组

| 字段 | 含义 |
|---|---|
| `depths[i]` | 返回样点的深度 |
| `rawValues[i]` | 同一位置的来源原值 |
| `values[i]` | 本次选用的原值或处理值 |
| `quality[i]` | 同一位置的质量说明 |

质量值为 0 正常、1 来源空值、2 阈值异常、4 人工修改。有限的原始空值哨兵也不能参与统计，不能只检查 `std::isfinite`。是否排除质量 2 和是否接受质量 4，应由业务明确决定；负数本身不一定异常。

编号在当前工区和对应资料类别内有效；不能把一个工区的编号直接用于另一个工区。深度筛选后的数组下标也不等于原始行号或原始采样索引。

## 常见类型

| 类型/写法 | 用途 |
|---|---|
| `std::string` | 井名、单位、错误说明等文字 |
| `std::vector<double>` | 长度可变的数值数组 |
| `long long` | 数据库编号或较大的数量 |
| `bool` | 是否成功、是否开启某选项 |
| `& result` | 接收调用方准备的结果对象，方法在其中填写内容 |
| `const` | 此处只读取对象 |
| `Missing` | 缺失数值；范围参数中表示不限制该端 |

成功但数组为空可能是正常结果，例如范围内没有样点。判断是否调用失败应读取返回的 `bool`，而不是只凭数组长度或 `error` 内容。

地震剖面振幅数组采用 `column * samples + sample`；时间切片采用 `inlineIndex * crosslineCount + crosslineIndex`。坐标、时间轴和实际测线号由返回对象同时提供，见 [地震接口](segyio-guide.md)。
