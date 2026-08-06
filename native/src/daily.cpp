#include "isaac_seed_seeker/daily.hpp"
#include "daily_q3_ratings_j460.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace isaac_seed_seeker {
namespace {

constexpr double maximum_eden_tears = 3.501433905;

struct WeightedCandidate {
    EdenStart start;
    DailyGoodScore score;
};

std::int32_t linear_bonus(
    double value,
    double minimum,
    double maximum,
    std::int32_t maximum_bonus
) noexcept {
    if (value <= minimum) return 0;
    if (value >= maximum) return maximum_bonus;
    const auto ratio = (value - minimum) / (maximum - minimum);
    return static_cast<std::int32_t>(std::lround(ratio * maximum_bonus));
}

std::uint64_t stable_hash(std::string_view value) noexcept {
    std::uint64_t result = 14'695'981'039'346'656'037ULL;
    for (const unsigned char character : value) {
        result ^= character;
        result *= 1'099'511'628'211ULL;
    }
    return result;
}

std::uint64_t splitmix64(std::uint64_t value) noexcept {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

bool valid_iso_date(std::string_view value) noexcept {
    if (value.size() != 10 || value[4] != '-' || value[7] != '-') return false;
    const auto digit = [&](std::size_t index) { return value[index] >= '0' && value[index] <= '9'; };
    for (const auto index : {0U, 1U, 2U, 3U, 5U, 6U, 8U, 9U}) {
        if (!digit(index)) return false;
    }
    const auto number = [&](std::size_t offset, std::size_t length) {
        unsigned result = 0;
        for (std::size_t index = 0; index < length; ++index) {
            result = result * 10U + static_cast<unsigned>(value[offset + index] - '0');
        }
        return result;
    };
    using namespace std::chrono;
    const year_month_day parsed{
        year{static_cast<int>(number(0, 4))},
        month{number(5, 2)},
        day{number(8, 2)},
    };
    return parsed.ok();
}

}  // namespace

DailyGoodScore score_daily_good_v0(const EdenStart& start) noexcept {
    DailyGoodScore result;
    result.eligible = start.active_quality >= 3
        && start.passive_quality >= 3
        && start.passive_id != 149
        && start.move_speed >= 1.0
        && start.tears >= 3.0
        && start.damage >= 3.0;
    if (!result.eligible) return result;

    result.active_quality_bonus = start.active_quality >= 4 ? 40 : 0;
    result.passive_quality_bonus = start.passive_quality >= 4 ? 40 : 0;
    result.death_certificate_bonus = start.active_id == 628 ? 30 : 0;
    result.damage_bonus = linear_bonus(start.damage, 3.0, 4.5, 30);
    result.tears_bonus = linear_bonus(start.tears, 3.0, maximum_eden_tears, 30);
    result.move_speed_bonus = linear_bonus(start.move_speed, 1.0, 1.15, 15);
    result.selection_weight = 100
        + result.active_quality_bonus
        + result.passive_quality_bonus
        + result.death_certificate_bonus
        + result.damage_bonus
        + result.tears_bonus
        + result.move_speed_bonus;
    return result;
}

DailyGoodScore score_daily_good_v1(const EdenStart& start) noexcept {
    DailyGoodScore result;
    result.active_q3_rating = start.active_quality == 3
        ? generated::daily_q3_active_rating(start.active_id)
        : 0;
    result.passive_q3_rating = start.passive_quality == 3
        ? generated::daily_q3_passive_rating(start.passive_id)
        : 0;
    result.eligible = start.active_quality >= 3
        && start.passive_quality >= 3
        && (start.active_quality != 3 || result.active_q3_rating > 0)
        && (start.passive_quality != 3 || result.passive_q3_rating > 0)
        && start.passive_id != 149
        && start.move_speed >= 1.0
        && start.tears >= 3.0
        && start.damage >= 3.0;
    if (!result.eligible) return result;

    result.active_quality_bonus = start.active_quality >= 4 ? 40 : 0;
    result.passive_quality_bonus = start.passive_quality >= 4 ? 40 : 0;
    result.active_q3_rating_bonus = generated::daily_q3_rating_bonus(result.active_q3_rating);
    result.passive_q3_rating_bonus = generated::daily_q3_rating_bonus(result.passive_q3_rating);
    result.death_certificate_bonus = start.active_id == 628 ? 30 : 0;
    result.damage_bonus = linear_bonus(start.damage, 3.0, 4.5, 30);
    result.tears_bonus = linear_bonus(start.tears, 3.0, maximum_eden_tears, 30);
    result.move_speed_bonus = linear_bonus(start.move_speed, 1.0, 1.15, 15);
    result.selection_weight = 100
        + result.active_quality_bonus
        + result.passive_quality_bonus
        + result.active_q3_rating_bonus
        + result.passive_q3_rating_bonus
        + result.death_certificate_bonus
        + result.damage_bonus
        + result.tears_bonus
        + result.move_speed_bonus;
    return result;
}

namespace {

using DailyScoreFunction = DailyGoodScore (*)(const EdenStart&) noexcept;

DailyGoodResult select_daily_good_impl(
    const ProfileTables& tables,
    const DailyGoodOptions& options,
    std::string_view rules_version,
    DailyScoreFunction score_function
) {
    if (!valid_iso_date(options.date_utc8)) {
        throw std::invalid_argument("daily date must use a valid YYYY-MM-DD value");
    }
    if (options.candidates == 0) {
        throw std::invalid_argument("daily candidate count must be positive");
    }
    if (options.candidates > (std::uint64_t{1} << 32U)) {
        throw std::invalid_argument("daily candidate count cannot exceed the 32-bit seed space");
    }

    const auto started = std::chrono::steady_clock::now();
    const auto key = options.date_utc8 + "|j460-full-unlock|" + std::string(rules_version);
    const auto key_hash = stable_hash(key);
    const auto sequence_seed = splitmix64(key_hash);
    const auto sequence_step = static_cast<std::uint32_t>(sequence_seed >> 32U) | 1U;
    const auto sequence_start = static_cast<std::uint32_t>(sequence_seed);

    auto thread_count = options.threads == 0 ? std::thread::hardware_concurrency() : options.threads;
    thread_count = std::clamp(thread_count, 1U, 64U);
    thread_count = static_cast<unsigned>(std::min<std::uint64_t>(thread_count, options.candidates));

    std::vector<std::vector<WeightedCandidate>> local_candidates(thread_count);
    std::vector<std::thread> workers;
    workers.reserve(thread_count);
    for (unsigned thread_index = 0; thread_index < thread_count; ++thread_index) {
        const auto begin = options.candidates * thread_index / thread_count;
        const auto end = options.candidates * (thread_index + 1U) / thread_count;
        workers.emplace_back([&, begin, end, thread_index] {
            auto& output = local_candidates[thread_index];
            output.reserve(static_cast<std::size_t>((end - begin) / 100U + 32U));
            for (std::uint64_t index = begin; index < end; ++index) {
                const auto seed = sequence_start
                    + static_cast<std::uint32_t>(sequence_step * static_cast<std::uint32_t>(index));
                if (seed == 0) continue;
                auto start = predict_eden_start(seed, tables);
                const auto score = score_function(start);
                if (score.eligible) {
                    output.push_back(WeightedCandidate{std::move(start), score});
                }
            }
        });
    }
    for (auto& worker : workers) worker.join();

    std::uint64_t eligible = 0;
    std::uint64_t total_weight = 0;
    for (const auto& candidates : local_candidates) {
        eligible += candidates.size();
        for (const auto& candidate : candidates) {
            total_weight += static_cast<std::uint64_t>(candidate.score.selection_weight);
        }
    }
    if (eligible == 0 || total_weight == 0) {
        throw std::runtime_error("daily scan produced no eligible good seeds");
    }

    auto draw_salt = 0x6461696c792d676fULL;
    if (options.draw_variant != 0) {
        draw_salt ^= splitmix64(options.draw_variant);
    }
    const auto draw_hash = splitmix64(key_hash ^ draw_salt);
    auto target = draw_hash % total_weight;
    const WeightedCandidate* selected = nullptr;
    for (const auto& candidates : local_candidates) {
        for (const auto& candidate : candidates) {
            const auto weight = static_cast<std::uint64_t>(candidate.score.selection_weight);
            if (target < weight) {
                selected = &candidate;
                break;
            }
            target -= weight;
        }
        if (selected != nullptr) break;
    }
    if (selected == nullptr) {
        throw std::runtime_error("daily weighted draw failed");
    }

    DailyGoodResult result;
    result.date_utc8 = options.date_utc8;
    result.rules_version = std::string(rules_version);
    result.primary = selected->start;
    result.primary_score = selected->score;
    result.scanned = options.candidates;
    result.eligible = eligible;
    result.threads = thread_count;
    result.elapsed_seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started
    ).count();
    return result;
}

}  // namespace

DailyGoodResult select_daily_good_v0(
    const ProfileTables& tables,
    const DailyGoodOptions& options
) {
    return select_daily_good_impl(
        tables,
        options,
        daily_good_rules_version_v0,
        score_daily_good_v0
    );
}

DailyGoodResult select_daily_good_v1(
    const ProfileTables& tables,
    const DailyGoodOptions& options
) {
    return select_daily_good_impl(
        tables,
        options,
        daily_good_rules_version_v1,
        score_daily_good_v1
    );
}

}  // namespace isaac_seed_seeker
