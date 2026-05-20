#pragma once

#include <algorithm>
#include <array>
#include <optional>

#include "common/pe.h"
#include "constants/constants.h"
#include "seml/operation.h"
#include "system/util.h"
#include "system/zombie/zombie.h"
#include "types.h"

namespace _imp_internal {

using scene_type = pvz_emulator::object::scene_type;
using plant_type = pvz_emulator::object::plant_type;
using zombie = pvz_emulator::object::zombie;
using zombie_action = pvz_emulator::object::zombie_action;
using zombie_attack_type = pvz_emulator::system::zombie_attack_type;
using zombie_reanim_name = pvz_emulator::object::zombie_reanim_name;
using zombie_status = pvz_emulator::object::zombie_status;
using zombie_type = pvz_emulator::object::zombie_type;

const int PLANT_INIT_HP = 2147483647 / 2;
const int IMP_LOW_INDEX_RESERVE_COUNT = 1023 / 3;

bool needs_low_index_slots(const Setting::ImpIndex& imp_index)
{
    using imp_index_mode_type = pvz_emulator::object::imp_index_mode_type;

    return imp_index.mode == imp_index_mode_type::low
        || (imp_index.mode == imp_index_mode_type::ratio && imp_index.high_ratio < 1.0f);
}

void reserve_low_index_imp_slots(pvz_emulator::world& w)
{
    for (int i = 0; i < IMP_LOW_INDEX_RESERVE_COUNT; i++) {
        auto& z = w.scene.zombies.alloc_lowest();
        z.type = zombie_type::imp;
        z.is_dead = true;
        z.is_not_dying = false;
    }
}

bool is_recorded_ash_card(plant_type type)
{
    return type == plant_type::jalapeno || type == plant_type::cherry_bomb
        || type == plant_type::squash || type == plant_type::doomshroom;
}

void insert_setup(Test& test, int tick, const std::vector<Setting::ProtectPos>& protect_positions)
{
    auto f = [&test, protect_positions](pvz_emulator::world& w) {
        for (const auto& pos : protect_positions) {
            auto type = pos.is_cob() ? plant_type::cob_cannon : plant_type::umbrella_leaf;
            auto col = pos.is_cob() ? pos.col - 2 : pos.col - 1;

            auto& p = w.plant_factory.create(type, pos.row - 1, col);
            p.ignore_garg_smash = true;
            p.hp = p.max_hp = PLANT_INIT_HP;
            test.protect_plants.push_back({&p, p.uuid});
        }
    };
    test.ops.push_back({tick, f});
}

void insert_spawn(Test& test, int tick, int wave, zombie_type garg_type)
{
    auto f = [&test, tick, wave, garg_type](pvz_emulator::world& w) {
        w.scene.spawn.wave = wave;
        if (needs_low_index_slots(test.imp_index)) {
            reserve_low_index_imp_slots(w);
        }

        for (int i = 0; i < 5; i++) {
            auto& z = w.zombie_factory.create(garg_type);
            test.garg_infos.push_back({{&z, z.uuid}, garg_type, z.row, wave, tick});
        }
    };
    test.ops.push_back({tick, f});
}

void insert_ice(Test& test, int tick)
{
    auto f = [](pvz_emulator::world& w) {
        w.plant_factory.create(plant_type::iceshroom, 0, 0, plant_type::none, true);
    };
    test.ops.push_back({tick, f});
}

void insert_cob(Test& test, int tick, int wave, const Cob* cob, const scene_type& scene_type)
{
    test.action_infos.push_back({wave, tick, cob->desc(), {}});
    auto idx = test.action_infos.size() - 1;
    auto cob_col = cob->cob_col;

    for (const auto& pos : cob->positions) {
        auto f = [&test, idx, pos, cob_col](pvz_emulator::world& w) {
            test.action_infos[idx].plants.push_back(
                {nullptr, launch_cob(w, pos.row, pos.col, cob_col)});
        };

        test.ops.push_back({tick - get_cob_fly_time(scene_type, pos.row, pos.col, cob_col), f});
    }
}

void insert_fixed_card(Test& test, int tick, int wave, const FixedCard* fixed_card)
{
    auto type = fixed_card->plant_type;
    auto pos = fixed_card->position;

    if (is_recorded_ash_card(type)) {
        test.action_infos.push_back({wave, tick, fixed_card->desc(), {}});
        auto idx = test.action_infos.size() - 1;

        auto f = [&test, idx, type, pos](pvz_emulator::world& w) {
            auto& p = w.plant_factory.create(type, pos.row - 1, pos.col - 1);
            test.action_infos[idx].plants.push_back({&p, p.uuid});
        };
        test.ops.push_back({get_fixed_card_op_tick(fixed_card, tick), f});

        if (fixed_card->shovel_time != -1) {
            auto f = [&test, idx](pvz_emulator::world& w) {
                for (const auto& plant : test.action_infos[idx].plants) {
                    if (plant.is_valid()) {
                        w.plant_factory.destroy(*plant.ptr);
                    }
                }
            };
            test.ops.push_back({tick - fixed_card->time + fixed_card->shovel_time, f});
        }
    } else {
        test.plants_to_be_shoveled.push_back({});
        auto idx = test.plants_to_be_shoveled.size() - 1;

        auto f = [&test, idx, type, pos](pvz_emulator::world& w) {
            auto& p = w.plant_factory.create(type, pos.row - 1, pos.col - 1);
            test.plants_to_be_shoveled[idx].push_back({&p, p.uuid});
        };
        test.ops.push_back({get_fixed_card_op_tick(fixed_card, tick), f});

        if (fixed_card->shovel_time != -1) {
            auto f = [&test, idx](pvz_emulator::world& w) {
                for (const auto& plant : test.plants_to_be_shoveled[idx]) {
                    if (plant.is_valid()) {
                        w.plant_factory.destroy(*plant.ptr);
                    }
                }
            };
            test.ops.push_back({tick - fixed_card->time + fixed_card->shovel_time, f});
        }
    }
}

void insert_smart_card(Test& test, int tick, int wave, const SmartCard* smart_card)
{
    auto type = smart_card->plant_type;

    test.action_infos.push_back({wave, tick, smart_card->desc(), {}});

    auto idx = test.action_infos.size() - 1;
    auto positions = smart_card->positions;
    int max_card_zombie_row_diff = get_smart_card_max_card_zombie_row_diff(smart_card);

    auto f = [&test, idx, type, positions, max_card_zombie_row_diff](pvz_emulator::world& w) {
        auto chosen = choose_by_num(w, positions, 1, {},
            {zombie_type::giga_gargantuar, zombie_type::gargantuar}, max_card_zombie_row_diff);
        assert(chosen.size() == 1);

        auto pos = positions[chosen[0]];
        auto& p = w.plant_factory.create(type, pos.row - 1, pos.col - 1);
        test.action_infos[idx].plants.push_back({&p, p.uuid});
    };
    test.ops.push_back({get_smart_card_op_tick(smart_card, tick), f});
}

void insert_fixed_fodder(Test& test, int tick, const FixedFodder* fodder)
{
    test.plants_to_be_shoveled.push_back({});
    auto idx = test.plants_to_be_shoveled.size() - 1;
    auto fodders = fodder->fodders;
    auto positions = fodder->positions;

    auto f = [&test, idx, fodders, positions](pvz_emulator::world& w) {
        assert(fodders.size() == positions.size());

        for (size_t i = 0; i < fodders.size(); i++) {
            auto& p = plant_fodder(w, fodders[i], positions[i]);
            test.plants_to_be_shoveled[idx].push_back({&p, p.uuid});
        }
    };
    test.ops.push_back({tick, f});

    if (fodder->shovel_time != -1) {
        auto f = [&test, idx](pvz_emulator::world& w) {
            for (const auto& plant : test.plants_to_be_shoveled[idx]) {
                if (plant.is_valid()) {
                    w.plant_factory.destroy(*plant.ptr);
                }
            }
        };
        test.ops.push_back({tick - fodder->time + fodder->shovel_time, f});
    }
}

void insert_smart_fodder(Test& test, int tick, const SmartFodder* fodder)
{
    test.plants_to_be_shoveled.push_back({});
    auto idx = test.plants_to_be_shoveled.size() - 1;
    auto symbol = fodder->symbol;
    auto fodders = fodder->fodders;
    auto positions = fodder->positions;
    auto choose = fodder->choose;
    auto waves = fodder->waves;

    auto f = [&test, idx, symbol, fodders, positions, choose, waves](pvz_emulator::world& w) {
        assert(fodders.size() == positions.size());

        std::vector<size_t> chosen;
        if (symbol == "C") {
            chosen.reserve(positions.size());
            for (size_t i = 0; i < positions.size(); i++) {
                chosen.push_back(i);
            }
        } else if (symbol == "C_POS") {
            chosen = choose_by_giga_pos(w, positions, choose, waves);
        } else if (symbol == "C_NUM") {
            chosen = choose_by_num(w, positions, choose, waves,
                {zombie_type::ladder, zombie_type::jack_in_the_box}, 0);
        } else {
            assert(false && "unreachable");
        }

        for (auto i : chosen) {
            auto& p = plant_fodder(w, fodders[i], positions[i]);
            test.plants_to_be_shoveled[idx].push_back({&p, p.uuid});
        }
    };
    test.ops.push_back({tick, f});

    if (fodder->shovel_time != -1) {
        auto f = [&test, idx](pvz_emulator::world& w) {
            for (const auto& plant : test.plants_to_be_shoveled[idx]) {
                if (plant.is_valid()) {
                    w.plant_factory.destroy(*plant.ptr);
                }
            }
        };
        test.ops.push_back({tick - fodder->time + fodder->shovel_time, f});
    }
}

void sync_one_imp(ImpInfo& imp_info, int tick)
{
    if (!imp_info.zombie.is_valid()) {
        return;
    }

    auto& z = *imp_info.zombie.ptr;
    for (int i = 0; i < z.hit_by_ash.size; i++) {
        imp_info.hit_by_ash.insert(z.hit_by_ash.arr[i]);
    }

    if (imp_info.death_tick == -1
        && (z.is_dead || z.has_death_status() || !z.is_not_dying)) {
        imp_info.death_tick = tick;
    }
}

void sync_imps(Test& test, pvz_emulator::world& w, int tick)
{
    for (auto& imp_info : test.imp_infos) {
        if (imp_info.death_tick == -1) {
            sync_one_imp(imp_info, tick);
        }
    }

    for (auto& z : w.scene.zombies) {
        if (z.type != zombie_type::imp || test.imp_index_by_uuid.count(z.uuid)) {
            continue;
        }

        ImpInfo imp_info;
        imp_info.zombie = {&z, z.uuid};
        imp_info.garg_type = test.garg_type;
        imp_info.row = z.row;
        imp_info.spawn_wave = static_cast<int>(z.spawn_wave);
        imp_info.spawn_tick = tick;
        imp_info.damage_by_protect.resize(test.protect_plants.size());

        for (int i = 0; i < z.hit_by_ash.size; i++) {
            imp_info.hit_by_ash.insert(z.hit_by_ash.arr[i]);
        }

        test.imp_index_by_uuid[z.uuid] = test.imp_infos.size();
        test.imp_infos.push_back(std::move(imp_info));
    }
}

std::vector<int> get_protect_hp(const Test& test)
{
    std::vector<int> hp;
    hp.reserve(test.protect_plants.size());
    for (const auto& plant : test.protect_plants) {
        hp.push_back(plant.is_valid() ? plant.ptr->hp : 0);
    }
    return hp;
}

std::optional<size_t> get_protect_index(const Test& test, const pvz_emulator::object::plant* target)
{
    if (target == nullptr) {
        return std::nullopt;
    }

    for (size_t i = 0; i < test.protect_plants.size(); i++) {
        if (test.protect_plants[i].uuid == target->uuid) {
            return i;
        }
    }
    return std::nullopt;
}

void update_imp_status_for_prediction(pvz_emulator::world& w, zombie& z)
{
    if (z.status == zombie_status::imp_flying) {
        z.d2y -= 0.05000000074505806f;
        z.dy += z.d2y;

        z.x -= z.dx;

        auto new_y = pvz_emulator::system::zombie_init_y(w.scene.type, z, z.row);
        auto new_dy = z.dy + new_y - z.y;

        z.y = new_y;
        z.dy = new_dy;

        if (z.dy <= 0) {
            z.dy = 0;
            z.status = zombie_status::imp_landing;
        }
    } else if (z.status == zombie_status::imp_landing && z.reanim.n_repeated > 0) {
        z.status = zombie_status::walking;
        w.zombie.reanim.update_status(z);
    }
}

void update_x_for_prediction(pvz_emulator::world& w, zombie& z)
{
    if (pvz_emulator::system::is_not_movable(w.scene, z)) {
        return;
    }

    double dx;
    if (z.has_reanim(zombie_reanim_name::_ground)) {
        dx = z.get_dx_from_ground();
    } else {
        dx = pvz_emulator::system::is_slowed(w.scene, z) ? z.dx * 0.4000000059604645 : z.dx;
    }

    if (z.is_walk_right() || z.status == zombie_status::dancing_moonwalk) {
        z.x += static_cast<float>(dx);
    } else {
        z.x -= static_cast<float>(dx);
    }
}

std::optional<size_t> predict_imp_bite(Test& test, pvz_emulator::world& w, const ImpInfo& imp_info)
{
    if (!imp_info.zombie.is_valid() || imp_info.death_tick != -1) {
        return std::nullopt;
    }

    auto z = *imp_info.zombie.ptr;
    if (z.type != zombie_type::imp || !z.is_not_dying || z.is_dead) {
        return std::nullopt;
    }

    z.time_since_spawn++;

    if (z.countdown.action > 0 && z.countdown.freeze == 0 && z.countdown.butter == 0) {
        z.countdown.action--;
    }
    if (z.countdown.freeze > 0) {
        z.countdown.freeze--;
    }
    if (z.countdown.slow > 0) {
        z.countdown.slow--;
    }
    if (z.countdown.butter > 0) {
        z.countdown.butter--;
    }

    if (z.countdown.freeze > 0 || z.countdown.butter > 0) {
        return std::nullopt;
    }

    update_imp_status_for_prediction(w, z);
    update_x_for_prediction(w, z);

    if (z.status == zombie_status::imp_flying || z.status == zombie_status::imp_landing
        || z.action == zombie_action::fall_from_sky || z.action == zombie_action::climbing_ladder
        || z.action == zombie_action::entering_pool || z.action == zombie_action::leaving_pool
        || z.action == zombie_action::falling || pvz_emulator::system::is_target_of_kelp(w.scene, z)
        || z.is_hypno) {
        return std::nullopt;
    }

    int op = z.countdown.slow > 0 ? 8 : 4;
    if (z.time_since_spawn % static_cast<unsigned int>(op) != 0) {
        return std::nullopt;
    }

    pvz_emulator::system::zombie_base zombie_base(w.scene);
    auto target = zombie_base.find_target(z, zombie_attack_type::smash_or_eat);
    return get_protect_index(test, target);
}

std::vector<int> record_imp_damage(Test& test, pvz_emulator::world& w)
{
    std::vector<int> predicted_damage(test.protect_plants.size());

    for (auto& imp_info : test.imp_infos) {
        auto protect_index = predict_imp_bite(test, w, imp_info);
        if (protect_index.has_value()) {
            imp_info.damage_by_protect[*protect_index] += 4;
            predicted_damage[*protect_index] += 4;
        }
    }

    return predicted_damage;
}

void validate_imp_damage(Test& test, const std::vector<int>& hp_before,
    const std::vector<int>& predicted_damage)
{
    for (size_t i = 0; i < test.protect_plants.size(); i++) {
        if (!test.protect_plants[i].is_valid()) {
            continue;
        }

        int hp_loss = hp_before[i] - test.protect_plants[i].ptr->hp;
        if (hp_loss != predicted_damage[i]) {
            test.attribution_error_count++;
        }
    }
}

void update_one_tick(Test& test, pvz_emulator::world& w, int tick)
{
    if (w.scene.is_game_over) {
        return;
    }

    w.scene.zombie_dancing_clock += 1;

    w.scene.plants.shrink_to_fit();
    w.scene.zombies.shrink_to_fit();
    w.scene.projectiles.shrink_to_fit();
    w.scene.griditems.shrink_to_fit();

    w.griditem.update();
    w.plant_system.update();

    sync_imps(test, w, tick);
    auto hp_before = get_protect_hp(test);
    auto predicted_damage = record_imp_damage(test, w);

    w.zombie.update();
    sync_imps(test, w, tick);

    validate_imp_damage(test, hp_before, predicted_damage);

    w.projectile.update();
    sync_imps(test, w, tick);

    for (auto& card : w.scene.cards) {
        if (card.cold_down > 0) {
            --card.cold_down;
        }
    }

    w.sun.update();
}

} // namespace _imp_internal

