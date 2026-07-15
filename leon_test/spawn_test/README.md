# spawn_test

本目录使用模拟器的出怪和选行逻辑进行独立实验。`spawn_test.cpp` 负责生成完整
20 波出怪和原生选行结果；`finish_table_test.cpp` 只采集收尾表实验所需的原始
数据；`analyze_finish_table.py` 负责概率计算和显著性分析。

## 收尾表测试流程

1. C++ 并行生成选卡、出怪列表和选行结果，输出紧凑的五路红眼计数直方图。
2. Python 读取直方图，对照“不考虑舞王”的旧收尾表，计算 IID 基线、误差和 p 值。

这种分工让模拟逻辑不依赖统计方法，也避免把百万次样本展开成体积很大的逐样本
CSV。

## C++ 配置

测试不解析命令行参数。直接修改 `finish_table_test.cpp` 开头的配置：

```cpp
constexpr std::size_t SAMPLE_COUNT = 1000000;
constexpr std::size_t BATCH_SIZE = 1000;
constexpr unsigned int THREAD_COUNT = 12;
constexpr std::uint32_t RANDOM_SEED = 20260714;
constexpr const char* OUTPUT_FILE
    = "leon_test/spawn_test/dest/finish_table_raw.csv";
constexpr std::array<int, 2> WAVES = {8, 18}; // w9, w19
```

| 配置 | 含义 |
| --- | --- |
| `SAMPLE_COUNT` | 每个场景生成的完整 20 波选卡样本数。统计前会过滤掉没有选中红眼的选卡，因此原始表中的有效样本数通常小于该值。 |
| `BATCH_SIZE` | 每个线程单次交给 `generate_spawn_samples()` 的样本数。增大它能减少生成器构造开销，但会增加每线程峰值内存。必须大于 0。 |
| `THREAD_COUNT` | 工作线程数。`0` 表示自动使用硬件线程数，正数表示固定线程数；实际线程数不会超过 `SAMPLE_COUNT`。 |
| `RANDOM_SEED` | 并行随机流的基础种子。所有配置相同时可以复现相同原始 CSV；改变线程数或批大小会改变具体样本，但不改变统计分布。 |
| `OUTPUT_FILE` | C++ 原始 CSV 路径，相对于启动程序时的当前工作目录。父目录必须已经存在。 |
| `WAVES` | 要保存的波次，使用模拟器内部 0 基下标；`{8, 18}` 表示玩家看到的第 9、19 波。 |

`SCENES` 紧随这些配置，每项只定义 CSV 标签和模拟器场景。当前测试为白天前院
`DE` 和夜晚前院 `NE`，都是五行场地。

运行时 C++ 会输出总工作量、实际线程数、10% 至 100% 的十个进度点，以及包含
CSV 写入在内的总运行时间。

## 原始 CSV

`finish_table_raw.csv` 按以下字段聚合重复样本：

```csv
scene,wave,dancer_selected,giga_row_1,giga_row_2,giga_row_3,giga_row_4,giga_row_5,frequency
```

| 列 | 含义 |
| --- | --- |
| `scene` | C++ `SCENES.name` 中定义的场景标签。 |
| `wave` | 玩家看到的 1 基波次。 |
| `dancer_selected` | 本次选卡是否包含舞王，`0` 表示没有，`1` 表示有。当前分析会合并两类，该列为后续分层分析保留。 |
| `giga_row_1` 至 `giga_row_5` | 该波分别出现在五路的红眼数量。五列可以同时为 0，因为选卡包含红眼不代表目标波一定出现红眼。 |
| `frequency` | 具有相同场景、波次、舞王状态和五路红眼计数的选卡样本数。 |

C++ 只输出选中了红眼的选卡。每个场景下不同目标波的 `frequency` 总和应相同。

## Python 分析

`analyze_finish_table.py` 顶部包含原始表、分析表路径，以及旧收尾表概率和 10 个
事件定义。当前事件为：无红眼、红眼不超过 1 至 4 路、预先固定 1 至 4 路，以及
可由一组连续三路覆盖。

脚本依赖 NumPy、pandas 和 SciPy，可使用以下命令安装：

```powershell
python -m pip install -r leon_test/spawn_test/requirements.txt
```

脚本使用 `frequency` 作为频数权重，不会将直方图展开为逐样本数据。它会合并
`dancer_selected` 两类样本，生成 `finish_table_analysis.csv`。

### 分析 CSV

