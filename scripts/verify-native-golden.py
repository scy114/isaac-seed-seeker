"""Verify a native full-search JSON artifact against the checked-in manifest."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "tests" / "fixtures" / "j460-target-169-golden.json"


def sha256(value: str) -> str:
    return hashlib.sha256(value.encode("utf-8")).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("result", type=Path)
    arguments = parser.parse_args()

    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))["corrected_result"]
    result = json.loads(arguments.result.read_text(encoding="utf-8"))
    matches = result["matches"]
    tuples = "".join(
        f"{row['seed_u32']}\t{row['seed']}\t{row['trinket_id']}\t"
        f"{row['active_id']}\t{row['passive_id']}\n"
        for row in matches
    )
    u32s = "".join(f"{row['seed_u32']}\n" for row in matches)
    actual = {
        "count": len(matches),
        "ordered_tuple_sha256": sha256(tuples),
        "ordered_u32_sha256": sha256(u32s),
    }
    expected = {key: manifest[key] for key in actual}
    if actual != expected:
        raise SystemExit(f"golden mismatch\nexpected={expected}\nactual={actual}")
    print(json.dumps(actual, indent=2))


if __name__ == "__main__":
    main()
