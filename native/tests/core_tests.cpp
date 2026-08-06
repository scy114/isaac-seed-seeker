#include "isaac_seed_seeker/core.hpp"
#include "isaac_seed_seeker/builtin_profile.hpp"
#include "isaac_seed_seeker/daily.hpp"

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

void require_game_near(double actual, double expected, const std::string& message) {
    if (std::abs(actual - expected) > 1.0e-6) {
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
            require(iss::string_to_seed(label) == seed, "seed decoder golden mismatch");
        }
        require(iss::seed_to_string(1U) == "B911 99AC", "special seed label mismatch");
        require(iss::string_to_seed("masv\tsyfs") == 1'473'169'325U,
                "seed decoder normalization mismatch");
        for (const auto invalid : {"MASV SYFA", "MASI SYFS", "ABC"}) {
            bool rejected = false;
            try {
                static_cast<void>(iss::string_to_seed(invalid));
            } catch (const std::invalid_argument&) {
                rejected = true;
            }
            require(rejected, "invalid seed label was accepted");
        }
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
        require(builtin_start.active_quality == 2, "builtin active quality mismatch");
        require(builtin_start.passive_quality == 2, "builtin passive quality mismatch");
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

        const auto card_with_items = iss::predict_eden_start(2U, builtin);
        require(card_with_items.pocket_kind == iss::PocketKind::card, "card pocket kind mismatch");
        require(card_with_items.pocket_id == 12, "card ID mismatch");
        require(card_with_items.active_id == 639, "card seed active mismatch");
        require(card_with_items.passive_id == 393, "card seed passive mismatch");
        require_near(card_with_items.red_hearts, 2.0, "card seed red hearts mismatch");
        require_near(card_with_items.soul_hearts, 0.0, "card seed soul hearts mismatch");
        require_near(card_with_items.damage_delta, -0.6225863096, "card seed damage mismatch");
        require_near(card_with_items.range, 7.194700883, "card seed range mismatch");
        require(card_with_items.coins == 0, "card seed coins mismatch");
        require(card_with_items.keys == 0, "card seed keys mismatch");
        require(card_with_items.bombs == 1, "card seed bombs mismatch");

        const auto key_start = iss::predict_eden_start(1U, builtin);
        require(key_start.coins == 0, "key seed coins mismatch");
        require(key_start.keys == 1, "key seed keys mismatch");
        require(key_start.bombs == 0, "key seed bombs mismatch");
        const auto coin_start = iss::predict_eden_start(8U, builtin);
        require(coin_start.coins == 2, "coin seed coins mismatch");
        require(coin_start.keys == 0, "coin seed keys mismatch");
        require(coin_start.bombs == 0, "coin seed bombs mismatch");
        require(iss::predict_eden_start(18U, builtin).coins == 5,
                "maximum starting coins mismatch");
        require(iss::predict_eden_start(33U, builtin).bombs == 2,
                "maximum starting bombs mismatch");

        iss::EdenStart daily_good_boundary;
        daily_good_boundary.active_id = 58;
        daily_good_boundary.active_quality = 3;
        daily_good_boundary.passive_id = 1;
        daily_good_boundary.passive_quality = 3;
        daily_good_boundary.damage = 3.0;
        daily_good_boundary.tears = 3.0;
        daily_good_boundary.move_speed = 1.0;
        const auto daily_boundary_score = iss::score_daily_good_v0(daily_good_boundary);
        require(daily_boundary_score.eligible, "daily good boundary seed was rejected");
        require(daily_boundary_score.selection_weight == 100,
                "daily good boundary weight mismatch");
        const auto daily_v1_boundary_score = iss::score_daily_good_v1(daily_good_boundary);
        require(daily_v1_boundary_score.eligible, "daily v1 boundary seed was rejected");
        require(daily_v1_boundary_score.active_q3_rating == 1,
                "daily v1 active Q3 rating mismatch");
        require(daily_v1_boundary_score.passive_q3_rating == 1,
                "daily v1 passive Q3 rating mismatch");
        require(daily_v1_boundary_score.selection_weight == 100,
                "daily v1 boundary weight mismatch");

        auto daily_v1_high_q3 = daily_good_boundary;
        daily_v1_high_q3.active_id = 127;
        daily_v1_high_q3.passive_id = 562;
        const auto daily_v1_high_q3_score = iss::score_daily_good_v1(daily_v1_high_q3);
        require(daily_v1_high_q3_score.active_q3_rating == 3,
                "daily v1 high active Q3 rating mismatch");
        require(daily_v1_high_q3_score.passive_q3_rating == 4,
                "daily v1 high passive Q3 rating mismatch");
        require(daily_v1_high_q3_score.active_q3_rating_bonus == 30,
                "daily v1 active Q3 bonus mismatch");
        require(daily_v1_high_q3_score.passive_q3_rating_bonus == 50,
                "daily v1 passive Q3 bonus mismatch");
        require(daily_v1_high_q3_score.selection_weight == 180,
                "daily v1 high Q3 weight mismatch");

        auto daily_v1_unknown_q3 = daily_good_boundary;
        daily_v1_unknown_q3.active_id = 999;
        require(!iss::score_daily_good_v1(daily_v1_unknown_q3).eligible,
                "unrated Q3 item passed the daily v1 gate");

        auto daily_good_maximum = daily_good_boundary;
        daily_good_maximum.active_id = 628;
        daily_good_maximum.active_quality = 4;
        daily_good_maximum.passive_id = 4;
        daily_good_maximum.passive_quality = 4;
        daily_good_maximum.damage = 4.5;
        daily_good_maximum.tears = 3.501433905;
        daily_good_maximum.move_speed = 1.15;
        const auto daily_maximum_score = iss::score_daily_good_v0(daily_good_maximum);
        require(daily_maximum_score.eligible, "maximum daily good seed was rejected");
        require(daily_maximum_score.selection_weight == 285,
                "maximum daily good weight mismatch");
        require(daily_maximum_score.death_certificate_bonus == 30,
                "Death Certificate daily bonus mismatch");

        auto daily_ipecac = daily_good_boundary;
        daily_ipecac.passive_id = 149;
        daily_ipecac.passive_quality = 4;
        require(!iss::score_daily_good_v0(daily_ipecac).eligible,
                "Ipecac was accepted as a daily good seed");
        auto daily_low_tears = daily_good_boundary;
        daily_low_tears.tears = 2.999;
        require(!iss::score_daily_good_v0(daily_low_tears).eligible,
                "low tears passed the daily good gate");

        iss::DailyGoodOptions daily_options;
        daily_options.date_utc8 = "2026-08-06";
        daily_options.candidates = 25'000;
        daily_options.threads = 1;
        const auto daily_single_thread = iss::select_daily_good_v0(builtin, daily_options);
        daily_options.threads = 4;
        const auto daily_four_threads = iss::select_daily_good_v0(builtin, daily_options);
        require(daily_single_thread.eligible > 0, "daily scan produced no eligible seeds");
        require(daily_single_thread.primary.seed == daily_four_threads.primary.seed,
                "daily selection changed with thread count");
        require(daily_single_thread.eligible == daily_four_threads.eligible,
                "daily eligible count changed with thread count");
        require(daily_single_thread.primary_score.selection_weight
                    == daily_four_threads.primary_score.selection_weight,
                "daily weight changed with thread count");
        daily_options.threads = 1;
        const auto daily_v1_single_thread = iss::select_daily_good_v1(builtin, daily_options);
        daily_options.threads = 4;
        const auto daily_v1_four_threads = iss::select_daily_good_v1(builtin, daily_options);
        require(daily_v1_single_thread.rules_version == iss::daily_good_rules_version_v1,
                "daily v1 result version mismatch");
        require(daily_v1_single_thread.primary.seed == daily_v1_four_threads.primary.seed,
                "daily v1 selection changed with thread count");
        require(daily_v1_single_thread.primary_score.selection_weight
                    == daily_v1_four_threads.primary_score.selection_weight,
                "daily v1 weight changed with thread count");
        bool invalid_daily_date_rejected = false;
        try {
            daily_options.date_utc8 = "2026-02-30";
            static_cast<void>(iss::select_daily_good_v0(builtin, daily_options));
        } catch (const std::invalid_argument&) {
            invalid_daily_date_rejected = true;
        }
        require(invalid_daily_date_rejected, "invalid daily date was accepted");

        const auto pill_start = iss::predict_eden_start(5U, builtin);
        require(pill_start.pocket_kind == iss::PocketKind::pill, "pill pocket kind mismatch");
        require(pill_start.pill_color == 12, "pill color mismatch");
        require(pill_start.pocket_id == 20, "run-specific pill effect mismatch");
        const auto second_pill_start = iss::predict_eden_start(64U, builtin);
        require(second_pill_start.pocket_kind == iss::PocketKind::pill, "second pill kind mismatch");
        require(second_pill_start.pill_color == 2, "second pill color mismatch");
        require(second_pill_start.pocket_id == 18, "second run-specific pill effect mismatch");
        const auto horse_pill_start = iss::predict_eden_start(325U, builtin);
        require(horse_pill_start.pocket_kind == iss::PocketKind::pill, "horse pill kind mismatch");
        require(horse_pill_start.pill_color == 2058, "horse pill color flag mismatch");
        require(horse_pill_start.pocket_id == 49, "horse pill effect mismatch");
        const auto golden_pill_start = iss::predict_eden_start(377U, builtin);
        require(golden_pill_start.pocket_kind == iss::PocketKind::pill, "golden pill kind mismatch");
        require(golden_pill_start.pill_color == 14, "golden pill color mismatch");
        require(golden_pill_start.pocket_id == -1, "golden pill should not have one fixed effect");

        iss::EdenCriteria bad_gas;
        bad_gas.pocket_kind = iss::PocketKind::pill;
        bad_gas.pocket_ids.any_of = {0};
        bad_gas.validate();
        iss::SearchOptions bad_gas_options;
        bad_gas_options.end = 1'000U;
        bad_gas_options.threads = 2;
        const auto bad_gas_result = iss::search(builtin, bad_gas, bad_gas_options);
        require(bad_gas_result.total_matches == 2, "pill effect zero search count mismatch");
        require(bad_gas_result.matches.front().start.seed == 791U,
                "pill effect zero search first seed mismatch");

        const auto card_start = iss::predict_eden_start(1U, builtin);
        require(card_start.pocket_kind == iss::PocketKind::card, "card pocket kind mismatch");
        require(card_start.pocket_id == 14, "normal card ID mismatch");
        const auto reversed_card_start = iss::predict_eden_start(60U, builtin);
        require(reversed_card_start.pocket_kind == iss::PocketKind::card, "reversed card kind mismatch");
        require(reversed_card_start.pocket_id == 63, "reversed card ID mismatch");
        const auto special_card_start = iss::predict_eden_start(117U, builtin);
        require(special_card_start.pocket_kind == iss::PocketKind::card, "special card kind mismatch");
        require(special_card_start.pocket_id == 43, "special card ID mismatch");

        const auto experimental_treatment = iss::predict_eden_start(20U, builtin);
        require(experimental_treatment.passive_id == 240,
                "Experimental Treatment passive mismatch");
        require(experimental_treatment.post_item_stats_available,
                "Experimental Treatment post-item stats missing");
        require(experimental_treatment.experimental_treatment_up_mask == 77,
                "Experimental Treatment up mask mismatch");
        require(experimental_treatment.experimental_treatment_down_mask == 34,
                "Experimental Treatment down mask mismatch");
        require_near(experimental_treatment.post_damage, 5.0035222145,
                     "Experimental Treatment post damage mismatch");
        require_near(experimental_treatment.post_move_speed, 0.8481249463,
                     "Experimental Treatment post speed mismatch");
        require_near(experimental_treatment.post_tears, 2.803615803,
                     "Experimental Treatment post tears mismatch");
        require_near(experimental_treatment.post_range, 6.012483505,
                     "Experimental Treatment post range mismatch");
        require_near(experimental_treatment.post_shot_speed, 0.9717903773,
                     "Experimental Treatment post shot speed mismatch");
        require_near(experimental_treatment.post_luck, 0.4833137638,
                     "Experimental Treatment post luck mismatch");

        iss::EdenCriteria treatment_criteria;
        treatment_criteria.experimental_treatment_directions[
            static_cast<std::size_t>(iss::ExperimentalTreatmentStat::health)
        ] = iss::ExperimentalTreatmentDirection::up;
        treatment_criteria.experimental_treatment_directions[
            static_cast<std::size_t>(iss::ExperimentalTreatmentStat::damage)
        ] = iss::ExperimentalTreatmentDirection::up;
        treatment_criteria.experimental_treatment_directions[
            static_cast<std::size_t>(iss::ExperimentalTreatmentStat::range)
        ] = iss::ExperimentalTreatmentDirection::unchanged;
        treatment_criteria.post_damage.minimum = 5.0;
        treatment_criteria.post_damage.maximum = 5.01;
        treatment_criteria.validate();
        iss::SearchOptions treatment_options;
        treatment_options.start = 20U;
        treatment_options.end = 20U;
        treatment_options.threads = 1;
        const auto treatment_result = iss::search(builtin, treatment_criteria, treatment_options);
        require(treatment_result.total_matches == 1,
                "Experimental Treatment fast search path mismatch");

        auto invalid_treatment = treatment_criteria;
        invalid_treatment.passive_items.none_of = {240};
        bool rejected_treatment_conflict = false;
        try {
            invalid_treatment.validate();
        } catch (const std::invalid_argument&) {
            rejected_treatment_conflict = true;
        }
        require(rejected_treatment_conflict,
                "conflicting Experimental Treatment criteria were accepted");

        constexpr std::uint32_t reported_card_false_positive = 230'816'840U;
        require(iss::seed_to_string(reported_card_false_positive) == "AJ1J HPQF",
                "reported pocket regression seed label mismatch");
        const auto reported_start = iss::predict_eden_start(reported_card_false_positive, builtin);
        require(reported_start.pocket_kind == iss::PocketKind::pill,
                "reported pocket regression kind mismatch");
        require(reported_start.pill_color == 6, "reported pocket regression pill color mismatch");
        require(reported_start.pocket_id == 6, "reported pocket regression raw pill effect mismatch");
        require(reported_start.active_id == 347, "reported pocket regression active mismatch");
        require(reported_start.passive_id == 402, "reported pocket regression passive mismatch");
        require_game_near(reported_start.damage, 3.8433690071106,
                          "reported pocket regression damage mismatch");
        require_game_near(reported_start.move_speed, 0.92413437366486,
                          "reported pocket regression speed mismatch");
        require_game_near(reported_start.tears, 2.1738228778223,
                          "reported pocket regression tears mismatch");
        require_game_near(reported_start.range, 257.14611816406 / 40.0,
                          "reported pocket regression range mismatch");
        require_game_near(reported_start.shot_speed, 1.2004964351654,
                          "reported pocket regression shot speed mismatch");
        require_game_near(reported_start.luck, -0.66076004505157,
                          "reported pocket regression luck mismatch");

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
        generic.pocket_kind = iss::PocketKind::card;
        generic.pocket_ids.any_of = {12};
        generic.active_items.any_of = {639};
        generic.passive_items.any_of = {393};
        generic.red_hearts.minimum = 2.0;
        generic.red_hearts.maximum = 2.0;
        generic.damage.minimum = 2.87;
        generic.range.minimum = 7.19;
        generic.range.maximum = 7.20;
        iss::SearchOptions generic_options;
        generic_options.end = 100U;
        generic_options.threads = 2;
        const auto generic_result = iss::search(builtin, generic, generic_options);
        require(generic_result.total_matches == 1, "generic criteria count mismatch");
        require(generic_result.matches.size() == 1, "generic criteria stored result mismatch");
        require(generic_result.matches.front().start.seed == 2U, "generic criteria seed mismatch");

        iss::EdenCriteria resource_criteria;
        resource_criteria.coins.minimum = 2.0;
        resource_criteria.coins.maximum = 2.0;
        iss::SearchOptions resource_options;
        resource_options.end = 10U;
        resource_options.threads = 1;
        const auto resource_result = iss::search(builtin, resource_criteria, resource_options);
        require(resource_result.total_matches == 1, "coin criteria count mismatch");
        require(resource_result.matches.front().start.seed == 8U,
                "coin criteria seed mismatch");
        auto invalid_resources = resource_criteria;
        invalid_resources.coins.minimum = 2.5;
        bool rejected_fractional_resource = false;
        try {
            invalid_resources.validate();
        } catch (const std::invalid_argument&) {
            rejected_fractional_resource = true;
        }
        require(rejected_fractional_resource, "fractional resource bound was accepted");

        iss::EdenCriteria resource_sort_criteria;
        resource_sort_criteria.coins.minimum = 0.0;
        iss::SearchOptions resource_sort_options;
        resource_sort_options.end = 30U;
        resource_sort_options.threads = 2;
        resource_sort_options.max_results = 5U;
        resource_sort_options.sort_key = iss::SortKey::coins;
        resource_sort_options.sort_direction = iss::SortDirection::descending;
        const auto resource_sorted = iss::search(
            builtin,
            resource_sort_criteria,
            resource_sort_options
        );
        require(resource_sorted.matches.front().start.coins == 5,
                "coin sort did not retain maximum starting coins");
        for (std::size_t index = 1; index < resource_sorted.matches.size(); ++index) {
            require(resource_sorted.matches[index - 1].start.coins
                        >= resource_sorted.matches[index].start.coins,
                    "coin results are not descending");
        }

        iss::EdenCriteria reported_card;
        reported_card.pocket_kind = iss::PocketKind::card;
        reported_card.pocket_ids.any_of = {80};
        reported_card.active_items.any_of = {347};
        reported_card.passive_items.any_of = {402};
        iss::SearchOptions reported_options;
        reported_options.start = reported_card_false_positive;
        reported_options.end = reported_card_false_positive;
        reported_options.threads = 1;
        const auto reported_card_result = iss::search(builtin, reported_card, reported_options);
        require(reported_card_result.total_matches == 0,
                "reported seed remained a card 80 false positive");

        auto reported_pill = reported_card;
        reported_pill.pocket_kind = iss::PocketKind::pill;
        reported_pill.pocket_ids.any_of = {6};
        const auto reported_pill_result = iss::search(builtin, reported_pill, reported_options);
        require(reported_pill_result.total_matches == 1,
                "reported seed did not match its actual pill branch");

        constexpr std::uint32_t texz_card_regression = 2'261'264'115U;
        require(iss::seed_to_string(texz_card_regression) == "TEXZ WDS0",
                "TEXZ card regression seed label mismatch");
        const auto texz_start = iss::predict_eden_start(texz_card_regression, builtin);
        require(texz_start.pocket_kind == iss::PocketKind::card,
                "TEXZ card regression kind mismatch");
        require(texz_start.pocket_id == 2, "TEXZ card regression ID mismatch");
        require(texz_start.active_id == 347, "TEXZ card regression active mismatch");
        require(texz_start.passive_id == 402, "TEXZ card regression passive mismatch");
        require_game_near(texz_start.damage, 3.6097302436829,
                          "TEXZ card regression damage mismatch");
        require_game_near(texz_start.move_speed, 1.122757434845,
                          "TEXZ card regression speed mismatch");
        require_game_near(texz_start.tears, 1.8518157899725,
                          "TEXZ card regression tears mismatch");
        require_game_near(texz_start.range, 268.00231933594 / 40.0,
                          "TEXZ card regression range mismatch");
        require_game_near(texz_start.shot_speed, 1.2088994979858,
                          "TEXZ card regression shot speed mismatch");
        require_game_near(texz_start.luck, -0.13791680335999,
                          "TEXZ card regression luck mismatch");

        iss::EdenCriteria texz_card;
        texz_card.pocket_kind = iss::PocketKind::card;
        texz_card.pocket_ids.any_of = {2};
        texz_card.active_items.any_of = {347};
        texz_card.passive_items.any_of = {402};
        auto texz_options = reported_options;
        texz_options.start = texz_card_regression;
        texz_options.end = texz_card_regression;
        const auto texz_result = iss::search(builtin, texz_card, texz_options);
        require(texz_result.total_matches == 1,
                "TEXZ card regression did not match the fast search path");

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

        iss::SearchOptions exhaustive_sort_options;
        exhaustive_sort_options.end = 5'000U;
        exhaustive_sort_options.threads = 1;
        exhaustive_sort_options.max_results = 5'000U;
        exhaustive_sort_options.sort_key = iss::SortKey::damage;
        exhaustive_sort_options.sort_direction = iss::SortDirection::descending;
        const auto exhaustive_damage = iss::search(builtin, capped, exhaustive_sort_options);

        auto top_damage_options = exhaustive_sort_options;
        top_damage_options.threads = 4;
        top_damage_options.block_size = 37U;
        top_damage_options.max_results = 7U;
        const auto top_damage = iss::search(builtin, capped, top_damage_options);
        require(top_damage.total_matches == exhaustive_damage.total_matches,
                "sorted Top-K changed the total match count");
        require(top_damage.matches.size() == 7, "sorted Top-K retained the wrong count");
        require(top_damage.sort_key == iss::SortKey::damage
                    && top_damage.sort_direction == iss::SortDirection::descending,
                "sorted Top-K metadata mismatch");
        for (std::size_t index = 0; index < top_damage.matches.size(); ++index) {
            require(top_damage.matches[index].start.seed == exhaustive_damage.matches[index].start.seed,
                    "parallel damage Top-K differs from exhaustive ordering");
            if (index != 0) {
                require(top_damage.matches[index - 1].start.damage
                            >= top_damage.matches[index].start.damage,
                        "damage results are not descending");
            }
        }

        auto total_quality_options = exhaustive_sort_options;
        total_quality_options.sort_key = iss::SortKey::total_quality;
        total_quality_options.max_results = 11U;
        total_quality_options.threads = 3;
        const auto total_quality = iss::search(builtin, capped, total_quality_options);
        for (std::size_t index = 1; index < total_quality.matches.size(); ++index) {
            const auto previous = total_quality.matches[index - 1].start.active_quality
                + total_quality.matches[index - 1].start.passive_quality;
            const auto current = total_quality.matches[index].start.active_quality
                + total_quality.matches[index].start.passive_quality;
            require(previous >= current, "total quality results are not descending");
            if (previous == current) {
                const auto& left = total_quality.matches[index - 1].start;
                const auto& right = total_quality.matches[index].start;
                require(left.active_id < right.active_id
                            || (left.active_id == right.active_id
                                && (left.passive_id < right.passive_id
                                    || (left.passive_id == right.passive_id
                                        && left.seed < right.seed))),
                        "total quality tie-break is unstable");
            }
        }

        auto health_options = exhaustive_sort_options;
        health_options.sort_key = iss::SortKey::health;
        health_options.sort_direction = iss::SortDirection::ascending;
        health_options.max_results = 20U;
        health_options.threads = 2;
        const auto health_sorted = iss::search(builtin, capped, health_options);
        for (std::size_t index = 1; index < health_sorted.matches.size(); ++index) {
            const auto& left = health_sorted.matches[index - 1].start;
            const auto& right = health_sorted.matches[index].start;
            require(left.red_hearts < right.red_hearts
                        || (left.red_hearts == right.red_hearts
                            && (left.soul_hearts < right.soul_hearts
                                || (left.soul_hearts == right.soul_hearts
                                    && left.seed < right.seed))),
                    "health sort did not compare red hearts before soul hearts");
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
