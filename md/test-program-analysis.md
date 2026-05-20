# SEML 测试程序功能分析

本文分析 `explode_test.cpp`、`pogo_test.cpp`、`pos_test.cpp`、`refresh_test.cpp` 和 `smash_test.cpp` 五个测试程序的作用、输入形式、核心逻辑和输出形式。它们不是传统单元测试，而是一组基于 PvZ Emulator 的蒙特卡洛/统计测试程序：统一读取 JSON 阵型脚本，重复模拟很多次，再把统计结果写成带时间戳的 CSV。

## 公共输入

公共命令行解析位于 `common/test.h`。各程序的基本调用形式一致：

```powershell
dest/bin/xxx_test.exe -f config.json [-o output_name] [-r repeat] [其他开关]
```

其中 `-f` 为必填配置文件路径，`-o` 为输出文件名前缀，`-r` 为重复模拟次数。CSV 实际文件名会自动追加时间戳：

```text
output_name (YYYY.MM.DD_HH.MM.SS) .csv
```

程序通过 `open_csv()` 以二进制方式打开文件，并写入 UTF-8 BOM，因此中文表头通常能被 Excel 正确识别。

这些程序共用 `seml/reader/types.h` 和 `seml/reader/reader.h` 定义的 JSON 配置结构。典型结构如下：

```json
{
  "setting": {
    "scene": "FE",
    "originalScene": "FE",
    "protect": [
      { "type": "Cob", "row": 2, "col": 5 },
      { "type": "Normal", "row": 4, "col": 7 }
    ]
  },
  "waves": [
    {
      "iceTimes": [298],
      "waveLength": 601,
      "startTick": 401,
      "actions": []
    }
  ]
}
```

`scene` 和 `originalScene` 可取 `DE`、`NE`、`PE`、`FE`、`RE`、`ME`，分别对应白天、黑夜、泳池、雾夜、屋顶、月夜屋顶。`protect` 表示保护位，其中 `Normal` 会按普通植物处理，通常种伞叶；`Cob` 会按玉米炮处理。普通保护植物的 `col` 是自身列，玉米炮的 `col` 表示右端列，实际种植时会使用 `col - 2` 作为 0 下标列。

`waves` 中每个元素描述一波：`iceTimes` 为冰时机，`waveLength` 为该波模拟长度，`startTick` 为部分测试开始统计的时刻，`actions` 为操作列表。reader 对 `iceTimes`、`waveLength`、`actions` 是直接读取的，因此最好总是写上这些字段。`startTick` 可省略，部分程序会自行设置默认值。

公共动作类型包括 `Cob`、`FixedCard`、`SmartCard`、`FixedFodder` 和 `SmartFodder`。`Cob.time` 表示炮落点生效时刻，程序会按炮飞行时间提前发射；`FixedCard` 和 `SmartCard` 的 `time` 表示卡片生效时刻，实际种植会按植物类型提前；垫材类 `Fodder` 在 `time` 时刻种下，可用 `shovelTime` 指定铲除时刻。

一个容易误读的公共开关是 `-cd`。代码里使用：

```cpp
auto disable_cob_delay = !get_cmd_flag(args, "cd");
```

因此默认会禁用炮弹延迟；传入 `-cd` 反而会保留模拟器默认的炮延迟行为。

## explode_test.cpp

`explode_test.cpp` 用来测试小丑爆炸以及其他伤害对保护植物的期望损失。

### 输入形式

命令行参数：

```powershell
dest/bin/explode_test.exe -f config.json [-o explode_test] [-r 10000] [-cd]
```

`-o` 默认值为 `explode_test`，`-r` 默认值为 `10000`，`-cd` 表示保留炮延迟。配置文件必须包含至少一波和至少一个保护位，保护位不允许同一行重复。如果某波没有 `startTick`，程序会把它设为该波 `waveLength`。

### 功能逻辑

每次重复、每一波都会重建场景。程序先种下保护植物，并把它们的 HP 设置为极大值，同时设置 `ignore_jack_explode`，让小丑爆炸不会真正删除保护植物，但仍然记录爆炸命中来源。

随后程序固定生成以下僵尸各 5 个：

- 小丑
- 梯子
- 橄榄
- 投篮

之后执行配置中的冰、炮、卡片、垫材动作。从 `startTick` 到 `waveLength`，程序每 tick 记录每个保护植物的爆炸命中次数和 HP 损失。

