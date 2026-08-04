"""Standard-library CLI for the Eden seed-search workflow."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Sequence

from .external_decoder import render_candidate_job, search_j460
from .job import SearchJob
from .logio import import_game_log, iter_jsonl, query_observations, write_jsonl
from .lua_job import render_lua_job
from .profile import detect_profile, load_profile
from .seed_codec import seed_to_string, string_to_seed


def _write_text(path: str | Path, text: str) -> None:
    output = Path(path)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(text, encoding="utf-8", newline="\n")


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="isaac-seed-seeker")
    commands = parser.add_subparsers(dest="command", required=True)

    seed = commands.add_parser("seed", help="encode or decode an Isaac run seed")
    seed.add_argument("mode", choices=("encode", "decode"))
    seed.add_argument("value")

    validate = commands.add_parser("validate", help="validate a SearchJob JSON file")
    validate.add_argument("job")

    candidates = commands.add_parser("candidates", help="write validated candidate seed strings")
    candidates.add_argument("job")
    candidates.add_argument("--output", required=True)

    detect = commands.add_parser("detect-profile", help="detect a local Repentance+ profile")
    detect.add_argument("--id", default="local-j460")
    detect.add_argument("--log", required=True)
    detect.add_argument("--game-dir", required=True)
    detect.add_argument("--output", required=True)

    compile_job = commands.add_parser("compile-job", help="compile a SearchJob for the Lua observer")
    compile_job.add_argument("job")
    compile_job.add_argument("--profile")
    compile_job.add_argument("--output", default="mod/isaac_seed_seeker/generated_job.lua")

    import_log = commands.add_parser("import-log", help="import observer records from game log.txt")
    import_log.add_argument("log")
    import_log.add_argument("--output", required=True)

    query = commands.add_parser("query", help="filter imported Eden observations")
    query.add_argument("observations")
    query.add_argument("--job", required=True)
    query.add_argument("--output", required=True)

    search = commands.add_parser(
        "search-j460", help="prefilter an Eden item target with an external J460 decoder"
    )
    search.add_argument("job")
    search.add_argument("--decoder-dir", required=True)
    search.add_argument("--proc-table", required=True)
    search.add_argument("--trinket-pool", required=True)
    search.add_argument("--start", type=lambda value: int(value, 0), default=1)
    search.add_argument("--max-scan", type=int, default=5_000_000)
    search.add_argument("--output", required=True)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = _build_parser().parse_args(argv)
    try:
        if args.command == "seed":
            if args.mode == "encode":
                print(seed_to_string(int(args.value, 0)))
            else:
                print(string_to_seed(args.value))
            return 0

        if args.command == "validate":
            job = SearchJob.load(args.job)
            count = sum(1 for _ in job.candidates.iter_seed_strings())
            print(json.dumps({"ok": True, "job_id": job.id, "candidate_count": count}))
            return 0

        if args.command == "candidates":
            job = SearchJob.load(args.job)
            _write_text(args.output, "\n".join(job.candidates.iter_seed_strings()) + "\n")
            return 0

        if args.command == "detect-profile":
            profile = detect_profile(args.id, args.log, args.game_dir)
            _write_text(args.output, json.dumps(profile.to_dict(), ensure_ascii=False, indent=2) + "\n")
            print(json.dumps(profile.to_dict(), ensure_ascii=False))
            return 0

        if args.command == "compile-job":
            job = SearchJob.load(args.job)
            profile_path = Path(args.profile) if args.profile else Path("data/profiles") / f"{job.profile_id}.json"
            profile = load_profile(profile_path) if profile_path.is_file() else None
            _write_text(args.output, render_lua_job(job, profile))
            print(args.output)
            return 0

        if args.command == "import-log":
            count = write_jsonl(args.output, import_game_log(args.log))
            print(json.dumps({"imported": count, "output": args.output}))
            return 0

        if args.command == "query":
            job = SearchJob.load(args.job)
            matches = query_observations(iter_jsonl(args.observations), job)
            count = write_jsonl(args.output, matches)
            print(json.dumps({"matched": count, "output": args.output}))
            return 0

        if args.command == "search-j460":
            job = SearchJob.load(args.job)
            result = search_j460(
                job,
                decoder_dir=args.decoder_dir,
                proc_table=args.proc_table,
                trinket_pool=args.trinket_pool,
                start_u32=args.start,
                max_scan=args.max_scan,
            )
            report = result.to_dict()
            if result.matches:
                _write_text(args.output, render_candidate_job(args.job, result))
                report["output"] = args.output
            else:
                report["output"] = None
            print(json.dumps(report, ensure_ascii=False))
            return 0
    except (ImportError, OSError, RuntimeError, ValueError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
