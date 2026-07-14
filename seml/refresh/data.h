#pragma once

#include <cmath>

#include "types.h"

namespace _refresh_stats {
[[nodiscard]] inline double standard_error(double sum, double sum_sq, long long count)
{
    if (count < 2) {
        return 0.0;
    }
    double n = static_cast<double>(count);
    double mean = sum / n;
    double variance = (sum_sq - n * mean * mean) / (n - 1.0);
    return variance > 0.0 ? std::sqrt(variance / n) : 0.0;
}

struct AccidentStats {
    double sum = 0.0;
    double sum_sq = 0.0;
    long long count = 0;
};
} // namespace _refresh_stats

struct TestInfo {
    friend struct TestInfos;

    std::unordered_map<ZombieTypes, _refresh_stats::AccidentStats, ZombieTypesHash>
        merged_accident_rates;
    std::vector<LogRow> logs;

private:
    void update(const Test& test)
    {
        for (const auto& [zombie_types, accident_rates] : test.accident_rates) {
            auto& merged_accident_rate = merged_accident_rates[zombie_types];
            for (const auto& accident_rate : accident_rates) {
                merged_accident_rate.sum += accident_rate;
                merged_accident_rate.sum_sq += static_cast<double>(accident_rate) * accident_rate;
                merged_accident_rate.count++;
            }
        }
        logs.push_back(test.log);
    }

    void merge(const TestInfo& other)
    {
        for (const auto& [zombie_types, accident_rates] : other.merged_accident_rates) {
            auto& merged_accident_rate = merged_accident_rates[zombie_types];
            merged_accident_rate.sum += accident_rates.sum;
            merged_accident_rate.sum_sq += accident_rates.sum_sq;
            merged_accident_rate.count += accident_rates.count;
        }
        logs.insert(logs.end(), other.logs.begin(), other.logs.end());
    }
};

struct Table {
    struct Row {
        ZombieTypes types;
        double mean;
        double se;
    };

    struct Col {
        double average_accident_rate;
        double average_accident_rate_se;
        std::vector<Row> rows;
    };

    std::vector<Col> cols;
    size_t max_row_count;
};

struct TestInfos {
    std::vector<TestInfo> test_infos;

    void update(const std::vector<Test>& tests)
    {
        if (test_infos.empty()) {
            test_infos.resize(tests.size());
        }

        for (size_t i = 0; i < tests.size(); i++) {
            test_infos[i].update(tests.at(i));
        }
    }

    void merge(const TestInfos& other)
    {
        if (test_infos.empty()) {
            test_infos = other.test_infos;
        } else {
            for (size_t i = 0; i < test_infos.size(); i++) {
                test_infos[i].merge(other.test_infos.at(i));
            }
        }
    }

    Table make_table() const
    {
        Table table = {};

        for (const auto& test_info : test_infos) {
            Table::Col col;
            double total_sum = 0.0;
            double total_sum_sq = 0.0;
            long long total_count = 0;

            for (const auto& [zombie_types, stats] : test_info.merged_accident_rates) {
                double mean = stats.count > 0 ? stats.sum / static_cast<double>(stats.count) : 0.0;
                double se = _refresh_stats::standard_error(stats.sum, stats.sum_sq, stats.count);
                col.rows.push_back({zombie_types, mean, se});
                total_sum += stats.sum;
                total_sum_sq += stats.sum_sq;
                total_count += stats.count;
            }

            std::sort(col.rows.begin(), col.rows.end(),
                [](const Table::Row& a, const Table::Row& b) { return a.mean > b.mean; });

            col.average_accident_rate
                = total_count > 0 ? total_sum / static_cast<double>(total_count) : 0.0;
            col.average_accident_rate_se
                = _refresh_stats::standard_error(total_sum, total_sum_sq, total_count);
            table.cols.push_back(col);
            table.max_row_count = std::max(table.max_row_count, col.rows.size());
        }

        return table;
    }
};