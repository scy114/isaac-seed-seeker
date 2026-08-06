#include "isaac_seed_seeker/local_web_app.hpp"

#ifdef _WIN32

#include "isaac_seed_seeker/builtin_profile.hpp"
#include "isaac_seed_seeker/core.hpp"
#include "isaac_seed_seeker/daily.hpp"
#include "../resources/resource.h"

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <charconv>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace isaac_seed_seeker {
namespace {

enum class SessionState : std::uint8_t { idle, running, completed, cancelled, failed };

std::string_view state_name(SessionState state) noexcept {
    switch (state) {
        case SessionState::idle: return "idle";
        case SessionState::running: return "running";
        case SessionState::completed: return "completed";
        case SessionState::cancelled: return "cancelled";
        case SessionState::failed: return "failed";
    }
    return "failed";
}

std::string json_escape(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const char c : value) {
        switch (c) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result.push_back(c); break;
        }
    }
    return result;
}

std::size_t find_json_value(std::string_view text, std::string_view key) {
    const std::string needle = "\"" + std::string(key) + "\"";
    const auto found = text.find(needle);
    if (found == std::string_view::npos) {
        throw std::invalid_argument("missing JSON field: " + std::string(key));
    }
    auto position = text.find(':', found + needle.size());
    if (position == std::string_view::npos) {
        throw std::invalid_argument("malformed JSON field: " + std::string(key));
    }
    ++position;
    while (position < text.size() && std::string_view(" \t\r\n").find(text[position]) != std::string_view::npos) {
        ++position;
    }
    return position;
}

std::optional<std::size_t> find_optional_json_value(std::string_view text, std::string_view key) {
    const std::string needle = "\"" + std::string(key) + "\"";
    const auto found = text.find(needle);
    if (found == std::string_view::npos) {
        return std::nullopt;
    }
    auto position = text.find(':', found + needle.size());
    if (position == std::string_view::npos) {
        throw std::invalid_argument("malformed JSON field: " + std::string(key));
    }
    ++position;
    while (position < text.size() && std::string_view(" \t\r\n").find(text[position]) != std::string_view::npos) {
        ++position;
    }
    return position;
}

std::uint32_t json_u32(std::string_view text, std::string_view key) {
    const auto position = find_json_value(text, key);
    std::uint64_t value = 0;
    const auto parsed = std::from_chars(text.data() + position, text.data() + text.size(), value);
    if (parsed.ec != std::errc() || value > 0xffffffffULL) {
        throw std::invalid_argument("invalid JSON uint32 field: " + std::string(key));
    }
    return static_cast<std::uint32_t>(value);
}

std::vector<std::int32_t> json_id_array(std::string_view text, std::string_view key) {
    auto position = find_json_value(text, key);
    if (position >= text.size() || text[position] != '[') {
        throw std::invalid_argument("JSON field must be an array: " + std::string(key));
    }
    ++position;
    std::vector<std::int32_t> values;
    while (position < text.size()) {
        while (position < text.size() && std::string_view(" \t\r\n,").find(text[position]) != std::string_view::npos) {
            ++position;
        }
        if (position < text.size() && text[position] == ']') {
            break;
        }
        std::int32_t value = 0;
        const auto parsed = std::from_chars(text.data() + position, text.data() + text.size(), value);
        if (parsed.ec != std::errc() || value < 0) {
            throw std::invalid_argument("invalid item ID in JSON field: " + std::string(key));
        }
        values.push_back(value);
        position = static_cast<std::size_t>(parsed.ptr - text.data());
    }
    if (values.empty()) {
        throw std::invalid_argument("item ID array cannot be empty: " + std::string(key));
    }
    return values;
}

std::optional<std::uint32_t> optional_json_u32(std::string_view text, std::string_view key) {
    const auto position = find_optional_json_value(text, key);
    if (!position.has_value()) return std::nullopt;
    std::uint64_t value = 0;
    const auto parsed = std::from_chars(text.data() + *position, text.data() + text.size(), value);
    if (parsed.ec != std::errc() || value > 0xffffffffULL) {
        throw std::invalid_argument("invalid JSON uint32 field: " + std::string(key));
    }
    return static_cast<std::uint32_t>(value);
}

std::optional<double> optional_json_number(std::string_view text, std::string_view key) {
    const auto position = find_optional_json_value(text, key);
    if (!position.has_value()) return std::nullopt;
    double value = 0.0;
    const auto parsed = std::from_chars(text.data() + *position, text.data() + text.size(), value);
    if (parsed.ec != std::errc() || !std::isfinite(value)) {
        throw std::invalid_argument("invalid JSON number field: " + std::string(key));
    }
    return value;
}

std::optional<std::string> optional_json_string(std::string_view text, std::string_view key) {
    const auto position = find_optional_json_value(text, key);
    if (!position.has_value()) return std::nullopt;
    if (*position >= text.size() || text[*position] != '"') {
        throw std::invalid_argument("JSON field must be a string: " + std::string(key));
    }
    const auto end = text.find('"', *position + 1);
    if (end == std::string_view::npos) {
        throw std::invalid_argument("unterminated JSON string field: " + std::string(key));
    }
    return std::string(text.substr(*position + 1, end - *position - 1));
}

std::optional<std::vector<std::int32_t>> optional_json_id_array(
    std::string_view text,
    std::string_view key
) {
    if (!find_optional_json_value(text, key).has_value()) return std::nullopt;
    return json_id_array(text, key);
}

PocketKind parse_pocket_kind(std::string_view text) {
    if (text == "none") return PocketKind::none;
    if (text == "trinket") return PocketKind::trinket;
    if (text == "card") return PocketKind::card;
    if (text == "pill") return PocketKind::pill;
    throw std::invalid_argument("invalid pocket_kind");
}

