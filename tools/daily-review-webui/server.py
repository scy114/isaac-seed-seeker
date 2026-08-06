#!/usr/bin/env python3
"""Disposable local WebUI for calibrating daily-good seed rules."""

from __future__ import annotations

import argparse
import csv
import json
import mimetypes
import os
import re
import subprocess
import tempfile
import threading
import webbrowser
from datetime import date
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import unquote, urlparse


HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[1]
DEFAULT_CANDIDATES = 10_000_000
MAX_DAYS = 100


def find_executable(explicit: Path | None) -> Path:
    candidates = [
        explicit,
        REPO_ROOT / "build" / "native" / "IsaacSeedSeeker.exe",
        REPO_ROOT / "dist" / "IsaacSeedSeeker.exe",
    ]
    for candidate in candidates:
        if candidate and candidate.is_file():
            return candidate.resolve()
    raise FileNotFoundError(
        "找不到 IsaacSeedSeeker.exe；请先构建项目，或使用 --exe 指定路径。"
    )


def load_item_names() -> dict[tuple[str, int], dict[str, object]]:
    catalog_path = REPO_ROOT / "data" / "catalog" / "eden-start-catalog.j460.json"
    with catalog_path.open("r", encoding="utf-8") as stream:
        catalog = json.load(stream)
    result: dict[tuple[str, int], dict[str, object]] = {}
    for entry in catalog["entries"]:
        kind = entry.get("kind")
        if kind not in {"active", "passive"} or entry.get("variant") != "normal":
            continue
        result[(kind, int(entry["search_id"]))] = {
            "name_zh": entry.get("name_zh") or entry.get("name_en") or "未知道具",
            "name_en": entry.get("name_en") or "",
            "quality": entry.get("quality"),
        }
    return result


def locate_collectible_icons(explicit_game_dir: Path | None) -> dict[int, Path]:
    candidates = [
        explicit_game_dir,
        Path(r"F:\steam\steamapps\common\The Binding of Isaac Rebirth"),
        Path(os.environ.get("PROGRAMFILES(X86)", r"C:\Program Files (x86)"))
        / "Steam"
        / "steamapps"
        / "common"
        / "The Binding of Isaac Rebirth",
    ]
    directory: Path | None = None
    for game_dir in candidates:
        if game_dir is None:
            continue
        possible = (
            game_dir
            / "extracted_resources"
            / "resources"
            / "gfx"
            / "items"
            / "collectibles"
        )
        if possible.is_dir():
            directory = possible
            break
    if directory is None:
        return {}

    result: dict[int, Path] = {}
    pattern = re.compile(r"^collectibles_(\d+)_", re.IGNORECASE)
    for path in directory.glob("*.png"):
        match = pattern.match(path.stem)
        if match:
            result.setdefault(int(match.group(1)), path)
    return result


def parse_generation_request(payload: dict[str, object]) -> tuple[str, int, int]:
    start_date = str(payload.get("start_date", ""))
    try:
        date.fromisoformat(start_date)
    except ValueError as error:
        raise ValueError("起始日期格式应为 YYYY-MM-DD。") from error

    try:
        days = int(payload.get("days", 30))
        candidates = int(payload.get("candidates", DEFAULT_CANDIDATES))
    except (TypeError, ValueError) as error:
        raise ValueError("样本天数和候选数必须是整数。") from error
    if not 1 <= days <= MAX_DAYS:
        raise ValueError(f"样本天数必须在 1 到 {MAX_DAYS} 之间。")
    if candidates != DEFAULT_CANDIDATES:
        raise ValueError(f"当前校准版本固定每天扫描 {DEFAULT_CANDIDATES:,} 个候选。")
    return start_date, days, candidates


