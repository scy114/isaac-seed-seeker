#include "isaac_seed_seeker/core.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <exception>
#include <iterator>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>

namespace isaac_seed_seeker {
namespace {

constexpr std::uint64_t qword_9eb880 = 98'784'247'811ULL;
constexpr std::uint32_t dword_9eb880 = 25;
constexpr std::uint64_t qword_b1f504 = 47'244'640'257ULL;
constexpr std::uint32_t dword_b1f50c = 16;
constexpr std::uint64_t qword_eden = 21'474'836'481ULL;
constexpr std::uint32_t dword_eden = 19;
constexpr std::uint64_t qword_pool_init = 38'654'705'665ULL;
constexpr std::uint32_t dword_pool_init = 29;
constexpr std::uint64_t qword_trinket_retry = 38'654'705'669ULL;
constexpr std::uint32_t dword_trinket_retry = 7;
constexpr std::uint32_t seed_xor = 0x0fef7ffdU;
constexpr double u32_to_unit = 2.3283062e-10;
constexpr char alphabet[] = "ABCDEFGHJKLMNPQRSTWXYZ01234V6789";

double fire_rate_from_tears_modifier(double modifier) noexcept {
    // Repentance Found HUD displays fire rate as 30 / (tear delay + 1).
    // Eden's generated modifier stays inside (-0.77, Tmax), so only the two
    // middle branches of the game's tear-delay formula are reachable here.
    if (modifier < 0.0) {
        // Repentance compresses Eden's negative random-tears range so the
        // generated offset bottoms out near -0.515 instead of -0.75.
        modifier *= 0.686655;
    }
    auto tear_delay = 16.0 - 6.0 * std::sqrt(modifier * 1.3 + 1.0);
    if (modifier < 0.0) {
        tear_delay -= 6.0 * modifier;
    }
    return 30.0 / (tear_delay + 1.0);
}

std::uint32_t mix(std::uint32_t seed, std::uint64_t qword, std::uint32_t third) noexcept {
    const auto shift_right = static_cast<std::uint32_t>(qword & 0xffffffffULL);
    const auto shift_left = static_cast<std::uint32_t>((qword >> 32U) & 0xffffffffULL);
    auto value = seed ^ (seed >> shift_right);
    value ^= value << shift_left;
    return value ^ (value >> third);
}

std::uint32_t eden_step(std::uint32_t seed) noexcept {
    return mix(seed, qword_eden, dword_eden);
}

using LinearTransform = std::array<std::uint32_t, 32>;

constexpr std::uint32_t xorshift_step(
    std::uint32_t value,
    std::uint8_t shift_right,
    std::uint8_t shift_left,
    std::uint8_t shift_final
) noexcept {
    value ^= value >> shift_right;
    value ^= value << shift_left;
    return value ^ (value >> shift_final);
}

constexpr LinearTransform xorshift_transform(
    std::uint8_t shift_right,
    std::uint8_t shift_left,
    std::uint8_t shift_final
) noexcept {
    LinearTransform result{};
    for (std::size_t bit = 0; bit < result.size(); ++bit) {
        result[bit] = xorshift_step(
            std::uint32_t{1} << bit,
            shift_right,
            shift_left,
            shift_final
        );
    }
    return result;
}

constexpr std::uint32_t apply_transform(
    const LinearTransform& transform,
    std::uint32_t value
) noexcept {
    std::uint32_t result = 0;
    for (std::size_t bit = 0; bit < transform.size(); ++bit) {
        if ((value & (std::uint32_t{1} << bit)) != 0) {
            result ^= transform[bit];
        }
    }
    return result;
}

constexpr LinearTransform compose_transform(
    const LinearTransform& outer,
    const LinearTransform& inner
) noexcept {
    LinearTransform result{};
    for (std::size_t bit = 0; bit < result.size(); ++bit) {
        result[bit] = apply_transform(outer, inner[bit]);
    }
    return result;
}

constexpr LinearTransform power_transform(LinearTransform base, std::uint32_t exponent) noexcept {
    LinearTransform result{};
    for (std::size_t bit = 0; bit < result.size(); ++bit) {
        result[bit] = std::uint32_t{1} << bit;
    }
    while (exponent != 0) {
        if ((exponent & 1U) != 0) {
            result = compose_transform(base, result);
        }
        exponent >>= 1U;
        if (exponent != 0) {
            base = compose_transform(base, base);
        }
    }
    return result;
}

constexpr auto experimental_treatment_collectible_transform = power_transform(
    xorshift_transform(1, 19, 3),
    241
);

constexpr std::uint8_t treatment_bit(ExperimentalTreatmentStat stat) noexcept {
    return static_cast<std::uint8_t>(1U << static_cast<std::uint8_t>(stat));
}

std::uint32_t card_step(std::uint32_t seed) noexcept {
    auto value = seed ^ (seed >> 3U);
    value ^= value << 3U;
    return value ^ (value >> 29U);
}

std::int32_t roll_card(std::uint32_t roll_seed) noexcept {
    auto state = card_step(roll_seed);
    if (state % 25U == 0) {
        state = card_step(state);
        auto card = static_cast<std::int32_t>(state % 15U + 42U);
        if (card == 55) card = 78;
        if (card == 56) card = 80;
        return card;
    }
    state = card_step(state);
    auto card = static_cast<std::int32_t>(state % 22U + 1U);
    state = card_step(state);
    if (state % 7U == 0) {
        card += 55;
    }
    return card;
}

std::uint32_t pill_color_step(std::uint32_t seed) noexcept {
    auto value = seed ^ (seed >> 2U);
    value ^= value << 7U;
    return value ^ (value >> 9U);
}

std::uint32_t item_pool_step(std::uint32_t seed) noexcept {
    auto value = seed ^ (seed >> 1U);
    value ^= value << 9U;
    return value ^ (value >> 29U);
}

std::array<std::int32_t, 14> pill_effect_pool(std::uint32_t start_seed) noexcept {
    // J460, full unlocks. The run seed initializes the item-pool RNG before
    // the 13 normal colors and 50 available effects are independently
    // shuffled. Effects are then assigned by the game's strength pattern.
    constexpr std::array<std::int8_t, 50> strengths{
        1, 2, 2, 2, 1, 2, 3, 3, 0, 0, 2, 3, 3, 3, 3, 3, 3,
        3, 3, 1, 2, 2, 1, 2, 1, 2, 1, 1, 1, 1, 0, 1, 1, 1,
        1, 1, 1, 1, 1, 0, 0, 1, 1, 2, 0, 1, 2, 1, 1, 3,
    };
    constexpr std::array<std::int8_t, 13> required_strengths{
        3, 3, 3, 3, 2, 1, 0, -1, -1, 3, 2, 1, -1,
    };

    auto state = start_seed;
    for (int iteration = 0; iteration < 17; ++iteration) {
        state = mix(state, qword_9eb880, dword_9eb880);
    }
    for (int iteration = 0; iteration < 63; ++iteration) {
        state = item_pool_step(state);
    }

    std::array<std::int32_t, 13> colors{};
    for (std::size_t index = 0; index < colors.size(); ++index) {
        colors[index] = static_cast<std::int32_t>(index + 1);
    }
    for (std::size_t remaining = colors.size(); remaining > 1; --remaining) {
        state = item_pool_step(state);
        const auto picked = static_cast<std::size_t>(state % remaining);
        std::swap(colors[remaining - 1], colors[picked]);
    }

    std::array<std::int32_t, 50> available{};
    for (std::size_t index = 0; index < available.size(); ++index) {
        available[index] = static_cast<std::int32_t>(index);
    }
    for (std::size_t remaining = available.size(); remaining > 1; --remaining) {
        state = item_pool_step(state);
        const auto picked = static_cast<std::size_t>(state % remaining);
        std::swap(available[remaining - 1], available[picked]);
    }

    std::array<std::int32_t, 14> effects{};
    std::size_t available_count = available.size();
    for (std::size_t index = 0; index < colors.size(); ++index) {
        std::size_t picked = 0;
        if (required_strengths[index] < 0) {
            state = item_pool_step(state);
            picked = static_cast<std::size_t>(state % available_count);
        } else {
            while (picked < available_count
                   && strengths[static_cast<std::size_t>(available[picked])]
                       != required_strengths[index]) {
                ++picked;
            }
        }
        effects[static_cast<std::size_t>(colors[index])] = available[picked];
        for (std::size_t move = picked + 1; move < available_count; ++move) {
            available[move - 1] = available[move];
        }
        --available_count;
    }
    return effects;
}

struct PillRoll {
    std::int32_t color = 0;
    std::int32_t effect = -1;
};

PillRoll roll_pill(std::uint32_t start_seed, std::uint32_t roll_seed) noexcept {
    auto state = pill_color_step(roll_seed);
    auto color = static_cast<std::int32_t>(state % 13U + 1U);

    // With a full unlock file, Eden can also receive golden and horse pills.
    state = pill_color_step(state);
    if (state % 140U == 0) {
        color = 14;
    }
    state = pill_color_step(state);
    if (state % 70U == 0) {
        color |= 2048;
    }

    const auto base_color = color & 2047;
    if (base_color == 14) {
        return {color, -1};
    }
    const auto effects = pill_effect_pool(start_seed);
    return {color, effects[static_cast<std::size_t>(base_color)]};
}

std::uint32_t trinket_rng(std::uint32_t a5) noexcept {
    auto state = mix(a5, qword_9eb880, dword_9eb880);
    state = mix(state, qword_9eb880, dword_9eb880);
    return mix(state, qword_pool_init, dword_pool_init);
}

std::pair<std::uint32_t, std::uint32_t> rng_next_int(
    std::uint32_t state,
    std::uint8_t shift_right,
    std::uint8_t shift_left,
    std::uint8_t shift_final,
    std::uint32_t bound
) noexcept {
    auto next = state ^ (state >> shift_right);
    next ^= next << shift_left;
    next ^= next >> shift_final;
    return {next, bound == 0 ? 0U : next % bound};
}

std::int32_t roll_trinket(std::uint32_t a5, const ProfileTables& tables) noexcept {
    const auto count = static_cast<std::uint32_t>(tables.trinkets.size());
    if (count == 0) {
        return 0;
    }
    auto [state, index] = rng_next_int(
        trinket_rng(a5),
        tables.trinket_shift_right,
        tables.trinket_shift_left,
        tables.trinket_shift_final,
        count
    );
    auto retry_state = state;
    const auto max_tries = std::max(1U, count >> 1U);
    for (std::uint32_t attempt = 0; attempt < max_tries; ++attempt) {
        if (index < count && tables.trinkets[index].available) {
            return tables.trinkets[index].base_id;
        }
        retry_state = mix(retry_state, qword_trinket_retry, dword_trinket_retry);
        index = retry_state % count;
    }
    auto fallback = rng_next_int(
        state,
        tables.trinket_shift_right,
        tables.trinket_shift_left,
        tables.trinket_shift_final,
        count
    );
    index = fallback.second;
    for (std::uint32_t offset = 0; offset < count; ++offset) {
        const auto candidate = (index + offset) % count;
        if (tables.trinkets[candidate].available) {
            return tables.trinkets[candidate].base_id;
        }
    }
    return tables.trinkets[index].base_id;
}

std::uint32_t after_pocket_roll(std::uint32_t p988) noexcept {
    auto state = eden_step(p988);
    if (state % 3U == 0) {
        return state;
    }
    state = eden_step(state);
    if ((state & 1U) != 0) {
        return state;
    }
    const auto v150 = eden_step(state);
    return eden_step(v150);
}

void roll_pocket(EdenStart& result, const ProfileTables& tables) noexcept {
    const auto first = eden_step(result.p988);
    if (first % 3U == 0) {
        result.pocket_kind = PocketKind::trinket;
        result.pocket_id = roll_trinket(result.a5, tables);
        return;
    }
    const auto second = eden_step(first);
    if ((second & 1U) != 0) {
        result.pocket_kind = PocketKind::none;
        result.pocket_id = 0;
        return;
    }
    const auto selector = eden_step(second);
    const auto roll_seed = eden_step(selector);
    if ((selector & 1U) == 0) {
        result.pocket_kind = PocketKind::card;
        result.pocket_id = roll_card(roll_seed);
    } else {
        result.pocket_kind = PocketKind::pill;
        const auto pill = roll_pill(result.seed, roll_seed);
        result.pill_color = pill.color;
        result.pocket_id = pill.effect;
    }
}

bool roll_and_match_pocket(
    std::uint32_t start_seed,
    std::uint32_t a5,
    std::uint32_t p988,
    const ProfileTables& tables,
    const EdenCriteria& criteria
) noexcept {
    const auto kind_matches = [&](PocketKind kind) {
        return !criteria.pocket_kind.has_value() || *criteria.pocket_kind == kind;
    };
    std::int32_t pocket_id = 0;
    const auto first = eden_step(p988);
    if (first % 3U == 0) {
        if (!kind_matches(PocketKind::trinket)) return false;
        if (criteria.pocket_ids.configured()) {
            pocket_id = roll_trinket(a5, tables);
        }
        return criteria.pocket_ids.matches(pocket_id);
    }
    const auto second = eden_step(first);
    if ((second & 1U) != 0) {
        return kind_matches(PocketKind::none) && criteria.pocket_ids.matches(0);
    }
    const auto selector = eden_step(second);
    const auto kind = (selector & 1U) == 0 ? PocketKind::card : PocketKind::pill;
    if (!kind_matches(kind)) return false;
    if (criteria.pocket_ids.configured()) {
        const auto roll_seed = eden_step(selector);
        pocket_id = kind == PocketKind::card
            ? roll_card(roll_seed)
            : roll_pill(start_seed, roll_seed).effect;
    }
    return criteria.pocket_ids.matches(pocket_id);
}

void roll_base_start(EdenStart& result) noexcept {
    auto state = eden_step(result.p988);
    const auto red_hearts = state & 3U;
    result.red_hearts = static_cast<double>(red_hearts);

    const auto soul_bound = red_hearts == 0U ? 4U : 4U - red_hearts;
    const auto soul_roll = rng_next_int(state, 1, 5, 19, soul_bound);
    state = soul_roll.first;
    result.soul_hearts = static_cast<double>(soul_roll.second);
    if (red_hearts == 0U && result.soul_hearts <= 1.0) {
        result.soul_hearts = 2.0;
    }

    auto branch = eden_step(state);
    auto stat_state = branch;
    if (branch % 3U != 0) {
        branch = eden_step(branch);
        stat_state = branch;
        if ((branch & 1U) != 0) {
            stat_state = eden_step(stat_state);
            const auto remainder = stat_state % 3U;
            if (remainder == 0U) {
                stat_state = eden_step(stat_state);
                result.coins = static_cast<std::int32_t>(stat_state % 5U + 1U);
            } else if (remainder == 1U) {
                result.keys = 1;
            } else {
                stat_state = eden_step(stat_state);
                result.bombs = static_cast<std::int32_t>(stat_state % 2U + 1U);
            }
        }
    }

    state = eden_step(stat_state);
    result.damage_delta = static_cast<double>(state) * u32_to_unit * 2.0 - 1.0;
    state = eden_step(state);
    result.move_speed_delta = static_cast<double>(state) * u32_to_unit * 0.30000001 - 0.15000001;
    state = eden_step(state);
    result.tears_delta = static_cast<double>(state) * u32_to_unit * 1.5 - 0.75;
    state = eden_step(state);
    result.range = 6.5 + (static_cast<double>(state) * u32_to_unit * 120.0 - 60.0) / 40.0;
    state = eden_step(state);
    result.shot_speed_delta = static_cast<double>(state) * u32_to_unit * 0.5 - 0.25;
    state = eden_step(state);
    result.luck_delta = static_cast<double>(state) * u32_to_unit * 2.0 - 1.0;

    result.damage = 3.5 + result.damage_delta;
    result.move_speed = 1.0 + result.move_speed_delta;
    result.tears = fire_rate_from_tears_modifier(result.tears_delta);
    result.shot_speed = 1.0 + result.shot_speed_delta;
    result.luck = result.luck_delta;
}

void apply_experimental_treatment(EdenStart& result) noexcept {
    if (result.passive_id != 240) {
        return;
    }

    result.post_item_stats_available = true;
    result.post_damage = result.damage;
    result.post_move_speed = result.move_speed;
    result.post_tears = result.tears;
    result.post_range = result.range;
    result.post_shot_speed = result.shot_speed;
    result.post_luck = result.luck;

    // EntityPlayer:GetCollectibleRNG(240): one player-init RNG step followed
    // by the collectible transform advanced ID + 1 times.  The resulting RNG
    // shuffles seven stats; the first four rise, the next two fall, and the
    // final one is unchanged.
    const auto collectible_seed = apply_transform(
        experimental_treatment_collectible_transform,
        xorshift_step(result.a5, 1, 11, 16)
    );
    auto state = collectible_seed;
    std::array<ExperimentalTreatmentStat, 7> stats{
        ExperimentalTreatmentStat::health,
        ExperimentalTreatmentStat::move_speed,
        ExperimentalTreatmentStat::tears,
        ExperimentalTreatmentStat::damage,
        ExperimentalTreatmentStat::range,
        ExperimentalTreatmentStat::shot_speed,
        ExperimentalTreatmentStat::luck,
    };
    for (std::size_t remaining = stats.size(); remaining > 1; --remaining) {
        state = xorshift_step(state, 5, 9, 7);
        const auto picked = static_cast<std::size_t>(state % remaining);
        std::swap(stats[remaining - 1], stats[picked]);
    }
    for (std::size_t index = 0; index < 4; ++index) {
        result.experimental_treatment_up_mask |= treatment_bit(stats[index]);
    }
    for (std::size_t index = 4; index < 6; ++index) {
        result.experimental_treatment_down_mask |= treatment_bit(stats[index]);
    }

    const auto direction = [&](ExperimentalTreatmentStat stat) {
        const auto bit = treatment_bit(stat);
        if ((result.experimental_treatment_up_mask & bit) != 0) return 1.0;
        if ((result.experimental_treatment_down_mask & bit) != 0) return -1.0;
        return 0.0;
    };
    result.post_damage += direction(ExperimentalTreatmentStat::damage);
    result.post_move_speed = std::clamp(
        result.post_move_speed + 0.2 * direction(ExperimentalTreatmentStat::move_speed),
        0.1,
        2.0
    );
    result.post_tears = std::max(
        0.0,
        result.post_tears + 0.5 * direction(ExperimentalTreatmentStat::tears)
    );
    result.post_range += 2.5 * direction(ExperimentalTreatmentStat::range);
    result.post_shot_speed = std::max(
        0.6,
        result.post_shot_speed + 0.2 * direction(ExperimentalTreatmentStat::shot_speed)
    );
    result.post_luck += direction(ExperimentalTreatmentStat::luck);
}

struct RolledItems {
    std::int32_t active_id = 0;
    std::int32_t passive_id = 0;
    std::int32_t active_quality = 0;
    std::int32_t passive_quality = 0;
};

RolledItems roll_items(
    std::uint32_t p988,
    const ProfileTables& tables
) noexcept {
    const auto count = static_cast<std::uint32_t>(tables.collectibles.size());
    if (count <= 1) {
        return {};
    }
    const auto bound = count - 1;
    auto state = after_pocket_roll(p988);
    std::uint32_t active_index = 0;
    std::uint32_t passive_index = 0;
    for (int attempt = 0; attempt < 100; ++attempt) {
        state = eden_step(state);
        const auto zero_based = state % bound;
        if (zero_based == 234 || zero_based == 42 || zero_based == 60) {
            continue;
        }
        const auto index = zero_based + 1;
        if (index >= count) {
            continue;
        }
        const auto& entry = tables.collectibles[index];
        if (!entry.present || entry.blocked) {
            continue;
        }
        if (entry.active_slot) {
            if (active_index == 0) {
                active_index = index;
            }
        } else if (passive_index == 0) {
            passive_index = index;
        }
        if (active_index != 0 && passive_index != 0) {
            break;
        }
    }
    RolledItems result;
    if (active_index != 0) {
        result.active_id = tables.collectibles[active_index].item_id;
        result.active_quality = tables.collectibles[active_index].quality;
    }
    if (passive_index != 0) {
        result.passive_id = tables.collectibles[passive_index].item_id;
        result.passive_quality = tables.collectibles[passive_index].quality;
    }
    return result;
}

bool contains(const std::vector<std::int32_t>& values, std::int32_t value) noexcept {
    return std::find(values.begin(), values.end(), value) != values.end();
}

bool is_special_seed(const std::string& label) {
    static constexpr const char* special[] = {
        "B911 99AC", "CAMO K1DD", "CAMO F0ES", "FART SNDS",
        "B00B T00B", "BRWN SNKE", "BASE MENT",
    };
    return std::any_of(std::begin(special), std::end(special), [&](const char* item) {
        return label == item;
    });
}

bool matches_pocket(const EdenStart& start, const EdenCriteria& criteria) noexcept {
    return (!criteria.pocket_kind.has_value() || start.pocket_kind == *criteria.pocket_kind)
        && criteria.pocket_ids.matches(start.pocket_id);
}

bool matches_items(const EdenStart& start, const EdenCriteria& criteria) noexcept {
    return criteria.active_items.matches(start.active_id)
        && criteria.passive_items.matches(start.passive_id);
}

bool matches_base_start(const EdenStart& start, const EdenCriteria& criteria) noexcept {
    return criteria.red_hearts.matches(start.red_hearts)
        && criteria.soul_hearts.matches(start.soul_hearts)
        && criteria.coins.matches(start.coins)
        && criteria.keys.matches(start.keys)
        && criteria.bombs.matches(start.bombs)
        && criteria.damage.matches(start.damage)
        && criteria.move_speed.matches(start.move_speed)
        && criteria.tears.matches(start.tears)
        && criteria.range.matches(start.range)
        && criteria.shot_speed.matches(start.shot_speed)
        && criteria.luck.matches(start.luck)
        && criteria.damage_delta.matches(start.damage_delta)
        && criteria.move_speed_delta.matches(start.move_speed_delta)
        && criteria.tears_delta.matches(start.tears_delta)
        && criteria.shot_speed_delta.matches(start.shot_speed_delta)
        && criteria.luck_delta.matches(start.luck_delta);
}

bool matches_post_item_start(const EdenStart& start, const EdenCriteria& criteria) noexcept {
    if (!criteria.needs_post_item_rolls()) {
        return true;
    }
    if (!start.post_item_stats_available) {
        return false;
    }
    if (!criteria.post_damage.matches(start.post_damage)
        || !criteria.post_move_speed.matches(start.post_move_speed)
        || !criteria.post_tears.matches(start.post_tears)
        || !criteria.post_range.matches(start.post_range)
        || !criteria.post_shot_speed.matches(start.post_shot_speed)
        || !criteria.post_luck.matches(start.post_luck)) {
        return false;
    }
    for (std::size_t index = 0; index < criteria.experimental_treatment_directions.size(); ++index) {
        const auto required = criteria.experimental_treatment_directions[index];
        if (!required.has_value()) continue;
        const auto bit = static_cast<std::uint8_t>(1U << index);
        const auto actual = (start.experimental_treatment_up_mask & bit) != 0
            ? ExperimentalTreatmentDirection::up
            : ((start.experimental_treatment_down_mask & bit) != 0
                ? ExperimentalTreatmentDirection::down
                : ExperimentalTreatmentDirection::unchanged);
        if (actual != *required) return false;
    }
    return true;
}

bool sort_needs_base_rolls(SortKey key) noexcept {
    return key == SortKey::health || key == SortKey::damage || key == SortKey::move_speed
        || key == SortKey::tears || key == SortKey::range || key == SortKey::shot_speed
        || key == SortKey::luck || key == SortKey::coins || key == SortKey::keys
        || key == SortKey::bombs;
}

bool sort_needs_item_rolls(SortKey key) noexcept {
    return key == SortKey::active_quality || key == SortKey::passive_quality
        || key == SortKey::total_quality;
}

template <typename Value>
bool directed_before(Value left, Value right, SortDirection direction) noexcept {
    if (left == right) return false;
    return direction == SortDirection::ascending ? left < right : left > right;
}

struct MatchOrder {
    SortKey key = SortKey::seed;
    SortDirection direction = SortDirection::ascending;

