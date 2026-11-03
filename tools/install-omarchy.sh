#!/bin/sh
# Opt-in Omarchy integration: `make install-omarchy` / `make uninstall-omarchy`.
# Usage: PREFIX=... sh tools/install-omarchy.sh install|uninstall
# Changes (each backed up under $XDG_STATE_HOME/sublimite/omarchy, restored exactly
# by uninstall unless you changed the file since):
#   <prefix>/libexec/sublimite/omarchy/fresh   the wait-aware launcher (new file)
#   ~/.local/state/omarchy/defaults/editor     Omarchy's default editor
#   ~/.config/omarchy/extensions/omarchy-menu.jsonc  Setup > Default > Editor row
#   $XDG_CONFIG_HOME/mimeapps.list             text MIME defaults (xdg-mime)
# Run `make install` first. Design: docs/decisions/edit-b6ai.11.md.
set -eu
action=${1:-}
case $action in install|uninstall) ;; *) echo "usage: PREFIX=... $0 install|uninstall" >&2; exit 2;; esac
: "${HOME:?HOME is required}"
PREFIX=${PREFIX:-$HOME/.local}
OMARCHY_PATH=${OMARCHY_PATH:-/usr/share/omarchy}
launcher=$OMARCHY_PATH/bin/omarchy-launch-editor
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
name=fresh
helper_dir=$PREFIX/libexec/sublimite/omarchy
helper=$helper_dir/$name
editor_file=$HOME/.local/state/omarchy/defaults/editor
menu_file=$HOME/.config/omarchy/extensions/omarchy-menu.jsonc
mime_file=${XDG_CONFIG_HOME:-$HOME/.config}/mimeapps.list
state=${XDG_STATE_HOME:-$HOME/.local/state}/sublimite/omarchy
mark_begin='// >>> sublimite omarchy (managed by make install-omarchy; make uninstall-omarchy removes it)'
mark_end='// <<< sublimite omarchy'
say() { printf '%s\n' "$*"; }
die() { printf 'install-omarchy: %s\n' "$*" >&2; exit 1; }

missing_chain() { # missing ancestors of $1 (incl. itself), outermost first
    d=$1; list=
    while [ ! -d "$d" ] && [ "$d" != / ]; do list="$d
$list"; d=$(dirname -- "$d"); done
    printf '%s\n' "$list"
}
mk() { # mkdir -p, remembering what was created so uninstall can remove it
    missing_chain "$1" >>"$state/created_dirs.new"
    mkdir -p -- "$1"
    cat "$state/created_dirs.new" >>"$state/created_dirs"; : >"$state/created_dirs.new"
}
sum() { if [ -e "$1" ]; then cksum <"$1"; else echo absent; fi; }
backup() { # backup KEY FILE: once only, so a second install never overwrites the original
    [ -e "$state/$1.orig" ] || [ -e "$state/$1.absent" ] && return 0
    if [ -e "$2" ]; then cp -p -- "$2" "$state/$1.orig"; else : >"$state/$1.absent"; fi
}
restore() { # restore KEY FILE: exact restore if the file is still what install wrote
    key=$1; file=$2
    [ -e "$state/$key.sum" ] || return 0
    if [ "$(sum "$file")" = "$(cat "$state/$key.sum")" ]; then
        if [ -e "$state/$key.orig" ]; then cp -p -- "$state/$key.orig" "$file"; else rm -f -- "$file"; fi
        return 0
    fi
    return 1
}

