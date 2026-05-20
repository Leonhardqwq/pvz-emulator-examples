# SEML 测试程序小数输出格式

本文记录 `seml/out/bin` 中各测试程序对应 C++ 源码的小数输出规则。结论主要来自 `pvz-emulator-examples` 中的 `smash_test.cpp`、`explode_test.cpp`、`refresh_test.cpp`、`pogo_test.cpp`，以及对 SEML 插件内 `pos_test.exe` 的实际输出观察。

## 总览

| 测试 | 小数字段 | 计算/存储类型 | 输出方式 | 精度特点 |
| --- | --- | --- | --- | --- |
| `smash_test` 砸率 | 单波砸率、各路砸率 | `double` | `std::fixed << std::setprecision(2) << value << "%"` | 固定 2 位小数，百分号文本，例如 `12.34%` |
| `explode_test` 炮伤 | 炮伤、瞬伤、损伤均值 | `std::optional<double>` | `std::ostringstream << double`，没有 `fixed/setprecision` | C++ 默认 `defaultfloat`，6 位有效数字；不补零，必要时可能使用科学计数法 |
| `refresh_test` 刷新 | 平均意外率、各出怪组合意外率 | 平均值为 `double`，行值为 `float` | `file << std::fixed << std::setprecision(3)` 后输出 `100.0 * rate << "%"` | 固定 3 位小数，百分号文本，例如 `1.234%` |
| `refresh_test` raw log | 血量比例 `ratio` | 表达式为 `double` | `std::fixed << std::setprecision(3)` | 固定 3 位小数，无百分号，例如 `0.650` |
| `pogo_test` 跳跳 | 无小数结果字段 | 整数范围 | 直接 `file << int` 或输出 `ERR` | CSV 结果没有浮点小数 |
| `pos_test` 坐标分布 | 累计概率 | 二进制输出表现为小数概率 | CSV 中直接输出十进制小数，无百分号 | 固定 6 位小数，例如 `0.522000` |
| `pos_test` 坐标分布 | 存活率/到达率 | 二进制输出表现为百分率 | 百分号文本 | 固定 2 位小数，例如 `52.20%` |
| `pos_test` 坐标分布 | 坐标 min/max | 二进制输出表现为坐标值 | 十进制小数 | 固定 3 位小数，例如 `720.069` |
| 所有测试 stdout | 耗时秒数 | `std::chrono::duration<double>` | `std::fixed << std::setprecision(2)` | 固定 2 位小数，例如 `0.06 秒` |

## 逐项说明

### `smash_test`

砸率由 `calc_smash_rate()` 返回 `double`，输出时统一使用：

```cpp
std::fixed << std::setprecision(2) << calc_smash_rate(...) << "%"
```

因此 CSV 中砸率不是纯数值概率，而是带 `%` 的文本，固定保留 2 位小数。

### `explode_test`

炮伤、瞬伤、损伤先按 `double` 计算平均值，再放入 `std::vector<std::optional<double>>`。输出时使用局部 `std::ostringstream`，但没有设置 `std::fixed` 或 `std::setprecision`。

这意味着它使用 C++ iostream 默认浮点格式：`defaultfloat`、6 位有效数字。输出不会强制补零，也不是固定小数位数。

### `refresh_test`

刷新测试内部的意外率主要以 `float` 累加，平均意外率汇总为 `double`。主 CSV 在输出表格前设置：

```cpp
file << std::fixed << std::setprecision(3);
```

后续输出 `100.0 * rate << "%"`, 因此主表中的意外率是带 `%` 的文本，固定 3 位小数。

如果开启 raw log，`ratio` 也使用 `std::fixed << std::setprecision(3)`，但它是普通比例值，不带 `%`。

### `pogo_test`

跳跳测试输出的是收跳范围的左右边界，字段是整数或 `ERR`，没有浮点格式设置。

### `pos_test`

当前检查到的 `pvz-emulator-examples` 工作树没有保留 SEML 插件中 `out/bin/pos_test.exe` 对应的完整源码，因此这里按二进制实际输出行为记录。

普通坐标分布模式会输出类似：

```csv
累积概率,
720,0.015000
721,0.033000
...
784,1.000000
```

累计概率是 `0` 到 `1` 区间的小数文本，无 `%`，固定 6 位小数。启用 `targetPos` 后，第一列从坐标变为时刻，累计概率仍然保持固定 6 位小数，但最后一项不一定到 `1.000000`，而是反映实际到达率。

## 公共 CSV 行为

所有这些测试程序都通过 `common/test.h` 中的 `open_csv()` 打开 CSV，文件以二进制方式写入，并手动写入 UTF-8 BOM。因此中文表头在 Excel 中通常能正常识别。
