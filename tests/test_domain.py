from isaac_seed_seeker.domain import EdenFilter, EdenObservation


def observation() -> EdenObservation:
    return EdenObservation.from_dict(
        {
            "schema_version": 1,
            "profile_id": "local-j460",
            "game_version": "v1.9.7.17.J460",
            "seed": "MASV SYFS",
            "seed_u32": 1473169325,
            "stats": {
                "damage": 4.2,
                "tears": 2.73,
                "move_speed": 1.1,
                "shot_speed": 0.9,
                "luck": -0.25,
            },
            "health": {"red_hearts": 1.0, "soul_hearts": 2.0},
            "pickups": {"coins": 3, "keys": 1, "bombs": 0},
            "active_items": [105],
            "passive_items": [182, 331],
            "pocket": {"card": 1, "pill": 0, "trinket": 0},
        }
    )


def test_filter_matches_ranges_items_and_pocket() -> None:
    eden_filter = EdenFilter.from_dict(
        {
            "damage": {"min": 4.0},
            "coins": {"min": 1, "max": 5},
            "active_items": {"all": [105]},
            "passive_items": {"any": [331, 999], "none": [36]},
            "card": 1,
        }
    )
    assert eden_filter.matches(observation())


def test_filter_rejects_missing_or_forbidden_values() -> None:
    assert not EdenFilter.from_dict({"damage": {"min": 5}}).matches(observation())
    assert not EdenFilter.from_dict({"passive_items": {"none": [182]}}).matches(observation())
    assert not EdenFilter.from_dict({"trinket": 12}).matches(observation())


def test_trinket_filter_accepts_normal_and_golden_base_id() -> None:
    normal = observation().to_dict()
    normal["pocket"]["trinket"] = 169
    golden = observation().to_dict()
    golden["pocket"].update(
        {"trinket": 169, "trinket_raw": 0x8000 | 169, "trinket_golden": 1}
    )

    target = EdenFilter.from_dict({"trinket": 169})
    assert target.matches(EdenObservation.from_dict(normal))
    assert target.matches(EdenObservation.from_dict(golden))


def test_trinket_filter_normalizes_legacy_raw_golden_value() -> None:
    legacy = observation().to_dict()
    legacy["pocket"]["trinket"] = 0x8000 | 169
    assert EdenFilter.from_dict({"trinket": 169}).matches(EdenObservation.from_dict(legacy))


def test_target_conditions_are_and_between_groups_and_or_within_groups() -> None:
    target = EdenFilter.from_dict(
        {
            "trinket": 169,
            "active_items": {"any": [145, 133]},
            "passive_items": {"any": [81, 134, 187, 212, 665]},
        }
    )
    matching = observation().to_dict()
    matching.update({"active_items": [133], "passive_items": [212]})
    matching["pocket"]["trinket"] = 169
    assert target.matches(EdenObservation.from_dict(matching))

    wrong_active = dict(matching)
    wrong_active["active_items"] = [105]
    assert not target.matches(EdenObservation.from_dict(wrong_active))

    wrong_passive = dict(matching)
    wrong_passive["passive_items"] = [331]
    assert not target.matches(EdenObservation.from_dict(wrong_passive))

    wrong_trinket = dict(matching)
    wrong_trinket["pocket"] = {**matching["pocket"], "trinket": 168}
    assert not target.matches(EdenObservation.from_dict(wrong_trinket))
