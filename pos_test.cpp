/* 测试僵尸在 wave_length 时的坐标分布. */

#include "common/pe.h"
#include "common/test.h"
#include "seml/pos/lib.h"
#include "seml/refresh/name.h"
#include "world.h"

#include <iomanip>
#include <mutex>
#include <set>

using namespace pvz_emulator;
using namespace pvz_emulator::object;

std::vector<zombie_type> parse_zombie_types(const std::string& zombie_nums)
{
    std::vector<zombie_type> zombie_types;
    for (const auto& zombie_num : split(zombie_nums, ',')) {
        zombie_types.push_back(static_cast<zombie_type>(std::stoi(zombie_num)));
    }
    return zombie_types;
}

void validate_config(const Config& config)
{
    if (config.waves.empty()) {
        std::cerr << "请提供操作." << std::endl;
        exit(1);
    }
}

void validate_zombie_types(const std::vector<zombie_type>& zombie_types)
{
    for (const auto& type : zombie_types) {
        if (!_refresh_internal::ZOMBIE_NAMES.count(type)) {
            std::cerr << "不支持的僵尸类型: " << static_cast<int>(type) << std::endl;
            exit(1);
        }
    }
}

std::mutex mtx;
PosTable table;
TimeTable time_table;

void test_one(const Config& config, int repeat, const std::vector<zombie_type>& zombie_types, bool disable_cob_delay)
{
    world w(config.setting.scene_type);
    PosTable local_table;

    for (int r = 0; r < repeat; r++) {
        for (size_t wave_idx = 0; wave_idx < config.waves.size(); wave_idx++) {
            const auto& wave = config.waves[wave_idx];
            Test test;
            load_wave(config.setting, wave, zombie_types, test);

            w.scene.reset();
            w.scene.stop_spawn = true;
            w.scene.ignore_game_over = true;
            w.scene.disable_cob_delay = disable_cob_delay;

            auto it = test.ops.begin();
            int curr_tick = it->tick;
            for (; it != test.ops.end() && it->tick <= wave.wave_length; it++) {
                run(w, curr_tick, it->tick);
                it->f(w);
            }
            run(w, curr_tick, wave.wave_length);

            StatsMap local_stats;

            for (const auto& type : zombie_types) {
                local_stats[{static_cast<int>(wave_idx), type}].total_count += 5;
            }

            for (const auto& z : w.scene.zombies) {
                bool is_requested = false;
                for (const auto& type : zombie_types) {
                    if (z.type == type) {
                        is_requested = true;
                        break;
                    }
                }
                if (!is_requested)
                    continue;

                if (!z.is_dead && z.is_not_dying) {
                    auto key = StatsKey {static_cast<int>(wave_idx), z.type};
                    local_stats[key].alive_count++;
                    if (z.x < local_stats[key].min_x)
                        local_stats[key].min_x = z.x;
                    if (z.x > local_stats[key].max_x)
                        local_stats[key].max_x = z.x;
                    local_stats[key].histogram[static_cast<int>(z.x)]++;
                }
            }

            local_table.update(local_stats);
        }
    }

    std::lock_guard<std::mutex> guard(mtx);
    table.merge(local_table);
}

void test_one_time(
    const Config& config, int repeat, const std::vector<zombie_type>& zombie_types, int target_x, bool disable_cob_delay)
{
    world w(config.setting.scene_type);
    TimeTable local_table;

    for (int r = 0; r < repeat; r++) {
        for (size_t wave_idx = 0; wave_idx < config.waves.size(); wave_idx++) {
            const auto& wave = config.waves[wave_idx];
            Test test;
            load_wave(config.setting, wave, zombie_types, test);

            w.scene.reset();
            w.scene.stop_spawn = true;
            w.scene.ignore_game_over = true;
            w.scene.disable_cob_delay = disable_cob_delay;

            TimeMap local_stats;
            for (const auto& type : zombie_types) {
                local_stats[{static_cast<int>(wave_idx), type}].total_count += 5;
            }
            int expected_finished_count = 0;
            for (const auto& [key, stats] : local_stats) {
                expected_finished_count += stats.total_count;
            }

            auto it = test.ops.begin();
            int curr_tick = it->tick;

            for (; it != test.ops.end() && it->tick < 0; it++) {
                run(w, curr_tick, it->tick);
                it->f(w);
            }
            run(w, curr_tick, 0);

            std::set<const zombie*> arrived_zombies;
            std::set<const zombie*> dead_zombies;

            for (int tick = 0; tick <= wave.wave_length; tick++) {
                for (; it != test.ops.end() && it->tick == tick; it++) {
                    it->f(w);
                }

                for (const auto& z : w.scene.zombies) {
                    if (arrived_zombies.count(&z))
                        continue;

                    bool is_requested = false;
                    for (const auto& type : zombie_types) {
                        if (z.type == type) {
                            is_requested = true;
                            break;
                        }
                    }
                    if (!is_requested)
                        continue;

                    if (arrived_zombies.count(&z) || dead_zombies.count(&z)) {
                        continue;
                    }

                    if (z.is_dead) {
                        dead_zombies.insert(&z);
                        continue;
                    }

                    if (!z.is_dead && z.is_not_dying && static_cast<int>(z.x) <= target_x) {
                        auto key = StatsKey {static_cast<int>(wave_idx), z.type};
                        local_stats[key].arrived_count++;
                        if (tick < local_stats[key].min_tick)
                            local_stats[key].min_tick = tick;
                        if (tick > local_stats[key].max_tick)
                            local_stats[key].max_tick = tick;
                        local_stats[key].histogram[tick]++;
                        arrived_zombies.insert(&z);
                    }
                }

                if (static_cast<int>(arrived_zombies.size() + dead_zombies.size()) >= expected_finished_count) {
                    break;
                }

                if (tick < wave.wave_length) {
                    w.update();
                }
            }

            local_table.update(local_stats);
        }
    }

    std::lock_guard<std::mutex> guard(mtx);
    time_table.merge(local_table);
}

