#!/usr/bin/env python3
"""Packs an Opus-MT model into one file for the local translation.

Needs: pip install ctranslate2 sentencepiece huggingface_hub transformers torch
Usage: pack_model.py ru-en [--out DIR]
"""
import argparse
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile

MAGIC = b"FGTMODEL"
EXTRA_FILES = ("source.spm", "target.spm")
SKIPPED = ("README.md",)


def convert(pair, directory):
    subprocess.run([
        "ct2-transformers-converter",
        "--model", f"Helsinki-NLP/opus-mt-{pair}",
        "--output_dir", directory,
        "--quantization", "int8",
        "--force",
    ], check=True)
    from huggingface_hub import hf_hub_download
    for name in EXTRA_FILES:
        path = hf_hub_download(f"Helsinki-NLP/opus-mt-{pair}", name)
        shutil.copyfile(path, os.path.join(directory, name))


def pack(directory, target):
    names = sorted(
        name for name in os.listdir(directory)
        if name not in SKIPPED and os.path.isfile(os.path.join(directory, name)))
    with open(target, "wb") as out:
        out.write(MAGIC)
        out.write(struct.pack("<I", len(names)))
        for name in names:
            size = os.path.getsize(os.path.join(directory, name))
            raw = name.encode("ascii")
            out.write(struct.pack("<H", len(raw)) + raw + struct.pack("<Q", size))
        for name in names:
            with open(os.path.join(directory, name), "rb") as source:
                shutil.copyfileobj(source, out)
    return names


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as file:
        for block in iter(lambda: file.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("pair", help="for example ru-en")
    parser.add_argument("--out", default=".")
    args = parser.parse_args()
    if "/" in args.pair or args.pair.startswith("."):
        sys.exit("Bad pair.")
    target = os.path.join(args.out, f"{args.pair}.fgtm")
    with tempfile.TemporaryDirectory() as directory:
        model = os.path.join(directory, "model")
        convert(args.pair, model)
        names = pack(model, target)
    print(json.dumps({
        "kind": "translate",
        "file": os.path.basename(target),
        "code": args.pair,
        "size": os.path.getsize(target),
        "sha256": sha256(target),
        "files": names,
    }, indent=2))


if __name__ == "__main__":
    main()