class ReviewApplication:
    def __init__(self, executable: Path, game_dir: Path | None):
        self.executable = executable
        self.items = load_item_names()
        self.icons = locate_collectible_icons(game_dir)

    def generate(self, payload: dict[str, object]) -> dict[str, object]:
        start_date, days, candidates = parse_generation_request(payload)
        with tempfile.TemporaryDirectory(prefix="isaac-daily-review-") as temporary:
            output_path = Path(temporary) / "daily-good.csv"
            command = [
                str(self.executable),
                "simulate-daily-good",
                "--start-date",
                start_date,
                "--days",
                str(days),
                "--candidates",
                str(candidates),
                "--output",
                str(output_path),
            ]
            creation_flags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
            completed = subprocess.run(
                command,
                cwd=REPO_ROOT,
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace",
                timeout=120,
                creationflags=creation_flags,
                check=False,
            )
            if completed.returncode != 0:
                message = completed.stderr.strip() or completed.stdout.strip()
                raise RuntimeError(message or "C++ 内核生成样本失败。")
            with output_path.open("r", encoding="utf-8", newline="") as stream:
                rows = list(csv.DictReader(stream))

        results = [self._decorate_row(row) for row in rows]
        return {
            "rules_version": "daily-good-v0",
            "start_date": start_date,
            "days": days,
            "candidates_per_day": candidates,
            "icons_available": bool(self.icons),
            "results": results,
        }

    def _decorate_row(self, row: dict[str, str]) -> dict[str, object]:
        integers = {
            "seed_u32",
            "weight",
            "active_id",
            "active_quality",
            "passive_id",
            "passive_quality",
            "active_q4_bonus",
            "passive_q4_bonus",
            "death_certificate_bonus",
            "damage_bonus",
            "tears_bonus",
            "move_speed_bonus",
            "eligible",
            "scanned",
        }
        decimals = {"damage", "tears", "move_speed", "elapsed_seconds"}
        result: dict[str, object] = {}
        for key, value in row.items():
            if key in integers:
                result[key] = int(value)
            elif key in decimals:
                result[key] = float(value)
            else:
                result[key] = value
        for kind in ("active", "passive"):
            item_id = int(result[f"{kind}_id"])
            item = self.items.get((kind, item_id), {})
            result[f"{kind}_name_zh"] = item.get("name_zh", f"道具 #{item_id}")
            result[f"{kind}_name_en"] = item.get("name_en", "")
            result[f"{kind}_has_icon"] = item_id in self.icons
        return result


class ReviewHandler(BaseHTTPRequestHandler):
    server_version = "IsaacDailyReview/0.1"

    @property
    def app(self) -> ReviewApplication:
        return self.server.app  # type: ignore[attr-defined]

    def do_GET(self) -> None:  # noqa: N802
        path = unquote(urlparse(self.path).path)
        if path in {"/", "/index.html"}:
            self._send_file(HERE / "index.html")
            return
        if path == "/app.js":
            self._send_file(HERE / "app.js")
            return
        if path == "/style.css":
            self._send_file(HERE / "style.css")
            return
        match = re.fullmatch(r"/icon/(\d+)\.png", path)
        if match:
            icon = self.app.icons.get(int(match.group(1)))
            if icon:
                self._send_file(icon)
                return
        self.send_error(HTTPStatus.NOT_FOUND)

    def do_POST(self) -> None:  # noqa: N802
        if urlparse(self.path).path != "/api/generate":
            self.send_error(HTTPStatus.NOT_FOUND)
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if length > 64 * 1024:
                raise ValueError("请求体过大。")
            payload = json.loads(self.rfile.read(length) or b"{}")
            response = self.app.generate(payload)
            self._send_json(HTTPStatus.OK, response)
        except (ValueError, json.JSONDecodeError) as error:
            self._send_json(HTTPStatus.BAD_REQUEST, {"error": str(error)})
        except subprocess.TimeoutExpired:
            self._send_json(HTTPStatus.GATEWAY_TIMEOUT, {"error": "生成超过 120 秒。"})
        except Exception as error:  # local debugging endpoint: surface the actual failure
            self._send_json(HTTPStatus.INTERNAL_SERVER_ERROR, {"error": str(error)})

    def _send_json(self, status: HTTPStatus, value: object) -> None:
        body = json.dumps(value, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _send_file(self, path: Path) -> None:
        body = path.read_bytes()
        content_type = mimetypes.guess_type(path.name)[0] or "application/octet-stream"
        if content_type.startswith("text/") or content_type in {"application/javascript"}:
            content_type += "; charset=utf-8"
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, format: str, *args: object) -> None:
        print(f"[daily-review] {self.address_string()} {format % args}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Daily-good rule calibration WebUI")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--exe", type=Path)
    parser.add_argument("--game-dir", type=Path)
    parser.add_argument("--no-browser", action="store_true")
    return parser.parse_args()


def main() -> int:
    arguments = parse_args()
    application = ReviewApplication(find_executable(arguments.exe), arguments.game_dir)
    server = ThreadingHTTPServer(("127.0.0.1", arguments.port), ReviewHandler)
    server.app = application  # type: ignore[attr-defined]
    url = f"http://127.0.0.1:{arguments.port}/"
    print(f"爽种校准台：{url}")
    print(f"C++ 内核：{application.executable}")
    print(f"道具图标：{len(application.icons)} 个")
    if not arguments.no_browser:
        threading.Timer(0.35, lambda: webbrowser.open(url)).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