爆炸信息分为三类来源：

- `from_upper`：来自上方行的小丑爆炸
- `from_same`：来自本行的小丑爆炸
- `from_lower`：来自下方行的小丑爆炸

统计时会把所有保护植物的记录合并，并在多线程之间继续合并。

### 输出形式

输出 CSV 按 tick 展开，主表有三组列：

- `炮伤`
- `瞬伤`
- `损伤`

按代码实际计算：

- 第一组 `炮伤` 对应总损失：`(爆炸折算损失 + HP 损失) / repeat`
- 第二组 `瞬伤` 对应爆炸折算损失：`爆炸命中次数 * 300 / repeat`
- 第三组 `损伤` 对应普通 HP 损失：`hp_loss / repeat`

每组内部每波一列，列顶会写该波动作描述。某个 tick 如果有多个有效值，程序会用方括号标出该组中的最小值，例如 `[12.3]`。

## pogo_test.cpp

`pogo_test.cpp` 用来测试跳跳僵尸与炮落点的交互，输出每个时刻可命中跳跳 hitbox 的炮横坐标范围。它实质上是在测“收跳范围”。

### 输入形式

命令行参数：

```powershell
dest/bin/pogo_test.exe -f config.json [-o pogo_test2] [-r 1000]
```

`-o` 默认值为 `pogo_test2`，`-r` 默认值为 `1000`。程序只使用 `waves[0]`。如果配置多波，它只会输出警告，仍然取第一波。若第一波没有 `startTick`，默认设为 `waveLength - 200`。

屋顶场景需要额外注意：程序必须知道发炮列 `cobCol` 才能计算屋顶炮落点 y 坐标。因此如果是 `RE` 或 `ME`，第一波动作中必须至少有一个 `Cob`，程序会取第一个 `Cob.cobCol`。

保护位也有限制：不同保护位的列范围不能重叠。普通保护位范围是 `[col, col]`，玉米炮保护位范围是 `[col - 1, col]`。

### 功能逻辑

每次重复从 tick `-100` 跑到 `waveLength`。如果配置了冰时机，程序会在 `iceTime - 99` 种冰菇。

tick 0 时：

- 在第 2 行种下配置中的保护物，普通位种伞叶，玉米炮位种玉米炮。
- 在第 2 行生成 1000 个跳跳僵尸。

从 `startTick` 开始，每个 tick 遍历所有跳跳僵尸，并针对三种收跳关系计算允许炮心 x 范围：

- 收上行跳跳
- 收本行跳跳
- 收下行跳跳

程序使用 `get_cob_hit_xy()` 计算炮落点坐标，再用 `get_cob_hit_x_range()` 根据跳跳 hitbox 和炮半径反推出炮心 x 的允许区间。对所有跳跳、所有重复，程序取区间交集。因此输出范围是保守交集：炮心落在该区间内才覆盖所有采样到的跳跳位置。

### 输出形式

CSV 表头固定为：

```csv
时刻,收上行跳跳左,右,收本行跳跳左,右,收下行跳跳左,右,
```

每行对应一个 tick，后面是三组整数左右边界。如果没有有效范围，则输出 `ERR`。

## pos_test.cpp

`pos_test.cpp` 用来统计指定僵尸类型在脚本结束时的位置分布，或者统计它们首次到达某个 x 坐标的时间分布。

### 输入形式

命令行参数：

```powershell
dest/bin/pos_test.exe -f config.json -z zombie_ids [-o pos_test] [-r 20000] [-x target_x] [-cd]
```

`-z` 必填，使用逗号分隔僵尸类型编号。`-o` 默认值为 `pos_test`，`-r` 默认值为 `20000`。如果提供 `-x target_x`，程序进入到达时间模式；否则为普通位置模式。`-cd` 表示保留炮延迟。

僵尸编号来自 `pvz_emulator::object::zombie_type`，常用值包括：

| 编号 | 类型 |
| --- | --- |
| 0 | 普僵 |
| 2 | 路障 |
| 3 | 撑杆 |
| 4 | 铁桶 |
| 7 | 橄榄 |
| 15 | 小丑 |
| 18 | 跳跳 |
| 21 | 梯子 |
| 22 | 投篮 |
| 23 | 白眼 |
| 32 | 红眼 |

