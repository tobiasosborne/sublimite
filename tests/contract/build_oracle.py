"""Build the inspected v1 reference in a disposable detached worktree."""

import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import stat
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
PIN = "6fec2e8c41220e284066ae8479d8f1b573febdb2"
EMPTY_TREE = "4b825dc642cb6eb9a060e54bf8d69288fbee4904"  # Git's SHA-1 empty tree
PATCH = HERE / "v1-inspector.patch"
ADDED = "src/editor/inspect.c"
BUILD = Path("build/contract-oracle")
DIFF = ("diff", "--no-ext-diff", "--no-textconv", "--no-color", "--no-relative",
        "--binary", "--full-index", "--no-renames", "--src-prefix=a/", "--dst-prefix=b/", "-U3")
STOCK = "sublimite-stock"
EDITOR = "sublimite"
FAULT = "sublimite-drop-submit"
FAULT_SOURCE = HERE / "oracle_drop_submit.c.in"
STAMP = "manifest.json"


def sha256(file):
    with file.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def git(tree, *args, exits=(0,), stdin=None):
    env = {key: value for key, value in os.environ.items() if not key.startswith("GIT_")}
    run = subprocess.run(["git", "-C", str(tree), *args], env=env,
                         input=stdin, stdout=subprocess.PIPE)
    if run.returncode not in exits:
        raise ValueError(f"git {args[0]} failed in {tree}")
    return run.stdout


def verify_head(tree):
    dotgit = tree / ".git"
    if dotgit.is_file():
        line = dotgit.read_text().strip()
        if not line.startswith("gitdir: "):
            raise ValueError("invalid worktree .git file")
        dotgit = tree / line.removeprefix("gitdir: ")
    if (dotgit / "HEAD").read_text().strip() != PIN:
        raise ValueError(f"oracle requires detached v1 commit {PIN}")


def extras(tree):
    # Diffing the index against the empty tree enumerates every tracked path.
    indexed = git(tree, *DIFF, "--cached", "--name-only", "-z", EMPTY_TREE, "--")
    known = {os.fsdecode(entry) for entry in indexed.split(b"\0") if entry}
    extra = set()
    for parent, dirs, files in os.walk(tree):
        path = Path(parent)
        for name in dirs[:]:
            if path == tree and name in (".git", "build"):
                dirs.remove(name)
            elif (path / name).is_symlink():
                dirs.remove(name)
                files.append(name)
        for name in files:
            relative = (path / name).relative_to(tree).as_posix()
            if relative != ".git" and relative not in known:
                extra.add(relative)
    return extra


def source_state(tree, patch):
    verify_head(tree)
    if git(tree, *DIFF, "--cached", PIN, "--"):
        raise ValueError("oracle index differs from the pin")
    extra = extras(tree)
    if extra - {ADDED}:
        raise ValueError(f"unexpected untracked paths: {', '.join(sorted(extra - {ADDED}))}")
    current = git(tree, *DIFF, PIN, "--")
    if extra:
        current += git(tree, *DIFF, "--no-index", "--", "/dev/null", ADDED, exits=(1,))
    if current and current != patch:
        raise ValueError("oracle worktree differs from both the pin and the exact patch")
    return bool(current)


def compile_oracle(tree, output, jobs, *, stock=False):
    env = os.environ.copy()
    env["TMPDIR"] = str(output)
    # Avoid ambient make settings selecting different objects or a sanitizer runtime.
    for key in ("MAKEFLAGS", "MFLAGS", "MAKEOVERRIDES", "GNUMAKEFLAGS"):
        env.pop(key, None)
    sysroot = tree / "build/sysroot/usr/include"
    if sysroot.is_dir():
        env["CPATH"] = str(sysroot)
    args = ["make", "-j", str(jobs), "B=" + output.relative_to(tree).as_posix()]
    binary = output / EDITOR
    goals = [binary.relative_to(tree).as_posix()]
    if not stock:
        # Link the fault control against the same release objects via the
        # reference Makefile, using its compiler and native X11 dependencies.
        makefile = output / "drop-submit.mk"
        wraps = ("main", "editor_inspect_marker", "editor_inspect_force_repaint",
                 "render_backend_submit", "render_backend_check", "plat_run_for",
                 "edit_malloc_guard_begin_thread")
        wrap_flags = " ".join(f"-Wl,--wrap={symbol}" for symbol in wraps)
        makefile.write_text(
            f"$(B)/oracle_drop_submit.o: {FAULT_SOURCE}\n"
            "\t$(CC_RELEASE) $(WARN) $(REL_F) $(INC) -x c -c $< -o $@\n"
            f"$(B)/{FAULT}: $(B)/rel/src/main.o $(B)/oracle_drop_submit.o $(REL_LIB)\n"
            f"\t$(CC_RELEASE) $(REL_F) -pthread $^ {wrap_flags} $(LDLIBS) -o $@\n"
        )
        args += ["-f", "Makefile", "-f", str(makefile)]
        goals.append((output / FAULT).relative_to(tree).as_posix())
    subprocess.run([*args, *goals], cwd=tree, env=env, check=True)


