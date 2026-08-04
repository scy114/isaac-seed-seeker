import json

from isaac_seed_seeker.domain import EdenObservation
from isaac_seed_seeker.job import SearchJob
from isaac_seed_seeker.logio import iter_log_observations, query_observations


def _record(profile_id: str = "local-j460") -> dict:
    return {
        "schema_version": 1,
        "profile_id": profile_id,
        "game_version": "v1.9.7.17.J460",
        "seed": "MASV SYFS",
        "seed_u32": 1473169325,
        "stats": {"damage": 4.2},
        "health": {"red_hearts": 1, "soul_hearts": 2},
        "pickups": {"coins": 3, "keys": 1, "bombs": 0},
        "active_items": [105],
        "passive_items": [182],
        "pocket": {"card": 1, "pill": 0, "trinket": 0},
    }


def test_import_ignores_noise_and_bad_records() -> None:
    payload = json.dumps(_record(), separators=(",", ":"))
    lines = [
        "ordinary game log line\n",
        "[INFO] - Lua Debug: ISAAC_SEED_SEEKER observation {bad}\n",
        f"[INFO] - Lua Debug: ISAAC_SEED_SEEKER observation {payload}\n",
    ]
    observations = list(iter_log_observations(lines))
    assert len(observations) == 1
    assert observations[0].seed_u32 == 1473169325


def test_query_enforces_profile_and_filter() -> None:
    job = SearchJob.from_dict(
        {
            "schema_version": 1,
            "id": "query",
            "profile_id": "local-j460",
            "candidates": {"values": ["MASV SYFS"]},
            "filter": {"damage": {"min": 4.0}},
            "max_results": 1,
        }
    )
    observations = [
        EdenObservation.from_dict(_record("other-profile")),
        EdenObservation.from_dict(_record()),
    ]
    matches = query_observations(observations, job)
    assert [item.seed for item in matches] == ["MASV SYFS"]
