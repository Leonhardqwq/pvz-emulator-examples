#include "spawn_test.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

#include "seml/refresh/spawn.h"
#include "world.h"

namespace leon_test {

GeneratedSpawn generate_full_spawn(
    std::mt19937& rng,
    pvz_emulator::object::scene_type scene)
{
    using pvz_emulator::object::zombie_type;

    const auto spawn_types = get_spawn_types(rng, scene);

    GeneratedSpawn generated;
    generated.selected_types.assign(spawn_types.begin(), spawn_types.end());
    std::sort(generated.selected_types.begin(), generated.selected_types.end(),
        [](zombie_type lhs, zombie_type rhs) {
            return static_cast<int>(lhs) < static_cast<int>(rhs);
        });

    int giga_limit = 50;
    for (std::size_t wave = 0; wave < generated.waves.size(); ++wave) {
        const bool huge = wave == 9 || wave == 19;
        auto spawn_list = get_spawn_list(rng, spawn_types, huge, true, giga_limit);

        if (huge) {
            // refresh currently emits flag then eight basics; native wave order
            // is eight basics followed by the flag. Reordering consumes no RNG.
            std::rotate(spawn_list.begin(), spawn_list.begin() + 1, spawn_list.begin() + 9);
        }

        std::copy(spawn_list.begin(), spawn_list.end(), generated.waves[wave].begin());
        giga_limit -= static_cast<int>(
            std::count(spawn_list.begin(), spawn_list.end(), zombie_type::giga_gargantuar));
    }

    return generated;
}

SpawnSamples generate_spawn_samples(
    pvz_emulator::object::scene_type scene,
    std::size_t selection_count,
    std::uint32_t seed)
{
    SpawnSamples samples;
    samples.reserve(selection_count);

    std::mt19937 sample_seed_rng(seed);
    pvz_emulator::world w(scene);

    for (std::size_t sample_index = 0; sample_index < selection_count; ++sample_index) {
        // Reset the row smoother for each card selection, then generate the
        // selected types and all 20 spawn lists with a reproducible seed.
        w.scene.reset(scene);
        w.scene.rng.seed(sample_seed_rng());

        auto generated = generate_full_spawn(w.scene.rng, scene);

        SpawnSample sample;
        sample.selected_types = std::move(generated.selected_types);

        for (std::size_t wave = 0; wave < sample.waves.size(); ++wave) {
            w.scene.spawn.wave = static_cast<unsigned int>(wave);

            for (std::size_t slot = 0; slot < sample.waves[wave].size(); ++slot) {
                const auto type = generated.waves[wave][slot];
                const auto& zombie = w.zombie_factory.create(type);
                if (zombie.row >= w.scene.rows) {
                    throw std::logic_error("native spawn selected an invalid row");
                }
                sample.waves[wave][slot] = {zombie.type, zombie.row};
            }

            // Zombie objects are no longer needed. Clearing only the object pool
            // preserves spawn.row_random, so smoothing continues across waves.
            w.scene.zombies.clear();
        }

        samples.push_back(std::move(sample));
    }

    return samples;
}

} // namespace leon_test
