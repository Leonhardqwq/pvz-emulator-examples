#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

#include "object/scene.h"
#include "object/zombie.h"

namespace leon_test {

using ZombieType = pvz_emulator::object::zombie_type;
using ZombieList = std::array<ZombieType, 50>;
using FullSpawnList = std::array<ZombieList, 20>;

struct GeneratedSpawn {
    std::vector<ZombieType> selected_types;
    FullSpawnList waves;
};

struct ZombieSpawn {
    ZombieType type;
    unsigned int row;
};

using WaveSpawn = std::array<ZombieSpawn, 50>;

struct SpawnSample {
    std::vector<ZombieType> selected_types;
    std::array<WaveSpawn, 20> waves;
};

using SpawnSamples = std::vector<SpawnSample>;

GeneratedSpawn generate_full_spawn(
    std::mt19937& rng,
    pvz_emulator::object::scene_type scene);

SpawnSamples generate_spawn_samples(
    pvz_emulator::object::scene_type scene,
    std::size_t selection_count,
    std::uint32_t seed);

} // namespace leon_test
