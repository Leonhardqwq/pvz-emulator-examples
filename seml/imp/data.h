#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <set>
#include <unordered_map>

#include "types.h"

enum OpState : char {
    Blank,
    Hit,
};

enum ImpResult : char {
    NotThrown,
    Leaked, // 拦截失败
    LateIntercepted, // 拦截过晚
    Intercepted, // 拦截成功
};

struct OpStates {
    std::vector<OpState> op_states;
    int wave;
    pvz_emulator::object::zombie_type garg_type;
    ImpResult result;

    bool operator==(const OpStates& other) const
    {
        return op_states == other.op_states && wave == other.wave
            && garg_type == other.garg_type && result == other.result;
    }

    struct Hash {
        size_t operator()(const OpStates& os) const noexcept
        {
            size_t hash = 0;
            hash ^= std::hash<int>()(os.wave) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            hash ^= std::hash<int>()(static_cast<int>(os.garg_type)) + 0x9e3779b9
                + (hash << 6) + (hash >> 2);
            hash ^= std::hash<int>()(static_cast<int>(os.result)) + 0x9e3779b9
                + (hash << 6) + (hash >> 2);
            for (const auto& state : os.op_states) {
                hash ^= std::hash<int>()(state) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            }
            return hash;
        }
    };
};

struct Data {
    int total_count = 0;
    std::array<int, 6> total_count_by_row = {};
    std::vector<long long> damage_by_protect;
};

using Table = std::vector<std::pair<OpStates, Data>>;

struct WaveSummary {
    int total_count = 0;
    int leaked_count = 0;
    std::array<int, 6> leaked_count_by_row = {};
    std::vector<long long> damage_by_protect;
};

struct Summary {
    std::set<int> waves;
    std::set<int> rows;
    std::unordered_map<int, WaveSummary> wave_summary;
};

namespace _imp_internal {

const int IMP_FATAL_DAMAGE = 300;

void ensure_damage_size(std::vector<long long>& damage_by_protect, size_t size)
{
    if (damage_by_protect.size() < size) {
        damage_by_protect.resize(size);
    }
}

long long total_damage(const ImpInfo& imp_info)
{
    long long damage = 0;
    for (const auto& value : imp_info.damage_by_protect) {
        damage += value;
    }
    return damage;
}

bool is_late_intercepted(const ImpInfo& imp_info)
{
    return total_damage(imp_info) >= IMP_FATAL_DAMAGE;
}

bool is_hit_by_action(const ActionInfo& action_info, const ImpInfo& imp_info)
{
    for (const auto& plant : action_info.plants) {
        if (imp_info.hit_by_ash.count(plant.uuid)) {
            return true;
        }
    }
    return false;
}

bool is_intercepted(const Test& test, const ImpInfo& imp_info)
{
    for (const auto& action_info : test.action_infos) {
        if (is_hit_by_action(action_info, imp_info)) {
            return true;
        }
    }
    return false;
}

ImpResult classify_imp_result(const Test& test, const ImpInfo& imp_info)
{
    if (!is_intercepted(test, imp_info)) {
        return ImpResult::Leaked;
    }
    if (is_late_intercepted(imp_info)) {
        return ImpResult::LateIntercepted;
    }
    return ImpResult::Intercepted;
}

OpState categorize(const ActionInfo& action_info, const ImpInfo& imp_info)
{
    if (is_hit_by_action(action_info, imp_info)) {
        return OpState::Hit;
    }
    return OpState::Blank;
}

void add_damage(Data& data, const ImpInfo& imp_info)
{
    ensure_damage_size(data.damage_by_protect, imp_info.damage_by_protect.size());
    for (size_t i = 0; i < imp_info.damage_by_protect.size(); i++) {
        data.damage_by_protect[i] += imp_info.damage_by_protect[i];
    }
}

} // namespace _imp_internal

struct TestInfo {
    std::unordered_map<OpStates, Data, OpStates::Hash> info;
    int attribution_error_count = 0;

