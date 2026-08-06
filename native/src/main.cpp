#include "isaac_seed_seeker/core.hpp"
#include "isaac_seed_seeker/builtin_profile.hpp"
#include "isaac_seed_seeker/local_web_app.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

namespace iss = isaac_seed_seeker;

namespace {

struct Arguments {
    std::string command;
    std::unordered_map<std::string, std::string> values;
};

Arguments parse_arguments(int argc, char** argv) {
    if (argc < 2) {
        throw std::invalid_argument("missing command");
    }
    Arguments result;
    result.command = argv[1];
    for (int index = 2; index < argc; ++index) {
        std::string key = argv[index];
        if (!key.starts_with("--") || index + 1 >= argc) {
            throw std::invalid_argument("expected --name value arguments");
        }
        result.values[key.substr(2)] = argv[++index];
    }
    return result;
}

const std::string& required(const Arguments& arguments, const std::string& name) {
    const auto found = arguments.values.find(name);
    if (found == arguments.values.end()) {
        throw std::invalid_argument("missing --" + name);
    }
    return found->second;
}

std::string optional(const Arguments& arguments, const std::string& name, std::string fallback = {}) {
    const auto found = arguments.values.find(name);
    return found == arguments.values.end() ? std::move(fallback) : found->second;
}

std::uint32_t parse_u32(const std::string& text, const std::string& name) {
    std::uint64_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc() || result.ptr != text.data() + text.size() || value > 0xffffffffULL) {
        throw std::invalid_argument("invalid uint32 for --" + name + ": " + text);
    }
    return static_cast<std::uint32_t>(value);
}

unsigned parse_unsigned(const std::string& text, const std::string& name) {
    const auto value = parse_u32(text, name);
    return static_cast<unsigned>(value);
}

std::vector<std::int32_t> parse_ids(const std::string& text, const std::string& name) {
    std::vector<std::int32_t> result;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto comma = text.find(',', start);
        const auto part = std::string_view(text).substr(
            start,
            comma == std::string::npos ? text.size() - start : comma - start
        );
        std::int32_t value = 0;
        const auto parsed = std::from_chars(part.data(), part.data() + part.size(), value);
        if (parsed.ec != std::errc() || parsed.ptr != part.data() + part.size() || value < 0) {
            throw std::invalid_argument("invalid item list for --" + name + ": " + text);
        }
        result.push_back(value);
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    return result;
}

double parse_double(const std::string& text, const std::string& name) {
    double value = 0.0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc() || result.ptr != text.data() + text.size() || !std::isfinite(value)) {
        throw std::invalid_argument("invalid number for --" + name + ": " + text);
    }
    return value;
}

bool has(const Arguments& arguments, const std::string& name) {
    return arguments.values.contains(name);
}

iss::PocketKind parse_pocket_kind(const std::string& text) {
    if (text == "none") return iss::PocketKind::none;
    if (text == "trinket") return iss::PocketKind::trinket;
    if (text == "card") return iss::PocketKind::card;
    if (text == "pill") return iss::PocketKind::pill;
    throw std::invalid_argument("invalid --pocket-kind: " + text);
}

iss::SortKey parse_sort_key(const std::string& text) {
    if (text == "seed") return iss::SortKey::seed;
    if (text == "health") return iss::SortKey::health;
    if (text == "damage") return iss::SortKey::damage;
    if (text == "move-speed") return iss::SortKey::move_speed;
    if (text == "tears") return iss::SortKey::tears;
    if (text == "range") return iss::SortKey::range;
    if (text == "shot-speed") return iss::SortKey::shot_speed;
    if (text == "luck") return iss::SortKey::luck;
    if (text == "coins") return iss::SortKey::coins;
    if (text == "keys") return iss::SortKey::keys;
    if (text == "bombs") return iss::SortKey::bombs;
    if (text == "active-quality") return iss::SortKey::active_quality;
    if (text == "passive-quality") return iss::SortKey::passive_quality;
    if (text == "total-quality") return iss::SortKey::total_quality;
    throw std::invalid_argument("invalid --sort: " + text);
}

iss::ExperimentalTreatmentDirection parse_treatment_direction(
    const std::string& text,
    const std::string& option
) {
    if (text == "up") return iss::ExperimentalTreatmentDirection::up;
    if (text == "down") return iss::ExperimentalTreatmentDirection::down;
    if (text == "unchanged") return iss::ExperimentalTreatmentDirection::unchanged;
    throw std::invalid_argument("invalid --" + option + ": expected up|down|unchanged");
}

