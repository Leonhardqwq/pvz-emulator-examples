#include "spawn_test.h"

#include <cstddef>
#include <cstdint>
#include <iostream>

namespace {

using pvz_emulator::object::scene_type;

constexpr auto TEST_SCENE = scene_type::pool;
constexpr std::size_t SELECTION_COUNT = 1000;
constexpr std::uint32_t RANDOM_SEED = 1;

} // namespace

int main()
{
    const auto samples =
        leon_test::generate_spawn_samples(TEST_SCENE, SELECTION_COUNT, RANDOM_SEED);

    if (samples.size() != SELECTION_COUNT) {
        std::cerr << "unexpected sample count\n";
        return 1;
    }

    std::cout << "generated " << samples.size()
              << " native spawn samples (20 waves x 50 zombies)\n";
    return 0;
}
