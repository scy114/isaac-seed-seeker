#include "isaac_seed_seeker/core.hpp"
#include "isaac_seed_seeker/builtin_profile.hpp"
#include "isaac_seed_seeker/local_web_app.hpp"

#include <algorithm>
#include <charconv>
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
        if (parsed.ec != std::errc() || parsed.ptr != part.data() + part.size() || value <= 0) {
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
           << "  \"schema_version\": 1,\n"
           << "  \"engine\": \"native-cpp\",\n"
           << "  \"start_u32\": " << options.start << ",\n"
           << "  \"end_u32\": " << options.end << ",\n"
           << "  \"scanned\": " << result.scanned << ",\n"
           << "  \"threads\": " << result.threads << ",\n"
           << "  \"elapsed_seconds\": " << std::fixed << std::setprecision(6)
           << result.elapsed_seconds << ",\n"
           << "  \"count\": " << result.matches.size() << ",\n"
           << "  \"matches\": [\n";
    for (std::size_t index = 0; index < result.matches.size(); ++index) {
        const auto& match = result.matches[index];
        output << "    {\"seed\": \"" << json_escape(match.label)
               << "\", \"seed_u32\": " << match.seed
               << ", \"trinket_id\": " << match.trinket_id
               << ", \"active_id\": " << match.active_id
               << ", \"passive_id\": " << match.passive_id << "}";
        output << (index + 1 == result.matches.size() ? "\n" : ",\n");
    }
    output << "  ]\n}\n";
}

void print_usage() {
    std::cout
        << "Isaac Seed Seeker\n\n"
        << "Inspect one seed:\n"
        << "  IsaacSeedSeeker inspect --seed 10161220\n\n"
        << "Search a range:\n"
        << "  IsaacSeedSeeker search "
           "--trinket 169 --active 145,133 --passive 81,134,187,212,665 "
           "[--start 1] [--end 4294967295] [--threads 8] [--block-size 1000000] [--output matches.json]\n";
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
            const auto seed = parse_u32(required(arguments, "seed"), "seed");
            const auto start = iss::predict_eden_start(seed, tables);
            std::cout << "{\"seed\":\"" << iss::seed_to_string(seed)
                      << "\",\"seed_u32\":" << seed
                      << ",\"a5\":" << start.a5
                      << ",\"p988\":" << start.p988
                      << ",\"pocket_kind\":" << static_cast<int>(start.pocket_kind)
                      << ",\"pocket_id\":" << start.pocket_id
                      << ",\"active_id\":" << start.active_id
                      << ",\"passive_id\":" << start.passive_id << "}\n";
            return 0;
        }
        if (arguments.command != "search") {
            throw std::invalid_argument("unknown command: " + arguments.command);
        }

        iss::ItemCriteria criteria;
        criteria.trinket_id = static_cast<std::int32_t>(parse_u32(required(arguments, "trinket"), "trinket"));
        criteria.active_any = parse_ids(required(arguments, "active"), "active");
        criteria.passive_any = parse_ids(required(arguments, "passive"), "passive");

        iss::SearchOptions options;
        options.start = parse_u32(optional(arguments, "start", "1"), "start");
        options.end = parse_u32(optional(arguments, "end", "4294967295"), "end");
        options.threads = std::min(64U, parse_unsigned(optional(arguments, "threads", "0"), "threads"));
        options.block_size = parse_u32(optional(arguments, "block-size", "1000000"), "block-size");

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
            std::cout << "matches=" << result.matches.size()
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