iss::SortDirection parse_sort_direction(const std::string& text) {
    if (text == "asc") return iss::SortDirection::ascending;
    if (text == "desc") return iss::SortDirection::descending;
    throw std::invalid_argument("invalid --direction: " + text);
}

void select_pocket_kind(iss::EdenCriteria& criteria, iss::PocketKind kind, const std::string& option) {
    if (criteria.pocket_kind.has_value() && *criteria.pocket_kind != kind) {
        throw std::invalid_argument("conflicting pocket kind from --" + option);
    }
    criteria.pocket_kind = kind;
}

iss::EdenCriteria parse_criteria(const Arguments& arguments) {
    iss::EdenCriteria criteria;
    if (has(arguments, "pocket-kind")) {
        criteria.pocket_kind = parse_pocket_kind(required(arguments, "pocket-kind"));
    }
    if (has(arguments, "trinket")) {
        select_pocket_kind(criteria, iss::PocketKind::trinket, "trinket");
        criteria.pocket_ids.any_of = parse_ids(required(arguments, "trinket"), "trinket");
    }
    if (has(arguments, "card")) {
        select_pocket_kind(criteria, iss::PocketKind::card, "card");
        criteria.pocket_ids.any_of = parse_ids(required(arguments, "card"), "card");
    }
    if (has(arguments, "pill")) {
        select_pocket_kind(criteria, iss::PocketKind::pill, "pill");
        criteria.pocket_ids.any_of = parse_ids(required(arguments, "pill"), "pill");
    }
    if (has(arguments, "pocket")) {
        criteria.pocket_ids.any_of = parse_ids(required(arguments, "pocket"), "pocket");
    }
    if (has(arguments, "pocket-exclude")) {
        criteria.pocket_ids.none_of = parse_ids(required(arguments, "pocket-exclude"), "pocket-exclude");
    }
    if (has(arguments, "active")) {
        criteria.active_items.any_of = parse_ids(required(arguments, "active"), "active");
    }
    if (has(arguments, "active-exclude")) {
        criteria.active_items.none_of = parse_ids(required(arguments, "active-exclude"), "active-exclude");
    }
    if (has(arguments, "passive")) {
        criteria.passive_items.any_of = parse_ids(required(arguments, "passive"), "passive");
    }
    if (has(arguments, "passive-exclude")) {
        criteria.passive_items.none_of = parse_ids(required(arguments, "passive-exclude"), "passive-exclude");
    }

    const auto range = [&](const std::string& prefix, iss::NumberRange& target) {
        if (has(arguments, prefix + "-min")) {
            target.minimum = parse_double(required(arguments, prefix + "-min"), prefix + "-min");
        }
        if (has(arguments, prefix + "-max")) {
            target.maximum = parse_double(required(arguments, prefix + "-max"), prefix + "-max");
        }
    };
    range("red-hearts", criteria.red_hearts);
    range("soul-hearts", criteria.soul_hearts);
    range("coins", criteria.coins);
    range("keys", criteria.keys);
    range("bombs", criteria.bombs);
    range("damage", criteria.damage);
    range("move-speed", criteria.move_speed);
    range("tears", criteria.tears);
    range("range", criteria.range);
    range("shot-speed", criteria.shot_speed);
    range("luck", criteria.luck);
    range("damage-delta", criteria.damage_delta);
    range("move-speed-delta", criteria.move_speed_delta);
    range("tears-delta", criteria.tears_delta);
    range("shot-speed-delta", criteria.shot_speed_delta);
    range("luck-delta", criteria.luck_delta);
    range("post-damage", criteria.post_damage);
    range("post-move-speed", criteria.post_move_speed);
    range("post-tears", criteria.post_tears);
    range("post-range", criteria.post_range);
    range("post-shot-speed", criteria.post_shot_speed);
    range("post-luck", criteria.post_luck);
    constexpr std::array treatment_options{
        "experimental-health", "experimental-move-speed", "experimental-tears",
        "experimental-damage", "experimental-range", "experimental-shot-speed",
        "experimental-luck",
    };
    for (std::size_t index = 0; index < treatment_options.size(); ++index) {
        const std::string option = treatment_options[index];
        if (has(arguments, option)) {
            criteria.experimental_treatment_directions[index] = parse_treatment_direction(
                required(arguments, option),
                option
            );
        }
    }
    criteria.validate();
    return criteria;
}

