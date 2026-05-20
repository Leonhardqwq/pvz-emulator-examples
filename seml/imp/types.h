#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "seml/reader/types.h"
#include "seml/types.h"
#include "world.h"

struct GargInfo {
    unique_zombie zombie;
    pvz_emulator::object::zombie_type type;
    unsigned int row; // [0, 5]
    int spawn_wave;
    int spawn_tick;
};

struct ActionInfo {
    int wave;
    int tick;
    std::string desc = "";

    std::vector<unique_plant> plants;
};

struct ImpInfo {
    unique_zombie zombie;
    pvz_emulator::object::zombie_type garg_type;
    unsigned int row; // [0, 5]
    int spawn_wave;
    int spawn_tick;
    int death_tick = -1;
    std::unordered_set<int> hit_by_ash;
    std::vector<int> damage_by_protect;
};

struct Test {
    std::vector<Setting::ProtectPos> protect_positions;
    std::vector<unique_plant> protect_plants;
    std::vector<GargInfo> garg_infos;
    std::vector<ImpInfo> imp_infos;
    std::unordered_map<int, size_t> imp_index_by_uuid;
    std::vector<ActionInfo> action_infos;
    std::vector<std::vector<unique_plant>> plants_to_be_shoveled;
    std::vector<Op> ops;
    pvz_emulator::object::zombie_type garg_type = pvz_emulator::object::zombie_type::gargantuar;
    Setting::ImpIndex imp_index;
    int end_tick = 0;
    int attribution_error_count = 0;
};
