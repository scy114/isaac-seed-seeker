#include "isaac_seed_seeker/core.hpp"
#include "isaac_seed_seeker/builtin_profile.hpp"

#include <cstdlib>
#include <array>
#include <cmath>
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

void require_near(double actual, double expected, const std::string& message) {
    if (std::abs(actual - expected) > 1.0e-7) {
        throw std::runtime_error(message + ": expected " + std::to_string(expected)
                                 + ", got " + std::to_string(actual));
    }
}

iss::EdenCriteria original_target() {
    iss::EdenCriteria result;
    result.pocket_kind = iss::PocketKind::trinket;
    result.pocket_ids.any_of = {169};
    result.active_items.any_of = {145, 133};
    result.passive_items.any_of = {81, 134, 187, 212, 665};
    return result;
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
        require_near(builtin_start.red_hearts, 1.0, "builtin red hearts mismatch");
        require_near(builtin_start.soul_hearts, 1.0, "builtin soul hearts mismatch");
        require_near(builtin_start.damage, 2.924349875850715, "builtin actual damage mismatch");
        require_near(builtin_start.move_speed, 1.0026879816750494, "builtin actual speed mismatch");
        require_near(builtin_start.tears, 2.996046515031133, "builtin actual tears mismatch");
        require_near(builtin_start.shot_speed, 0.9083650745217387, "builtin actual shot speed mismatch");
        require_near(builtin_start.luck, -0.4185999840698891, "builtin actual luck mismatch");
        require_near(builtin_start.damage_delta, -0.5756501241492851, "builtin damage mismatch");
        require_near(builtin_start.move_speed_delta, 0.0026879816750493835, "builtin speed mismatch");
        require_near(builtin_start.tears_delta, 0.27383407490872536, "builtin tears mismatch");
        require_near(builtin_start.range, 7.310333206531665, "builtin range mismatch");
        require_near(builtin_start.shot_speed_delta, -0.0916349254782613, "builtin shot speed mismatch");
        require_near(builtin_start.luck_delta, -0.4185999840698891, "builtin luck mismatch");

        const auto pill_start = iss::predict_eden_start(2U, builtin);
        require(pill_start.pocket_kind == iss::PocketKind::pill, "pill pocket kind mismatch");
        require(pill_start.pocket_id == 12, "pill effect mismatch");
        require(pill_start.active_id == 639, "pill seed active mismatch");
        require(pill_start.passive_id == 393, "pill seed passive mismatch");
        require_near(pill_start.red_hearts, 2.0, "pill seed red hearts mismatch");
        require_near(pill_start.soul_hearts, 0.0, "pill seed soul hearts mismatch");
        require_near(pill_start.damage_delta, 0.5564722532531445, "pill seed damage mismatch");
        require_near(pill_start.range, 7.4267219649934215, "pill seed range mismatch");
        const auto horse_pill_start = iss::predict_eden_start(60U, builtin);
        require(horse_pill_start.pocket_kind == iss::PocketKind::pill, "horse pill kind mismatch");
        require(horse_pill_start.pocket_id == 63, "horse pill effect mismatch");

        const auto card_start = iss::predict_eden_start(5U, builtin);
        require(card_start.pocket_kind == iss::PocketKind::card, "card pocket kind mismatch");
        require(card_start.pocket_id == 2, "normal card ID mismatch");
        const auto reversed_card_start = iss::predict_eden_start(7U, builtin);
        require(reversed_card_start.pocket_kind == iss::PocketKind::card, "reversed card kind mismatch");
        require(reversed_card_start.pocket_id == 58, "reversed card ID mismatch");

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
        const auto target = original_target();
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
        require(regression.total_matches == expected_seeds.size(), "range regression total mismatch");
        for (std::size_t index = 0; index < expected_seeds.size(); ++index) {
            require(regression.matches[index].start.seed == expected_seeds[index], "range regression seed mismatch");
        }

        iss::EdenCriteria generic;
        generic.pocket_kind = iss::PocketKind::pill;
        generic.pocket_ids.any_of = {12};
        generic.active_items.any_of = {639};
        generic.passive_items.any_of = {393};
        generic.red_hearts.minimum = 2.0;
        generic.red_hearts.maximum = 2.0;
        generic.damage.minimum = 4.05;
        generic.range.minimum = 7.42;
        generic.range.maximum = 7.43;
        iss::SearchOptions generic_options;
        generic_options.end = 100U;
        generic_options.threads = 2;
        const auto generic_result = iss::search(builtin, generic, generic_options);
        require(generic_result.total_matches == 1, "generic criteria count mismatch");
        require(generic_result.matches.size() == 1, "generic criteria stored result mismatch");
        require(generic_result.matches.front().start.seed == 2U, "generic criteria seed mismatch");

        iss::EdenCriteria capped;
        capped.pocket_kind = iss::PocketKind::none;
        iss::SearchOptions capped_options;
        capped_options.end = 100U;
        capped_options.threads = 2;
        capped_options.block_size = 7U;
        capped_options.max_results = 3U;
        const auto capped_result = iss::search(builtin, capped, capped_options);
        require(capped_result.total_matches > capped_result.matches.size(), "result cap did not truncate");
        require(capped_result.matches.size() == 3, "result cap stored wrong count");
        require(capped_result.truncated(), "truncated flag mismatch");
        require(capped_result.matches[0].start.seed < capped_result.matches[1].start.seed
                    && capped_result.matches[1].start.seed < capped_result.matches[2].start.seed,
                "capped results are not sorted");

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
