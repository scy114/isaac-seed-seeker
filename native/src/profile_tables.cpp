#include "isaac_seed_seeker/core.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace isaac_seed_seeker {
namespace {

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open profile table: " + path.string());
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

void skip_space(std::string_view text, std::size_t& position) {
    while (position < text.size()) {
        const char c = text[position];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            break;
        }
        ++position;
    }
}

std::size_t find_named_value(std::string_view text, std::string_view name) {
    const std::string needle = "\"" + std::string(name) + "\"";
    const auto key = text.find(needle);
    if (key == std::string_view::npos) {
        throw std::runtime_error("profile JSON is missing key: " + std::string(name));
    }
    const auto colon = text.find(':', key + needle.size());
    if (colon == std::string_view::npos) {
        throw std::runtime_error("profile JSON has a malformed key: " + std::string(name));
    }
    auto value = colon + 1;
    skip_space(text, value);
    return value;
}

std::int64_t parse_integer_at(std::string_view text, std::size_t position) {
    const char* first = text.data() + position;
    const char* last = text.data() + text.size();
    std::int64_t value = 0;
    const auto result = std::from_chars(first, last, value);
    if (result.ec != std::errc()) {
        throw std::runtime_error("profile JSON contains an invalid integer");
    }
    return value;
}

std::int64_t named_integer(std::string_view text, std::string_view name) {
    return parse_integer_at(text, find_named_value(text, name));
}

bool named_boolean(std::string_view text, std::string_view name) {
    const auto position = find_named_value(text, name);
    if (text.substr(position, 4) == "true") {
        return true;
    }
    if (text.substr(position, 5) == "false") {
        return false;
    }
    throw std::runtime_error("profile JSON contains an invalid boolean for: " + std::string(name));
}

std::string_view capture_object(std::string_view text, std::size_t& position) {
    if (position >= text.size() || text[position] != '{') {
        throw std::runtime_error("expected a JSON object in entries array");
    }
    const auto start = position;
    int depth = 0;
    bool quoted = false;
    bool escaped = false;
    for (; position < text.size(); ++position) {
        const char c = text[position];
        if (quoted) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                quoted = false;
            }
            continue;
        }
        if (c == '"') {
            quoted = true;
        } else if (c == '{') {
            ++depth;
        } else if (c == '}') {
            --depth;
            if (depth == 0) {
                ++position;
                return text.substr(start, position - start);
            }
        }
    }
    throw std::runtime_error("unterminated JSON object in entries array");
}

template <typename EntryFactory>
auto parse_entries(std::string_view text, EntryFactory factory) {
    using Entry = decltype(factory(std::string_view{}));
    std::vector<Entry> entries;
    auto position = find_named_value(text, "entries");
    if (position >= text.size() || text[position] != '[') {
        throw std::runtime_error("profile JSON entries must be an array");
    }
    ++position;
    while (position < text.size()) {
        skip_space(text, position);
        if (position >= text.size()) {
            break;
        }
        if (text[position] == ']') {
            return entries;
        }
        if (text.substr(position, 4) == "null") {
            entries.emplace_back();
            position += 4;
        } else if (text[position] == '{') {
            entries.push_back(factory(capture_object(text, position)));
        } else {
            throw std::runtime_error("unsupported value in profile entries array");
        }
        skip_space(text, position);
        if (position < text.size() && text[position] == ',') {
            ++position;
        }
    }
    throw std::runtime_error("unterminated profile entries array");
}

}  // namespace

ProfileTables ProfileTables::load(
    const std::filesystem::path& collectible_table,
    const std::filesystem::path& trinket_pool
) {
    const auto collectible_json = read_file(collectible_table);
    const auto trinket_json = read_file(trinket_pool);

    ProfileTables result;
    result.collectibles = parse_entries(collectible_json, [](std::string_view object) {
        CollectibleEntry entry;
        entry.item_id = static_cast<std::int32_t>(named_integer(object, "id"));
        entry.blocked = (named_integer(object, "flag47") & 1) != 0;
        // Isaac collectible type 3 is the active-item slot in this runtime table.
        entry.active_slot = named_integer(object, "type") == 3;
        entry.present = true;
        return entry;
    });
    result.trinkets = parse_entries(trinket_json, [](std::string_view object) {
        TrinketEntry entry;
        entry.base_id = static_cast<std::int32_t>(named_integer(object, "raw") & 0x7fff);
        entry.available = named_boolean(object, "flag4") && named_boolean(object, "flag5");
        return entry;
    });
    const auto shift_right = named_integer(trinket_json, "rngShr");
    const auto shift_left = named_integer(trinket_json, "rngShl");
    const auto shift_final = named_integer(trinket_json, "rngFin");
    const auto valid_shift = [](std::int64_t value) { return value >= 1 && value <= 31; };
    if (!valid_shift(shift_right) || !valid_shift(shift_left) || !valid_shift(shift_final)) {
        throw std::runtime_error("trinket Profile RNG shifts must be within 1..31");
    }
    result.trinket_shift_right = static_cast<std::uint8_t>(shift_right);
    result.trinket_shift_left = static_cast<std::uint8_t>(shift_left);
    result.trinket_shift_final = static_cast<std::uint8_t>(shift_final);

    if (result.collectibles.size() < 2) {
        throw std::runtime_error("collectible profile table is empty");
    }
    if (result.trinkets.empty()) {
        throw std::runtime_error("trinket profile table is empty");
    }
    if (std::none_of(result.trinkets.begin(), result.trinkets.end(), [](const TrinketEntry& entry) {
            return entry.available;
        })) {
        throw std::runtime_error("trinket profile table has no available entries");
    }
    return result;
}

}  // namespace isaac_seed_seeker
