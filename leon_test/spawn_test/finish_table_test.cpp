#include "spawn_test.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <random>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <vector>

namespace {

using pvz_emulator::object::scene_type;
using pvz_emulator::object::zombie_type;

// 修改这里配置测试。
constexpr std::size_t SAMPLE_COUNT = 1000000;
constexpr std::size_t BATCH_SIZE = 1000;
constexpr unsigned int THREAD_COUNT = 12; // 0 uses hardware concurrency
constexpr std::uint32_t RANDOM_SEED = 20260714;
constexpr const char* OUTPUT_FILE
    = "leon_test/spawn_test/dest/finish_table_raw.csv";
constexpr std::array<int, 2> WAVES = {8, 18}; // w9, w19

struct Scene {
    const char* name;
    scene_type type;
};

constexpr std::array<Scene, 2> SCENES = {{
    {"DE", scene_type::day},
    {"NE", scene_type::night},
}};

struct HistogramKey {
    bool dancer_selected = false;
    std::array<unsigned int, 5> giga_by_row = {};

    bool operator<(const HistogramKey& other) const
    {
        return std::tie(dancer_selected, giga_by_row)
            < std::tie(other.dancer_selected, other.giga_by_row);
    }
};

using Histogram = std::map<HistogramKey, std::uint64_t>;
using TestData
    = std::array<std::array<Histogram, WAVES.size()>, SCENES.size()>;

bool has_type(const leon_test::SpawnSample& sample, zombie_type type)
{
    return std::find(sample.selected_types.begin(), sample.selected_types.end(), type)
        != sample.selected_types.end();
}

void add_sample(
    const leon_test::SpawnSample& sample, int wave, Histogram& histogram)
{
    HistogramKey key;
    key.dancer_selected = has_type(sample, zombie_type::dancing);

    for (const auto& zombie : sample.waves[wave]) {
        if (zombie.type != zombie_type::giga_gargantuar) {
            continue;
        }
        if (zombie.row >= key.giga_by_row.size()) {
            throw std::logic_error("finish table only supports five-row scenes");
        }
        ++key.giga_by_row[zombie.row];
    }
    ++histogram[key];
}

void merge(TestData& target, const TestData& source)
{
    for (std::size_t scene = 0; scene < SCENES.size(); ++scene) {
        for (std::size_t wave = 0; wave < WAVES.size(); ++wave) {
            for (const auto& [key, frequency] : source[scene][wave]) {
                target[scene][wave][key] += frequency;
            }
        }
    }
}

} // namespace

int main()
{
    try {
        static_assert(SAMPLE_COUNT > 0);
        static_assert(BATCH_SIZE > 0);

        const auto start = std::chrono::steady_clock::now();
        const unsigned int requested_threads
            = THREAD_COUNT == 0 ? std::thread::hardware_concurrency() : THREAD_COUNT;
        const std::size_t worker_count
            = std::min<std::size_t>(SAMPLE_COUNT, std::max(1u, requested_threads));
        const std::size_t total_work = SAMPLE_COUNT * SCENES.size();

        std::cout << "generating " << total_work << " selections with "
                  << worker_count << " threads\n";

        std::vector<TestData> worker_data(worker_count);
        std::vector<std::exception_ptr> worker_errors(worker_count);
        std::vector<std::thread> workers;
        workers.reserve(worker_count);

        std::atomic<std::size_t> completed = 0;
        unsigned int next_progress = 10;
        std::mutex progress_mutex;
        auto report_progress = [&](std::size_t count) {
            const std::size_t done
                = completed.fetch_add(count, std::memory_order_relaxed) + count;
            std::lock_guard<std::mutex> lock(progress_mutex);
            while (next_progress <= 100
                && done * 100 >= total_work * next_progress) {
                std::cout << "progress " << next_progress << "%\n";
                next_progress += 10;
            }
        };

        for (std::size_t worker = 0; worker < worker_count; ++worker) {
            workers.emplace_back([&, worker]() {
                try {
                    const std::size_t worker_samples = SAMPLE_COUNT / worker_count
                        + (worker < SAMPLE_COUNT % worker_count);
                    for (std::size_t scene_index = 0;
                         scene_index < SCENES.size(); ++scene_index) {
                        const auto& scene = SCENES[scene_index];
                        const auto seed_offset = static_cast<std::uint32_t>(
                            scene_index * worker_count + worker);
                        std::mt19937 batch_seeds(RANDOM_SEED + seed_offset);

                        for (std::size_t done = 0; done < worker_samples;) {
                            const std::size_t count
                                = std::min(BATCH_SIZE, worker_samples - done);
                            const auto samples = leon_test::generate_spawn_samples(
                                scene.type, count, batch_seeds());
                            for (const auto& sample : samples) {
                                if (!has_type(sample, zombie_type::giga_gargantuar)) {
                                    continue;
                                }
                                for (std::size_t wave = 0; wave < WAVES.size(); ++wave) {
                                    add_sample(sample, WAVES[wave],
                                        worker_data[worker][scene_index][wave]);
                                }
                            }
                            done += count;
                            report_progress(count);
                        }
                    }
                } catch (...) {
                    worker_errors[worker] = std::current_exception();
                }
            });
        }

        for (auto& worker : workers) {
            worker.join();
        }

        TestData data;
        for (std::size_t worker = 0; worker < worker_count; ++worker) {
            if (worker_errors[worker]) {
                std::rethrow_exception(worker_errors[worker]);
            }
            merge(data, worker_data[worker]);
        }

        std::ofstream file(OUTPUT_FILE);
        if (!file) {
            throw std::runtime_error("cannot open output file");
        }
        file << "scene,wave,dancer_selected,giga_row_1,giga_row_2,giga_row_3,"
                "giga_row_4,giga_row_5,frequency\n";
        for (std::size_t scene = 0; scene < SCENES.size(); ++scene) {
            if (data[scene][0].empty()) {
                throw std::runtime_error("no giga selections; increase sample count");
            }
            for (std::size_t wave = 0; wave < WAVES.size(); ++wave) {
                for (const auto& [key, frequency] : data[scene][wave]) {
                    file << SCENES[scene].name << ',' << WAVES[wave] + 1 << ','
                         << key.dancer_selected;
                    for (const auto count : key.giga_by_row) {
                        file << ',' << count;
                    }
                    file << ',' << frequency << '\n';
                }
            }
        }
        file.close();
        if (!file) {
            throw std::runtime_error("failed to write output file");
        }

        const double elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - start).count();
        std::cout << "saved " << OUTPUT_FILE << '\n'
                  << "elapsed " << std::fixed << std::setprecision(2)
                  << elapsed << " s\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
