#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace isaac_seed_seeker {

enum class PocketKind : std::uint8_t {
    none = 0,
    trinket = 1,
    card = 2,
    pill = 3,
};

struct CollectibleEntry {
    std::int32_t item_id = 0;
    bool blocked = true;
    bool active_slot = false;
    bool present = false;
};

struct TrinketEntry {
    std::int32_t base_id = 0;
    bool available = false;
};

struct ProfileTables {
    std::vector<CollectibleEntry> collectibles;
    std::vector<TrinketEntry> trinkets;
    std::uint8_t trinket_shift_right = 1;
    std::uint8_t trinket_shift_left = 5;
    std::uint8_t trinket_shift_final = 19;

    static ProfileTables load(
        const std::filesystem::path& collectible_table,
        const std::filesystem::path& trinket_pool
    );
};

struct EdenStart {
    std::uint32_t seed = 0;
    std::uint32_t a5 = 0;
    std::uint32_t p988 = 0;
    PocketKind pocket_kind = PocketKind::none;
    std::int32_t pocket_id = 0;
    std::int32_t active_id = 0;
    std::int32_t passive_id = 0;
};

struct ItemCriteria {
    std::int32_t trinket_id = 0;
    std::vector<std::int32_t> active_any;
    std::vector<std::int32_t> passive_any;

    void validate() const;
};

struct SearchOptions {
    std::uint32_t start = 1;
    std::uint32_t end = 0xffffffffU;
    std::uint32_t block_size = 1'000'000;
    unsigned threads = 0;
};

struct Match {
    std::uint32_t seed = 0;
    std::string label;
    std::int32_t trinket_id = 0;
    std::int32_t active_id = 0;
    std::int32_t passive_id = 0;
};

struct SearchProgress {
    std::uint64_t scanned = 0;
    std::uint64_t total = 0;
    std::size_t matches = 0;
    double elapsed_seconds = 0.0;
};

struct SearchResult {
    std::vector<Match> matches;
    std::uint64_t scanned = 0;
    double elapsed_seconds = 0.0;
    unsigned threads = 0;
};

using ProgressCallback = std::function<void(const SearchProgress&)>;

std::uint32_t seed_checksum(std::uint32_t seed) noexcept;
std::string seed_to_string(std::uint32_t seed);
std::uint32_t a5_from_seed(std::uint32_t seed) noexcept;
std::uint32_t p988_from_a5(std::uint32_t a5) noexcept;
std::uint32_t p988_from_seed(std::uint32_t seed) noexcept;

EdenStart predict_eden_start(std::uint32_t seed, const ProfileTables& tables);
bool matches(const EdenStart& start, const ItemCriteria& criteria) noexcept;

SearchResult search(
    const ProfileTables& tables,
    const ItemCriteria& criteria,
    const SearchOptions& options,
    const ProgressCallback& progress = {},
    const std::atomic_bool* cancel = nullptr
);

}  // namespace isaac_seed_seeker
