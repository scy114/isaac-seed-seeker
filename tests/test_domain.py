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
