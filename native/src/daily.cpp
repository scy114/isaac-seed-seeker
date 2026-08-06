#include "isaac_seed_seeker/daily.hpp"
#include "daily_q3_ratings_j460.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <numeric>
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

std::uint32_t inverse_odd_u32(std::uint32_t value) noexcept {
    auto inverse = value;
    inverse *= 2U - value * inverse;
    inverse *= 2U - value * inverse;
    inverse *= 2U - value * inverse;
    inverse *= 2U - value * inverse;
    inverse *= 2U - value * inverse;
    return inverse;
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

DailyBadScore score_daily_bad_v0(const EdenStart& start) noexcept {
    DailyBadScore result;
    result.eligible = start.active_quality + start.passive_quality <= 1
        && start.move_speed < 1.0
        && start.tears < 3.0
        && start.damage < 3.0;
    if (result.eligible) result.selection_weight = 1;
    return result;
}

DailyBadScore score_daily_bad_v1(const EdenStart& start) noexcept {
    DailyBadScore result;
    const auto excluded_item = [](std::uint16_t item_id) noexcept {
        return item_id == 19 || item_id == 59 || item_id == 137 || item_id == 161;
    };
    result.eligible = start.active_quality <= 1
        && start.passive_quality == 0
        && !excluded_item(start.active_id)
        && !excluded_item(start.passive_id)
        && start.move_speed < 1.0
        && start.tears < 3.0
        && start.damage < 3.0;
    if (result.eligible) {
        result.selection_weight = start.active_id == 721 || start.passive_id == 721 ? 7 : 10;
    }
    return result;
}

DailyBadScore score_daily_bad_v2(const EdenStart& start) noexcept {
    static constexpr std::array<std::uint16_t, 7> extra_active_ids{
        33, 38, 45, 298, 522, 639, 729,
    };
    static constexpr std::array<std::uint16_t, 5> extra_passive_ids{
        149, 222, 329, 529, 561,
    };
    const auto excluded_item = [](std::uint16_t item_id) noexcept {
        return item_id == 19 || item_id == 59 || item_id == 137 || item_id == 161;
    };
    const auto active_allowed = start.active_quality == 0
        || std::find(extra_active_ids.begin(), extra_active_ids.end(), start.active_id)
            != extra_active_ids.end();
    const auto passive_allowed = start.passive_quality == 0
        || std::find(extra_passive_ids.begin(), extra_passive_ids.end(), start.passive_id)
            != extra_passive_ids.end();
    DailyBadScore result;
    result.eligible = active_allowed
        && passive_allowed
        && !excluded_item(start.active_id)
        && !excluded_item(start.passive_id)
        && start.move_speed < 1.0
        && start.tears < 3.0
        && start.damage < 3.0;
    if (result.eligible) {
        result.selection_weight = start.active_id == 721 || start.passive_id == 721 ? 7 : 10;
    }
    return result;
}

DailyBadScore score_daily_bad_v3(const EdenStart& start) noexcept {
    auto result = score_daily_bad_v2(start);
    result.eligible = result.eligible && start.bombs == 0;
    if (!result.eligible) result.selection_weight = 0;
    return result;
}

DailyBadScore score_daily_bad_v4(const EdenStart& start) noexcept {
    auto result = score_daily_bad_v3(start);
    result.eligible = result.eligible && start.tears < 2.5;
    if (!result.eligible) result.selection_weight = 0;
    return result;
}

DailyBadScore score_daily_bad_v5(const EdenStart& start) noexcept {
    auto result = score_daily_bad_v4(start);
    result.eligible = result.eligible && start.active_id != 482;
    if (!result.eligible) result.selection_weight = 0;
    return result;
}

DailyBadScore score_daily_bad_challenge_v1(const EdenStart& start) noexcept {
    static constexpr std::array<std::uint16_t, 12> active_ids{
        36, 39, 41, 177, 287, 290, 294, 325, 475, 480, 481, 582,
    };
    static constexpr std::array<std::int32_t, 6> bad_pill_effect_ids{
        1, 6, 11, 13, 15, 17,
    };
    const auto active_allowed = std::find(
        active_ids.begin(), active_ids.end(), start.active_id
    ) != active_ids.end();
    const auto empty_pocket = start.pocket_kind == PocketKind::none;
    const auto bad_pill = start.pocket_kind == PocketKind::pill
        && std::find(
            bad_pill_effect_ids.begin(), bad_pill_effect_ids.end(), start.pocket_id
        ) != bad_pill_effect_ids.end();
    const auto resource_gate = start.coins == 0 && start.keys == 0 && start.bombs == 0;
    const auto low_panel_passive = (start.passive_id == 697 || start.passive_id == 561)
        && start.move_speed < 1.0
        && start.damage < 3.0
        && start.tears < 2.0;
    const auto treatment_passive = start.passive_id == 240
        && start.post_item_stats_available
        && start.post_damage < 2.0
        && start.post_tears < 1.5;

    DailyBadScore result;
    result.eligible = active_allowed
        && (empty_pocket || bad_pill)
        && resource_gate
        && (low_panel_passive || treatment_passive);
    if (result.eligible) {
        // Passive branches are balanced separately during selection. This
        // weight only controls the bad-pill preference inside one branch.
        result.selection_weight = bad_pill ? 3 : 1;
    }
    return result;
}

namespace {

using DailyScoreFunction = DailyGoodScore (*)(const EdenStart&) noexcept;

DailyGoodResult select_daily_impl(
    const ProfileTables& tables,
    const DailyGoodOptions& options,
    std::string_view rules_version,
    DailyScoreFunction score_function,
    std::uint64_t draw_salt
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
        throw std::runtime_error("daily scan produced no eligible seeds");
    }

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
    return select_daily_impl(
        tables,
        options,
        daily_good_rules_version_v0,
        score_daily_good_v0,
        0x6461696c792d676fULL
    );
}

DailyGoodResult select_daily_good_v1(
    const ProfileTables& tables,
    const DailyGoodOptions& options
) {
    return select_daily_impl(
        tables,
        options,
        daily_good_rules_version_v1,
        score_daily_good_v1,
        0x6461696c792d676fULL
    );
}

DailyBadResult select_daily_bad_v0(
    const ProfileTables& tables,
    const DailyGoodOptions& options
) {
    return select_daily_impl(
        tables,
        options,
        daily_bad_rules_version_v0,
        score_daily_bad_v0,
        0x6461696c792d6261ULL
    );
}

DailyBadResult select_daily_bad_v1(
    const ProfileTables& tables,
    const DailyGoodOptions& options
) {
    return select_daily_impl(
        tables,
        options,
        daily_bad_rules_version_v1,
        score_daily_bad_v1,
        0x6461696c792d6261ULL
    );
}

DailyBadResult select_daily_bad_v2(
    const ProfileTables& tables,
    const DailyGoodOptions& options
) {
    return select_daily_impl(
        tables,
        options,
        daily_bad_rules_version_v2,
        score_daily_bad_v2,
        0x6461696c792d6261ULL
    );
}

DailyBadResult select_daily_bad_v3(
    const ProfileTables& tables,
    const DailyGoodOptions& options
) {
    return select_daily_impl(
        tables,
        options,
        daily_bad_rules_version_v3,
        score_daily_bad_v3,
        0x6461696c792d6261ULL
    );
}

DailyBadResult select_daily_bad_v4(
    const ProfileTables& tables,
    const DailyGoodOptions& options
) {
    return select_daily_impl(
        tables,
        options,
        daily_bad_rules_version_v4,
        score_daily_bad_v4,
        0x6461696c792d6261ULL
    );
}

DailyBadResult select_daily_bad_v5(
    const ProfileTables& tables,
    const DailyGoodOptions& options
) {
    return select_daily_impl(
        tables,
        options,
        daily_bad_rules_version_v5,
        score_daily_bad_v5,
        0x6461696c792d6261ULL
    );
}

DailyBadChallengePool scan_daily_bad_challenge_pool_v1(
    const ProfileTables& tables,
    std::uint64_t candidates,
    unsigned threads
) {
    if (candidates == 0 || candidates > (std::uint64_t{1} << 32U)) {
        throw std::invalid_argument(
            "challenge pool candidate count must be within the 32-bit seed space"
        );
    }
    const auto started = std::chrono::steady_clock::now();
    auto thread_count = threads == 0 ? std::thread::hardware_concurrency() : threads;
    thread_count = std::clamp(thread_count, 1U, 64U);
    thread_count = static_cast<unsigned>(std::min<std::uint64_t>(thread_count, candidates));

    std::vector<std::vector<DailyBadChallengeCandidate>> local_candidates(thread_count);
    std::vector<std::thread> workers;
    workers.reserve(thread_count);
    for (unsigned thread_index = 0; thread_index < thread_count; ++thread_index) {
        const auto begin = candidates * thread_index / thread_count;
        const auto end = candidates * (thread_index + 1U) / thread_count;
        workers.emplace_back([&, begin, end, thread_index] {
            auto& output = local_candidates[thread_index];
            output.reserve(static_cast<std::size_t>((end - begin) / 100'000U + 32U));
            for (std::uint64_t value = begin; value < end; ++value) {
                const auto seed = static_cast<std::uint32_t>(value);
                if (seed == 0) continue;
                const auto start = predict_eden_start(seed, tables);
                const auto score = score_daily_bad_challenge_v1(start);
                if (score.eligible) {
                    output.push_back({
                        seed,
                        score.selection_weight,
                        static_cast<std::uint16_t>(start.passive_id),
                    });
                }
            }
        });
    }
    for (auto& worker : workers) worker.join();

    DailyBadChallengePool pool;
    pool.scanned = candidates;
    pool.threads = thread_count;
    for (auto& local : local_candidates) {
        pool.candidates.insert(
            pool.candidates.end(),
            std::make_move_iterator(local.begin()),
            std::make_move_iterator(local.end())
        );
    }
    std::sort(
        pool.candidates.begin(),
        pool.candidates.end(),
        [](const auto& left, const auto& right) { return left.seed < right.seed; }
    );
    pool.elapsed_seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started
    ).count();
    if (pool.candidates.empty()) {
        throw std::runtime_error("challenge pool scan produced no eligible seeds");
    }
    return pool;
}

DailyBadResult select_daily_bad_challenge_v1_from_pool(
    const ProfileTables& tables,
    const DailyGoodOptions& options,
    const DailyBadChallengePool& pool
) {
    if (!valid_iso_date(options.date_utc8)) {
        throw std::invalid_argument("daily date must use a valid YYYY-MM-DD value");
    }
    if (pool.candidates.empty() || pool.scanned == 0) {
        throw std::invalid_argument("challenge pool cannot be empty");
    }
    const auto started = std::chrono::steady_clock::now();
    const auto key = options.date_utc8 + "|j460-full-unlock|"
        + std::string(daily_bad_challenge_rules_version_v1);
    const auto key_hash = stable_hash(key);
    const auto sequence_seed = splitmix64(key_hash);
    const auto sequence_step = static_cast<std::uint32_t>(sequence_seed >> 32U) | 1U;
    const auto sequence_start = static_cast<std::uint32_t>(sequence_seed);
    const auto inverse_step = inverse_odd_u32(sequence_step);

    std::vector<std::size_t> order(pool.candidates.size());
    std::iota(order.begin(), order.end(), 0U);
    std::sort(order.begin(), order.end(), [&](std::size_t left, std::size_t right) {
        const auto left_index = static_cast<std::uint32_t>(
            (pool.candidates[left].seed - sequence_start) * inverse_step
        );
        const auto right_index = static_cast<std::uint32_t>(
            (pool.candidates[right].seed - sequence_start) * inverse_step
        );
        return left_index < right_index;
    });

    struct ChallengeBranch {
        std::uint16_t passive_id;
        std::uint64_t draw_weight;
    };
    static constexpr std::array branches{
        ChallengeBranch{240, 40},
        ChallengeBranch{697, 30},
        ChallengeBranch{561, 30},
    };
    std::array<std::uint64_t, branches.size()> candidate_weights{};
    for (const auto& candidate : pool.candidates) {
        if (candidate.seed == 0
            || (candidate.selection_weight != 1 && candidate.selection_weight != 3)) {
            throw std::invalid_argument("challenge pool contains an invalid candidate");
        }
        const auto branch = std::find_if(
            branches.begin(),
            branches.end(),
            [&](const auto& value) { return value.passive_id == candidate.passive_id; }
        );
        if (branch == branches.end()) {
            throw std::invalid_argument("challenge pool contains an invalid passive branch");
        }
        const auto index = static_cast<std::size_t>(branch - branches.begin());
        candidate_weights[index] += static_cast<std::uint64_t>(candidate.selection_weight);
    }
    auto draw_salt = 0x6368616c6c656e67ULL;
    if (options.draw_variant != 0) draw_salt ^= splitmix64(options.draw_variant);

    std::uint64_t available_branch_weight = 0;
    for (std::size_t index = 0; index < branches.size(); ++index) {
        if (candidate_weights[index] != 0) {
            available_branch_weight += branches[index].draw_weight;
        }
    }
    if (available_branch_weight == 0) {
        throw std::runtime_error("challenge pool has no selectable passive branch");
    }
    auto branch_target = splitmix64(
        key_hash ^ draw_salt ^ 0x6272616e63682d76ULL
    ) % available_branch_weight;
    std::size_t selected_branch = 0;
    for (; selected_branch < branches.size(); ++selected_branch) {
        if (candidate_weights[selected_branch] == 0) continue;
        if (branch_target < branches[selected_branch].draw_weight) break;
        branch_target -= branches[selected_branch].draw_weight;
    }
    if (selected_branch == branches.size()) {
        throw std::runtime_error("daily challenge branch draw failed");
    }

    auto target = splitmix64(
        key_hash ^ draw_salt ^ 0x63616e6469646174ULL
    ) % candidate_weights[selected_branch];
    const DailyBadChallengeCandidate* selected = nullptr;
    for (const auto index : order) {
        const auto& candidate = pool.candidates[index];
        if (candidate.passive_id != branches[selected_branch].passive_id) continue;
        const auto weight = static_cast<std::uint64_t>(candidate.selection_weight);
        if (target < weight) {
            selected = &candidate;
            break;
        }
        target -= weight;
    }
    if (selected == nullptr) throw std::runtime_error("daily weighted draw failed");

    DailyBadResult result;
    result.date_utc8 = options.date_utc8;
    result.rules_version = std::string(daily_bad_challenge_rules_version_v1);
    result.primary = predict_eden_start(selected->seed, tables);
    result.primary_score = score_daily_bad_challenge_v1(result.primary);
    result.scanned = pool.scanned;
    result.eligible = pool.candidates.size();
    result.threads = pool.threads;
    result.elapsed_seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started
    ).count();
    if (!result.primary_score.eligible
        || result.primary_score.selection_weight != selected->selection_weight
        || result.primary.passive_id != selected->passive_id) {
        throw std::runtime_error("challenge pool candidate no longer matches its rules");
    }
    return result;
}

std::optional<DailyBadChallengePool> load_daily_bad_challenge_pool_v1(
    const std::filesystem::path& path,
    const ProfileTables& tables
) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return std::nullopt;
    std::string magic;
    std::string rules_version;
    std::string profile_id;
    std::uint64_t scanned = 0;
    std::size_t count = 0;
    if (!(input >> magic >> rules_version >> profile_id >> scanned >> count)
        || magic != "ISSS_CHALLENGE_POOL_V2"
        || rules_version != daily_bad_challenge_rules_version_v1
        || profile_id != "j460-full-unlock"
        || scanned != (std::uint64_t{1} << 32U)
        || count == 0
        || count > 1'000'000U) {
        return std::nullopt;
    }

    DailyBadChallengePool pool;
    pool.scanned = scanned;
    pool.candidates.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        DailyBadChallengeCandidate candidate;
        if (!(input >> candidate.seed >> candidate.selection_weight >> candidate.passive_id)
            || candidate.seed == 0
            || (candidate.selection_weight != 1 && candidate.selection_weight != 3)) {
            return std::nullopt;
        }
        const auto start = predict_eden_start(candidate.seed, tables);
        const auto score = score_daily_bad_challenge_v1(start);
        if (!score.eligible
            || score.selection_weight != candidate.selection_weight
            || start.passive_id != candidate.passive_id) {
            return std::nullopt;
        }
        pool.candidates.push_back(candidate);
    }
    std::sort(
        pool.candidates.begin(),
        pool.candidates.end(),
        [](const auto& left, const auto& right) { return left.seed < right.seed; }
    );
    const auto duplicate = std::adjacent_find(
        pool.candidates.begin(),
        pool.candidates.end(),
        [](const auto& left, const auto& right) { return left.seed == right.seed; }
    );
    if (duplicate != pool.candidates.end()) return std::nullopt;
    return pool;
}

void save_daily_bad_challenge_pool_v1(
    const std::filesystem::path& path,
    const DailyBadChallengePool& pool
) {
    if (pool.scanned != (std::uint64_t{1} << 32U) || pool.candidates.empty()) {
        throw std::invalid_argument("only a complete challenge pool can be cached");
    }
    std::filesystem::create_directories(path.parent_path());
    auto temporary = path;
    temporary += ".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("cannot create challenge pool cache");
    output << "ISSS_CHALLENGE_POOL_V2\n"
           << daily_bad_challenge_rules_version_v1 << '\n'
           << "j460-full-unlock\n"
           << pool.scanned << '\n'
           << pool.candidates.size() << '\n';
    for (const auto& candidate : pool.candidates) {
        output << candidate.seed << ' ' << candidate.selection_weight << ' '
               << candidate.passive_id << '\n';
    }
    output.close();
    if (!output) throw std::runtime_error("cannot finish challenge pool cache");
    std::error_code error;
    std::filesystem::remove(path, error);
    error.clear();
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        throw std::runtime_error("cannot install challenge pool cache");
    }
}

}  // namespace isaac_seed_seeker