程序会用 `seml/refresh/name.h` 中的名称表校验 `-z` 指定的类型。如果配置中没有 waves，会直接退出。

### 功能逻辑

普通位置模式下，每次重复、每一波都会：

1. 重建场景。
2. 种下配置中的保护植物，并把保护植物 HP 设置为极大。
3. tick 0 生成 `-z` 指定的每种僵尸各 5 个。
4. 执行配置动作并跑到 `waveLength`。
5. 遍历场上僵尸，只统计指定类型、仍存活且非濒死的僵尸。

统计内容包括：

- 总生成数
- 存活数
- 最小 x
- 最大 x
- `int(x)` 位置直方图

时间模式下，程序从 tick 0 到 `waveLength` 逐 tick 检查每只目标僵尸。僵尸第一次满足：

```cpp
static_cast<int>(z.x) <= target_x
```

就记录为到达，并记录当前 tick。死亡但没到达的僵尸计入未到达。若所有目标僵尸都已到达或死亡，该波提前结束。

### 输出形式

CSV 首先按“波”和“僵尸类型”铺列：第一行写各列对应的 `waveLength`，第二行写僵尸类别。

普通位置模式输出：

- `存活率`
- `存活数`
- `总数`
- `坐标min`
- `坐标max`
- `累积概率`

时间模式输出：

- `到达率`
- `到达数`
- `总数`
- `时刻min`
- `时刻max`
- `累积概率`

累计概率的分母是总生成数，而不是存活数或到达数。因此普通位置模式下，最后一项通常等于存活率；时间模式下，最后一项通常等于到达率。

输出精度方面，程序先设置 `std::fixed`。百分率使用 `setprecision(2)` 并带 `%`，坐标 min/max 使用 `setprecision(3)`，累计概率使用 `setprecision(10)`。

## refresh_test.cpp

`refresh_test.cpp` 用来评估不同出怪组合下的刷新意外率。

### 输入形式

命令行参数：

```powershell
dest/bin/refresh_test.exe -f config.json [-o refresh_test] [-r 1000] [-req ids] [-ban ids] [-h] [-a] [-d] [-n] [-raw] [-cd]
```

参数含义：

- `-o`：输出前缀，默认 `refresh_test`
- `-r`：重复次数，默认 `1000`
- `-req`：必出僵尸类型编号列表，逗号分隔
- `-ban`：禁出僵尸类型编号列表，逗号分隔
- `-h`：按旗帜波/大波处理
- `-a`：按“激活”假设计算意外率
- `-d`：启用舞王 dance cheat
- `-n`：按权重自然出怪；不传则均匀出怪
- `-raw`：额外输出原始日志
- `-cd`：保留炮延迟

`setting.originalScene` 会用于按场地限制可出怪类型。配置中必须有至少一波。

### 功能逻辑

每个 repeat 先随机生成一组出怪类型。生成逻辑位于 `seml/refresh/spawn.h`：

- 固定包含普僵和雪人。
- 路障与读报二选一作为基础类型。
- 加入 `-req` 指定的必出类型。
- 排除 `-ban` 指定的禁出类型。
- 按场地排除天然不可出类型。
- 随机补足到目标类型数。

每个 repeat 内又会做 20 次 spawn list 测试。每次会为每个配置波生成 50 只僵尸，然后执行脚本到：

```cpp
wave.wave_length - 200
```

程序记录初始总血量 `init_hp` 和当前总血量 `curr_hp`，并计算血量比例：

```cpp
hp_ratio = curr_hp / init_hp
```

意外率公式为：

```cpp
refresh_prob = (0.65 - clamp(hp_ratio, 0.5, 0.65)) / 0.15;
accident = assume_activate ? 1.0 - refresh_prob : refresh_prob;
```

也就是说，`-a` 会把结果解释为“激活假设”下的意外率；不传 `-a` 时则按“分离假设”下的刷新概率输出。

### 输出形式

主 CSV 前几行写：

- 测试环境
- 普通波/旗帜波
- 激活/分离
- dance 设置
- 自然出怪/均匀出怪
- 必出类型
- 禁出类型
- 每波动作描述

随后每个配置波对应一组列。每组先输出 `平均意外率`，再列出出怪组合和该组合对应的意外率。组合按意外率从高到低排序。

主表在输出前设置：

```cpp
std::fixed << std::setprecision(3)
```