if [ "$action" = install ]; then
    [ -x "$launcher" ] || { say "Omarchy not found ($launcher is missing): nothing to do."; exit 0; }
    case $PREFIX in /*) ;; *) die "PREFIX must be absolute: $PREFIX";; esac
    case $PREFIX in *[\"\'\\\$\`]*|*"
"*) die "PREFIX may not contain quotes, backslashes, \$ or newlines: $PREFIX";; esac
    [ -x "$PREFIX/bin/sublimite" ] || die "$PREFIX/bin/sublimite not found: run 'make install' first (same PREFIX)."
    grep -qw -- "$name" "$launcher" || die "$launcher no longer runs '$name' in place; this Omarchy needs a new wrapper name (see docs/decisions/edit-b6ai.11.md). Nothing was changed."
    mk_state=$(missing_chain "$state")
    mkdir -p -- "$state"
    printf '%s\n' "$mk_state" >>"$state/created_dirs"
    : >"$state/created_dirs.new"
    backup editor "$editor_file"; backup menu "$menu_file"; backup mime "$mime_file"
    # 1. wrapper
    mk "$helper_dir"
    sed "s|@SUBLIMITE@|$PREFIX/bin/sublimite|" "$here/omarchy/sublimite-editor.in" >"$helper.tmp"
    chmod 755 "$helper.tmp"; mv -f -- "$helper.tmp" "$helper"
    # 2. default editor
    mk "$(dirname -- "$editor_file")"
    printf '%s\n' "$helper" >"$editor_file"
    # 3. menu row (a marked block right after the opening brace; the shell's JSONC
    # reader drops whole-line comments and trailing commas)
    mk "$(dirname -- "$menu_file")"
    comma=,
    [ -f "$menu_file" ] && sed "/$(printf '%s' "$mark_begin" | sed 's|[]/.*[^$]|\\&|g')/,/$(printf '%s' "$mark_end" | sed 's|[]/.*[^$]|\\&|g')/d" "$menu_file" >"$state/menu.stripped" || : >"$state/menu.stripped"
    if ! grep -v '^[[:space:]]*//' "$state/menu.stripped" | grep -q '"'; then comma=; fi
    row="  $mark_begin
  \"setup.default.editor.sublimite\": {\"icon\":\"\\uf044\",\"label\":\"sublimit\\u00e9\",\"when\":\"[ -x '$helper' ]\",\"checked\":\"[[ \\\"\$(omarchy-default-editor)\\\" == '$helper' ]]\",\"action\":\"mkdir -p ~/.local/state/omarchy/defaults && printf '%s\\\\n' '$helper' > ~/.local/state/omarchy/defaults/editor && omarchy-notification-send 'sublimit\\u00e9 is now the default editor'\"}$comma
  $mark_end"
    if [ -f "$menu_file" ]; then
        if grep -q '^[[:space:]]*{[[:space:]]*$' "$state/menu.stripped"; then
            printf '%s\n' "$row" >"$state/menu.row"   # a file, not awk -v: -v would unescape the JSON backslashes
            awk -v rowfile="$state/menu.row" 'BEGIN{done=0} {print} !done && /^[[:space:]]*\{[[:space:]]*$/ {while ((getline l < rowfile) > 0) print l; done=1}' "$state/menu.stripped" >"$menu_file.tmp"
            mv -f -- "$menu_file.tmp" "$menu_file"
        else
            say "install-omarchy: $menu_file has no line holding only '{'; menu row skipped (add it by hand, see docs/config.md)."
        fi
    else
        printf '{\n%s\n}\n' "$row" >"$menu_file"
    fi
    rm -f -- "$state/menu.stripped" "$state/menu.row"
    # 4. MIME defaults for what the desktop entry declares
    desktop=$PREFIX/share/applications/sublimite.desktop
    [ -f "$desktop" ] || desktop=$here/sublimite.desktop
    if command -v xdg-mime >/dev/null 2>&1; then
        mk "$(dirname -- "$mime_file")"
        mimes=$(sed -n 's/^MimeType=//p' "$desktop" | tr ';' ' ')
        printf '%s\n' $mimes >"$state/mime.types"
        for m in $mimes; do xdg-mime default sublimite.desktop "$m"; done
    else
        say "install-omarchy: xdg-mime not found; MIME defaults not set."
    fi
    # 5. what uninstall may restore exactly
    sum "$editor_file" >"$state/editor.sum"; sum "$menu_file" >"$state/menu.sum"; sum "$mime_file" >"$state/mime.sum"
    rm -f -- "$state/created_dirs.new"
    say "sublimite is set up for Omarchy:"
    say "  default editor : $helper (Setup > Default > Editor also has a sublimité row)"
    say "  \$EDITOR and 'omarchy-launch-editor --inline' wait for the tab to close; Omarchy menus open without waiting"
    say "  undo with: make uninstall-omarchy PREFIX=$PREFIX"
    exit 0
fi

# ---- uninstall ----
if [ ! -d "$state" ]; then say "sublimite Omarchy integration is not installed (no $state): nothing to do."; exit 0; fi
left=
restore editor "$editor_file" || left="$left $editor_file"
if ! restore menu "$menu_file"; then
    if [ -f "$menu_file" ]; then
        sed "/$(printf '%s' "$mark_begin" | sed 's|[]/.*[^$]|\\&|g')/,/$(printf '%s' "$mark_end" | sed 's|[]/.*[^$]|\\&|g')/d" "$menu_file" >"$menu_file.tmp" && mv -f -- "$menu_file.tmp" "$menu_file"
        say "uninstall-omarchy: $menu_file changed since install; only the sublimite row was removed."
    fi
fi
if ! restore mime "$mime_file"; then
    # mimeapps.list moves on its own: give back just the types we pointed at sublimite
    if [ -f "$mime_file" ] && [ -f "$state/mime.types" ]; then
        orig=/dev/null; [ -e "$state/mime.orig" ] && orig=$state/mime.orig
        awk -v types="$state/mime.types" -v orig="$orig" '
            BEGIN { while ((getline t < types) > 0) mine[t] = 1
                    sec = ""; while ((getline l < orig) > 0) {
                        if (l ~ /^\[/) sec = l
                        else if (sec == "[Default Applications]" && (i = index(l, "=")) > 0) was[substr(l, 1, i - 1)] = l } }
            /^\[/ { sec = $0 }
            sec == "[Default Applications]" && (i = index($0, "=")) > 0 && (substr($0, 1, i - 1) in mine) && substr($0, i + 1) ~ /^sublimite\.desktop(;|$)/ {
                k = substr($0, 1, i - 1); if (k in was) print was[k]; next }
            { print }' "$mime_file" >"$mime_file.tmp" && mv -f -- "$mime_file.tmp" "$mime_file"
        say "uninstall-omarchy: $mime_file changed since install; only the sublimite defaults were rolled back."
    fi
fi
rm -f -- "$helper"
if [ -n "$left" ]; then
    for f in $left; do say "uninstall-omarchy: $f was changed after install (now: $(head -n 1 "$f")); left in place."; done
fi
created=$(cat "$state/created_dirs" 2>/dev/null || true)
rm -rf -- "$state"
# directories this install created, innermost first (rmdir only removes empty ones)
printf '%s\n' "$created" | awk 'NF{a[n++]=$0} END{for(i=n-1;i>=0;i--)print a[i]}' | while IFS= read -r d; do rmdir -- "$d" 2>/dev/null || true; done
say "sublimite Omarchy integration removed."
exit 0
