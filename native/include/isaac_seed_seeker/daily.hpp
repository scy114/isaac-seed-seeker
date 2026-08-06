#pragma once

#include "isaac_seed_seeker/core.hpp"

#include <cstdint>
#include <string>

namespace isaac_seed_seeker {

inline constexpr std::string_view daily_good_rules_version = "daily-good-v0";

struct DailyGoodScore {
    bool eligible = false;
    std::int32_t selection_weight = 0;
    std::int32_t active_quality_bonus = 0;
    std::int32_t passive_quality_bonus = 0;
    std::int32_t death_certificate_bonus = 0;
    std::int32_t damage_bonus = 0;
    std::int32_t tears_bonus = 0;
    std::int32_t move_speed_bonus = 0;
};

struct DailyGoodOptions {
    std::string date_utc8;
    std::uint64_t candidates = 10'000'000;
    unsigned threads = 0;
};

struct DailyGoodResult {
    std::string date_utc8;
    std::string rules_version;
    EdenStart primary;
    DailyGoodScore primary_score;
    std::uint64_t scanned = 0;
    std::uint64_t eligible = 0;
    double elapsed_seconds = 0.0;
    unsigned threads = 0;
};

DailyGoodScore score_daily_good_v0(const EdenStart& start) noexcept;

DailyGoodResult select_daily_good_v0(
    const ProfileTables& tables,
    const DailyGoodOptions& options
);

}  // namespace isaac_seed_seeker
