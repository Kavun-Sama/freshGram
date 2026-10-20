#!/usr/bin/env python3
"""Packs Opus-MT models into one file each for the local translation.

Needs: pip install ctranslate2 sentencepiece huggingface_hub transformers torch
Usage: pack_model.py ru-en en-ru ... [--out DIR]
For a model with another name use one pair and --repo, for example
pack_model.py ja-en --repo Helsinki-NLP/opus-mt-jap-en
A model of several target languages needs a token, for example
pack_model.py en-zh --prefix ">>cmn_Hans<<"
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


def convert(pair, repo, directory, prefix=None):
    subprocess.run([
        "ct2-transformers-converter",
        "--model", repo,
        "--output_dir", directory,
        "--quantization", "int8",
        "--force",
    ], check=True)
    from huggingface_hub import hf_hub_download
    for name in EXTRA_FILES:
        path = hf_hub_download(repo, name)
        shutil.copyfile(path, os.path.join(directory, name))
    if prefix:
        with open(os.path.join(directory, "prefix.txt"), "w", encoding="utf-8") as file:
            file.write(prefix)


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
    parser.add_argument("pairs", nargs="+", help="for example ru-en")
    parser.add_argument("--out", default=".")
    parser.add_argument("--repo", help="the model repository, for one pair")
    parser.add_argument("--prefix", help="the target language token, for one pair")
    args = parser.parse_args()
    if (args.repo or args.prefix) and len(args.pairs) != 1:
        sys.exit("--repo and --prefix work with one pair.")
    results = []
    for pair in args.pairs:
        if "/" in pair or pair.startswith("."):
            sys.exit("Bad pair.")
        repo = args.repo or f"Helsinki-NLP/opus-mt-{pair}"
        target = os.path.join(args.out, f"{pair}.fgtm")
        with tempfile.TemporaryDirectory() as directory:
            model = os.path.join(directory, "model")
            convert(pair, repo, model, args.prefix)
            names = pack(model, target)
        results.append({
            "file": os.path.basename(target),
            "code": pair,
            "size": os.path.getsize(target),
            "sha256": sha256(target),
            "files": names,
        })
    print(json.dumps(results, indent=2))


if __name__ == "__main__":
    main()