SortKey parse_sort_key(std::string_view text) {
    if (text == "seed") return SortKey::seed;
    if (text == "health") return SortKey::health;
    if (text == "damage") return SortKey::damage;
    if (text == "move_speed") return SortKey::move_speed;
    if (text == "tears") return SortKey::tears;
    if (text == "range") return SortKey::range;
    if (text == "shot_speed") return SortKey::shot_speed;
    if (text == "luck") return SortKey::luck;
    if (text == "coins") return SortKey::coins;
    if (text == "keys") return SortKey::keys;
    if (text == "bombs") return SortKey::bombs;
    if (text == "active_quality") return SortKey::active_quality;
    if (text == "passive_quality") return SortKey::passive_quality;
    if (text == "total_quality") return SortKey::total_quality;
    throw std::invalid_argument("invalid sort_key");
}

ExperimentalTreatmentDirection parse_treatment_direction(std::string_view text) {
    if (text == "up") return ExperimentalTreatmentDirection::up;
    if (text == "down") return ExperimentalTreatmentDirection::down;
    if (text == "unchanged") return ExperimentalTreatmentDirection::unchanged;
    throw std::invalid_argument("invalid Experimental Treatment direction");
}

SortDirection parse_sort_direction(std::string_view text) {
    if (text == "asc") return SortDirection::ascending;
    if (text == "desc") return SortDirection::descending;
    throw std::invalid_argument("invalid sort_direction");
}

void select_pocket_kind(EdenCriteria& criteria, PocketKind kind, std::string_view field) {
    if (criteria.pocket_kind.has_value() && *criteria.pocket_kind != kind) {
        throw std::invalid_argument("conflicting pocket kind in JSON field: " + std::string(field));
    }
    criteria.pocket_kind = kind;
}

EdenCriteria json_criteria(std::string_view body) {
    EdenCriteria criteria;
    if (const auto kind = optional_json_string(body, "pocket_kind")) {
        criteria.pocket_kind = parse_pocket_kind(*kind);
    }
    if (const auto legacy = optional_json_u32(body, "trinket_id")) {
        select_pocket_kind(criteria, PocketKind::trinket, "trinket_id");
        criteria.pocket_ids.any_of = {static_cast<std::int32_t>(*legacy)};
    }
    if (const auto values = optional_json_id_array(body, "trinket_ids")) {
        select_pocket_kind(criteria, PocketKind::trinket, "trinket_ids");
        criteria.pocket_ids.any_of = *values;
    }
    if (const auto values = optional_json_id_array(body, "card_ids")) {
        select_pocket_kind(criteria, PocketKind::card, "card_ids");
        criteria.pocket_ids.any_of = *values;
    }
    if (const auto values = optional_json_id_array(body, "pill_effect_ids")) {
        select_pocket_kind(criteria, PocketKind::pill, "pill_effect_ids");
        criteria.pocket_ids.any_of = *values;
    }
    if (const auto values = optional_json_id_array(body, "pocket_ids")) {
        criteria.pocket_ids.any_of = *values;
    }
    if (const auto values = optional_json_id_array(body, "pocket_exclude_ids")) {
        criteria.pocket_ids.none_of = *values;
    }
    if (const auto values = optional_json_id_array(body, "active_ids")) {
        criteria.active_items.any_of = *values;
    }
    if (const auto values = optional_json_id_array(body, "active_exclude_ids")) {
        criteria.active_items.none_of = *values;
    }
    if (const auto values = optional_json_id_array(body, "passive_ids")) {
        criteria.passive_items.any_of = *values;
    }
    if (const auto values = optional_json_id_array(body, "passive_exclude_ids")) {
        criteria.passive_items.none_of = *values;
    }

    const auto range = [&](std::string_view prefix, NumberRange& target) {
        target.minimum = optional_json_number(body, std::string(prefix) + "_min");
        target.maximum = optional_json_number(body, std::string(prefix) + "_max");
    };
    range("red_hearts", criteria.red_hearts);
    range("soul_hearts", criteria.soul_hearts);
    range("coins", criteria.coins);
    range("keys", criteria.keys);
    range("bombs", criteria.bombs);
    range("damage", criteria.damage);
    range("move_speed", criteria.move_speed);
    range("tears", criteria.tears);
    range("range", criteria.range);
    range("shot_speed", criteria.shot_speed);
    range("luck", criteria.luck);
    range("damage_delta", criteria.damage_delta);
    range("move_speed_delta", criteria.move_speed_delta);
    range("tears_delta", criteria.tears_delta);
    range("shot_speed_delta", criteria.shot_speed_delta);
    range("luck_delta", criteria.luck_delta);
    range("post_damage", criteria.post_damage);
    range("post_move_speed", criteria.post_move_speed);
    range("post_tears", criteria.post_tears);
    range("post_range", criteria.post_range);
    range("post_shot_speed", criteria.post_shot_speed);
    range("post_luck", criteria.post_luck);
    constexpr std::array treatment_fields{
        "experimental_health", "experimental_move_speed", "experimental_tears",
        "experimental_damage", "experimental_range", "experimental_shot_speed",
        "experimental_luck",
    };
    for (std::size_t index = 0; index < treatment_fields.size(); ++index) {
        if (const auto value = optional_json_string(body, treatment_fields[index])) {
            criteria.experimental_treatment_directions[index] = parse_treatment_direction(*value);
        }
    }
    criteria.validate();
    return criteria;
}

