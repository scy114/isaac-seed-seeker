#include "isaac_seed_seeker/core.hpp"

#include <algorithm>
#include <chrono>
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
constexpr std::uint64_t qword_pool_init = 38'654'705'665ULL;
constexpr std::uint32_t dword_pool_init = 29;
constexpr std::uint64_t qword_trinket_retry = 38'654'705'669ULL;
constexpr std::uint32_t dword_trinket_retry = 7;
constexpr std::uint32_t seed_xor = 0x0fef7ffdU;
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

}  // namespace

void ItemCriteria::validate() const {
    if (trinket_id <= 0) {
        throw std::invalid_argument("trinket ID must be positive");
    }
    if (active_any.empty()) {
        throw std::invalid_argument("at least one active item ID is required");
    }
    if (passive_any.empty()) {
        throw std::invalid_argument("at least one passive item ID is required");
    }
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
    const auto pocket_roll = eden_step(result.p988);
    if (pocket_roll % 3U == 0) {
        result.pocket_kind = PocketKind::trinket;
        result.pocket_id = roll_trinket(result.a5, tables);
    } else {
        const auto second = eden_step(pocket_roll);
        if ((second & 1U) != 0) {
            result.pocket_kind = PocketKind::none;
        } else {
            const auto v150 = eden_step(second);
            result.pocket_kind = (v150 & 1U) != 0 ? PocketKind::card : PocketKind::pill;
        }
    }
    const auto [active_id, passive_id] = roll_items(result.p988, tables);
    result.active_id = active_id;
    result.passive_id = passive_id;
    return result;
}

bool matches(const EdenStart& start, const ItemCriteria& criteria) noexcept {
    return start.pocket_kind == PocketKind::trinket
        && start.pocket_id == criteria.trinket_id
        && contains(criteria.active_any, start.active_id)
        && contains(criteria.passive_any, start.passive_id);
}

SearchResult search(
    const ProfileTables& tables,
    const ItemCriteria& criteria,
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
    std::atomic<std::size_t> match_count{0};
    std::atomic_bool abort{false};
    std::mutex matches_mutex;
    std::mutex error_mutex;
    std::exception_ptr worker_error;
    std::vector<Match> all_matches;
    const auto started = std::chrono::steady_clock::now();

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
                            const auto pocket_roll = eden_step(p988);
                            if (pocket_roll % 3U != 0) {
                                continue;
                            }
                            const auto trinket_id = roll_trinket(a5, tables);
                            if (trinket_id != criteria.trinket_id) {
                                continue;
                            }
                            const auto [active_id, passive_id] = roll_items(p988, tables);
                            if (!contains(criteria.active_any, active_id)
                                || !contains(criteria.passive_any, passive_id)) {
                                continue;
                            }
                            const auto label = seed_to_string(seed);
                            if (is_special_seed(label)) {
                                continue;
                            }
                            local.push_back(Match{seed, label, trinket_id, active_id, passive_id});
                        }
                        scanned.fetch_add(completed, std::memory_order_relaxed);
                        match_count.fetch_add(local.size(), std::memory_order_relaxed);
                        if (!local.empty()) {
                            std::lock_guard lock(matches_mutex);
                            all_matches.insert(all_matches.end(), local.begin(), local.end());
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
        return left.seed < right.seed;
    });
    const auto finished = std::chrono::steady_clock::now();
    return SearchResult{
        std::move(all_matches),
        scanned.load(std::memory_order_relaxed),
        std::chrono::duration<double>(finished - started).count(),
        thread_count,
    };
}

}  // namespace isaac_seed_seeker
