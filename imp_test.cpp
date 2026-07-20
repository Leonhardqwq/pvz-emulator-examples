/* 测试小鬼拦截失败率和炮伤.
 */

#include "common/pe.h"
#include "common/test.h"
#include "seml/imp/lib.h"
#include "world.h"

#include <algorithm>
#include <mutex>
#include <unordered_set>

using namespace pvz_emulator;
using namespace pvz_emulator::object;

std::vector<zombie_type> get_garg_types(const Setting& setting)
{
    std::vector<zombie_type> garg_types;
    std::unordered_set<int> seen;

    if (setting.types.empty()) {
        return {zombie_type::gargantuar, zombie_type::giga_gargantuar};
    }

    for (const auto& garg_type : setting.types) {
        int type = static_cast<int>(garg_type);
        if (type != static_cast<int>(zombie_type::gargantuar)
            && type != static_cast<int>(zombie_type::giga_gargantuar)) {
            std::cerr << "不支持的巨人类型: " << type << std::endl;
            exit(1);
        }
        if (seen.count(type)) {
            std::cerr << "巨人类型重复: " << type << std::endl;
            exit(1);
        }

        seen.insert(type);
        garg_types.push_back(garg_type);
    }

    if (garg_types.empty()) {
        std::cerr << "请提供巨人类型." << std::endl;
        exit(1);
    }

    return garg_types;
}

std::vector<unsigned int> parse_spawn_rows(const std::string& row_nums)
{
    std::vector<unsigned int> rows;
    for (const auto row_num : row_nums) {
        if (row_num < '1' || row_num > '6') {
            std::cerr << "spawnRow 应由 1~6 的不重复路数组成" << std::endl;
            exit(1);
        }
        auto row = static_cast<unsigned int>(row_num - '1');
        if (std::find(rows.begin(), rows.end(), row) != rows.end()) {
            std::cerr << "spawnRow 中的路数不可重复" << std::endl;
            exit(1);
        }
        rows.push_back(row);
    }
    return rows;
}

void validate_config(const Config& config)
{
    if (config.waves.empty()) {
        std::cerr << "请提供操作." << std::endl;
        exit(1);
    }

    if (config.waves.size() > 200) {
        std::cerr << "波数超过 200: " << config.waves.size() << std::endl;
        exit(1);
    }
    if (config.setting.imp_index.high_ratio < 0.0f || config.setting.imp_index.high_ratio > 1.0f) {
        std::cerr << "impIndex:ratio value should be in [0, 1]" << std::endl;
        exit(1);
    }
}

void validate_spawn_rows(scene_type scene_type, const std::vector<zombie_type>& garg_types,
    const std::vector<unsigned int>& spawn_rows)
{
    if (spawn_rows.empty()) {
        return;
    }
    world w(scene_type);
    for (const auto row : spawn_rows) {
        if (row >= w.scene.rows) {
            std::cerr << "spawnRow 超出当前场景的路数范围" << std::endl;
            exit(1);
        }
        for (const auto type : garg_types) {
            auto& z = w.zombie_factory.create(type, static_cast<int>(row));
            if (z.row != row) {
                std::cerr << garg_type_to_string(type) << "不能在" << row + 1 << "路生成"
                          << std::endl;
                exit(1);
            }
        }
    }
}

void apply_imp_index_setting(scene& scene, const Setting::ImpIndex& imp_index)
{
    scene.imp_index_mode = imp_index.mode;
    scene.imp_high_ratio = imp_index.high_ratio;
}

std::string protect_desc(const Setting::ProtectPos& protect_position)
{
    std::ostringstream os;
    os << protect_position.row << "路" << protect_position.col;
    if (protect_position.is_cob()) {
        os << "炮";
    } else {
        os << "普通";
    }
    return os.str();
}

double calc_rate(int leaked_count, int total_count)
{
    if (total_count == 0) {
        return 0;
    }
    return 100.0 * leaked_count / total_count;
}

std::mutex mtx;
TestInfo test_info;

void test_one(const Config& config, int repeat, const std::vector<zombie_type>& garg_types,
    const std::vector<unsigned int>& spawn_rows, bool disable_cob_delay)
{
    world w(config.setting.scene_type);
    TestInfo local_test_info;

    for (int r = 0; r < repeat; r++) {
        for (const auto& garg_type : garg_types) {
            Test test;
            load_config(config, garg_type, spawn_rows, test);

            w.scene.reset();
            w.scene.stop_spawn = true;
            w.scene.ignore_game_over = true;
            w.scene.disable_cob_delay = disable_cob_delay;
            apply_imp_index_setting(w.scene, config.setting.imp_index);

            auto it = test.ops.begin();
            int curr_tick = it == test.ops.end() ? 0 : it->tick;

            while (curr_tick <= test.end_tick) {
                for (; it != test.ops.end() && it->tick == curr_tick; it++) {
                    it->f(w);
                }

                update_one_tick(test, w, curr_tick);
                curr_tick++;
            }

            local_test_info.update(test);
        }
    }

    std::lock_guard<std::mutex> guard(mtx);
    test_info.merge(local_test_info);
}

