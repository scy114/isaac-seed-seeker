#include "isaac_seed_seeker/core.hpp"
#include "isaac_seed_seeker/builtin_profile.hpp"

#include <cstdlib>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace iss = isaac_seed_seeker;

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        constexpr std::array seed_labels{
            std::pair{107593840U, "BGDM 9DPK"},
            std::pair{698597460U, "E3GM 9LJ7"},
            std::pair{3641186988U, "4VX0 LYNB"},
            std::pair{1286605119U, "JPR1 PSTT"},
            std::pair{3011071800U, "1WMD BTJH"},
            std::pair{1015388575U, "GPZQ N2X1"},
        };
        for (const auto& [seed, label] : seed_labels) {
            require(iss::seed_to_string(seed) == label, "seed codec golden mismatch");
        }
        require(iss::seed_to_string(1U) == "B911 99AC", "special seed label mismatch");
        require(iss::a5_from_seed(10161220U) == 778875255U, "a5 golden mismatch");
        require(iss::p988_from_seed(10161220U) == 2935808445U, "p988 golden mismatch");

        const auto builtin = iss::builtin_j460_profile();
        require(builtin.collectibles.size() == 733, "unexpected builtin collectible table size");
        require(builtin.trinkets.size() == 190, "unexpected builtin trinket table size");
        const auto builtin_start = iss::predict_eden_start(10161220U, builtin);
        require(builtin_start.pocket_kind == iss::PocketKind::trinket, "builtin pocket mismatch");
        require(builtin_start.pocket_id == 169, "builtin trinket mismatch");
        require(builtin_start.active_id == 145, "builtin active mismatch");
        require(builtin_start.passive_id == 134, "builtin passive mismatch");

        struct GoldenStart {
            std::uint32_t seed;
            std::int32_t active;
            std::int32_t passive;
        };
        constexpr std::array golden_starts{
            GoldenStart{101128687U, 133, 81},
            GoldenStart{59308086U, 133, 134},
            GoldenStart{18741439U, 133, 187},
            GoldenStart{23668499U, 133, 212},
            GoldenStart{70660144U, 133, 665},
            GoldenStart{34913796U, 145, 81},
            GoldenStart{10161220U, 145, 134},
            GoldenStart{26300087U, 145, 187},
            GoldenStart{65439425U, 145, 212},
            GoldenStart{49946388U, 145, 665},
        };
        const iss::ItemCriteria target{169, {145, 133}, {81, 134, 187, 212, 665}};
        for (const auto& golden : golden_starts) {
            const auto actual = iss::predict_eden_start(golden.seed, builtin);
            require(actual.pocket_kind == iss::PocketKind::trinket, "golden pocket kind mismatch");
            require(actual.pocket_id == 169, "golden trinket mismatch");
            require(actual.active_id == golden.active, "golden active mismatch");
            require(actual.passive_id == golden.passive, "golden passive mismatch");
            require(iss::matches(actual, target), "golden criteria mismatch");
            require(!iss::matches(iss::predict_eden_start(golden.seed - 1U, builtin), target),
                    "neighbor before golden unexpectedly matched");
            require(!iss::matches(iss::predict_eden_start(golden.seed + 1U, builtin), target),
                    "neighbor after golden unexpectedly matched");
        }

        constexpr std::array recovered_false_negatives{
            GoldenStart{319314092U, 133, 665},
            GoldenStart{385707447U, 133, 187},
            GoldenStart{665083697U, 145, 81},
            GoldenStart{1080602528U, 133, 665},
            GoldenStart{1190319945U, 133, 134},
            GoldenStart{1717018556U, 145, 187},
            GoldenStart{2296536759U, 145, 212},
            GoldenStart{3321677848U, 145, 187},
            GoldenStart{3329586080U, 145, 187},
            GoldenStart{4157422988U, 133, 187},
            GoldenStart{4292488097U, 133, 665},
        };
        for (const auto& recovered : recovered_false_negatives) {
            const auto actual = iss::predict_eden_start(recovered.seed, builtin);
            require(actual.active_id == recovered.active, "recovered active mismatch");
            require(actual.passive_id == recovered.passive, "recovered passive mismatch");
            require(iss::matches(actual, target), "recovered false negative did not match");
        }

        iss::SearchOptions regression_options;
        regression_options.end = 25'010'000U;
        regression_options.threads = 2;
        const auto regression = iss::search(builtin, target, regression_options);
        constexpr std::array<std::uint32_t, 5> expected_seeds{
            10'161'220U, 12'520'235U, 18'741'439U, 23'668'499U, 24'042'022U,
        };
        require(regression.matches.size() == expected_seeds.size(), "range regression count mismatch");
        for (std::size_t index = 0; index < expected_seeds.size(); ++index) {
            require(regression.matches[index].seed == expected_seeds[index], "range regression seed mismatch");
        }

        iss::SearchOptions exception_options;
        exception_options.end = 10'000U;
        exception_options.block_size = 1'000U;
        exception_options.threads = 2;
        bool worker_exception_propagated = false;
        try {
            static_cast<void>(iss::search(
                builtin,
                target,
                exception_options,
                [](const iss::SearchProgress&) { throw std::runtime_error("progress failure"); }
            ));
        } catch (const std::runtime_error&) {
            worker_exception_propagated = true;
        }
        require(worker_exception_propagated, "worker exception was not propagated safely");

        const auto temporary = std::filesystem::temp_directory_path();
        const auto bad_proc = temporary / "isaac_seed_seeker_bad_proc.json";
        const auto bad_trinkets = temporary / "isaac_seed_seeker_bad_trinkets.json";
        {
            std::ofstream(bad_proc)
                << R"({"entries":[null,{"type":3,"id":1,"flag47":0},{"type":1,"id":2,"flag47":0}]})";
            std::ofstream(bad_trinkets)
                << R"({"rngShr":32,"rngShl":5,"rngFin":19,"entries":[{"raw":1,"flag4":true,"flag5":true}]})";
        }
        bool rejected_bad_shift = false;
        try {
            static_cast<void>(iss::ProfileTables::load(bad_proc, bad_trinkets));
        } catch (const std::runtime_error&) {
            rejected_bad_shift = true;
        }
        std::filesystem::remove(bad_proc);
        std::filesystem::remove(bad_trinkets);
        require(rejected_bad_shift, "invalid Profile RNG shift was accepted");

        if (argc == 3) {
            const auto tables = iss::ProfileTables::load(argv[1], argv[2]);
            require(tables.collectibles.size() == 733, "unexpected collectible table size");
            require(tables.trinkets.size() == 190, "unexpected trinket table size");
            const auto start = iss::predict_eden_start(10161220U, tables);
            require(start.pocket_kind == iss::PocketKind::trinket, "pocket kind mismatch");
            require(start.pocket_id == 169, "trinket golden mismatch");
            require(start.active_id == 145, "active item golden mismatch");
            require(start.passive_id == 134, "passive item golden mismatch");
        }

        std::cout << "native core tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "native core tests failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