void append_start_json(std::ostream& output, const EdenStart& start, std::string_view label) {
    output << "{\"seed\":\"" << json_escape(label)
           << "\",\"seed_u32\":" << start.seed
           << ",\"a5\":" << start.a5
           << ",\"p988\":" << start.p988
           << ",\"pocket_kind\":\"" << pocket_kind_name(start.pocket_kind)
           << "\",\"pocket_id\":" << start.pocket_id
           << ",\"pill_color\":" << start.pill_color
           << ",\"trinket_id\":" << (start.pocket_kind == PocketKind::trinket ? start.pocket_id : 0)
           << ",\"active_id\":" << start.active_id
           << ",\"passive_id\":" << start.passive_id
           << ",\"active_quality\":" << start.active_quality
           << ",\"passive_quality\":" << start.passive_quality
           << ",\"total_quality\":" << start.active_quality + start.passive_quality
           << ",\"red_hearts\":" << start.red_hearts
           << ",\"soul_hearts\":" << start.soul_hearts
           << ",\"coins\":" << start.coins
           << ",\"keys\":" << start.keys
           << ",\"bombs\":" << start.bombs
           << ",\"damage\":" << start.damage
           << ",\"move_speed\":" << start.move_speed
           << ",\"tears\":" << start.tears
           << ",\"range\":" << start.range
           << ",\"shot_speed\":" << start.shot_speed
           << ",\"luck\":" << start.luck
           << ",\"damage_delta\":" << start.damage_delta
           << ",\"move_speed_delta\":" << start.move_speed_delta
           << ",\"tears_delta\":" << start.tears_delta
           << ",\"shot_speed_delta\":" << start.shot_speed_delta
           << ",\"luck_delta\":" << start.luck_delta
           << ",\"post_item_stats_available\":"
           << (start.post_item_stats_available ? "true" : "false")
           << ",\"experimental_treatment_up_mask\":"
           << static_cast<unsigned>(start.experimental_treatment_up_mask)
           << ",\"experimental_treatment_down_mask\":"
           << static_cast<unsigned>(start.experimental_treatment_down_mask)
           << ",\"post_damage\":" << start.post_damage
           << ",\"post_move_speed\":" << start.post_move_speed
           << ",\"post_tears\":" << start.post_tears
           << ",\"post_range\":" << start.post_range
           << ",\"post_shot_speed\":" << start.post_shot_speed
           << ",\"post_luck\":" << start.post_luck << '}';
}

class SearchSession {
public:
    ~SearchSession() {
        cancel_.store(true, std::memory_order_relaxed);
        join_previous();
    }

    void start(EdenCriteria criteria, SearchOptions options) {
        if (state_.load(std::memory_order_acquire) == SessionState::running) {
            throw std::runtime_error("a search is already running");
        }
        join_previous();
        criteria.validate();
        cancel_.store(false, std::memory_order_relaxed);
        scanned_.store(0, std::memory_order_relaxed);
        total_.store(static_cast<std::uint64_t>(options.end) - options.start + 1U, std::memory_order_relaxed);
        match_count_.store(0, std::memory_order_relaxed);
        elapsed_millis_.store(0, std::memory_order_relaxed);
        {
            std::lock_guard lock(mutex_);
            result_ = {};
            error_.clear();
        }
        state_.store(SessionState::running, std::memory_order_release);
        try {
            worker_ = std::thread([this, criteria = std::move(criteria), options] {
                try {
                    const auto tables = builtin_j460_profile();
                    auto result = search(
                        tables,
                        criteria,
                        options,
                        [this](const SearchProgress& progress) {
                            scanned_.store(progress.scanned, std::memory_order_relaxed);
                            total_.store(progress.total, std::memory_order_relaxed);
                            match_count_.store(progress.matches, std::memory_order_relaxed);
                            elapsed_millis_.store(
                                static_cast<std::uint64_t>(progress.elapsed_seconds * 1000.0),
                                std::memory_order_relaxed
                            );
                        },
                        &cancel_
                    );
                    scanned_.store(result.scanned, std::memory_order_relaxed);
                    match_count_.store(result.total_matches, std::memory_order_relaxed);
                    elapsed_millis_.store(
                        static_cast<std::uint64_t>(result.elapsed_seconds * 1000.0),
                        std::memory_order_relaxed
                    );
                    {
                        std::lock_guard lock(mutex_);
                        result_ = std::move(result);
                    }
                    state_.store(
                        cancel_.load(std::memory_order_relaxed) ? SessionState::cancelled : SessionState::completed,
                        std::memory_order_release
                    );
                } catch (const std::exception& error) {
                    {
                        std::lock_guard lock(mutex_);
                        error_ = error.what();
                    }
                    state_.store(SessionState::failed, std::memory_order_release);
                }
            });
        } catch (const std::exception& error) {
            {
                std::lock_guard lock(mutex_);
                error_ = error.what();
            }
            state_.store(SessionState::failed, std::memory_order_release);
            throw;
        }
    }

    void cancel() noexcept { cancel_.store(true, std::memory_order_relaxed); }

    bool running() const noexcept {
        return state_.load(std::memory_order_acquire) == SessionState::running;
    }

    std::string status_json() const {
        const auto state = state_.load(std::memory_order_acquire);
        const auto total = total_.load(std::memory_order_relaxed);
        const auto scanned = scanned_.load(std::memory_order_relaxed);
        std::string error;
        {
            std::lock_guard lock(mutex_);
            error = error_;
        }
        std::ostringstream output;
        output << "{\"state\":\"" << state_name(state)
               << "\",\"scanned\":" << scanned
               << ",\"total\":" << total
               << ",\"progress\":" << (total == 0 ? 0.0 : 100.0 * static_cast<double>(scanned) / total)
               << ",\"matches\":" << match_count_.load(std::memory_order_relaxed)
               << ",\"elapsed_seconds\":" << std::fixed << std::setprecision(3)
               << elapsed_millis_.load(std::memory_order_relaxed) / 1000.0
               << ",\"error\":\"" << json_escape(error) << "\"}";
        return output.str();
    }

    std::string results_json() const {
        std::lock_guard lock(mutex_);
        std::ostringstream output;
        output << std::setprecision(10);
        output << "{\"count\":" << result_.matches.size()
               << ",\"total_count\":" << result_.total_matches
               << ",\"truncated\":" << (result_.truncated() ? "true" : "false")
               << ",\"sort_key\":\"" << sort_key_name(result_.sort_key)
               << "\",\"sort_direction\":\"" << sort_direction_name(result_.sort_direction)
               << "\",\"result_limit\":" << result_.result_limit
               << ",\"matches\":[";
        for (std::size_t index = 0; index < result_.matches.size(); ++index) {
            const auto& match = result_.matches[index];
            if (index != 0) output << ',';
            append_start_json(output, match.start, match.label);
        }
        output << "]}";
        return output.str();
    }