int main()
{
    auto start = std::chrono::high_resolution_clock::now();

    ::system("chcp 65001 > nul");

    auto args = parse_cmd_line();
    auto config_file = get_cmd_arg(args, "f");
    auto output_file = get_cmd_arg(args, "o", "imp_test");
    auto total_repeat_num = std::stoi(get_cmd_arg(args, "r", "10000"));
    auto spawn_rows = parse_spawn_rows(get_cmd_arg(args, "row", ""));
    auto disable_cob_delay = !get_cmd_flag(args, "cd");

    auto [file, full_output_file] = open_csv(output_file);

    auto config = read_json(config_file);
    validate_config(config);
    auto garg_types = get_garg_types(config.setting);
    validate_spawn_rows(config.setting.scene_type, garg_types, spawn_rows);

    std::vector<std::thread> threads;
    for (int repeat : assign_repeat(total_repeat_num, std::thread::hardware_concurrency())) {
        threads.emplace_back([config, repeat, garg_types, spawn_rows, disable_cob_delay]() {
            test_one(config, repeat, garg_types, spawn_rows, disable_cob_delay);
        });
    }
    for (auto& t : threads) {
        t.join();
    }

    auto [table, summary] = test_info.make_table_and_summary();

    file << "单波拦截失败率,";
    for (const auto& wave : summary.waves) {
        file << "w" << wave << ",";
    }
    file << "\n";

    file << "总和,";
    for (const auto& wave : summary.waves) {
        const auto& wave_summary = summary.wave_summary.at(wave);
        file << std::fixed << std::setprecision(6)
             << calc_rate(wave_summary.leaked_count, wave_summary.total_count) << "%,";
    }
    file << "\n";

    for (const auto& row : summary.rows) {
        file << row + 1 << "路,";
        for (const auto& wave : summary.waves) {
            const auto& wave_summary = summary.wave_summary.at(wave);
            file << std::fixed << std::setprecision(6)
                 << calc_rate(wave_summary.leaked_count_by_row[row], wave_summary.total_count)
                 << "%,";
        }
        file << "\n";
    }

    file << "\n小鬼炮伤,";
    for (const auto& wave : summary.waves) {
        file << "w" << wave << ",";
    }
    file << "\n";

    file << "总和,";
    for (const auto& wave : summary.waves) {
        const auto& wave_summary = summary.wave_summary.at(wave);
        long long damage = 0;
        for (const auto& value : wave_summary.damage_by_protect) {
            damage += value;
        }
        file << std::fixed << std::setprecision(6)
             << static_cast<double>(damage) / total_repeat_num << ",";
    }
    file << "\n";

    for (size_t i = 0; i < config.setting.protect_positions.size(); i++) {
        file << protect_desc(config.setting.protect_positions[i]) << ",";
        for (const auto& wave : summary.waves) {
            const auto& wave_summary = summary.wave_summary.at(wave);
            long long damage = i < wave_summary.damage_by_protect.size()
                ? wave_summary.damage_by_protect[i]
                : 0;
            file << std::fixed << std::setprecision(6)
                 << static_cast<double>(damage) / total_repeat_num << ",";
        }
        file << "\n";
    }

    Test sample_test;
    load_config(config, garg_types.front(), spawn_rows, sample_test);

    file << "\n出生波数,巨人类型,拦截结果,总数,";
    for (const auto& row : summary.rows) {
        file << row + 1 << "路,";
    }
    for (const auto& protect_position : config.setting.protect_positions) {
        file << protect_desc(protect_position) << ",";
    }

    auto prev_wave = -1;
    for (const auto& action_info : sample_test.action_infos) {
        if (action_info.wave != prev_wave) {
            file << "[w" << action_info.wave << "] ";
            prev_wave = action_info.wave;
        }
        file << action_info.desc << ",";
    }
    file << "\n";

    for (const auto& [os, data] : table) {
        file << os.wave << "," << garg_type_to_string(os.garg_type) << ","
             << imp_result_to_string(os.result) << "," << data.total_count << ",";

        for (const auto& row : summary.rows) {
            file << data.total_count_by_row[row] << ",";
        }

        for (size_t i = 0; i < config.setting.protect_positions.size(); i++) {
            if (os.result == ImpResult::Leaked || os.result == ImpResult::LateIntercepted) {
                file << "INF,";
            } else if (os.result == ImpResult::NotThrown) {
                file << "0,";
            } else {
                long long damage = i < data.damage_by_protect.size()
                    ? data.damage_by_protect[i]
                    : 0;
                file << damage << ",";
            }
        }

        for (const auto& op_state : os.op_states) {
            file << op_state_to_string(op_state) << ",";
        }
        file << "\n";
    }

    std::chrono::duration<double> elapsed = std::chrono::high_resolution_clock::now() - start;
    std::cout << "输出文件已保存至 " << full_output_file << ".\n"
              << "耗时 " << std::fixed << std::setprecision(2) << elapsed.count() << " 秒, 使用了 "
              << threads.size() << " 个线程." << std::endl;

    if (test_info.attribution_error_count > 0) {
        std::cout << "警告: 小鬼伤害归因校验出现 " << test_info.attribution_error_count
                  << " 次不一致，请检查模拟器啃食预测逻辑。" << std::endl;
    }

    return 0;
}
