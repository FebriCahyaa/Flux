#!/usr/bin/env python3
"""
telegram.py — Telegram build notification for Flux Tweaks CI.

Usage:
  telegram.py <flashable-zip> <success|failure|cancelled>

Environment:
  BOT_TOKEN, CHAT_ID       required (skipped when missing)
  TG_DEV_ID                optional, pinged on failures
  TITLE, VERSION           project title and build number (from build.yml)
  COMMIT_MESSAGE, COMMIT_URL, RUN_URL   from the workflow event
  TG_DRY_RUN=1             print the payload instead of sending

Posts a detailed build card (HTML, expandable quotes, inline buttons) and the
flashable zip as a reply. Sending is best effort and never fails the build.
"""

import hashlib
import html
import json
import os
import re
import subprocess
import sys
import urllib.error
import urllib.request
import uuid
from datetime import datetime, timezone
from pathlib import Path

ENV = os.environ
TITLE = ENV.get("TITLE", "Flux Tweaks")
TAGLINE = "Adaptive gaming & battery optimization"
MESSAGE_LIMIT = 4096
CAPTION_LIMIT = 1024

REPO = ENV.get("GITHUB_REPOSITORY", "FebriCahyaa/Flux")
SERVER = ENV.get("GITHUB_SERVER_URL", "https://github.com")
REPO_URL = f"{SERVER}/{REPO}"
SHA = ENV.get("GITHUB_SHA", "")
RUN_URL = ENV.get("RUN_URL") or f"{REPO_URL}/actions/runs/{ENV.get('GITHUB_RUN_ID', '')}"
COMMIT_URL = ENV.get("COMMIT_URL") or f"{REPO_URL}/commit/{SHA}"
ACTOR = ENV.get("GITHUB_ACTOR", "unknown")

ABI_NAMES = {"arm64-v8a": "arm64", "armeabi-v7a": "arm", "x86_64": "x86_64", "x86": "x86"}


# ── helpers ──────────────────────────────────────────────────────────────────

def esc(text) -> str:
    return html.escape(str(text), quote=False)


def link(url: str, label: str) -> str:
    return f'<a href="{html.escape(url, quote=True)}">{esc(label)}</a>'


def code(text) -> str:
    return f"<code>{esc(text)}</code>"


def tree(rows) -> str:
    rows = [r for r in rows if r is not None]
    return "\n".join(
        f"{'└' if i == len(rows) - 1 else '├'} {esc(label)}: {value}" for i, (label, value) in enumerate(rows)
    )


