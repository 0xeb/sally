# SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
# SPDX-License-Identifier: GPL-2.0-or-later
"""Generate deterministic display-only metadata from public release sources."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

MAX_TIME = 253402300799
ID = re.compile(r"[a-z0-9][a-z0-9-]{0,63}\Z")
VERSION = re.compile(r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\Z")


def unique(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON field: {key}")
        result[key] = value
    return result


def load_json(data):
    if isinstance(data, Path):
        data = data.read_bytes()
    if isinstance(data, bytes):
        data = data.decode("utf-8")
    return json.loads(data, object_pairs_hook=unique, parse_constant=lambda value: fail("non-finite JSON number"))


def fail(message):
    raise ValueError(message)


def encoded(model):
    return (json.dumps(model, ensure_ascii=False, sort_keys=True, indent=2, allow_nan=False) + "\n").encode("utf-8")


def languages(root):
    return re.findall(r'\{0x[0-9A-Fa-f]+, L"([a-z]{2}-[A-Z]{2})"', (root / "src/common/BuiltinLanguages.h").read_text(encoding="utf-8"))


def app_version(root):
    text = (root / "src/plugins/shared/spl_vers.h").read_text(encoding="utf-8")
    return ".".join(re.search(r"^#define VERSINFO_SALAMANDER_" + part + r"\s+(\d+)\s*$", text, re.M)[1] for part in ("MAJOR", "MINORA", "MINORB"))


def text(value, limit):
    if not isinstance(value, str) or "\0" in value or len(value.encode("utf-8")) > limit:
        fail("invalid text or byte limit exceeded")


def number(value):
    if type(value) is not int or not 0 <= value <= MAX_TIME:
        fail("invalid UTC timestamp")


def locales(value, maximum, tags=None, publication=False):
    if not isinstance(value, dict):
        fail("locales must be an object")
    if publication and set(value) != set(tags or []):
        fail("all supported locales are required")
    for record in value.values():
        if not isinstance(record, dict) or not {"title", "markdown"} <= record.keys():
            fail("locale requires title and markdown")
        text(record["title"], 256)
        text(record["markdown"], maximum)


def validate_runtime(model, tags=None, publication=False):
    if not isinstance(model, dict) or type(model.get("schema_version")) is not int or model["schema_version"] != 1:
        fail("unsupported metadata schema")
    kind = model.get("kind")
    if kind == "sally-release-metadata":
        if not isinstance(model.get("release_version"), str) or not VERSION.fullmatch(model["release_version"]):
            fail("invalid release version")
        locales(model.get("release_notes"), 65536, tags, publication)
    elif kind == "sally-live-messages":
        number(model.get("published_at"))
        number(model.get("valid_until"))
        if not 0 < model["valid_until"] - model["published_at"] <= 7 * 86400:
            fail("live feed lifetime must be positive and at most seven days")
    else:
        fail("unsupported metadata kind")
    announcements = model.get("announcements")
    if not isinstance(announcements, list) or len(announcements) > 16:
        fail("invalid announcement array")
    ids = set()
    for notice in announcements:
        if not isinstance(notice, dict) or not isinstance(notice.get("id"), str) or not ID.fullmatch(notice["id"]) or notice["id"] in ids:
            fail("invalid or duplicate notice ID")
        ids.add(notice["id"])
        target = notice.get("target_release", "*")
        if not isinstance(target, str) or (target != "*" and not VERSION.fullmatch(target)):
            fail("invalid notice target")
        for key in ("valid_from", "valid_until"):
            if key in notice:
                number(notice[key])
        if "valid_until" in notice and notice.get("valid_from", 0) >= notice["valid_until"]:
            fail("invalid notice validity")
        locales(notice.get("locales"), 16384, tags, publication)
        if publication:
            empties = [not record["markdown"].strip() for record in notice["locales"].values()]
            if any(empties) and not all(empties):
                fail("mixed empty/nonempty notice translations")
    if len(encoded(model)) > 512 * 1024:
        fail("metadata exceeds 512 KiB")
    return model


def source_hash(record):
    return hashlib.sha256(json.dumps(record, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")).hexdigest()


def source_locales(folder, titles, reviewed_hash, translation_hashes, tags, strict):
    records = {}
    if not isinstance(titles, dict) or not isinstance(translation_hashes, dict):
        fail("invalid translation descriptors")
    for tag in tags:
        path = folder / (tag + ".md")
        if not path.is_file() or tag not in titles:
            if strict:
                fail(f"missing translation: {tag}")
            continue
        body = path.read_text(encoding="utf-8")
        if body.startswith("\ufeff"):
            fail("author Markdown must be UTF-8 without BOM")
        records[tag] = {"title": titles[tag], "markdown": body}
    english = records.get("en-US")
    if not english:
        fail("English source is missing")
    digest = source_hash(english)
    if strict and (reviewed_hash != digest or any(translation_hashes.get(tag) != digest for tag in tags if tag != "en-US")):
        fail("translation review hashes are missing or stale")
    return records


def compile_source(root, strict=False):
    version, tags = app_version(root), languages(root)
    folder = root / "doc/releases" / version
    descriptor = load_json(folder / "release.json")
    if descriptor.get("schema_version") != 1 or descriptor.get("release_version") != version:
        fail("source version/header mismatch")
    model = {"schema_version": 1, "kind": "sally-release-metadata", "release_version": version,
             "release_notes": source_locales(folder / "notes", descriptor.get("notes_titles"), descriptor.get("notes_source_sha256"), descriptor.get("notes_translation_source_sha256", {}), tags, strict), "announcements": []}
    for notice in descriptor.get("announcements", []):
        kid = notice.get("id", "")
        if not ID.fullmatch(kid):
            fail("invalid source notice ID")
        item = {key: notice[key] for key in ("id", "target_release", "valid_from", "valid_until") if key in notice}
        item["locales"] = source_locales(folder / "notices" / kid, notice.get("titles"), notice.get("source_sha256"), notice.get("translation_source_sha256", {}), tags, strict)
        model["announcements"].append(item)
    return validate_runtime(model, tags, strict)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sally-root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--publication-check", action="store_true")
    args = parser.parse_args()
    try:
        model = compile_source(args.sally_root, args.publication_check)
    except (OSError, ValueError, KeyError, TypeError) as error:
        if args.publication_check:
            parser.error(str(error))
        print(f"metadata draft fallback: {error}", file=sys.stderr)
        model = {"schema_version": 1, "kind": "sally-release-metadata", "release_version": app_version(args.sally_root), "release_notes": {}, "announcements": []}
    data = encoded(model)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_bytes() != data:
        args.output.write_bytes(data)


if __name__ == "__main__":
    main()