iss::ProfileTables load_tables(const Arguments& arguments) {
    const auto proc = optional(arguments, "proc");
    const auto trinkets = optional(arguments, "trinkets");
    if (proc.empty() && trinkets.empty()) {
        return iss::builtin_j460_profile();
    }
    if (proc.empty() || trinkets.empty()) {
        throw std::invalid_argument("--proc and --trinkets must be supplied together");
    }
    return iss::ProfileTables::load(proc, trinkets);
}

std::string json_escape(std::string_view value) {
    std::string result;
    for (const char c : value) {
        if (c == '"' || c == '\\') {
            result.push_back('\\');
        }
        result.push_back(c);
    }
    return result;
}

void write_result(std::ostream& output, const iss::SearchResult& result, const iss::SearchOptions& options) {
    output << "{\n"
           << "  \"schema_version\": 2,\n"
           << "  \"engine\": \"native-cpp\",\n"
           << "  \"start_u32\": " << options.start << ",\n"
           << "  \"end_u32\": " << options.end << ",\n"
           << "  \"scanned\": " << result.scanned << ",\n"
           << "  \"threads\": " << result.threads << ",\n"
           << "  \"elapsed_seconds\": " << std::fixed << std::setprecision(6)
           << result.elapsed_seconds << ",\n"
           << "  \"count\": " << result.matches.size() << ",\n"
           << "  \"total_count\": " << result.total_matches << ",\n"
           << "  \"truncated\": " << (result.truncated() ? "true" : "false") << ",\n"
           << "  \"sort_key\": \"" << iss::sort_key_name(result.sort_key) << "\",\n"
           << "  \"sort_direction\": \"" << iss::sort_direction_name(result.sort_direction) << "\",\n"
           << "  \"result_limit\": " << result.result_limit << ",\n"
           << "  \"matches\": [\n";
    output << std::defaultfloat << std::setprecision(10);
    for (std::size_t index = 0; index < result.matches.size(); ++index) {
        const auto& match = result.matches[index];
        const auto& start = match.start;
        output << "    {\"seed\": \"" << json_escape(match.label)
               << "\", \"seed_u32\": " << start.seed
               << ", \"pocket_kind\": \"" << iss::pocket_kind_name(start.pocket_kind)
               << "\", \"pocket_id\": " << start.pocket_id
               << ", \"pill_color\": " << start.pill_color
               << ", \"trinket_id\": "
               << (start.pocket_kind == iss::PocketKind::trinket ? start.pocket_id : 0)
               << ", \"active_id\": " << start.active_id
               << ", \"passive_id\": " << start.passive_id
               << ", \"active_quality\": " << start.active_quality
               << ", \"passive_quality\": " << start.passive_quality
               << ", \"total_quality\": " << start.active_quality + start.passive_quality
               << ", \"red_hearts\": " << start.red_hearts
               << ", \"soul_hearts\": " << start.soul_hearts
               << ", \"coins\": " << start.coins
               << ", \"keys\": " << start.keys
               << ", \"bombs\": " << start.bombs
               << ", \"damage\": " << start.damage
               << ", \"move_speed\": " << start.move_speed
               << ", \"tears\": " << start.tears
               << ", \"range\": " << start.range
               << ", \"shot_speed\": " << start.shot_speed
               << ", \"luck\": " << start.luck
               << ", \"damage_delta\": " << start.damage_delta
               << ", \"move_speed_delta\": " << start.move_speed_delta
               << ", \"tears_delta\": " << start.tears_delta
               << ", \"shot_speed_delta\": " << start.shot_speed_delta
               << ", \"luck_delta\": " << start.luck_delta
               << ", \"post_item_stats_available\": "
               << (start.post_item_stats_available ? "true" : "false")
               << ", \"experimental_treatment_up_mask\": "
               << static_cast<unsigned>(start.experimental_treatment_up_mask)
               << ", \"experimental_treatment_down_mask\": "
               << static_cast<unsigned>(start.experimental_treatment_down_mask)
               << ", \"post_damage\": " << start.post_damage
               << ", \"post_move_speed\": " << start.post_move_speed
               << ", \"post_tears\": " << start.post_tears
               << ", \"post_range\": " << start.post_range
               << ", \"post_shot_speed\": " << start.post_shot_speed
               << ", \"post_luck\": " << start.post_luck << "}";
        output << (index + 1 == result.matches.size() ? "\n" : ",\n");
    }
    output << "  ]\n}\n";
}