    std::string results_text() const {
        std::lock_guard lock(mutex_);
        std::ostringstream output;
        output << std::setprecision(10)
               << "seed\tseed_u32\tpocket_kind\tpocket_id\tpill_color\tactive_id\tpassive_id"
                  "\tactive_quality\tpassive_quality\ttotal_quality"
                  "\tred_hearts\tsoul_hearts\tcoins\tkeys\tbombs\tdamage\tmove_speed\ttears\trange"
                  "\tshot_speed\tluck\tdamage_delta\tmove_speed_delta\ttears_delta"
                  "\tshot_speed_delta\tluck_delta\tpost_item_stats_available"
                  "\texperimental_treatment_up_mask\texperimental_treatment_down_mask"
                  "\tpost_damage\tpost_move_speed\tpost_tears\tpost_range\tpost_shot_speed\tpost_luck\n";
        for (const auto& match : result_.matches) {
            output << match.label << '\t' << match.start.seed
                   << '\t' << pocket_kind_name(match.start.pocket_kind) << '\t' << match.start.pocket_id
                   << '\t' << match.start.pill_color
                   << '\t' << match.start.active_id << '\t' << match.start.passive_id
                   << '\t' << match.start.active_quality << '\t' << match.start.passive_quality
                   << '\t' << match.start.active_quality + match.start.passive_quality
                   << '\t' << match.start.red_hearts << '\t' << match.start.soul_hearts
                   << '\t' << match.start.coins << '\t' << match.start.keys
                   << '\t' << match.start.bombs
                   << '\t' << match.start.damage << '\t' << match.start.move_speed
                   << '\t' << match.start.tears << '\t' << match.start.range
                   << '\t' << match.start.shot_speed << '\t' << match.start.luck
                   << '\t' << match.start.damage_delta << '\t' << match.start.move_speed_delta
                   << '\t' << match.start.tears_delta
                   << '\t' << match.start.shot_speed_delta << '\t' << match.start.luck_delta
                   << '\t' << (match.start.post_item_stats_available ? 1 : 0)
                   << '\t' << static_cast<unsigned>(match.start.experimental_treatment_up_mask)
                   << '\t' << static_cast<unsigned>(match.start.experimental_treatment_down_mask)
                   << '\t' << match.start.post_damage << '\t' << match.start.post_move_speed
                   << '\t' << match.start.post_tears << '\t' << match.start.post_range
                   << '\t' << match.start.post_shot_speed << '\t' << match.start.post_luck << '\n';
        }
        return output.str();
    }

private:
    void join_previous() {
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    mutable std::mutex mutex_;
    std::thread worker_;
    std::atomic_bool cancel_{false};
    std::atomic<SessionState> state_{SessionState::idle};
    std::atomic<std::uint64_t> scanned_{0};
    std::atomic<std::uint64_t> total_{0};
    std::atomic<std::uint64_t> match_count_{0};
    std::atomic<std::uint64_t> elapsed_millis_{0};
    SearchResult result_;
    std::string error_;
};

std::string load_resource(int identifier) {
    const auto module = GetModuleHandleW(nullptr);
    const auto resource = FindResourceW(module, MAKEINTRESOURCEW(identifier), MAKEINTRESOURCEW(10));
    if (resource == nullptr) {
        throw std::runtime_error("embedded web resource is missing");
    }
    const auto loaded = LoadResource(module, resource);
    const auto size = SizeofResource(module, resource);
    const auto data = static_cast<const char*>(LockResource(loaded));
    if (data == nullptr) {
        throw std::runtime_error("cannot load embedded web resource");
    }
    return std::string(data, size);
}

std::string read_binary_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open local game asset");
    }
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

std::optional<std::filesystem::path> read_registry_path(
    HKEY root,
    const wchar_t* subkey,
    const wchar_t* value_name
) {
    DWORD size = 0;
    if (RegGetValueW(root, subkey, value_name, RRF_RT_REG_SZ, nullptr, nullptr, &size) != ERROR_SUCCESS
        || size < sizeof(wchar_t)) {
        return std::nullopt;
    }
    std::vector<wchar_t> value(size / sizeof(wchar_t) + 1, L'\0');
    if (RegGetValueW(root, subkey, value_name, RRF_RT_REG_SZ, nullptr, value.data(), &size) != ERROR_SUCCESS) {
        return std::nullopt;
    }
    return std::filesystem::path(value.data());
}

std::optional<std::filesystem::path> environment_path(const wchar_t* name) {
    const DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
    if (size == 0) return std::nullopt;
    std::vector<wchar_t> value(size, L'\0');
    if (GetEnvironmentVariableW(name, value.data(), size) == 0) return std::nullopt;
    return std::filesystem::path(value.data());
}

void append_unique_path(
    std::vector<std::filesystem::path>& paths,
    const std::filesystem::path& candidate
) {
    if (candidate.empty()) return;
    const auto normalized = candidate.lexically_normal();
    if (std::find(paths.begin(), paths.end(), normalized) == paths.end()) {
        paths.push_back(normalized);
    }
}

std::vector<std::filesystem::path> steam_library_roots(const std::filesystem::path& steam_root) {
    std::vector<std::filesystem::path> roots;
    append_unique_path(roots, steam_root);
    const auto libraries = steam_root / "steamapps" / "libraryfolders.vdf";
    std::ifstream input(libraries, std::ios::binary);
    std::string line;
    while (std::getline(input, line)) {
        const auto key = line.find("\"path\"");
        if (key == std::string::npos) continue;
        const auto key_end = line.find('"', key + 1);
        const auto value_start = line.find('"', key_end + 1);
        const auto value_end = value_start == std::string::npos
            ? std::string::npos
            : line.find('"', value_start + 1);
        if (value_start == std::string::npos || value_end == std::string::npos) continue;
        std::string value = line.substr(value_start + 1, value_end - value_start - 1);
        for (std::size_t index = 0; index + 1 < value.size();) {
            if (value[index] == '\\' && value[index + 1] == '\\') {
                value.erase(index, 1);
            } else {
                ++index;
            }
        }
        append_unique_path(roots, std::filesystem::path(value));
    }
    return roots;
}

