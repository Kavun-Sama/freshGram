#!/usr/bin/env python3
"""Adds packed translation models to manifest.json of freshgram_models.

Usage: manifest_add.py manifest.json ru-en.fgtm:31 en-ru.fgtm:32 ... [--out FILE]
Each argument is a packed file and the number of its post in the channel.
"""
import argparse
import hashlib
import json
import os
import re
import sys

CHANNEL = "freshgram_models"
CODE = re.compile(r"^[a-z]{2,3}-[a-z]{2,3}$")
MAX_SIZE = 200 * 1024 * 1024
NAMES = {
    "ar": "Arabic", "bg": "Bulgarian", "cs": "Czech", "da": "Danish",
    "de": "German", "el": "Greek", "en": "English", "es": "Spanish",
    "fi": "Finnish", "fr": "French", "he": "Hebrew", "hi": "Hindi",
    "hu": "Hungarian", "id": "Indonesian", "it": "Italian",
    "ja": "Japanese", "ko": "Korean", "nl": "Dutch", "pl": "Polish",
    "pt": "Portuguese", "ro": "Romanian", "ru": "Russian",
    "sv": "Swedish", "tr": "Turkish", "uk": "Ukrainian",
    "vi": "Vietnamese", "zh": "Chinese",
}


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as file:
        for block in iter(lambda: file.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("manifest")
    parser.add_argument("items", nargs="+", help="file.fgtm:POST")
    parser.add_argument("--out", help="where to write, by default next to the input")
    args = parser.parse_args()

    with open(args.manifest, encoding="utf-8") as file:
        data = json.load(file)
    models = data.setdefault("models", [])
    next_id = max([m.get("id", 0) for m in models] + [0]) + 1
    known = {(m.get("kind"), m.get("code")): m for m in models}

    for item in args.items:
        path, _, post = item.rpartition(":")
        if not path or not post.isdigit():
            sys.exit(f"Bad argument {item!r}, expected file.fgtm:POST")
        code = os.path.basename(path)[: -len(".fgtm")]
        if not path.endswith(".fgtm") or not CODE.match(code):
            sys.exit(f"Bad file name {path!r}, expected like ru-en.fgtm")
        size = os.path.getsize(path)
        if size > MAX_SIZE:
            sys.exit(f"{path} is too big ({size} bytes), the limit is {MAX_SIZE}")
        source, target = code.split("-")
        entry = {
            "id": next_id,
            "kind": "translate",
            "name": f"{NAMES.get(source, source)} → {NAMES.get(target, target)}",
            "file": os.path.basename(path),
            "code": code,
            "size": size,
            "sha256": sha256(path),
            "location": f"{CHANNEL}#{int(post)}",
        }
        old = known.get(("translate", code))
        if old:
            entry["id"] = old["id"]
            models[models.index(old)] = entry
            print(f"updated {code}")
        else:
            models.append(entry)
            next_id += 1
            print(f"added {code} (id {entry['id']}, post {post})")

    out = args.out or os.path.join(
        os.path.dirname(os.path.abspath(args.manifest)), "manifest.new.json")
    with open(out, "w", encoding="utf-8") as file:
        json.dump(data, file, ensure_ascii=False, indent=2)
        file.write("\n")
    print("written", out)


if __name__ == "__main__":
    main()