每行对应一个“场景 + 波次 + 事件”组合。当前配置输出
`2 个场景 × 2 个波次 × 10 个事件 = 40` 行，概率和误差使用 `0-1` 小数。

| 列 | 含义 |
| --- | --- |
| `scene` | 场景标签。 |
| `wave` | 玩家看到的 1 基波次。 |
| `event` | 当前收尾事件。 |
| `samples` | 选中了红眼并进入统计的选卡样本数，不是红眼僵尸总数。 |
| `old_probability` | “不考虑舞王”的旧收尾表事件概率。 |
| `iid_same_count_probability` | 保留各样本实际红眼数，但假设每只红眼独立、等概率落入五路时的事件概率。 |
| `actual_probability` | 使用模拟器原生平滑选行得到的事件概率。`fixed_k` 对所有固定 `k` 路集合取平均。 |
| `count_effect` | 红眼数量分布误差，等于 `iid_same_count_probability - old_probability`。 |
| `row_effect` | 原生选行误差，等于 `actual_probability - iid_same_count_probability`。 |
| `total_effect` | 总误差，等于 `actual_probability - old_probability`，也等于 `count_effect + row_effect`。 |
| `row_se` | `row_effect` 的配对样本标准误。 |
| `row_p_bonf` | 检验 `row_effect = 0` 的双侧正态近似 p 值，并进行 10 重 Bonferroni 校正。 |
| `total_se` | `total_effect` 的标准误，旧表概率视为固定值。 |
| `total_p_bonf` | 检验 `total_effect = 0` 的双侧正态近似 p 值，并进行 10 重 Bonferroni 校正。 |

误差为正表示事件概率高于对照值，为负表示降低。重点查看 `row_effect` 和
`row_p_bonf` 可判断忽略平滑选行是否造成显著误差。

Python 还会输出：

```text
DE w9 edge=40.1234% p=0.5678
```

`edge` 是全部红眼中出现在第 1、5 路的比例；`p` 检验其是否偏离独立均匀选行的
`40%` 基线，并对 2 个场景 × 2 个波次的四次检验做 Bonferroni 校正。

## 修正表对比

`compare_corrected_finish_table.py` 使用同一份 `finish_table_raw.csv`，对比
`part-1.ipynb` 中考虑舞王边际选行概率后的 16 项修正收尾表，不需要重新运行 C++。

```powershell
python leon_test/spawn_test/compare_corrected_finish_table.py
```

脚本生成 `finish_table_corrected_comparison.csv`。其中固定 E/M 事件会对所有对称路集合
取平均，例如 `fixed_2_edge_middle` 同时包含六种“一个边路 + 一个中路”的组合，保证事件定义
与 notebook 的对称化计算一致。

| 列 | 含义 |
| --- | --- |
| `corrected_probability` | notebook 修正收尾表中的对照概率。 |
| `iid_corrected_same_count_probability` | 保留 raw 中每个样本的红眼数和舞王选中状态，使用 notebook 的舞王边际选行概率，但假定每只红眼独立选行。 |
| `actual_probability` | 模拟器完整平滑选行得到的实际概率。 |
| `count_effect` | raw 红眼数量/舞王混合分布与修正表之间的差异。 |
| `dependence_effect` | 平滑选行相关性带来的差异，即实际值减去独立选行值。 |
| `total_effect` | 实际值减去修正表值，等于 `count_effect + dependence_effect`。 |
| `dependence_se` / `dependence_p_bonf` | 相关性差异的配对标准误和 16 重 Bonferroni 校正 p 值。 |
| `total_se` / `total_p_bonf` | 总差异的标准误和 16 重 Bonferroni 校正 p 值。 |

修正表数值来自 notebook 的六位小数输出，因此小于约 `1e-6` 的差异不宜作精度层面的解释。

## 编译运行

在仓库根目录执行：

```powershell
mingw32-make -C leon_test/spawn_test
.\leon_test\spawn_test\finish_table_test.exe
python leon_test/spawn_test/analyze_finish_table.py
python leon_test/spawn_test/compare_corrected_finish_table.py
```

输出文件为：

```text
leon_test/spawn_test/dest/finish_table_raw.csv
leon_test/spawn_test/dest/finish_table_analysis.csv
leon_test/spawn_test/dest/finish_table_corrected_comparison.csv
```

仅清理本目录生成的可执行文件：

```powershell
mingw32-make -C leon_test/spawn_test clean
```
