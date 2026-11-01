#!/bin/bash
# test_sublimite_win.sh - tools/sublimite-win contract (edit-177r.24). The exe is
# a stub (SUBLIMITE_WIN_EXE) that records argv NUL-separated, stdin, and exits with
# $STUB_EXIT; no Windows program ever runs. Skips where wslpath is absent.
# Usage: sh tools/test_sublimite_win.sh
if [ -z "${BASH_VERSION:-}" ]; then exec bash "$0" "$@"; fi
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
tool=$root/tools/sublimite-win
command -v wslpath >/dev/null 2>&1 || { echo "SKIP (no wslpath: not WSL)"; exit 0; }
work=$(mktemp -d) || exit 1
trap 'rm -rf "$work"' EXIT HUP INT TERM
fails=0
stub=$work/stub.sh
cat >"$stub" <<'STUB'
#!/bin/sh
: >"$STUB_OUT/argv"; for a in "$@"; do printf '%s\0' "$a" >>"$STUB_OUT/argv"; done
cat >"$STUB_OUT/stdin"
exit "${STUB_EXIT:-0}"
STUB
chmod +x "$stub"
export SUBLIMITE_WIN_EXE=$stub STUB_OUT=$work/out
mkdir -p "$STUB_OUT"
distro=$(wslpath -w /); distro=${distro%\\}   # \\wsl.localhost\<distro>
fs=$work/fs; mkdir -p "$fs/dir with space"
# Expected UNC form: of an existing file, wslpath's own (it maps ':' and '"' to the
# private-use characters NTFS-style names need, invisible in a terminal); of a missing
# one, the translated directory dir/ plus literal names.
w() { wslpath -w "$1"; }
m() { printf '%s%s' "$distro" "$(printf '%s' "$1" | tr / '\\')"; }   # missing path, ASCII only

# check <name> <expected argv, one printf arg each...>  (run via $RUN)
expect() {
    name=$1; shift
    : >"$STUB_OUT/argv.exp"; for x in "$@"; do printf '%s\0' "$x" >>"$STUB_OUT/argv.exp"; done
    if cmp -s "$STUB_OUT/argv" "$STUB_OUT/argv.exp"; then echo "ok   $name"
    else echo "FAIL $name"; echo "  want: $(tr '\0' '|' <"$STUB_OUT/argv.exp")"; echo "  got : $(tr '\0' '|' <"$STUB_OUT/argv")"; fails=$((fails+1)); fi
}
run() { rm -f "$STUB_OUT/argv" "$STUB_OUT/stdin"; "$tool" "$@" </dev/null; }

: >"$fs/a.txt"; : >"$fs/a:3"; : >"$fs/dir with space/q\"u'ote \$x ñ.txt"; : >"$fs/-dash"
ln -s nowhere "$fs/dangling:7"

run "$fs/a.txt";                       expect "plain existing file" "$(w "$fs/a.txt")"
run "$fs/a.txt:12";                    expect "line suffix" "$(w "$fs/a.txt"):12"
run "$fs/a.txt:12:4";                  expect "line:col suffix" "$(w "$fs/a.txt"):12:4"
run "$fs/a:3";                         expect "exact existing name with :3 is a file, not a suffix" "$(w "$fs/a:3")"
run "$fs/dangling:7";                  expect "dangling symlink name wins over suffix (wslpath follows the link)" "$(m "$fs/nowhere")"
run "$fs/a:3:9";                       expect "absent a:3:9 peels both numbers like args.c, even though a:3 exists" "$(m "$fs/a"):3:9"
run "$fs/new.txt";                     expect "missing file: parent translated" "$(m "$fs/new.txt")"
run "$fs/new.txt:5:2";                 expect "missing file with coordinates" "$(m "$fs/new.txt"):5:2"
run "$fs/no/such/dir/new.txt:3";       expect "missing directories too" "$(m "$fs/no/such/dir/new.txt"):3"
run "$fs/dir with space/q\"u'ote \$x ñ.txt:2"; expect "spaces quotes dollar non-ASCII" "$(w "$fs/dir with space/q\"u'ote \$x ñ.txt"):2"
( cd "$fs" && run a.txt:4 );           expect "relative path" "$(w "$fs/a.txt"):4"
( cd "$fs/dir with space" && run ../a.txt ./x/../y.txt ); expect "relative with .. and ." "$(w "$fs/a.txt")" "$(m "$fs/dir with space/y.txt")"
( cd "$fs" && run -- -dash -new:6 );   expect "leading dash after --" "--" "$(w "$fs/-dash")" "$(m "$fs/-new"):6"
run --wait --new-instance "$fs/a.txt"; expect "options pass through" --wait --new-instance "$(w "$fs/a.txt")"
run --font-family "Fira Code" --font-fallbacks "A,B" --nonsense "$fs/a.txt"
expect "non-path options unchanged" --font-family "Fira Code" --font-fallbacks "A,B" --nonsense "$(w "$fs/a.txt")"
run --font-family=Mono "$fs/a.txt";    expect "= form non-path option" --font-family=Mono "$(w "$fs/a.txt")"
run --font-file "$fs/a.txt#2";         expect "font-file value translated, #index kept" --font-file "$(w "$fs/a.txt")#2"
run --font-file="$fs/a.txt";           expect "font-file= form" "--font-file=$(w "$fs/a.txt")"
( cd "$fs" && run --font-file a.txt );  expect "relative font file" --font-file "$(w "$fs/a.txt")"
run /mnt/c/sublimite-test-nonexistent/new.txt:8:1; expect "drive path" 'C:\sublimite-test-nonexistent\new.txt:8:1'
run /mnt/c;                            expect "drive root" 'C:\'
run "$fs/a.txt" - ;                    expect "dash is stdin marker" "$(w "$fs/a.txt")" -
run;                                   expect "no arguments"
run "";                                expect "empty argument passes through" ""

# stdin bytes pass through unchanged (NUL, high bytes, no trailing newline)
printf 'a\0b\377\r\nlast' >"$work/in.bin"
"$tool" - <"$work/in.bin"
if cmp -s "$work/in.bin" "$STUB_OUT/stdin"; then echo "ok   stdin bytes"; else echo "FAIL stdin bytes"; fails=$((fails+1)); fi
expect "stdin argv" -

# exit status is the exe's, exactly
for code in 0 1 3 42 255; do
    STUB_EXIT=$code "$tool" --wait "$fs/a.txt" </dev/null; rc=$?
    if [ "$rc" = "$code" ]; then echo "ok   --wait exit status $code"; else echo "FAIL exit status want $code got $rc"; fails=$((fails+1)); fi
done

# exe lookup failure: clear message, nonzero, nothing started
msg=$(SUBLIMITE_WIN_EXE=$work/absent.exe "$tool" "$fs/a.txt" 2>&1 >/dev/null); rc=$?
case $msg in "sublimite-win: sublimite.exe not found at '$work/absent.exe'"*) ok=1;; *) ok=0;; esac
if [ "$rc" != 0 ] && [ $ok = 1 ]; then echo "ok   missing exe"; else echo "FAIL missing exe rc=$rc msg=$msg"; fails=$((fails+1)); fi

# launcher writes nothing under its own directory or the cwd
if [ "$fails" = 0 ]; then echo "test_sublimite_win: all passed"; else echo "test_sublimite_win: $fails FAILED"; exit 1; fi
