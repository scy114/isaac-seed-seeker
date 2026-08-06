#pragma once

#include "isaac_seed_seeker/core.hpp"

#include <cstdint>
#include <string>

namespace isaac_seed_seeker {

inline constexpr std::string_view daily_good_rules_version_v0 = "daily-good-v0";
inline constexpr std::string_view daily_good_rules_version_v1 = "daily-good-v1";
inline constexpr std::string_view daily_good_rules_version = daily_good_rules_version_v1;
inline constexpr std::string_view daily_bad_rules_version_v0 = "daily-bad-v0";
inline constexpr std::string_view daily_bad_rules_version = daily_bad_rules_version_v0;

struct DailyGoodScore {
    bool eligible = false;
    std::int32_t selection_weight = 0;
    std::int32_t active_quality_bonus = 0;
    std::int32_t passive_quality_bonus = 0;
    std::uint8_t active_q3_rating = 0;
    std::uint8_t passive_q3_rating = 0;
    std::int32_t active_q3_rating_bonus = 0;
    std::int32_t passive_q3_rating_bonus = 0;
    std::int32_t death_certificate_bonus = 0;
    std::int32_t damage_bonus = 0;
    std::int32_t tears_bonus = 0;
    std::int32_t move_speed_bonus = 0;
};

struct DailyGoodOptions {
    std::string date_utc8;
    std::uint64_t candidates = 10'000'000;
    unsigned threads = 0;
    std::uint32_t draw_variant = 0;
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

using DailyBadScore = DailyGoodScore;
using DailyBadResult = DailyGoodResult;

DailyGoodScore score_daily_good_v0(const EdenStart& start) noexcept;
DailyGoodScore score_daily_good_v1(const EdenStart& start) noexcept;
DailyBadScore score_daily_bad_v0(const EdenStart& start) noexcept;

DailyGoodResult select_daily_good_v0(
    const ProfileTables& tables,
    const DailyGoodOptions& options
);

DailyGoodResult select_daily_good_v1(
    const ProfileTables& tables,
    const DailyGoodOptions& options
);

DailyBadResult select_daily_bad_v0(
    const ProfileTables& tables,
    const DailyGoodOptions& options
);

}  // namespace isaac_seed_seeker
