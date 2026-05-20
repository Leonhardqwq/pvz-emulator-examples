# imp_test 中 uuid 的作用与风险报告

## Summary

`imp_test` 中的 `uuid` 不是单纯的输出编号，而是对象身份追踪的一部分。它用于确认对象池槽位没有被复用、避免重复记录同一只小鬼、判断小鬼被哪次炮/灰烬命中，并把小鬼啃食伤害归到对应保护植物。

旧版 `get_uuid()` 如果使用普通全局 `int next_uuid++`，在多线程测试下会出现非线程安全的全局自增问题。`imp_test` 对对象身份依赖很重，因此比其他测试更容易把这个隐患暴露为统计异常。

## uuid 的来源

植物和僵尸创建时都会分配 `uuid`：

```cpp
z.uuid = get_uuid();
p.uuid = get_uuid();
```

它的设计目的接近“对象池 generation id”：指针负责找到对象，`uuid` 负责确认该指针指向的仍是当初记录的对象，而不是对象池槽位被释放后复用出来的新对象。

测试框架中的 `unique_plant` / `unique_zombie` 依赖这个语义：

```cpp
bool is_valid() const { return ptr && ptr->uuid == uuid; }
```

## imp_test 中的具体用途

### 1. 防止对象池槽位复用误认

`imp_test` 会保存保护植物、炮/灰烬来源植物、临时垫材植物和小鬼对象：

```cpp
test.protect_plants.push_back({&p, p.uuid});
test.action_infos[idx].plants.push_back({&p, p.uuid});
test.plants_to_be_shoveled[idx].push_back({&p, p.uuid});
imp_info.zombie = {&z, z.uuid};
```

如果只保存指针，旧对象死亡后，同一槽位可能被新对象复用，测试代码就可能把新对象误认为旧对象。`uuid` 用于避免这种误认。

### 2. 小鬼去重

`sync_imps()` 每 tick 会多次扫描僵尸对象池，以便及时记录新生小鬼、命中信息和死亡时刻。为了避免同一只小鬼被重复加入 `imp_infos`，代码使用：

```cpp
test.imp_index_by_uuid.count(z.uuid)
test.imp_index_by_uuid[z.uuid] = test.imp_infos.size();
```

如果同一次模拟中两个不同小鬼被分配到相同 `uuid`，后一个小鬼会被误判为“已记录”，导致实际投出小鬼数偏少，进一步让 `未投出` 统计偏多。

### 3. 炮/灰烬命中归因

炮和灰烬命中僵尸时，模拟器会把来源植物的 `uuid` 记录到僵尸的 `hit_by_ash` 中：

```cpp
z.hit_by_ash.arr[z.hit_by_ash.size++] = from_plant;
```

`imp_test` 同步小鬼时会把这些来源 `uuid` 复制到 `ImpInfo::hit_by_ash`。详细表判断某个操作是否命中这组小鬼时，会比较：

```cpp
imp_info.hit_by_ash.count(plant.uuid)
```

因此，`uuid` 决定了详细表操作列中的 `HIT`，也影响小鬼结果分类：`拦截失败`、`拦截过晚`、`拦截成功`。

### 4. 保护植物伤害归因

小鬼啃食伤害预测会找到当前目标植物，再用 `uuid` 判断它是不是某个保护植物：

```cpp
if (test.protect_plants[i].uuid == target->uuid) {
    return i;
}
```

如果保护植物身份判断错误，详细表和小鬼炮伤表中的保护植物伤害都会被归到错误列。

## 多线程下的风险

旧版实现若为：

```cpp
int next_uuid = 0;

int get_uuid() {
    return next_uuid++;
}
```

则多个线程会共享同一个 `next_uuid`。`next_uuid++` 不是原子操作，它大致包含读取、加一、写回三步。两个线程同时执行时，可能读到同一个旧值并返回相同 `uuid`。

这种问题不会必然立刻报错。C++ 普通变量的并发读写属于数据竞争，标准层面是未定义行为；实际运行时常见表现是偶发重复、丢增量或统计异常。

## 对 imp_test 的具体影响

### 小鬼记录偏少

如果同一次模拟中不同小鬼拿到相同 `uuid`，`imp_index_by_uuid` 会把后出现的小鬼当成已记录对象跳过。结果是：

```text
实际投出小鬼数偏少
父巨人数 - 实际小鬼数 偏大
未投出统计偏多
```

### 操作 HIT 归因错误

如果炮/灰烬来源植物的 `uuid` 和其他来源冲突，或者小鬼 `hit_by_ash` 中记录的来源无法可靠对应到 `ActionInfo.plants`，详细表中的 `HIT` 列可能错误，从而影响拦截结果分类。

### 保护植物伤害归因错误

如果保护植物 `uuid` 和其他植物冲突，或者旧保护植物槽位被复用后仍被误认为有效，小鬼啃食伤害可能被归到错误保护植物上。

### 有效性判断错误

`unique_plant::is_valid()` 和 `unique_zombie::is_valid()` 依赖 `ptr + uuid` 判断对象是否仍为原对象。若对象池槽位复用后新对象碰巧拿到旧 `uuid`，测试代码可能误判旧引用仍有效，进而误铲植物、误读 HP 或误同步小鬼状态。

## 需要区分的概念

不同 `world` 中出现相同 `uuid`，从设计语义上不一定是错误。如果 `uuid` 被设计为 world-local，那么跨 world 重复是自然且合理的。

当前风险的关键不在于“跨 world 的编号本来不能重复”，而在于旧版 `get_uuid()` 是进程级全局普通自增。多个线程同时创建对象时，它既不是 world-local 编号器，也不是线程安全的全局编号器。

当前 `imp_index_by_uuid` 是 `Test` 的成员，每次模拟单独创建，并不会直接把多个线程、多个 world 的小鬼 uuid 合并到同一个 map 中。真正危险的是对象创建阶段的全局自增不可靠，而 `imp_test` 后续又高度依赖对象身份进行去重和归因。

## 与反编译源码的关系

反编译项目中，原版使用 `DataArray<T>` 管理对象池。`DataArrayGetID()` 返回的 ID 由两部分组成：

```text
高位 generation/key + 低位 slot index
```

本项目的 `obj_list.get_index()` 接近原版 ID 的低位槽位，而 `uuid` 接近原版 ID 的高位 generation/key。区别是，原版的 key 属于每个 `DataArray`，而本项目当前的 `uuid` 由全局 `get_uuid()` 生成。

因此，更贴近原版的设计是让对象池自身维护 generation/key，而不是使用进程级全局 `next_uuid`。