std::optional<std::filesystem::path> locate_game_resource_root() {
    constexpr auto game_folder = "The Binding of Isaac Rebirth";
    std::vector<std::filesystem::path> direct_candidates;
    if (const auto configured = environment_path(L"ISAAC_GAME_DIR")) {
        append_unique_path(direct_candidates, *configured);
    }

    wchar_t executable_buffer[32'768]{};
    const DWORD executable_size = GetModuleFileNameW(nullptr, executable_buffer, 32'768);
    if (executable_size > 0 && executable_size < 32'768) {
        auto ancestor = std::filesystem::path(executable_buffer).parent_path();
        for (int depth = 0; depth < 5 && !ancestor.empty(); ++depth) {
            append_unique_path(direct_candidates, ancestor);
            ancestor = ancestor.parent_path();
        }
    }

    std::vector<std::filesystem::path> steam_roots;
    const auto add_registry_root = [&](HKEY root, const wchar_t* key, const wchar_t* value) {
        if (const auto path = read_registry_path(root, key, value)) append_unique_path(steam_roots, *path);
    };
    add_registry_root(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath");
    add_registry_root(HKEY_LOCAL_MACHINE, L"Software\\WOW6432Node\\Valve\\Steam", L"InstallPath");
    if (const auto program_files_x86 = environment_path(L"ProgramFiles(x86)")) {
        append_unique_path(steam_roots, *program_files_x86 / "Steam");
    }
    if (const auto program_files = environment_path(L"ProgramFiles")) {
        append_unique_path(steam_roots, *program_files / "Steam");
    }
    for (const auto& steam_root : steam_roots) {
        for (const auto& library : steam_library_roots(steam_root)) {
            append_unique_path(
                direct_candidates,
                library / "steamapps" / "common" / game_folder
            );
        }
    }

    for (const auto& game_root : direct_candidates) {
        const auto resources = game_root / "extracted_resources" / "resources";
        if (std::filesystem::is_directory(resources / "gfx" / "items" / "collectibles")
            && std::filesystem::is_directory(resources / "gfx" / "items" / "trinkets")) {
            return resources;
        }
    }
    return std::nullopt;
}

class GameIconCatalog {
public:
    GameIconCatalog() {
        const auto root = locate_game_resource_root();
        if (!root) return;
        resource_root_ = *root;
        index_directory(
            resource_root_ / "gfx" / "items" / "collectibles",
            "collectibles_",
            collectible_icons_
        );
        index_directory(
            resource_root_ / "gfx" / "items" / "trinkets",
            "trinket_",
            trinket_icons_
        );
    }

    std::optional<std::filesystem::path> find(std::string_view kind, std::int32_t id) const {
        const auto& icons = kind == "trinket" ? trinket_icons_ : collectible_icons_;
        if (kind != "trinket" && kind != "active" && kind != "passive") return std::nullopt;
        const auto found = icons.find(id);
        if (found == icons.end()) return std::nullopt;
        return found->second;
    }

    std::optional<std::filesystem::path> find_ui(std::string_view name) const {
        if (resource_root_.empty()) return std::nullopt;
        std::filesystem::path relative;
        if (name == "basement-floor") {
            relative = std::filesystem::path("gfx") / "backdrop" / "01_lbasementfloor.png";
        } else if (name == "basement-walls") {
            relative = std::filesystem::path("gfx") / "backdrop" / "01_basement.png";
        } else if (name == "seed-paper") {
            relative = std::filesystem::path("gfx") / "ui" / "seed paper.png";
        } else if (name == "seed-entry") {
            relative = std::filesystem::path("gfx") / "ui" / "main menu" / "seedentry.png";
        } else {
            return std::nullopt;
        }
        const auto candidate = resource_root_ / relative;
        return std::filesystem::is_regular_file(candidate)
            ? std::optional<std::filesystem::path>(candidate)
            : std::nullopt;
    }

    bool available() const noexcept {
        return !collectible_icons_.empty() && !trinket_icons_.empty();
    }

    std::string status_json() const {
        std::ostringstream output;
        output << "{\"item_icons\":" << (available() ? "true" : "false")
               << ",\"collectible_icons\":" << collectible_icons_.size()
               << ",\"trinket_icons\":" << trinket_icons_.size()
               << ",\"basement_texture\":" << (find_ui("basement-floor") ? "true" : "false")
               << ",\"basement_walls\":" << (find_ui("basement-walls") ? "true" : "false")
               << ",\"seed_paper\":" << (find_ui("seed-paper") ? "true" : "false") << '}';
        return output.str();
    }

private:
    static void index_directory(
        const std::filesystem::path& directory,
        std::string_view prefix,
        std::unordered_map<std::int32_t, std::filesystem::path>& output
    ) {
        std::error_code error;
        for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
            if (error || !entry.is_regular_file() || entry.path().extension() != ".png") continue;
            const auto stem = entry.path().stem().string();
            if (!stem.starts_with(prefix)) continue;
            const auto digits_start = prefix.size();
            const auto digits_end = stem.find('_', digits_start);
            if (digits_end == std::string::npos) continue;
            std::int32_t id = 0;
            const auto parsed = std::from_chars(
                stem.data() + digits_start,
                stem.data() + digits_end,
                id
            );
            if (parsed.ec == std::errc() && parsed.ptr == stem.data() + digits_end && id > 0) {
                output.try_emplace(id, entry.path());
            }
        }
    }

    std::filesystem::path resource_root_;
    std::unordered_map<std::int32_t, std::filesystem::path> collectible_icons_;
    std::unordered_map<std::int32_t, std::filesystem::path> trinket_icons_;
};

struct IconRequest {
    std::string kind;
    std::int32_t id = 0;
};

std::optional<IconRequest> parse_icon_request(std::string_view path) {
    constexpr std::string_view prefix = "/game-assets/";
    constexpr std::string_view suffix = ".png";
    if (!path.starts_with(prefix) || !path.ends_with(suffix)) return std::nullopt;
    path.remove_prefix(prefix.size());
    path.remove_suffix(suffix.size());
    const auto slash = path.find('/');
    if (slash == std::string_view::npos || path.find('/', slash + 1) != std::string_view::npos) {
        return std::nullopt;
    }
    IconRequest request{std::string(path.substr(0, slash)), 0};
    const auto id_text = path.substr(slash + 1);
    const auto parsed = std::from_chars(id_text.data(), id_text.data() + id_text.size(), request.id);
    if (parsed.ec != std::errc() || parsed.ptr != id_text.data() + id_text.size() || request.id <= 0) {
        return std::nullopt;
    }
    return request;
}

struct Request {
    std::string method;
    std::string path;
    std::string body;
    std::string session_token;
};

std::string lower_ascii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

std::string header_value(std::string_view headers, std::string_view name) {
    const std::string needle = std::string(name) + ':';
    auto position = headers.find(needle);
    if (position == std::string_view::npos) {
        return {};
    }
    position += needle.size();
    while (position < headers.size() && (headers[position] == ' ' || headers[position] == '\t')) {
        ++position;
    }
    const auto end = headers.find("\r\n", position);
    return std::string(headers.substr(position, end == std::string_view::npos ? headers.size() - position : end - position));
}

std::string make_session_token() {
    std::random_device random;
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (int index = 0; index < 4; ++index) {
        output << std::setw(8) << static_cast<std::uint32_t>(random());
    }
    return output.str();
}

Request receive_request(SOCKET client) {
    std::string data;
    char buffer[8192];
    std::size_t header_end = std::string::npos;
    while (header_end == std::string::npos) {
        const auto received = recv(client, buffer, sizeof(buffer), 0);
        if (received <= 0) {
            throw std::runtime_error("client disconnected before sending headers");
        }
        data.append(buffer, static_cast<std::size_t>(received));
        if (data.size() > 65'536) {
            throw std::runtime_error("HTTP request is too large");
        }
        header_end = data.find("\r\n\r\n");
    }
    const auto first_line_end = data.find("\r\n");
    const auto first_space = data.find(' ');
    const auto second_space = data.find(' ', first_space + 1);
    if (first_space == std::string::npos || second_space == std::string::npos || second_space > first_line_end) {
        throw std::runtime_error("malformed HTTP request line");
    }
    Request request;
    request.method = data.substr(0, first_space);
    request.path = data.substr(first_space + 1, second_space - first_space - 1);

    std::size_t content_length = 0;
    const auto headers_lower = lower_ascii(data.substr(first_line_end + 2, header_end - first_line_end - 2));
    request.session_token = header_value(headers_lower, "x-isaac-token");
    const auto length_key = headers_lower.find("content-length:");
    if (length_key != std::string::npos) {
        auto value_start = length_key + 15;
        while (value_start < headers_lower.size() && headers_lower[value_start] == ' ') ++value_start;
        const auto parsed = std::from_chars(
            headers_lower.data() + value_start,
            headers_lower.data() + headers_lower.size(),
            content_length
        );
        if (parsed.ec != std::errc()) {
            throw std::runtime_error("invalid Content-Length");
        }
    }
    if (content_length > 65'536) {
        throw std::runtime_error("HTTP request body is too large");
    }
    const auto body_start = header_end + 4;
    while (data.size() - body_start < content_length) {
        const auto received = recv(client, buffer, sizeof(buffer), 0);
        if (received <= 0) {
            throw std::runtime_error("client disconnected before sending body");
        }
        data.append(buffer, static_cast<std::size_t>(received));
    }
    request.body = data.substr(body_start, content_length);
    return request;
}

void send_all(SOCKET client, std::string_view data) {
    while (!data.empty()) {
        const auto sent = send(client, data.data(), static_cast<int>(std::min<std::size_t>(data.size(), INT_MAX)), 0);
        if (sent <= 0) {
            return;
        }
        data.remove_prefix(static_cast<std::size_t>(sent));
    }
}

void respond(
    SOCKET client,
    int status,
    std::string_view status_text,
    std::string_view content_type,
    std::string body,
    std::string_view extra_headers = {}
) {
    std::ostringstream headers;
    headers << "HTTP/1.1 " << status << ' ' << status_text << "\r\n"
            << "Content-Type: " << content_type << "\r\n"
            << "Content-Length: " << body.size() << "\r\n"
            << "Cache-Control: no-store\r\n"
            << "Connection: close\r\n"
            << extra_headers << "\r\n";
    const auto header_text = headers.str();
    send_all(client, header_text);
    send_all(client, body);
}

std::string profile_json(const GameIconCatalog& game_icons) {
    const auto& profile = builtin_j460_profile_info();
    std::ostringstream output;
    output << "{\"id\":\"" << profile.id
           << "\",\"game_version\":\"" << profile.game_version
           << "\",\"game_build\":\"" << profile.game_build
           << "\",\"proc_source_sha256\":\"" << profile.source_collectible_table_sha256
           << "\",\"trinket_pool_source_sha256\":\"" << profile.source_trinket_pool_sha256
           << "\",\"proc_semantic_sha256\":\"" << profile.collectible_semantic_sha256
           << "\",\"trinket_pool_semantic_sha256\":\"" << profile.trinket_semantic_sha256
           << "\",\"local_game_icons\":" << (game_icons.available() ? "true" : "false")
           << '}';
    return output.str();
}

}  // namespace

int run_local_web_app(bool open_browser) {
    WSADATA winsock{};
    if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0) {
        throw std::runtime_error("WSAStartup failed");
    }
    const SOCKET server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server == INVALID_SOCKET) {
        WSACleanup();
        throw std::runtime_error("cannot create local HTTP socket");
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR
        || listen(server, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(server);
        WSACleanup();
        throw std::runtime_error("cannot bind local HTTP server");
    }
    int address_size = sizeof(address);
    getsockname(server, reinterpret_cast<sockaddr*>(&address), &address_size);
    const auto port = ntohs(address.sin_port);
    const std::string base_url = "http://127.0.0.1:" + std::to_string(port) + '/';
    const std::string session_token = make_session_token();
    const std::string url = base_url + "?token=" + session_token;

    const auto index_html = load_resource(IDR_WEB_INDEX);
    const auto style_css = load_resource(IDR_WEB_STYLE);
    const auto app_js = load_resource(IDR_WEB_APP);
    const auto item_catalog_json = load_resource(IDR_ITEM_CATALOG);
    const auto isaac_sans_font = load_resource(IDR_ISAAC_SANS_FONT);
    const auto seeker_title = load_resource(IDR_SEEKER_TITLE);
    const auto lana_pixel_font = load_resource(IDR_LANA_PIXEL_FONT);
    const auto treatment_html = load_resource(IDR_WEB_TREATMENT);
    const auto inspector_html = load_resource(IDR_WEB_INSPECTOR);
    const auto inspector_js = load_resource(IDR_WEB_INSPECTOR_APP);
    const auto daily_good_html = load_resource(IDR_WEB_DAILY_GOOD);
    const auto daily_good_js = load_resource(IDR_WEB_DAILY_GOOD_APP);
    const auto daily_bad_html = load_resource(IDR_WEB_DAILY_BAD);
    const GameIconCatalog game_icons;
    SearchSession session;
    std::cout << "Isaac Seed Seeker: " << url << std::endl;
    if (open_browser) {
        ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    bool keep_running = true;
    while (keep_running) {
        const SOCKET client = accept(server, nullptr, nullptr);
        if (client == INVALID_SOCKET) {
            break;
        }
        const DWORD socket_timeout_ms = 5'000;
        setsockopt(
            client,
            SOL_SOCKET,
            SO_RCVTIMEO,
            reinterpret_cast<const char*>(&socket_timeout_ms),
            sizeof(socket_timeout_ms)
        );
        setsockopt(
            client,
            SOL_SOCKET,
            SO_SNDTIMEO,
            reinterpret_cast<const char*>(&socket_timeout_ms),
            sizeof(socket_timeout_ms)
        );
        try {
            const auto request = receive_request(client);
            if (request.method == "POST" && request.session_token != session_token) {
                respond(client, 403, "Forbidden", "application/json; charset=utf-8", "{\"error\":\"invalid session token\"}");
            } else if (request.method == "GET" && (request.path == "/" || request.path.starts_with("/?"))) {
                respond(client, 200, "OK", "text/html; charset=utf-8", index_html);
            } else if (request.method == "GET"
                       && (request.path == "/experimental-treatment.html"
                           || request.path.starts_with("/experimental-treatment.html?"))) {
                respond(client, 200, "OK", "text/html; charset=utf-8", treatment_html);
            } else if (request.method == "GET"
                       && (request.path == "/seed-inspector.html"
                           || request.path.starts_with("/seed-inspector.html?"))) {
                respond(client, 200, "OK", "text/html; charset=utf-8", inspector_html);
            } else if (request.method == "GET"
                       && (request.path == "/daily-good.html"
                           || request.path.starts_with("/daily-good.html?"))) {
                respond(client, 200, "OK", "text/html; charset=utf-8", daily_good_html);
            } else if (request.method == "GET"
                       && (request.path == "/daily-bad.html"
                           || request.path.starts_with("/daily-bad.html?"))) {
                respond(client, 200, "OK", "text/html; charset=utf-8", daily_bad_html);
            } else if (request.method == "GET" && request.path == "/style.css") {
                respond(client, 200, "OK", "text/css; charset=utf-8", style_css);
            } else if (request.method == "GET" && request.path == "/app.js") {
                respond(client, 200, "OK", "text/javascript; charset=utf-8", app_js);
            } else if (request.method == "GET" && request.path == "/seed-inspector.js") {
                respond(client, 200, "OK", "text/javascript; charset=utf-8", inspector_js);
            } else if (request.method == "GET" && request.path == "/daily-good.js") {
                respond(client, 200, "OK", "text/javascript; charset=utf-8", daily_good_js);
            } else if (request.method == "GET" && request.path == "/assets/isaacsans.ttf") {
                respond(client, 200, "OK", "font/ttf", isaac_sans_font);
            } else if (request.method == "GET" && request.path == "/assets/isaac-seed-seeker-title.png") {
                respond(client, 200, "OK", "image/png", seeker_title);
            } else if (request.method == "GET" && request.path == "/assets/lanapixel.ttf") {
                respond(client, 200, "OK", "font/ttf", lana_pixel_font);
            } else if (request.method == "GET" && request.path == "/game-assets/ui/basement-floor.png") {
                const auto path = game_icons.find_ui("basement-floor");
                if (path) {
                    respond(client, 200, "OK", "image/png", read_binary_file(*path));
                } else {
                    respond(client, 404, "Not Found", "application/json; charset=utf-8", "{\"error\":\"asset not found\"}");
                }
            } else if (request.method == "GET" && request.path == "/game-assets/ui/basement-walls.png") {
                const auto path = game_icons.find_ui("basement-walls");
                if (path) {
                    respond(client, 200, "OK", "image/png", read_binary_file(*path));
                } else {
                    respond(client, 404, "Not Found", "application/json; charset=utf-8", "{\"error\":\"asset not found\"}");
                }
            } else if (request.method == "GET" && request.path == "/game-assets/ui/seed-paper.png") {
                const auto path = game_icons.find_ui("seed-paper");
                if (path) {
                    respond(client, 200, "OK", "image/png", read_binary_file(*path));
                } else {
                    respond(client, 404, "Not Found", "application/json; charset=utf-8", "{\"error\":\"asset not found\"}");
                }
            } else if (request.method == "GET" && request.path == "/game-assets/ui/seed-entry.png") {
                const auto path = game_icons.find_ui("seed-entry");
                if (path) {
                    respond(client, 200, "OK", "image/png", read_binary_file(*path));
                } else {
                    respond(client, 404, "Not Found", "application/json; charset=utf-8", "{\"error\":\"asset not found\"}");
                }
            } else if (request.method == "GET" && request.path == "/catalog.json") {
                respond(client, 200, "OK", "application/json; charset=utf-8", item_catalog_json);
            } else if (request.method == "GET" && request.path == "/api/v1/assets") {
                respond(client, 200, "OK", "application/json; charset=utf-8", game_icons.status_json());
            } else if (request.method == "GET" && request.path == "/api/v1/profile") {
                respond(client, 200, "OK", "application/json; charset=utf-8", profile_json(game_icons));
            } else if (request.method == "POST" && request.path == "/api/v1/inspect") {
                const auto seed_label = optional_json_string(request.body, "seed");
                const auto seed_value = optional_json_u32(request.body, "seed_u32");
                if (seed_label.has_value() == seed_value.has_value()) {
                    throw std::invalid_argument("provide exactly one of seed or seed_u32");
                }
                const auto seed = seed_label.has_value()
                    ? string_to_seed(*seed_label)
                    : *seed_value;
                const auto start = predict_eden_start(seed, builtin_j460_profile());
                std::ostringstream output;
                output << std::setprecision(10);
                append_start_json(output, start, seed_to_string(seed));
                respond(client, 200, "OK", "application/json; charset=utf-8", output.str());
            } else if (request.method == "POST" && request.path == "/api/v1/daily-good") {
                const auto date = optional_json_string(request.body, "date");
                if (!date) {
                    throw std::invalid_argument("missing JSON field: date");
                }
                DailyGoodOptions options;
                options.date_utc8 = *date;
                options.draw_variant = optional_json_u32(request.body, "variant").value_or(0U);
                const auto result = select_daily_good_v1(builtin_j460_profile(), options);
                std::ostringstream output;
                output << "{\"rules_version\":\"" << result.rules_version
                       << "\",\"date\":\"" << json_escape(result.date_utc8)
                       << "\",\"seed\":\"" << seed_to_string(result.primary.seed)
                       << "\",\"seed_u32\":" << result.primary.seed
                       << ",\"variant\":" << options.draw_variant << '}';
                respond(client, 200, "OK", "application/json; charset=utf-8", output.str());
            } else if (request.method == "POST" && request.path == "/api/v1/daily-bad") {
                const auto date = optional_json_string(request.body, "date");
                if (!date) {
                    throw std::invalid_argument("missing JSON field: date");
                }
                DailyGoodOptions options;
                options.date_utc8 = *date;
                options.draw_variant = optional_json_u32(request.body, "variant").value_or(0U);
                const auto result = select_daily_bad_v4(builtin_j460_profile(), options);
                std::ostringstream output;
                output << "{\"rules_version\":\"" << result.rules_version
                       << "\",\"date\":\"" << json_escape(result.date_utc8)
                       << "\",\"seed\":\"" << seed_to_string(result.primary.seed)
                       << "\",\"seed_u32\":" << result.primary.seed
                       << ",\"variant\":" << options.draw_variant << '}';
                respond(client, 200, "OK", "application/json; charset=utf-8", output.str());
            } else if (request.method == "GET" && request.path == "/api/v1/search/status") {
                respond(client, 200, "OK", "application/json; charset=utf-8", session.status_json());
            } else if (request.method == "GET" && request.path == "/api/v1/search/results") {
                respond(client, 200, "OK", "application/json; charset=utf-8", session.results_json());
            } else if (request.method == "GET" && request.path == "/api/v1/search/results.txt") {
                respond(
                    client,
                    200,
                    "OK",
                    "text/plain; charset=utf-8",
                    session.results_text(),
                    "Content-Disposition: attachment; filename=isaac-seeds.txt\r\n"
                );
            } else if (request.method == "POST" && request.path == "/api/v1/search") {
                auto criteria = json_criteria(request.body);
                SearchOptions options;
                options.start = json_u32(request.body, "start");
                options.end = json_u32(request.body, "end");
                options.threads = std::min(64U, json_u32(request.body, "threads"));
                options.block_size = 1'000'000;
                options.max_results = optional_json_u32(request.body, "max_results").value_or(1'000U);
                if (options.max_results == 0 || options.max_results > 10'000) {
                    throw std::invalid_argument("max_results must be within 1..10000");
                }
                if (const auto key = optional_json_string(request.body, "sort_key")) {
                    options.sort_key = parse_sort_key(*key);
                }
                if (const auto direction = optional_json_string(request.body, "sort_direction")) {
                    options.sort_direction = parse_sort_direction(*direction);
                }
                session.start(std::move(criteria), options);
                respond(client, 202, "Accepted", "application/json; charset=utf-8", session.status_json());
            } else if (request.method == "POST" && request.path == "/api/v1/search/cancel") {
                session.cancel();
                respond(client, 202, "Accepted", "application/json; charset=utf-8", session.status_json());
            } else if (request.method == "POST" && request.path == "/api/v1/shutdown") {
                session.cancel();
                respond(client, 200, "OK", "application/json; charset=utf-8", "{\"ok\":true}");
                keep_running = false;
            } else if (request.method == "GET") {
                const auto icon_request = parse_icon_request(request.path);
                const auto icon_path = icon_request
                    ? game_icons.find(icon_request->kind, icon_request->id)
                    : std::nullopt;
                if (icon_path) {
                    respond(client, 200, "OK", "image/png", read_binary_file(*icon_path));
                } else {
                    respond(client, 404, "Not Found", "application/json; charset=utf-8", "{\"error\":\"asset not found\"}");
                }
            } else {
                respond(client, 404, "Not Found", "application/json; charset=utf-8", "{\"error\":\"not found\"}");
            }
        } catch (const std::exception& error) {
            respond(
                client,
                400,
                "Bad Request",
                "application/json; charset=utf-8",
                "{\"error\":\"" + json_escape(error.what()) + "\"}"
            );
        }
        shutdown(client, SD_BOTH);
        closesocket(client);
    }
    closesocket(server);
    WSACleanup();
    return 0;
}

}  // namespace isaac_seed_seeker

#else

#include <stdexcept>

namespace isaac_seed_seeker {

int run_local_web_app(bool) {
    throw std::runtime_error("the local WebUI launcher currently supports Windows only");
}

}  // namespace isaac_seed_seeker

#endif