    void update(const Test& test)
    {
        using namespace _imp_internal;

        attribution_error_count += test.attribution_error_count;

        std::unordered_map<int, std::array<int, 6>> garg_count_by_wave;
        std::unordered_map<int, std::array<int, 6>> imp_count_by_wave;

        for (const auto& garg_info : test.garg_infos) {
            if (garg_info.row < 6) {
                garg_count_by_wave[garg_info.spawn_wave][garg_info.row]++;
            }
        }

        for (const auto& imp_info : test.imp_infos) {
            if (imp_info.row < 6) {
                imp_count_by_wave[imp_info.spawn_wave][imp_info.row]++;
            }

            const auto result = classify_imp_result(test, imp_info);

            OpStates os;
            os.wave = imp_info.spawn_wave;
            os.garg_type = imp_info.garg_type;
            os.result = result;
            os.op_states.reserve(test.action_infos.size());
            for (const auto& action_info : test.action_infos) {
                os.op_states.push_back(categorize(action_info, imp_info));
            }

            auto& data = info[os];
            _imp_internal::ensure_damage_size(data.damage_by_protect, test.protect_positions.size());
            data.total_count++;
            if (imp_info.row < 6) {
                data.total_count_by_row[imp_info.row]++;
            }
            if (result == ImpResult::Intercepted) {
                add_damage(data, imp_info);
            }
        }

        for (const auto& [wave, garg_rows] : garg_count_by_wave) {
            const auto imp_it = imp_count_by_wave.find(wave);

            OpStates os;
            os.wave = wave;
            os.garg_type = test.garg_type;
            os.result = ImpResult::NotThrown;
            os.op_states.assign(test.action_infos.size(), OpState::Blank);

            auto& data = info[os];
            _imp_internal::ensure_damage_size(data.damage_by_protect, test.protect_positions.size());

            for (size_t row = 0; row < garg_rows.size(); row++) {
                int thrown_count = 0;
                if (imp_it != imp_count_by_wave.end()) {
                    thrown_count = imp_it->second[row];
                }

                int not_thrown_count = garg_rows[row] - thrown_count;
                if (not_thrown_count > 0) {
                    data.total_count += not_thrown_count;
                    data.total_count_by_row[row] += not_thrown_count;
                }
            }
        }
    }

    void merge(const TestInfo& other)
    {
        attribution_error_count += other.attribution_error_count;

        for (const auto& [op_states, data] : other.info) {
            auto& current = info[op_states];
            current.total_count += data.total_count;
            for (size_t i = 0; i < current.total_count_by_row.size(); i++) {
                current.total_count_by_row[i] += data.total_count_by_row[i];
            }

            _imp_internal::ensure_damage_size(
                current.damage_by_protect, data.damage_by_protect.size());
            for (size_t i = 0; i < data.damage_by_protect.size(); i++) {
                current.damage_by_protect[i] += data.damage_by_protect[i];
            }
        }
    }

    std::pair<Table, Summary> make_table_and_summary() const
    {
        Table table;
        table.reserve(info.size());
        for (const auto& item : info) {
            if (item.second.total_count > 0) {
                table.push_back(item);
            }
        }
        std::sort(table.begin(), table.end(),
            [](const std::pair<OpStates, Data>& a, const std::pair<OpStates, Data>& b) {
                if (a.first.wave != b.first.wave) {
                    return a.first.wave < b.first.wave;
                }
                if (a.first.garg_type != b.first.garg_type) {
                    return static_cast<int>(a.first.garg_type)
                        < static_cast<int>(b.first.garg_type);
                }
                if (a.first.result != b.first.result) {
                    return a.first.result < b.first.result;
                }
                for (size_t i = 0; i < a.first.op_states.size(); ++i) {
                    if (a.first.op_states[i] != b.first.op_states[i]) {
                        return a.first.op_states[i] < b.first.op_states[i];
                    }
                }
                return false;
            });

        Summary summary;
        for (const auto& [os, data] : table) {
            summary.waves.insert(os.wave);

            auto& wave_summary = summary.wave_summary[os.wave];
            _imp_internal::ensure_damage_size(
                wave_summary.damage_by_protect, data.damage_by_protect.size());

            wave_summary.total_count += data.total_count;

            for (size_t row = 0; row < data.total_count_by_row.size(); row++) {
                if (data.total_count_by_row[row] > 0) {
                    summary.rows.insert(static_cast<int>(row));
                }
            }

            if (os.result == ImpResult::Leaked || os.result == ImpResult::LateIntercepted) {
                wave_summary.leaked_count += data.total_count;
                for (size_t row = 0; row < data.total_count_by_row.size(); row++) {
                    wave_summary.leaked_count_by_row[row] += data.total_count_by_row[row];
                }
            } else if (os.result == ImpResult::Intercepted) {
                for (size_t i = 0; i < data.damage_by_protect.size(); i++) {
                    wave_summary.damage_by_protect[i] += data.damage_by_protect[i];
                }
            }
        }

        return {table, summary};
    }
};

std::string op_state_to_string(const OpState& op_state)
{
    switch (op_state) {
    case OpState::Blank:
        return "";
    case OpState::Hit:
        return "HIT";
    default:
        assert(false && "unreachable");
        return "";
    }
}

std::string imp_result_to_string(const ImpResult& result)
{
    switch (result) {
    case ImpResult::NotThrown:
        return "未投出";
    case ImpResult::Leaked:
        return "拦截失败";
    case ImpResult::LateIntercepted:
        return "拦截过晚";
    case ImpResult::Intercepted:
        return "拦截成功";
    default:
        assert(false && "unreachable");
        return "";
    }
}

std::string garg_type_to_string(const pvz_emulator::object::zombie_type& type)
{
    using pvz_emulator::object::zombie_type;

    if (type == zombie_type::gargantuar) {
        return "白眼";
    } else if (type == zombie_type::giga_gargantuar) {
        return "红眼";
    }

    assert(false && "unreachable");
    return "";
}
