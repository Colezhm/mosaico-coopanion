#!/usr/bin/env python3
"""Replace desktop/coopanion with an upstream Coopanion release plus the Mosaico patches.

desktop/coopanion is upstream Coopanion as released, with two kinds of local change:

* files listed under "keep" in upstream.json: Mosaico's own (its package, its console page,
  the runtime lock). Sync never touches them;
* patches/*.patch, applied in name order after each sync: the body relay seam (a candidate
  for upstream) and the app's Mosaico wiring and branding.

Usage (repository root):
    python desktop/upstream/sync.py                 # re-sync the pinned commit
    python desktop/upstream/sync.py --ref v0.1.27   # move the pin to another tag or commit
    python desktop/upstream/sync.py --stack DIR [--ref v0.1.27]
        # a git checkout of upstream at that commit with the patches as commits (git am -3),
        # for resolving a patch that no longer applies; see README.md here

A patch that no longer applies leaves *.rej files next to the files it touches and the
script exits non-zero; resolve it in a --stack checkout and export the patches again.
"""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
DEST = ROOT / "desktop" / "coopanion"
PIN = HERE / "upstream.json"
PATCHES = HERE / "patches"
CACHE = Path.home() / ".cache" / "mosaico-coopanion" / "Coopanion.git"
SUBMODULE = "vendor/cortico"
# Generated or installed locally; never part of a sync.
LOCAL = ("node_modules", "dist", "build", "out", ".DS_Store")


def run(*args: str, cwd: Path | None = None, capture: bool = False) -> str:
    done = subprocess.run(args, cwd=cwd, check=True, text=True, capture_output=capture)
    return done.stdout.strip() if capture else ""


def fetch(repo: str, ref: str) -> str:
    if not CACHE.exists():
        CACHE.parent.mkdir(parents=True, exist_ok=True)
        run("git", "clone", "--bare", "--quiet", repo, str(CACHE))
    run("git", "--git-dir", str(CACHE), "fetch", "--quiet", "--tags", "origin", "+refs/heads/*:refs/heads/*")
    return run("git", "--git-dir", str(CACHE), "rev-parse", f"{ref}^{{commit}}", capture=True)


def export(commit: str, into: Path) -> str:
    archive = into / "tree.tar"
    run("git", "--git-dir", str(CACHE), "archive", "--format=tar", "-o", str(archive), commit)
    tree = into / "tree"
    tree.mkdir()
    with tarfile.open(archive) as tar:
        tar.extractall(tree, filter="data")
    # the gitlink: Cortico's commit for this release
    line = run("git", "--git-dir", str(CACHE), "ls-tree", commit, SUBMODULE, capture=True)
    return line.split()[2]


def kept(rel: Path, keep: list[str]) -> bool:
    text = rel.as_posix()
    if any(part in LOCAL for part in rel.parts) or text == SUBMODULE or text.startswith(SUBMODULE + "/"):
        return True
    return any(text == k.rstrip("/") or text.startswith(k.rstrip("/") + "/") for k in keep)


def replace(tree: Path, keep: list[str]) -> None:
    # remove what upstream owned last time, leaving kept and local files
    for path in sorted(DEST.rglob("*"), key=lambda p: len(p.parts), reverse=True):
        rel = path.relative_to(DEST)
        if kept(rel, keep):
            continue
        if path.is_dir() and not path.is_symlink():
            if not any(path.iterdir()):
                path.rmdir()
        else:
            path.unlink()
    for path in sorted(tree.rglob("*")):
        rel = path.relative_to(tree)
        if rel.as_posix() == ".gitmodules" or kept(rel, keep):
            continue
        target = DEST / rel
        if path.is_dir():
            target.mkdir(parents=True, exist_ok=True)
        else:
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, target)


def apply_patches() -> list[str]:
    failed = []
    for patch in sorted(PATCHES.glob("*.patch")):
        base = ["git", "apply", "--directory=desktop/coopanion", "--whitespace=nowarn"]
        try:
            run(*base, str(patch), cwd=ROOT)
            print(f"applied {patch.name}")
        except subprocess.CalledProcessError:
            subprocess.run([*base, "--reject", str(patch)], cwd=ROOT)
            failed.append(patch.name)
    return failed


def pin_submodule(commit: str) -> None:
    sub = DEST / SUBMODULE
    if not (sub / ".git").exists():
        run("git", "submodule", "update", "--init", f"desktop/coopanion/{SUBMODULE}", cwd=ROOT)
    run("git", "-C", str(sub), "fetch", "--quiet", "origin", commit)
    run("git", "-C", str(sub), "checkout", "--quiet", commit)


def stack(directory: Path, repo: str, commit: str) -> int:
    """Upstream at `commit` in `directory`, with each patch as a commit on branch `mosaico`."""
    if directory.exists():
        print(f"{directory} exists; give a new directory", file=sys.stderr)
        return 2
    run("git", "clone", "--quiet", str(CACHE), str(directory))
    run("git", "-C", str(directory), "remote", "set-url", "origin", repo)
    run("git", "-C", str(directory), "checkout", "--quiet", "-b", "mosaico", commit)
    patches = [str(p) for p in sorted(PATCHES.glob("*.patch"))]
    done = subprocess.run(["git", "-C", str(directory), "am", "-3", *patches])
    print(f"stack in {directory} on {commit[:12]}; export with: git -C {directory} format-patch {commit[:12]} -o <dir>")
    return done.returncode


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--ref", help="upstream tag or commit to move to (default: the pinned commit)")
    parser.add_argument("--stack", type=Path, metavar="DIR", help="make a git checkout with the patches as commits instead of syncing")
    args = parser.parse_args()
    pin = json.loads(PIN.read_text())
    commit = fetch(pin["repo"], args.ref or pin["commit"])
    if args.stack:
        return stack(args.stack.resolve(), pin["repo"], commit)
    with tempfile.TemporaryDirectory() as tmp:
        cortico = export(commit, Path(tmp))
        replace(Path(tmp) / "tree", pin["keep"])
    pin_submodule(cortico)
    if args.ref:
        pin.update(ref=args.ref, commit=commit, cortico=cortico)
        PIN.write_text(json.dumps(pin, indent=2, ensure_ascii=False) + "\n")
    print(f"desktop/coopanion = {pin['repo']} {args.ref or pin['ref']} ({commit[:12]}), Cortico {cortico[:12]}")
    failed = apply_patches()
    if failed:
        print(f"patches that did not apply cleanly: {', '.join(failed)} (see *.rej)", file=sys.stderr)
        return 1
    print("next: cd desktop/coopanion && pnpm install && the checks in docs/coopanion-implementation.md")
    return 0


if __name__ == "__main__":
    sys.exit(main())