void print_usage() {
    std::cout
        << "Isaac Seed Seeker\n\n"
        << "Inspect one seed:\n"
        << "  IsaacSeedSeeker inspect --seed 10161220\n"
        << "  IsaacSeedSeeker inspect --seed-label \"B74H HQPR\"\n\n"
        << "Search a range:\n"
        << "  IsaacSeedSeeker search "
           "--trinket 1,2 --active 105 --damage-min 4.0 "
           "[--start 1] [--end 4294967295] [--threads 8] [--output matches.json]\n\n"
        << "Generic filters (categories are AND; comma-separated IDs are OR):\n"
        << "  --pocket-kind none|trinket|card|pill; --pocket/--card/--pill ID[,ID]\n"
        << "  --active ID[,ID]; --passive ID[,ID]; each also supports -exclude\n"
         << "  --red-hearts-min/max, --soul-hearts-min/max, --coins-min/max,\n"
         << "  --keys-min/max, --bombs-min/max, --damage-min/max,\n"
         << "  --move-speed-min/max, --tears-min/max, --range-min/max,\n"
         << "  --shot-speed-min/max, --luck-min/max, --max-results N\n"
         << "  --post-damage-min/max ... --post-luck-min/max (Experimental Treatment only)\n"
         << "  --experimental-damage up|down|unchanged (same for all seven stats)\n"
         << "  --sort seed|health|damage|move-speed|tears|range|shot-speed|luck|\n"
         << "         coins|keys|bombs|\n"
         << "         active-quality|passive-quality|total-quality; --direction asc|desc\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            return iss::run_local_web_app();
        }
        if (std::string_view(argv[1]) == "help" || std::string_view(argv[1]) == "--help") {
            print_usage();
            return 0;
        }
        const auto arguments = parse_arguments(argc, argv);
        if (arguments.command == "serve") {
            return iss::run_local_web_app(optional(arguments, "no-browser", "0") != "1");
        }
        const auto tables = load_tables(arguments);
        if (arguments.command == "inspect") {
            const auto seed_label = optional(arguments, "seed-label");
            const auto seed = seed_label.empty()
                ? parse_u32(required(arguments, "seed"), "seed")
                : iss::string_to_seed(seed_label);
            const auto start = iss::predict_eden_start(seed, tables);
            std::cout << std::setprecision(10);
            std::cout << "{\"seed\":\"" << iss::seed_to_string(seed)
                      << "\",\"seed_u32\":" << seed
                      << ",\"a5\":" << start.a5
                      << ",\"p988\":" << start.p988
                      << ",\"pocket_kind\":\"" << iss::pocket_kind_name(start.pocket_kind) << "\""
                      << ",\"pocket_id\":" << start.pocket_id
                      << ",\"pill_color\":" << start.pill_color
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
                      << ",\"post_luck\":" << start.post_luck << "}\n";
            return 0;
        }
        if (arguments.command != "search") {
            throw std::invalid_argument("unknown command: " + arguments.command);
        }

        const auto criteria = parse_criteria(arguments);

        iss::SearchOptions options;
        options.start = parse_u32(optional(arguments, "start", "1"), "start");
        options.end = parse_u32(optional(arguments, "end", "4294967295"), "end");
        options.threads = std::min(64U, parse_unsigned(optional(arguments, "threads", "0"), "threads"));
        options.block_size = parse_u32(optional(arguments, "block-size", "1000000"), "block-size");
        options.max_results = parse_u32(optional(arguments, "max-results", "1000"), "max-results");
        options.sort_key = parse_sort_key(optional(arguments, "sort", "seed"));
        options.sort_direction = parse_sort_direction(optional(arguments, "direction", "asc"));

        const auto result = iss::search(tables, criteria, options);
        const auto output_path = optional(arguments, "output");
        if (output_path.empty()) {
            write_result(std::cout, result, options);
        } else {
            std::ofstream output(output_path, std::ios::binary);
            if (!output) {
                throw std::runtime_error("cannot write output: " + output_path);
            }
            write_result(output, result, options);
            std::cout << "matches=" << result.total_matches
                      << " stored=" << result.matches.size()
                      << " scanned=" << result.scanned
                      << " elapsed=" << std::fixed << std::setprecision(3)
                      << result.elapsed_seconds << "s output=" << output_path << "\n";
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 2;
    }
}