def git(*args) -> str:
    try:
        return subprocess.run(["git", *args], capture_output=True, text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return ""


def human_size(num_bytes: int) -> str:
    size = float(num_bytes)
    for unit in ("B", "KB", "MB", "GB"):
        if size < 1024 or unit == "GB":
            return f"{size:.0f} {unit}" if unit == "B" else f"{size:.1f} {unit}"
        size /= 1024
    return f"{num_bytes} B"


def now_utc() -> str:
    return datetime.now(timezone.utc).strftime("%d %b %Y · %H:%M UTC")


def hashtag(text: str) -> str:
    return "#" + re.sub(r"[^0-9A-Za-z_]", "_", text)


def strip_trailers(message: str) -> str:
    """Drop git trailers (Co-Authored-By, Signed-off-by, ...)."""
    return "\n".join(l for l in message.splitlines() if not re.match(r"^[A-Za-z][\w-]*: \S", l)).strip()


def fit(text: str, limit: int) -> str:
    return text if len(text) <= limit else text[: limit - 1] + "…"


# ── Telegram API ─────────────────────────────────────────────────────────────

def api(method: str, fields: dict, file_field=None):
    if ENV.get("TG_DRY_RUN"):
        print(f"--- {method}{' + ' + str(file_field[1]) if file_field else ''}")
        print(fields.get("text") or fields.get("caption"))
        if "reply_markup" in fields:
            print("buttons:", [[b["text"] for b in row] for row in fields["reply_markup"]["inline_keyboard"]])
        return {"message_id": 1}

    url = f"https://api.telegram.org/bot{ENV['BOT_TOKEN']}/{method}"
    if file_field is None:
        body, headers = json.dumps(fields).encode(), {"Content-Type": "application/json"}
    else:
        name, path = file_field
        boundary = uuid.uuid4().hex
        parts = [
            f'--{boundary}\r\nContent-Disposition: form-data; name="{k}"\r\n\r\n'
            f"{json.dumps(v) if isinstance(v, (dict, list)) else v}\r\n".encode()
            for k, v in fields.items()
        ]
        parts.append(
            f'--{boundary}\r\nContent-Disposition: form-data; name="{name}"; filename="{Path(path).name}"\r\n'
            f"Content-Type: application/zip\r\n\r\n".encode() + Path(path).read_bytes() + b"\r\n"
        )
        parts.append(f"--{boundary}--\r\n".encode())
        body, headers = b"".join(parts), {"Content-Type": f"multipart/form-data; boundary={boundary}"}

    request = urllib.request.Request(url, data=body, headers=headers, method="POST")
    try:
        with urllib.request.urlopen(request, timeout=120) as response:
            result = json.load(response)
    except urllib.error.HTTPError as error:
        result = {"ok": False, "description": error.read().decode(errors="replace")}
    except (urllib.error.URLError, OSError) as error:
        result = {"ok": False, "description": str(error)}

    if result.get("ok"):
        print(f"Telegram {method}: sent")
        return result.get("result", {})
    print(f"WARNING: Telegram {method} failed: {result.get('description')}", file=sys.stderr)
    return None


def buttons(*rows):
    return {"inline_keyboard": [[{"text": t, "url": u} for t, u in row if u] for row in rows if row]}


def send_message(text: str, keyboard=None):
    fields = {
        "chat_id": ENV["CHAT_ID"],
        "text": fit(text, MESSAGE_LIMIT),
        "parse_mode": "HTML",
        "link_preview_options": {"is_disabled": True},
    }
    if keyboard:
        fields["reply_markup"] = keyboard
    return api("sendMessage", fields)


def send_document(path: str, caption: str, reply_to=None):
    fields = {"chat_id": ENV["CHAT_ID"], "caption": fit(caption, CAPTION_LIMIT), "parse_mode": "HTML"}
    if reply_to:
        fields["reply_parameters"] = {"message_id": reply_to, "allow_sending_without_reply": True}
    return api("sendDocument", fields, ("document", path))


# ── build facts ──────────────────────────────────────────────────────────────

def synthesiscore_info() -> str:
    manifest = Path("prebuilt/synthesiscore.json")
    try:
        data = json.loads(manifest.read_text())
        tag = data.get("tag", "?")
        url = f"{SERVER}/{data.get('repository', 'FebriCahyaa/SynthesisCore')}/releases/tag/{tag}"
        return f"{link(url, tag)} · ✅ signature &amp; checksum verified"
    except (OSError, ValueError):
        return "legacy prebuilt · 🔒 checksum pinned"


def architectures() -> str:
    libs = Path("libs")
    abis = sorted(p.name for p in libs.iterdir() if p.is_dir()) if libs.is_dir() else []
    return " · ".join(ABI_NAMES.get(a, a) for a in abis) or "—"


def commit_message() -> str:
    message = ENV.get("COMMIT_MESSAGE") or git("log", "-1", "--format=%B")
    return strip_trailers(message) or "(no message)"


def changed_files() -> tuple:
    stat = git("show", "--shortstat", "--format=", "HEAD")
    numbers = [int(n) for n in re.findall(r"(\d+) (?:file|insertion|deletion)", stat)]
    files = numbers[0] if numbers else 0
    plus = int(m.group(1)) if (m := re.search(r"(\d+) insertion", stat)) else 0
    minus = int(m.group(1)) if (m := re.search(r"(\d+) deletion", stat)) else 0
    return files, plus, minus


# ── command ──────────────────────────────────────────────────────────────────

def notify(zip_path: str, status: str):
    version = (Path("version").read_text().strip() if Path("version").is_file() else "?")
    build = ENV.get("VERSION") or git("rev-list", "--count", "HEAD") or "?"
    branch = ENV.get("GITHUB_REF_NAME", "unknown")
    event = ENV.get("GITHUB_EVENT_NAME", "push").replace("_", " ")
    author = git("log", "-1", "--format=%an") or ACTOR
    zip_file = Path(zip_path) if zip_path else None
    tags = f"{hashtag(TITLE)} #ci {hashtag('build_' + str(build))}"

    commit_rows = [
        ("Branch", code(branch)),
        ("Commit", link(COMMIT_URL, SHA[:7] or "HEAD")),
        ("Author", link(f"{SERVER}/{ACTOR}", author)),
        ("Trigger", esc(event)),
        ("Time", esc(now_utc())),
    ]

    if status != "success":
        icon, word = ("⏹", "cancelled") if status == "cancelled" else ("❌", "failed")
        dev = ENV.get("TG_DEV_ID", "").strip()
        ping = f'\n\n⚠️ <a href="tg://user?id={esc(dev)}">Developer</a>, please take a look.' if dev.isdigit() else ""
        text = (
            f"{icon} <b>{esc(TITLE)}</b>: build #{esc(build)} {word}\n<i>{esc(TAGLINE)}</i>\n\n"
            f"🧾 <b>Commit</b>\n{tree(commit_rows)}\n\n"
            f"💬 <b>Message</b>\n<blockquote expandable>{esc(fit(commit_message(), 700))}</blockquote>"
            f"{ping}\n\n{tags} #failed"
        )
        send_message(text, buttons([("⚙️ Workflow log", RUN_URL), ("🔖 Commit", COMMIT_URL)]))
        return

    files, plus, minus = changed_files()
    total = plus + minus
    filled = round(10 * plus / total) if total else 0
    bar = ("🟩" * filled + "🟥" * (10 - filled)) if total else "⬜" * 10

    package_rows = [
        ("File", code(zip_file.name)),
        ("Size", human_size(zip_file.stat().st_size)),
        ("SHA-256", code(hashlib.sha256(zip_file.read_bytes()).hexdigest())),
    ] if zip_file and zip_file.is_file() else [("File", "not produced")]

    head = (
        f"🚀 <b>{esc(TITLE)}</b> v{esc(version)}: build #{esc(build)}\n<i>{esc(TAGLINE)}</i>\n\n"
        f"🧾 <b>Commit</b>\n{tree(commit_rows)}\n\n"
        f"🧩 <b>Components</b>\n"
        + tree([
            ("Daemon", f"fluxd · {esc(architectures())}"),
            ("SynthesisCore", synthesiscore_info()),
            ("WebUI", "Vue 3 · 10 languages"),
            ("Root", "Magisk · KernelSU · APatch"),
        ])
        + f"\n\n📦 <b>Package</b>\n{tree(package_rows)}\n\n"
        f"📊 <b>Changes</b>\n"
        + tree([("Files", f"<b>{files}</b>"), ("Lines", f"<b>+{plus}</b> / <b>−{minus}</b>"), ("Balance", bar)])
    )
    tail = (
        "\n\n🛡 <b>Install</b>\nFlash in your root manager and reboot. The installer verifies every file's "
        f"SHA-256 and aborts on any mismatch.\n\n{tags}"
    )
    budget = MESSAGE_LIMIT - len(head) - len(tail) - 80
    message = f"\n\n💬 <b>Message</b>\n<blockquote expandable>{esc(fit(commit_message(), budget))}</blockquote>"

    sent = send_message(head + message + tail, buttons(
        [("📥 Download (artifact)", RUN_URL), ("🔖 Commit", COMMIT_URL)],
        [("📂 Repository", REPO_URL)],
    ))

    if zip_file and zip_file.is_file():
        caption = (
            f"📦 <b>{esc(TITLE)}</b> v{esc(version)} · build #{esc(build)}\n"
            f"<i>Flash in Magisk, KernelSU or APatch, then reboot.</i>"
        )
        send_document(str(zip_file), caption, reply_to=(sent or {}).get("message_id"))


def main(argv):
    if not ENV.get("BOT_TOKEN") or not ENV.get("CHAT_ID"):
        print("BOT_TOKEN / CHAT_ID not set, skipping Telegram notification")
        return 0
    zip_path = argv[1] if len(argv) > 1 else ""
    status = argv[2] if len(argv) > 2 else "success"
    notify(zip_path, status)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