def build(tree, jobs):
    tree = tree.resolve(strict=True)
    if tree in {Path.home() / "Projects" / name for name in ("sublimite", "sublimite-v1-oracle")}:
        raise ValueError("use a disposable v1 worktree")
    verify_head(tree)
    output = tree / BUILD
    for directory in (output.parent, output):
        if directory.is_symlink():
            raise ValueError(f"refusing symlinked build directory {directory}")
        directory.mkdir(exist_ok=True)
    patch = PATCH.read_bytes()
    digest = hashlib.sha256(patch).hexdigest()
    lock_fd = os.open(output / "lock", os.O_WRONLY | os.O_CREAT | os.O_NOFOLLOW | os.O_CLOEXEC, 0o600)
    with os.fdopen(lock_fd, "w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        stamp = output / STAMP
        patched = source_state(tree, patch)
        if stamp.is_symlink():
            raise ValueError("oracle manifest is a symlink")
        if stamp.exists():
            recorded = json.loads(stamp.read_text())
            if (not patched or recorded != manifest(output, digest)):
                raise ValueError("oracle source, patch digest, or executable drifted")
            return
        with tempfile.TemporaryDirectory(prefix="compile-", dir=output) as scratch:
            compile_pair(tree, Path(scratch), jobs, patch, patched)
            for name in (EDITOR, STOCK, FAULT):
                (Path(scratch) / name).replace(output / name)
            pending = Path(scratch) / STAMP
            pending.write_text(json.dumps(manifest(output, digest), indent=2) + "\n")
            pending.replace(stamp)
        print(f"Oracle {PIN}, patch SHA-256 {digest}")


def apply(tree, patch, *flags):
    git(tree, "apply", "--check", *flags, "-", stdin=patch)
    git(tree, "apply", *flags, "-", stdin=patch)


def compile_pair(tree, work, jobs, patch, patched):
    if patched:
        # Restore the pin for the stock control when resuming a prior build.
        apply(tree, patch, "-R")
    try:
        if source_state(tree, patch):
            raise ValueError("oracle did not return to the pin")
        git(tree, "apply", "--check", "-", stdin=patch)
        compile_oracle(tree, work, jobs, stock=True)
        (work / EDITOR).replace(work / STOCK)
    finally:
        if patched or (work / STOCK).exists():
            if source_state(tree, patch):
                raise ValueError("oracle source changed during the stock build")
            apply(tree, patch)
    if not source_state(tree, patch):
        raise ValueError("patch application did not produce the expected postimage")
    compile_oracle(tree, work, jobs)
    if not source_state(tree, patch):
        raise ValueError("oracle source changed during the patched build")


def manifest(output, digest):
    binaries = {}
    for name in (EDITOR, STOCK, FAULT):
        binary = output / name
        info = binary.lstat()
        if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1 or not info.st_mode & 0o111:
            raise ValueError(f"invalid oracle executable {binary}")
        binaries[name] = sha256(binary)
    return {"pin": PIN, "patch_sha256": digest, "executables": binaries,
            "fault_source_sha256": sha256(FAULT_SOURCE)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tree", type=Path)
    parser.add_argument("--jobs", type=int, default=1)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    try:
        build(args.tree, args.jobs)
    except (OSError, ValueError, subprocess.CalledProcessError) as exc:
        parser.exit(1, f"build_oracle: {exc}\n")


if __name__ == "__main__":
    main()
