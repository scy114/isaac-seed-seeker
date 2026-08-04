from isaac_seed_seeker.job import SearchJob
from isaac_seed_seeker.lua_job import render_lua_job
from isaac_seed_seeker.profile import GameProfile


def test_lua_job_embeds_profile_and_candidates() -> None:
    job = SearchJob.from_dict(
        {
            "schema_version": 1,
            "id": "golden",
            "profile_id": "local-j460",
            "candidates": {"values": ["MASV SYFS"]},
            "filter": {},
        }
    )
    profile = GameProfile(
        schema_version=1,
        id="local-j460",
        game_version="v1.9.7.17.J460",
        game_build="J460",
        game_dir="F:/game",
        item_count=732,
        resources={},
        loaded_mods=(),
        mods_hash="empty",
    )
    rendered = render_lua_job(job, profile)
    assert 'game_version = "v1.9.7.17.J460"' in rendered
    assert '"MASV SYFS"' in rendered
    assert "item_count = 732" in rendered
