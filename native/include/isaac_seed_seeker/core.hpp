#pragma once

#include <atomic>
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace isaac_seed_seeker {

enum class PocketKind : std::uint8_t {
    none = 0,
    trinket = 1,
    card = 2,
    pill = 3,
};

std::string_view pocket_kind_name(PocketKind kind) noexcept;

enum class SortKey : std::uint8_t {
    seed = 0,
    health,
    damage,
    move_speed,
    tears,
    range,
    shot_speed,
    luck,
    coins,
    keys,
    bombs,
    active_quality,
    passive_quality,
    total_quality,
};

enum class ExperimentalTreatmentStat : std::uint8_t {
    health = 0,
    move_speed = 1,
    tears = 2,
    damage = 3,
    range = 4,
    shot_speed = 5,
    luck = 6,
};

enum class ExperimentalTreatmentDirection : std::uint8_t {
    up = 0,
    down = 1,
    unchanged = 2,
};

enum class SortDirection : std::uint8_t {
    ascending = 0,
    descending = 1,
};

std::string_view sort_key_name(SortKey key) noexcept;
std::string_view sort_direction_name(SortDirection direction) noexcept;

struct CollectibleEntry {
    std::int32_t item_id = 0;
    bool blocked = true;
    bool active_slot = false;
    bool present = false;
    std::uint8_t quality = 0;
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
    // For pills, pocket_id is the run-specific raw effect ID and pill_color
    // is the PillColor value (including the 2048 horse-pill flag). A golden
    // pill has no single fixed effect, so its pocket_id is -1.
    std::int32_t pill_color = 0;
    std::int32_t active_id = 0;
    std::int32_t passive_id = 0;
    std::int32_t active_quality = 0;
    std::int32_t passive_quality = 0;
    std::int32_t coins = 0;
    std::int32_t keys = 0;
    std::int32_t bombs = 0;

    // Health is expressed in full-heart units.  The primary stat fields are
    // the pre-item values shown by Found HUD.  Raw Eden modifiers are retained
    // for backwards-compatible API output and decoder verification.
    double red_hearts = 0.0;
    double soul_hearts = 0.0;
    double damage = 3.5;
    double move_speed = 1.0;
    double tears = 30.0 / 11.0;
    double range = 6.5;
    double shot_speed = 1.0;
    double luck = 0.0;
    double damage_delta = 0.0;
    double move_speed_delta = 0.0;
    double tears_delta = 0.0;
    double shot_speed_delta = 0.0;
    double luck_delta = 0.0;

    // Experimental Treatment (collectible 240) is intentionally modeled as
    // a narrow post-processor instead of a generic item-effect engine.  Bits
    // use ExperimentalTreatmentStat values.  Post-item values are meaningful
    // only when post_item_stats_available is true.
    bool post_item_stats_available = false;
    std::uint8_t experimental_treatment_up_mask = 0;
    std::uint8_t experimental_treatment_down_mask = 0;
    double post_damage = 3.5;
    double post_move_speed = 1.0;
    double post_tears = 30.0 / 11.0;
    double post_range = 6.5;
    double post_shot_speed = 1.0;
    double post_luck = 0.0;
};

struct NumberRange {
    std::optional<double> minimum;
    std::optional<double> maximum;

    [[nodiscard]] bool configured() const noexcept;
    [[nodiscard]] bool matches(double value) const noexcept;
    void validate(std::string_view name) const;
};

struct IdSetCriteria {
    std::vector<std::int32_t> any_of;
    std::vector<std::int32_t> none_of;

    [[nodiscard]] bool configured() const noexcept;
    [[nodiscard]] bool matches(std::int32_t value) const noexcept;
    void validate(std::string_view name, std::int32_t minimum_id = 1) const;
};

struct EdenCriteria {
    std::optional<PocketKind> pocket_kind;
    IdSetCriteria pocket_ids;
    IdSetCriteria active_items;
    IdSetCriteria passive_items;

    NumberRange red_hearts;
    NumberRange soul_hearts;
    NumberRange damage;
    NumberRange move_speed;
    NumberRange tears;
    NumberRange range;
    NumberRange shot_speed;
    NumberRange luck;
    NumberRange coins;
    NumberRange keys;
    NumberRange bombs;

    NumberRange post_damage;
    NumberRange post_move_speed;
    NumberRange post_tears;
    NumberRange post_range;
    NumberRange post_shot_speed;
    NumberRange post_luck;
    std::array<std::optional<ExperimentalTreatmentDirection>, 7>
        experimental_treatment_directions{};

    // Legacy raw-modifier filters.  New callers should use the Found HUD
    // fields above; these remain valid so existing scripts do not break.
    NumberRange damage_delta;
    NumberRange move_speed_delta;
    NumberRange tears_delta;
    NumberRange shot_speed_delta;
    NumberRange luck_delta;

    void validate() const;
    [[nodiscard]] bool configured() const noexcept;
    [[nodiscard]] bool needs_pocket() const noexcept;
    [[nodiscard]] bool needs_items() const noexcept;
    [[nodiscard]] bool needs_base_rolls() const noexcept;
    [[nodiscard]] bool needs_post_item_rolls() const noexcept;
};

struct SearchOptions {
    std::uint32_t start = 1;
    std::uint32_t end = 0xffffffffU;
    std::uint32_t block_size = 1'000'000;
    unsigned threads = 0;
    std::size_t max_results = 1'000;
    SortKey sort_key = SortKey::seed;
    SortDirection sort_direction = SortDirection::ascending;
};

struct Match {
    std::string label;
    EdenStart start;
};

struct SearchProgress {
    std::uint64_t scanned = 0;
    std::uint64_t total = 0;
    std::uint64_t matches = 0;
    double elapsed_seconds = 0.0;
};

struct SearchResult {
    std::vector<Match> matches;
    std::uint64_t total_matches = 0;
    std::uint64_t scanned = 0;
    double elapsed_seconds = 0.0;
    unsigned threads = 0;
    SortKey sort_key = SortKey::seed;
    SortDirection sort_direction = SortDirection::ascending;
    std::size_t result_limit = 1'000;

    [[nodiscard]] bool truncated() const noexcept {
        return total_matches > matches.size();
    }
};

using ProgressCallback = std::function<void(const SearchProgress&)>;

std::uint32_t seed_checksum(std::uint32_t seed) noexcept;
std::string seed_to_string(std::uint32_t seed);
std::uint32_t a5_from_seed(std::uint32_t seed) noexcept;
std::uint32_t p988_from_a5(std::uint32_t a5) noexcept;
std::uint32_t p988_from_seed(std::uint32_t seed) noexcept;

EdenStart predict_eden_start(std::uint32_t seed, const ProfileTables& tables);
bool matches(const EdenStart& start, const EdenCriteria& criteria) noexcept;

SearchResult search(
    const ProfileTables& tables,
    const EdenCriteria& criteria,
    const SearchOptions& options,
    const ProgressCallback& progress = {},
    const std::atomic_bool* cancel = nullptr
);

}  // namespace isaac_seed_seeker