    bool operator()(const Match& left, const Match& right) const noexcept {
        const auto& a = left.start;
        const auto& b = right.start;
        switch (key) {
            case SortKey::seed:
                return directed_before(a.seed, b.seed, direction);
            case SortKey::health:
                if (a.red_hearts != b.red_hearts) {
                    return directed_before(a.red_hearts, b.red_hearts, direction);
                }
                if (a.soul_hearts != b.soul_hearts) {
                    return directed_before(a.soul_hearts, b.soul_hearts, direction);
                }
                break;
            case SortKey::damage:
                if (a.damage != b.damage) return directed_before(a.damage, b.damage, direction);
                break;
            case SortKey::move_speed:
                if (a.move_speed != b.move_speed) {
                    return directed_before(a.move_speed, b.move_speed, direction);
                }
                break;
            case SortKey::tears:
                if (a.tears != b.tears) return directed_before(a.tears, b.tears, direction);
                break;
            case SortKey::range:
                if (a.range != b.range) return directed_before(a.range, b.range, direction);
                break;
            case SortKey::shot_speed:
                if (a.shot_speed != b.shot_speed) {
                    return directed_before(a.shot_speed, b.shot_speed, direction);
                }
                break;
            case SortKey::luck:
                if (a.luck != b.luck) return directed_before(a.luck, b.luck, direction);
                break;
            case SortKey::coins:
                if (a.coins != b.coins) return directed_before(a.coins, b.coins, direction);
                break;
            case SortKey::keys:
                if (a.keys != b.keys) return directed_before(a.keys, b.keys, direction);
                break;
            case SortKey::bombs:
                if (a.bombs != b.bombs) return directed_before(a.bombs, b.bombs, direction);
                break;
            case SortKey::active_quality:
                if (a.active_quality != b.active_quality) {
                    return directed_before(a.active_quality, b.active_quality, direction);
                }
                if (a.active_id != b.active_id) return a.active_id < b.active_id;
                break;
            case SortKey::passive_quality:
                if (a.passive_quality != b.passive_quality) {
                    return directed_before(a.passive_quality, b.passive_quality, direction);
                }
                if (a.passive_id != b.passive_id) return a.passive_id < b.passive_id;
                break;
            case SortKey::total_quality: {
                const auto total_a = a.active_quality + a.passive_quality;
                const auto total_b = b.active_quality + b.passive_quality;
                if (total_a != total_b) return directed_before(total_a, total_b, direction);
                if (a.active_id != b.active_id) return a.active_id < b.active_id;
                if (a.passive_id != b.passive_id) return a.passive_id < b.passive_id;
                break;
            }
        }
        return a.seed < b.seed;
    }
};

}  // namespace

std::string_view pocket_kind_name(PocketKind kind) noexcept {
    switch (kind) {
        case PocketKind::none: return "none";
        case PocketKind::trinket: return "trinket";
        case PocketKind::card: return "card";
        case PocketKind::pill: return "pill";
    }
    return "unknown";
}

std::string_view sort_key_name(SortKey key) noexcept {
    switch (key) {
        case SortKey::seed: return "seed";
        case SortKey::health: return "health";
        case SortKey::damage: return "damage";
        case SortKey::move_speed: return "move_speed";
        case SortKey::tears: return "tears";
        case SortKey::range: return "range";
        case SortKey::shot_speed: return "shot_speed";
        case SortKey::luck: return "luck";
        case SortKey::coins: return "coins";
        case SortKey::keys: return "keys";
        case SortKey::bombs: return "bombs";
        case SortKey::active_quality: return "active_quality";
        case SortKey::passive_quality: return "passive_quality";
        case SortKey::total_quality: return "total_quality";
    }
    return "unknown";
}

std::string_view sort_direction_name(SortDirection direction) noexcept {
    return direction == SortDirection::ascending ? "asc" : "desc";
}

bool NumberRange::configured() const noexcept {
    return minimum.has_value() || maximum.has_value();
}

bool NumberRange::matches(double value) const noexcept {
    return (!minimum.has_value() || value >= *minimum)
        && (!maximum.has_value() || value <= *maximum);
}

void NumberRange::validate(std::string_view name) const {
    if ((minimum.has_value() && !std::isfinite(*minimum))
        || (maximum.has_value() && !std::isfinite(*maximum))) {
        throw std::invalid_argument(std::string(name) + " bounds must be finite");
    }
    if (minimum.has_value() && maximum.has_value() && *minimum > *maximum) {
        throw std::invalid_argument(std::string(name) + " minimum cannot exceed maximum");
    }
}

bool IdSetCriteria::configured() const noexcept {
    return !any_of.empty() || !none_of.empty();
}

bool IdSetCriteria::matches(std::int32_t value) const noexcept {
    return (any_of.empty() || contains(any_of, value)) && !contains(none_of, value);
}

void IdSetCriteria::validate(std::string_view name, std::int32_t minimum_id) const {
    const auto invalid = [minimum_id](std::int32_t value) { return value < minimum_id; };
    if (std::any_of(any_of.begin(), any_of.end(), invalid)
        || std::any_of(none_of.begin(), none_of.end(), invalid)) {
        throw std::invalid_argument(
            std::string(name)
            + (minimum_id == 0 ? " IDs must be non-negative" : " IDs must be positive")
        );
    }
    for (const auto value : any_of) {
        if (contains(none_of, value)) {
            throw std::invalid_argument(std::string(name) + " cannot both require and exclude ID "
                                        + std::to_string(value));
        }
    }
}

void EdenCriteria::validate() const {
    if (!configured()) {
        throw std::invalid_argument("at least one Eden criterion is required");
    }
    pocket_ids.validate("pocket", pocket_kind == PocketKind::pill ? 0 : 1);
    active_items.validate("active item");
    passive_items.validate("passive item");
    if (pocket_kind == PocketKind::none && pocket_ids.configured()) {
        throw std::invalid_argument("pocket IDs cannot be combined with pocket kind none");
    }
    red_hearts.validate("red hearts");
    soul_hearts.validate("soul hearts");
    coins.validate("coins");
    keys.validate("keys");
    bombs.validate("bombs");
    const auto validate_resource = [](const NumberRange& range, std::string_view name, double limit) {
        const auto invalid = [limit](const std::optional<double>& value) {
            return value.has_value()
                && (*value < 0.0 || *value > limit || std::trunc(*value) != *value);
        };
        if (invalid(range.minimum) || invalid(range.maximum)) {
            throw std::invalid_argument(
                std::string(name) + " bounds must be whole numbers between 0 and "
                + std::to_string(static_cast<int>(limit))
            );
        }
    };
    validate_resource(coins, "coins", 5.0);
    validate_resource(keys, "keys", 1.0);
    validate_resource(bombs, "bombs", 2.0);
    damage.validate("damage");
    move_speed.validate("move speed");
    tears.validate("tears");
    range.validate("range");
    shot_speed.validate("shot speed");
    luck.validate("luck");
    damage_delta.validate("damage delta");
    move_speed_delta.validate("move speed delta");
    tears_delta.validate("tears delta");
    shot_speed_delta.validate("shot speed delta");
    luck_delta.validate("luck delta");
    post_damage.validate("post-item damage");
    post_move_speed.validate("post-item move speed");
    post_tears.validate("post-item tears");
    post_range.validate("post-item range");
    post_shot_speed.validate("post-item shot speed");
    post_luck.validate("post-item luck");
    if (needs_post_item_rolls()) {
        if (contains(passive_items.none_of, 240)) {
            throw std::invalid_argument(
                "Experimental Treatment criteria cannot exclude passive item 240"
            );
        }
        if (!passive_items.any_of.empty() && !contains(passive_items.any_of, 240)) {
            throw std::invalid_argument(
                "post-item criteria currently require passive item 240"
            );
        }
    }
}

bool EdenCriteria::configured() const noexcept {
    return pocket_kind.has_value() || pocket_ids.configured()
        || active_items.configured() || passive_items.configured()
        || needs_base_rolls();
}

bool EdenCriteria::needs_pocket() const noexcept {
    return pocket_kind.has_value() || pocket_ids.configured();
}

bool EdenCriteria::needs_items() const noexcept {
    return active_items.configured() || passive_items.configured() || needs_post_item_rolls();
}

bool EdenCriteria::needs_base_rolls() const noexcept {
    return red_hearts.configured() || soul_hearts.configured()
        || coins.configured() || keys.configured() || bombs.configured()
        || damage.configured() || move_speed.configured()
        || tears.configured() || range.configured()
        || shot_speed.configured() || luck.configured()
        || damage_delta.configured() || move_speed_delta.configured()
        || tears_delta.configured()
        || shot_speed_delta.configured() || luck_delta.configured()
        || needs_post_item_rolls();
}

bool EdenCriteria::needs_post_item_rolls() const noexcept {
    return post_damage.configured() || post_move_speed.configured()
        || post_tears.configured() || post_range.configured()
        || post_shot_speed.configured() || post_luck.configured()
        || std::any_of(
            experimental_treatment_directions.begin(),
            experimental_treatment_directions.end(),
            [](const auto& value) { return value.has_value(); }
        );
}

std::uint32_t seed_checksum(std::uint32_t seed) noexcept {
    auto value = seed;
    std::uint32_t checksum = 0;
    while (value != 0) {
        const auto low = (value + checksum) & 0xffU;
        checksum = ((low >> 7U) + 2U * low) & 0xffU;
        value >>= 5U;
    }
    return checksum;
}

std::string seed_to_string(std::uint32_t seed) {
    const std::uint64_t payload =
        (static_cast<std::uint64_t>(seed ^ seed_xor) << 8U) | seed_checksum(seed);
    std::string compact(8, 'A');
    for (int index = 0; index < 8; ++index) {
        const auto shift = static_cast<unsigned>(5 * (7 - index));
        compact[static_cast<std::size_t>(index)] = alphabet[(payload >> shift) & 31U];
    }
    return compact.substr(0, 4) + " " + compact.substr(4);
}

std::uint32_t string_to_seed(std::string_view value) {
    std::string compact;
    compact.reserve(8);
    for (const char raw : value) {
        const auto byte = static_cast<unsigned char>(raw);
        if (std::isspace(byte) != 0) {
            continue;
        }
        compact.push_back(static_cast<char>(std::toupper(byte)));
    }
    if (compact.size() != 8) {
        throw std::invalid_argument("seed must contain exactly eight characters");
    }

    std::uint64_t payload = 0;
    for (const char character : compact) {
        const auto found = std::find(std::begin(alphabet), std::end(alphabet) - 1, character);
        if (found == std::end(alphabet) - 1) {
            throw std::invalid_argument(std::string("seed contains invalid character: ") + character);
        }
        payload = (payload << 5U) | static_cast<std::uint64_t>(found - std::begin(alphabet));
    }

    const auto seed = static_cast<std::uint32_t>((payload >> 8U) ^ seed_xor);
    const auto normalized = compact.substr(0, 4) + " " + compact.substr(4);
    if (seed_to_string(seed) != normalized) {
        throw std::invalid_argument("seed checksum is invalid");
    }
    return seed;
}

std::uint32_t a5_from_seed(std::uint32_t seed) noexcept {
    auto state = seed;
    for (int iteration = 0; iteration < 15; ++iteration) {
        state = mix(state, qword_9eb880, dword_9eb880);
    }
    return state;
}

std::uint32_t p988_from_a5(std::uint32_t a5) noexcept {
    auto state = a5;
    for (int iteration = 0; iteration < 5; ++iteration) {
        state = mix(state, qword_b1f504, dword_b1f50c);
    }
    return state;
}

std::uint32_t p988_from_seed(std::uint32_t seed) noexcept {
    return p988_from_a5(a5_from_seed(seed));
}

EdenStart predict_eden_start(std::uint32_t seed, const ProfileTables& tables) {
    EdenStart result;
    result.seed = seed;
    result.a5 = a5_from_seed(seed);
    result.p988 = p988_from_a5(result.a5);
    roll_pocket(result, tables);
    const auto items = roll_items(result.p988, tables);
    result.active_id = items.active_id;
    result.passive_id = items.passive_id;
    result.active_quality = items.active_quality;
    result.passive_quality = items.passive_quality;
    roll_base_start(result);
    apply_experimental_treatment(result);
    return result;
}

bool matches(const EdenStart& start, const EdenCriteria& criteria) noexcept {
    return matches_pocket(start, criteria)
        && matches_items(start, criteria)
        && matches_base_start(start, criteria)
        && matches_post_item_start(start, criteria);
}

SearchResult search(
    const ProfileTables& tables,
    const EdenCriteria& criteria,
    const SearchOptions& options,
    const ProgressCallback& progress,
    const std::atomic_bool* cancel
) {
    criteria.validate();
    if (options.start == 0 || options.start > options.end) {
        throw std::invalid_argument("search range must be within 1..4294967295");
    }
    if (options.block_size == 0) {
        throw std::invalid_argument("block size must be positive");
    }
    if (options.max_results == 0 || options.max_results > 100'000) {
        throw std::invalid_argument("max results must be within 1..100000");
    }

    const auto total = static_cast<std::uint64_t>(options.end) - options.start + 1U;
    unsigned thread_count = options.threads == 0 ? std::thread::hardware_concurrency() : options.threads;
    thread_count = std::min(64U, std::max(1U, thread_count));
    std::atomic<std::uint64_t> next_offset{0};
    std::atomic<std::uint64_t> scanned{0};
    std::atomic<std::uint64_t> match_count{0};
    std::atomic_bool abort{false};
    std::mutex matches_mutex;
    std::mutex error_mutex;
    std::exception_ptr worker_error;
    std::vector<Match> all_matches;
    const MatchOrder order{options.sort_key, options.sort_direction};
    const auto started = std::chrono::steady_clock::now();
    const bool needs_pocket = criteria.needs_pocket();
    const bool needs_base_rolls = criteria.needs_base_rolls()
        || sort_needs_base_rolls(options.sort_key);
    const bool needs_items = criteria.needs_items() || sort_needs_item_rolls(options.sort_key);

    std::vector<std::thread> workers;
    workers.reserve(thread_count);
    try {
        for (unsigned worker_index = 0; worker_index < thread_count; ++worker_index) {
            workers.emplace_back([&] {
                try {
                    std::priority_queue<Match, std::vector<Match>, MatchOrder> local(order);
                    while (true) {
                        if (abort.load(std::memory_order_relaxed)
                            || (cancel != nullptr && cancel->load(std::memory_order_relaxed))) {
                            break;
                        }
                        const auto offset = next_offset.fetch_add(options.block_size, std::memory_order_relaxed);
                        if (offset >= total) {
                            break;
                        }
                        const auto count = std::min<std::uint64_t>(options.block_size, total - offset);
                        const auto first = static_cast<std::uint64_t>(options.start) + offset;
                        std::uint64_t completed = 0;
                        for (; completed < count; ++completed) {
                            if ((completed & 0xffffU) == 0
                                && (abort.load(std::memory_order_relaxed)
                                    || (cancel != nullptr && cancel->load(std::memory_order_relaxed)))) {
                                break;
                            }
                            const auto seed = static_cast<std::uint32_t>(first + completed);
                            const auto a5 = a5_from_seed(seed);
                            const auto p988 = p988_from_a5(a5);
                            EdenStart sort_start;
                            sort_start.seed = seed;
                            sort_start.a5 = a5;
                            sort_start.p988 = p988;
                            if (needs_pocket) {
                                if (!roll_and_match_pocket(seed, a5, p988, tables, criteria)) {
                                    continue;
                                }
                            }
                            if (needs_base_rolls) {
                                roll_base_start(sort_start);
                                if (!matches_base_start(sort_start, criteria)) {
                                    continue;
                                }
                            }
                            if (needs_items) {
                                const auto items = roll_items(p988, tables);
                                sort_start.active_id = items.active_id;
                                sort_start.passive_id = items.passive_id;
                                sort_start.active_quality = items.active_quality;
                                sort_start.passive_quality = items.passive_quality;
                                if (!criteria.active_items.matches(items.active_id)
                                    || !criteria.passive_items.matches(items.passive_id)) {
                                    continue;
                                }
                                if (criteria.needs_post_item_rolls()) {
                                    apply_experimental_treatment(sort_start);
                                    if (!matches_post_item_start(sort_start, criteria)) {
                                        continue;
                                    }
                                }
                            }
                            const auto label = seed_to_string(seed);
                            if (is_special_seed(label)) {
                                continue;
                            }
                            match_count.fetch_add(1, std::memory_order_relaxed);
                            Match candidate{label, sort_start};
                            if (local.size() < options.max_results || order(candidate, local.top())) {
                                candidate.start = predict_eden_start(seed, tables);
                                if (local.size() == options.max_results) {
                                    local.pop();
                                }
                                local.push(std::move(candidate));
                            }
                        }
                        scanned.fetch_add(completed, std::memory_order_relaxed);
                        if (progress) {
                            const auto now = std::chrono::steady_clock::now();
                            progress(SearchProgress{
                                scanned.load(std::memory_order_relaxed),
                                total,
                                match_count.load(std::memory_order_relaxed),
                                std::chrono::duration<double>(now - started).count(),
                            });
                        }
                        if (completed != count) {
                            break;
                        }
                    }
                    std::vector<Match> retained;
                    retained.reserve(local.size());
                    while (!local.empty()) {
                        retained.push_back(local.top());
                        local.pop();
                    }
                    if (!retained.empty()) {
                        std::lock_guard lock(matches_mutex);
                        all_matches.insert(
                            all_matches.end(),
                            std::make_move_iterator(retained.begin()),
                            std::make_move_iterator(retained.end())
                        );
                        if (all_matches.size() > options.max_results) {
                            const auto keep = all_matches.begin()
                                + static_cast<std::vector<Match>::difference_type>(options.max_results);
                            std::nth_element(all_matches.begin(), keep, all_matches.end(), order);
                            all_matches.resize(options.max_results);
                        }
                    }
                } catch (...) {
                    abort.store(true, std::memory_order_relaxed);
                    std::lock_guard lock(error_mutex);
                    if (worker_error == nullptr) {
                        worker_error = std::current_exception();
                    }
                }
            });
        }
    } catch (...) {
        abort.store(true, std::memory_order_relaxed);
        for (auto& worker : workers) {
            worker.join();
        }
        throw;
    }
    for (auto& worker : workers) {
        worker.join();
    }
    if (worker_error != nullptr) {
        std::rethrow_exception(worker_error);
    }
    std::sort(all_matches.begin(), all_matches.end(), order);
    const auto finished = std::chrono::steady_clock::now();
    return SearchResult{
        std::move(all_matches),
        match_count.load(std::memory_order_relaxed),
        scanned.load(std::memory_order_relaxed),
        std::chrono::duration<double>(finished - started).count(),
        thread_count,
        options.sort_key,
        options.sort_direction,
        options.max_results,
    };
}

}  // namespace isaac_seed_seeker
