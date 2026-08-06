import json
import subprocess
import sys
from collections import Counter
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RATINGS = ROOT / "data" / "daily-good" / "q3-ratings.j460.json"
CATALOG = ROOT / "data" / "catalog" / "eden-start-catalog.j460.json"
GENERATED = ROOT / "native" / "generated" / "daily_q3_ratings_j460.hpp"
GENERATOR = ROOT / "scripts" / "generate-daily-q3-ratings.py"


def test_daily_q3_ratings_cover_the_full_eden_pool() -> None:
    payload = json.loads(RATINGS.read_text(encoding="utf-8"))
    catalog = json.loads(CATALOG.read_text(encoding="utf-8"))
    expected = {
        f"{entry['kind']}:{int(entry['search_id'])}"
        for entry in catalog["entries"]
        if entry.get("variant") == "normal"
        and entry.get("kind") in {"active", "passive"}
        and entry.get("quality") == 3
        and entry.get("available_for_eden")
    }
    ratings = payload["ratings"]
    assert set(ratings) == expected
    assert sum(key.startswith("active:") for key in ratings) == 27
    assert sum(key.startswith("passive:") for key in ratings) == 156
    assert Counter(ratings.values()) == {1: 17, 2: 83, 3: 52, 4: 31}
    assert payload["weight_bonus"] == {"0": 0, "1": 0, "2": 15, "3": 30, "4": 50}


def test_generated_daily_q3_header_is_current(tmp_path: Path) -> None:
    regenerated = tmp_path / "daily_q3_ratings_j460.hpp"
    subprocess.run(
        [sys.executable, str(GENERATOR), "--output", str(regenerated)],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    assert regenerated.read_bytes() == GENERATED.read_bytes()
