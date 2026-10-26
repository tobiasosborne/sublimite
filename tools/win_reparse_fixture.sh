#!/usr/bin/env bash
# win_reparse_fixture.sh create|remove: the os suite's reparse fixture (os_test
# case `reparse`, OS_TEST_DATA_REPARSE), a data file carrying a WOF reparse tag.
# Made as a Windows process (cmd.exe through WSL interop; /mnt/c is read-only
# from WSL) under %TEMP%\sublimite-test-reparse: `compact /c /exe:lzx` leaves
# the file an ordinary document that NTFS serves through the WOF filter
# (decision doc edit-177r.2 §3.10). `create` prints the file's Windows path;
# `remove` deletes the directory. One plain command per cmd.exe call: interop
# re-quotes arguments, so a quoted compound command does not survive the trip
# (M, edit-177r.3); %TEMP% must therefore contain no blanks.
set -u
temp=$(cmd.exe /c 'echo %TEMP%' 2>/dev/null </dev/null | tr -d '\r')
[ -n "$temp" ] || { echo "win_reparse_fixture: cannot read %TEMP%" >&2; exit 2; }
case "$temp" in *" "*) echo "win_reparse_fixture: %TEMP% contains a blank: $temp" >&2; exit 2;; esac
# One directory per invocation (edit-177r.5; review edit-177r.3 finding 32):
# concurrent worktree checks must not delete each other's fixture. `create`
# prints the file's path; `remove <that path>` deletes its directory only.
dir="$temp\\sublimite-test-reparse-$$-$(date +%s)"
file="$dir\\wof.txt"
case "${1:-}" in
create)
  # (P) 65536 zero bytes: well above the MFT-resident limit (about 700
  # bytes), so WOF has a non-resident stream to compress; a tiny file keeps
  # no reparse tag and the case would pass without exercising the filter.
  cmd.exe /c mkdir "$dir" >/dev/null 2>&1 </dev/null
  cmd.exe /c fsutil file createnew "$file" 65536 >/dev/null 2>&1 </dev/null
  # compact must report the file compressed, or the fixture is no fixture.
  # (The WOF filter hides its reparse tag from FSCTL_GET_REPARSE_POINT and
  # from FileAttributeTagInfo on this build, M: `fsutil reparsepoint query`
  # answers "not a reparse point" and a FILE_FLAG_OPEN_REPARSE_POINT handle
  # reads attributes 0x20, tag 0; compact's own query is the usable check.)
  cmd.exe /c compact /c /exe:lzx "$file" 2>/dev/null </dev/null | tr -d '\r' | grep -q '^1 files within 1 directories were compressed' ||
    { echo "win_reparse_fixture: compact did not compress $file" >&2; exit 2; }
  echo "$file" ;;
remove)
  f="${2:-}"; d="${f%\\*}"
  case "$d" in "$temp\\sublimite-test-reparse-"*) cmd.exe /c rmdir /s /q "$d" >/dev/null 2>&1 </dev/null ;;
  *) echo "win_reparse_fixture: remove needs the path create printed" >&2; exit 2 ;; esac ;;
*) echo "usage: win_reparse_fixture.sh create|remove <file>" >&2; exit 2 ;;
esac