void load_config(const Config& config, pvz_emulator::object::zombie_type garg_type, Test& test)
{
    using namespace _imp_internal;

    test = {};
    test.garg_type = garg_type;
    test.imp_index = config.setting.imp_index;
    test.protect_positions = config.setting.protect_positions;
    test.protect_plants.reserve(config.setting.protect_positions.size());
    test.garg_infos.reserve(config.waves.size() * 5);

    int base_tick = 0;

    insert_setup(test, base_tick, config.setting.protect_positions);
    for (size_t i = 0; i < config.waves.size(); i++) {
        const int wave_num = static_cast<int>(i + 1);
        const auto& wave = config.waves[i];

        insert_spawn(test, base_tick, wave_num, garg_type);

        for (const auto& ice_time : wave.ice_times) {
            insert_ice(test, base_tick + ice_time - 99);
        }

        for (const auto& action : wave.actions) {
            if (auto a = dynamic_cast<const Cob*>(action.get())) {
                insert_cob(test, base_tick + a->time, wave_num, a, config.setting.scene_type);
            } else if (auto a = dynamic_cast<const FixedCard*>(action.get())) {
                insert_fixed_card(test, base_tick + a->time, wave_num, a);
            } else if (auto a = dynamic_cast<const SmartCard*>(action.get())) {
                insert_smart_card(test, base_tick + a->time, wave_num, a);
            } else if (auto a = dynamic_cast<const FixedFodder*>(action.get())) {
                insert_fixed_fodder(test, base_tick + a->time, a);
            } else if (auto a = dynamic_cast<const SmartFodder*>(action.get())) {
                insert_smart_fodder(test, base_tick + a->time, a);
            } else {
                assert(false && "unreachable");
            }
        }

        base_tick += wave.wave_length;
    }

    test.end_tick = base_tick;

    test.ops.erase(std::remove_if(test.ops.begin(), test.ops.end(),
                       [base_tick](const Op& op) { return op.tick > base_tick; }),
        test.ops.end());
    std::stable_sort(
        test.ops.begin(), test.ops.end(), [](const Op& a, const Op& b) { return a.tick < b.tick; });
}

void update_one_tick(Test& test, pvz_emulator::world& w, int tick)
{
    _imp_internal::update_one_tick(test, w, tick);
}
