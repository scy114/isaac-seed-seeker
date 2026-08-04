#include "isaac_seed_seeker/core.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <mutex>
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
constexpr std::uint64_t qword_pill = 12'884'901'891ULL;
constexpr std::uint32_t dword_pill = 29;
constexpr std::uint64_t qword_pool_init = 38'654'705'665ULL;
constexpr std::uint32_t dword_pool_init = 29;
constexpr std::uint64_t qword_trinket_retry = 38'654'705'669ULL;
constexpr std::uint32_t dword_trinket_retry = 7;
constexpr std::uint32_t seed_xor = 0x0fef7ffdU;
constexpr double u32_to_unit = 2.3283062e-10;
constexpr char alphabet[] = "ABCDEFGHJKLMNPQRSTWXYZ01234V6789";

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

std::uint32_t card_step(std::uint32_t seed) noexcept {
    auto value = seed ^ (seed >> 2U);
    value ^= value << 7U;
    return value ^ (value >> 9U);
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

std::int32_t roll_pill(std::uint32_t roll_seed) noexcept {
    auto state = mix(roll_seed, qword_pill, dword_pill);
    state = mix(state, qword_pill, dword_pill);
    auto effect = static_cast<std::int32_t>(state % 22U + 1U);
    state = mix(state, qword_pill, dword_pill);
    if (state % 7U == 0) {
        effect += 55;
    }
    return effect;
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
    if ((selector & 1U) != 0) {
        result.pocket_kind = PocketKind::card;
        result.pocket_id = roll_card(roll_seed);
    } else {
        result.pocket_kind = PocketKind::pill;
        result.pocket_id = roll_pill(roll_seed);
    }
}

bool roll_and_match_pocket(
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
    const auto kind = (selector & 1U) != 0 ? PocketKind::card : PocketKind::pill;
    if (!kind_matches(kind)) return false;
    if (criteria.pocket_ids.configured()) {
        const auto roll_seed = eden_step(selector);
        pocket_id = kind == PocketKind::card ? roll_card(roll_seed) : roll_pill(roll_seed);
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
}

std::pair<std::int32_t, std::int32_t> roll_items(
    std::uint32_t p988,
    const ProfileTables& tables
) noexcept {
    const auto count = static_cast<std::uint32_t>(tables.collectibles.size());
    if (count <= 1) {
        return {0, 0};
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
    const auto active_id = active_index == 0 ? 0 : tables.collectibles[active_index].item_id;
    const auto passive_id = passive_index == 0 ? 0 : tables.collectibles[passive_index].item_id;
    return {active_id, passive_id};
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
        && criteria.damage_delta.matches(start.damage_delta)
        && criteria.move_speed_delta.matches(start.move_speed_delta)
        && criteria.tears_delta.matches(start.tears_delta)
        && criteria.range.matches(start.range)
        && criteria.shot_speed_delta.matches(start.shot_speed_delta)
        && criteria.luck_delta.matches(start.luck_delta);
}

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
        throw std::invalid_argument(std::string(name) + " IDs must be positive");
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
    pocket_ids.validate("pocket");
    active_items.validate("active item");
    passive_items.validate("passive item");
    if (pocket_kind == PocketKind::none && pocket_ids.configured()) {
        throw std::invalid_argument("pocket IDs cannot be combined with pocket kind none");
    }
    red_hearts.validate("red hearts");
    soul_hearts.validate("soul hearts");
    damage_delta.validate("damage delta");
    move_speed_delta.validate("move speed delta");
    tears_delta.validate("tears delta");
    range.validate("range");
    shot_speed_delta.validate("shot speed delta");
    luck_delta.validate("luck delta");
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
    return active_items.configured() || passive_items.configured();
}

bool EdenCriteria::needs_base_rolls() const noexcept {
    return red_hearts.configured() || soul_hearts.configured()
        || damage_delta.configured() || move_speed_delta.configured()
        || tears_delta.configured() || range.configured()
        || shot_speed_delta.configured() || luck_delta.configured();
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
    const auto [active_id, passive_id] = roll_items(result.p988, tables);
    result.active_id = active_id;
    result.passive_id = passive_id;
    roll_base_start(result);
    return result;
}

bool matches(const EdenStart& start, const EdenCriteria& criteria) noexcept {
    return matches_pocket(start, criteria)
        && matches_items(start, criteria)
        && matches_base_start(start, criteria);
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
    const auto started = std::chrono::steady_clock::now();
    const bool needs_pocket = criteria.needs_pocket();
    const bool needs_base_rolls = criteria.needs_base_rolls();
    const bool needs_items = criteria.needs_items();

    std::vector<std::thread> workers;
    workers.reserve(thread_count);
    try {
        for (unsigned worker_index = 0; worker_index < thread_count; ++worker_index) {
            workers.emplace_back([&] {
                try {
                    std::vector<Match> local;
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
                            if (needs_pocket) {
                                if (!roll_and_match_pocket(a5, p988, tables, criteria)) {
                                    continue;
                                }
                            }
                            if (needs_base_rolls) {
                                EdenStart base_start;
                                base_start.p988 = p988;
                                roll_base_start(base_start);
                                if (!matches_base_start(base_start, criteria)) {
                                    continue;
                                }
                            }
                            if (needs_items) {
                                const auto [active_id, passive_id] = roll_items(p988, tables);
                                if (!criteria.active_items.matches(active_id)
                                    || !criteria.passive_items.matches(passive_id)) {
                                    continue;
                                }
                            }
                            const auto label = seed_to_string(seed);
                            if (is_special_seed(label)) {
                                continue;
                            }
                            match_count.fetch_add(1, std::memory_order_relaxed);
                            if (local.size() < options.max_results) {
                                local.push_back(Match{label, predict_eden_start(seed, tables)});
                            }
                        }
                        scanned.fetch_add(completed, std::memory_order_relaxed);
                        if (!local.empty()) {
                            std::lock_guard lock(matches_mutex);
                            all_matches.insert(all_matches.end(), local.begin(), local.end());
                            if (all_matches.size() > options.max_results) {
                                const auto keep = all_matches.begin()
                                    + static_cast<std::vector<Match>::difference_type>(options.max_results);
                                std::nth_element(
                                    all_matches.begin(), keep, all_matches.end(),
                                    [](const Match& left, const Match& right) {
                                        return left.start.seed < right.start.seed;
                                    }
                                );
                                all_matches.resize(options.max_results);
                            }
                            local.clear();
                        }
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
    std::sort(all_matches.begin(), all_matches.end(), [](const Match& left, const Match& right) {
        return left.start.seed < right.start.seed;
    });
    const auto finished = std::chrono::steady_clock::now();
    return SearchResult{
        std::move(all_matches),
        match_count.load(std::memory_order_relaxed),
        scanned.load(std::memory_order_relaxed),
        std::chrono::duration<double>(finished - started).count(),
        thread_count,
    };
}

}  // namespace isaac_seed_seeker
