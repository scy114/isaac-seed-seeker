from pathlib import Path

from isaac_seed_seeker.profile import detect_profile


def test_detect_profile_from_log_and_extracted_items(tmp_path: Path) -> None:
    log = tmp_path / "log.txt"
    log.write_text(
        "[INFO] - Binding of Isaac: Repentance+ v1.9.7.17.J460\n"
        "[INFO] - LOADED MOD F:/game/mods/example_mod/content/\n",
        encoding="utf-8",
    )
    resources = tmp_path / "game" / "extracted_resources" / "resources"
    resources.mkdir(parents=True)
    (resources / "items.xml").write_text(
        '<items><passive id="1"/><active id="732"/></items>',
        encoding="utf-8",
    )
    profile = detect_profile("test-j460", log, tmp_path / "game")
    assert profile.game_version == "v1.9.7.17.J460"
    assert profile.game_build == "J460"
    assert profile.item_count == 732
    assert "items.xml" in profile.resources
    assert profile.loaded_mods == ("example_mod",)
    assert len(profile.mods_hash) == 64