int main()
{
    auto start = std::chrono::high_resolution_clock::now();

    ::system("chcp 65001 > nul");

    auto args = parse_cmd_line();
    auto config_file = get_cmd_arg(args, "f");
    auto output_file = get_cmd_arg(args, "o", "pos_test");
    auto total_repeat_num = std::stoi(get_cmd_arg(args, "r", "20000"));
    auto zombie_types = parse_zombie_types(get_cmd_arg(args, "z"));
    auto x_arg = get_cmd_arg(args, "x", "");
    bool time_mode = !x_arg.empty();
    int target_x = time_mode ? std::stoi(x_arg) : -1;
    auto disable_cob_delay = !get_cmd_flag(args, "cd");
    auto show_std = get_cmd_flag(args, "std");

    auto [file, full_output_file] = open_csv(output_file);

    auto config = read_json(config_file);
    validate_config(config);
    validate_zombie_types(zombie_types);

    std::vector<std::thread> threads;
    if (time_mode) {
        for (int repeat : assign_repeat(total_repeat_num, std::thread::hardware_concurrency())) {
            threads.emplace_back([config, repeat, zombie_types, target_x, disable_cob_delay]() {
                test_one_time(config, repeat, zombie_types, target_x, disable_cob_delay);
            });
        }
    } else {
        for (int repeat : assign_repeat(total_repeat_num, std::thread::hardware_concurrency())) {
            threads.emplace_back(
                [config, repeat, zombie_types, disable_cob_delay]() { test_one(config, repeat, zombie_types, disable_cob_delay); });
        }
    }
    for (auto& t : threads) {
        t.join();
    }

    file << std::fixed;

    struct Column {
        int wave_idx;
        int wave_length;
        zombie_type type;
    };
    std::vector<Column> columns;
    for (size_t wave_idx = 0; wave_idx < config.waves.size(); wave_idx++) {
        for (const auto& type : zombie_types) {
            columns.push_back(
                {static_cast<int>(wave_idx), config.waves[wave_idx].wave_length, type});
        }
    }

    for (size_t i = 0; i < columns.size(); i++) {
        file << ",";
        if (i % zombie_types.size() == 0) {
            file << columns[i].wave_length;
        }
    }
    file << "\n";

    file << "僵尸类别";
    for (const auto& col : columns) {
        file << "," << zombie_type_to_name(col.type);
    }
    file << "\n";

    if (time_mode) {
        auto get_time_stats = [&](const Column& col) -> const TimeStats& {
            static const TimeStats empty;
            auto key = StatsKey {col.wave_idx, col.type};
            auto it = time_table.stats.find(key);
            if (it != time_table.stats.end())
                return it->second;
            return empty;
        };

        file << "到达率";
        for (const auto& col : columns) {
            const auto& s = get_time_stats(col);
            double p = s.total_count > 0 ? static_cast<double>(s.arrived_count) / s.total_count : 0.0;
            file << ",";
            if (show_std) {
                double se = s.total_count > 0 ? std::sqrt(p * (1.0 - p) / s.total_count) : 0.0;
                file << format_mean_std(100.0 * p, 100.0 * se, "%");
            } else {
                file << std::setprecision(2) << 100.0 * p << "%";
            }
        }
        file << "\n";

        file << "到达数";
        for (const auto& col : columns) {
            file << "," << get_time_stats(col).arrived_count;
        }
        file << "\n";

        file << "总数";
        for (const auto& col : columns) {
            file << "," << get_time_stats(col).total_count;
        }
        file << "\n";

        file << "时刻min";
        for (const auto& col : columns) {
            const auto& s = get_time_stats(col);
            file << ",";
            if (s.arrived_count > 0) {
                file << s.min_tick;
            }
        }
        file << "\n";

        file << "时刻max";
        for (const auto& col : columns) {
            const auto& s = get_time_stats(col);
            file << ",";
            if (s.arrived_count > 0) {
                file << s.max_tick;
            }
        }
        file << "\n";

        for (size_t i = 0; i < columns.size(); i++) {
            file << ",";
        }
        file << "\n";

        file << "累积概率";
        for (size_t i = 0; i < columns.size(); i++) {
            file << ",";
        }
        file << "\n";

        std::set<int> all_ticks;
        for (const auto& col : columns) {
            for (const auto& [tick, count] : get_time_stats(col).histogram) {
                all_ticks.insert(tick);
            }
        }

        std::vector<int> cumulative_time_counts(columns.size(), 0);
        for (int tick : all_ticks) {
            file << tick;
            for (size_t c = 0; c < columns.size(); c++) {
                file << ",";
                const auto& s = get_time_stats(columns[c]);
                auto it = s.histogram.find(tick);
                if (it != s.histogram.end()) {
                    cumulative_time_counts[c] += it->second;
                }
                if (s.arrived_count > 0 && s.total_count > 0
                    && tick >= s.min_tick && tick <= s.max_tick) {
                    double q = static_cast<double>(cumulative_time_counts[c]) / s.total_count;
                    if (show_std) {
                        double se = std::sqrt(q * (1.0 - q) / s.total_count);
                        file << format_mean_std(q, se, "", 10);
                    } else {
                        file << std::setprecision(10) << q;
                    }
                }
            }
            file << "\n";
        }
    } else {
        auto get_stats = [&](const Column& col) -> const ZombieStats& {
            static const ZombieStats empty;
            auto key = StatsKey {col.wave_idx, col.type};
            auto it = table.stats.find(key);
            if (it != table.stats.end())
                return it->second;
            return empty;
        };

        file << "存活率";
        for (const auto& col : columns) {
            const auto& s = get_stats(col);
            double p = s.total_count > 0 ? static_cast<double>(s.alive_count) / s.total_count : 0.0;
            file << ",";
            if (show_std) {
                double se = s.total_count > 0 ? std::sqrt(p * (1.0 - p) / s.total_count) : 0.0;
                file << format_mean_std(100.0 * p, 100.0 * se, "%");
            } else {
                file << std::setprecision(2) << 100.0 * p << "%";
            }
        }
        file << "\n";

        file << "存活数";
        for (const auto& col : columns) {
            file << "," << get_stats(col).alive_count;
        }
        file << "\n";

        file << "总数";
        for (const auto& col : columns) {
            file << "," << get_stats(col).total_count;
        }
        file << "\n";

        file << "坐标min";
        for (const auto& col : columns) {
            const auto& s = get_stats(col);
            file << ",";
            if (s.alive_count > 0) {
                file << std::setprecision(3) << s.min_x;
            }
        }
        file << "\n";

        file << "坐标max";
        for (const auto& col : columns) {
            const auto& s = get_stats(col);
            file << ",";
            if (s.alive_count > 0) {
                file << std::setprecision(3) << s.max_x;
            }
        }
        file << "\n";

        for (size_t i = 0; i < columns.size(); i++) {
            file << ",";
        }
        file << "\n";

        file << "累积概率";
        for (size_t i = 0; i < columns.size(); i++) {
            file << ",";
        }
        file << "\n";

        std::set<int> all_x_values;
        for (const auto& col : columns) {
            for (const auto& [x, count] : get_stats(col).histogram) {
                all_x_values.insert(x);
            }
        }

        std::vector<int> cumulative_pos_counts(columns.size(), 0);
        for (int x : all_x_values) {
            file << x;
            for (size_t c = 0; c < columns.size(); c++) {
                file << ",";
                const auto& s = get_stats(columns[c]);
                auto it = s.histogram.find(x);
                if (it != s.histogram.end()) {
                    cumulative_pos_counts[c] += it->second;
                }
                if (s.alive_count > 0 && s.total_count > 0
                    && x >= static_cast<int>(s.min_x) && x <= static_cast<int>(s.max_x)) {
                    double q = static_cast<double>(cumulative_pos_counts[c]) / s.total_count;
                    if (show_std) {
                        double se = std::sqrt(q * (1.0 - q) / s.total_count);
                        file << format_mean_std(q, se, "", 10);
                    } else {
                        file << std::setprecision(10) << q;
                    }
                }
            }
            file << "\n";
        }
    }

    std::chrono::duration<double> elapsed = std::chrono::high_resolution_clock::now() - start;
    std::cout << "输出文件已保存至 " << full_output_file << ".\n"
              << "耗时 " << std::fixed << std::setprecision(2) << elapsed.count() << " 秒, 使用了 "
              << threads.size() << " 个线程." << std::endl;

    return 0;
}
