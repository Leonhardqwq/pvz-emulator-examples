#pragma once

#include "types.h"

pvz_emulator::object::plant::explode_info operator+(
    const pvz_emulator::object::plant::explode_info& lhs,
    const pvz_emulator::object::plant::explode_info& rhs)
{
    return {lhs.from_upper + rhs.from_upper, lhs.from_same + rhs.from_same,
        lhs.from_lower + rhs.from_lower};
}

pvz_emulator::object::plant::explode_info& operator+=(
    pvz_emulator::object::plant::explode_info& lhs,
    const pvz_emulator::object::plant::explode_info& rhs)
{
    lhs.from_upper += rhs.from_upper;
    lhs.from_same += rhs.from_same;
    lhs.from_lower += rhs.from_lower;
    return lhs;
}

// Per-tick sum-of-squares of the per-run loss values, used to derive standard errors.
struct LossSumSq {
    double explode = 0.0;
    double hp = 0.0;
    double total = 0.0;
};

struct TestInfo {
    friend struct Table;

    int start_tick;
    std::vector<LossInfo> merged_loss_info;
    std::vector<LossSumSq> loss_sum_sq;

private:
    void update(const Test& test)
    {
        if (merged_loss_info.empty()) {
            merged_loss_info.resize(test.loss_infos.size());
            loss_sum_sq.resize(test.loss_infos.size());
            start_tick = test.start_tick;
        }

        assert(start_tick == test.start_tick);

        for (size_t tick = 0; tick < test.loss_infos.size(); tick++) {
            const auto& loss_info = test.loss_infos.at(tick);

            pvz_emulator::object::plant::explode_info run_explode = {0, 0, 0};
            int run_hp = 0;
            for (const auto& plant : test.protect_plants) {
                run_explode += loss_info[plant->row].explode;
                run_hp += loss_info[plant->row].hp_loss;
            }

            merged_loss_info[tick].explode += run_explode;
            merged_loss_info[tick].hp_loss += run_hp;

            double explode_damage
                = (run_explode.from_upper + run_explode.from_same + run_explode.from_lower) * 300.0;
            double hp_loss = run_hp;
            double total = explode_damage + hp_loss;
            loss_sum_sq[tick].explode += explode_damage * explode_damage;
            loss_sum_sq[tick].hp += hp_loss * hp_loss;
            loss_sum_sq[tick].total += total * total;
        }
    }

    void merge(const TestInfo& other)
    {
        assert(other.start_tick == start_tick);

        for (size_t tick = 0; tick < other.merged_loss_info.size(); tick++) {
            merged_loss_info[tick].explode += other.merged_loss_info[tick].explode;
            merged_loss_info[tick].hp_loss += other.merged_loss_info[tick].hp_loss;
            loss_sum_sq[tick].explode += other.loss_sum_sq[tick].explode;
            loss_sum_sq[tick].hp += other.loss_sum_sq[tick].hp;
            loss_sum_sq[tick].total += other.loss_sum_sq[tick].total;
        }
    }
};

struct Table {
    std::vector<TestInfo> test_infos;
    int repeat = 0;

    void update(const std::vector<Test>& tests)
    {
        if (test_infos.empty()) {
            test_infos.resize(tests.size());
        }

        for (size_t i = 0; i < tests.size(); i++) {
            test_infos[i].update(tests.at(i));
        }

        repeat++;
    }

    void merge(const Table& other)
    {
        if (test_infos.empty()) {
            test_infos = other.test_infos;
            repeat = other.repeat;
        } else {
            for (size_t i = 0; i < other.test_infos.size(); i++) {
                test_infos[i].merge(other.test_infos.at(i));
            }
            repeat += other.repeat;
        }
    }
};