因此意外率为带 `%` 的文本，固定 3 位小数。

如果开启 `-raw`，程序会额外生成一个：

```text
output_raw (YYYY.MM.DD_HH.MM.SS) .csv
```

raw 文件记录每次模拟的 `index`、`wave`、`init_hp`、`curr_hp`、`ratio`，以及各类僵尸按剩余血量分桶的数量。

## smash_test.cpp

`smash_test.cpp` 用来统计红眼巨人对保护位的砸率，并按每个动作是否命中或垫到巨人做分类表。

### 输入形式

命令行参数：

```powershell
dest/bin/smash_test.exe -f config.json [-o smash_test] [-r 10000] [-cd]
```

`-o` 默认值为 `smash_test`，`-r` 默认值为 `10000`，`-cd` 表示保留炮延迟。配置必须有至少一波，保护位不能为空，同一行不能有多个保护位，波数不能超过 200。

### 功能逻辑

`smash_test` 和其他几个程序有一个明显不同点：它不是逐波独立模拟，而是通过 `load_config()` 把所有 waves 串成一条连续时间线。每一波开始都会生成 5 个红眼巨人，行号只从配置的保护位行中随机选择。

程序会先种下保护植物，并设置：

```cpp
p.ignore_garg_smash = true;
```

这样保护植物被巨人砸时不会真的消失，但巨人的 `ignored_smashes` 会记录这次砸击。程序还会跟踪每个红眼：

- 出生波数
- 出生时刻
- 所在行
- 存活时间
- 被哪些灰烬 uuid 命中
- 尝试砸过哪些垫材 uuid
- 因保护植物忽略而记录的砸击

每个炮、灰烬卡片或垫材动作都会生成一条 `ActionInfo`，记录动作类型、波数、时刻、描述和相关植物或炮弹 uuid。

最终对每个红眼、每个动作分类：

- `NotBorn`：动作发生时红眼尚未出生，输出为空
- `Dead`：动作发生时红眼已经死亡，输出为空
- `Hit`：灰烬命中该红眼，或该红眼尝试砸该垫材
- `Miss`：动作发生在红眼存活期间，但没有命中或垫到

需要注意的是，`FixedCard` 中只有辣椒、樱桃和倭瓜被归为灰烬动作；其他植物会进入垫材分支。虽然 reader 允许 `doomshroom`，但当前 `smash_test` 的分类逻辑没有把核蘑列入灰烬分支。

### 输出形式

第一块是单波砸率汇总：

```csv
单波砸率,w1,w2,...
总和,...
<保护位1>,...
<保护位2>,...
```

保护位行名形如 `2路5炮` 或 `4路7普通`。砸率由 `calc_smash_rate()` 计算：

```cpp
100.0 * (smashed_garg_count / (total_garg_count / 5.0))
    * (protect_positions.size() / total_garg_rows)
```

其中泳池/雾夜场地的 `total_garg_rows` 为 4，其他场地为 5。输出为固定 2 位小数并带 `%`。

第二块是明细表：

```csv
出生波数,单波砸率,砸炮数,总数,<各动作状态>,<各保护行砸数>
```

动作列会按配置中的动作顺序排列，并在波数变化时加上 `[wN]` 前缀。动作状态只输出 `HIT` 或 `MISS`；出生前和死亡后为空。最后几列输出每个保护行实际记录到的砸击次数。

## 五个程序对比

| 程序 | 主要用途 | 主要统计对象 | 核心输出 |
| --- | --- | --- | --- |
| `explode_test.cpp` | 测小丑爆炸/普通损伤对保护位的期望损失 | 保护植物 | 每 tick 的总损失、爆炸折算损失、HP 损失 |
| `pogo_test.cpp` | 测跳跳收跳炮点范围 | 跳跳 hitbox 与炮心范围 | 每 tick 的上/本/下行收跳 x 区间 |
| `pos_test.cpp` | 测僵尸最终位置或到达时间分布 | 指定僵尸类型 | 存活率/到达率、min/max、累计概率 |
| `refresh_test.cpp` | 测不同出怪组合的刷新意外率 | 出怪类型组合 | 平均意外率、组合意外率排行 |
| `smash_test.cpp` | 测红眼对保护位的砸率 | 红眼巨人和保护位 | 单波砸率、动作 HIT/MISS 明细 |
