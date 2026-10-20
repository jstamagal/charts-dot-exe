#!/bin/sh
# Install or remove the charts agent skill:
#   tools/install-skill.sh install|uninstall     (make install-skill / uninstall-skill)
# The real copy goes to ~/.agents/skills/charts ($SKILL_HOME/charts), and each
# agent's own global skills directory gets a relative symlink to it.
#   AGENTS=claude,codex   only these agents (default: all of them)
#   AGENTS=none           only ~/.agents/skills, no agent links
#   SKILL_COPY=1          copy into each agent's directory instead of linking
# An agent whose home directory does not exist is skipped, and said so. A
# directory in the way that this script did not make is moved to
# charts.bak-<date>, never deleted.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SRC=${SKILL_SRC:-$ROOT/skill/charts}
# `npx skills` puts its "universal" global skills in
# ${XDG_CONFIG_HOME:-~/.config}/agents/skills; ~/.agents/skills is this
# box's universal directory, and omp reads it too.
UNIVERSAL=${SKILL_HOME:-$HOME/.agents/skills}
NAME=charts
MARK=.charts-skill-copy
ALL="claude codex js pi omp opencode"
ACTION=${1:-install}
STAMP=$(date +%Y%m%d-%H%M%S)

# agent -> "home directory|global skills directory"
agent_dirs() {
    case $1 in
        claude)   h=${CLAUDE_CONFIG_DIR:-$HOME/.claude}; echo "$h|$h/skills" ;;
        codex)    h=${CODEX_HOME:-$HOME/.codex}; echo "$h|$h/skills" ;;
        opencode) h=${XDG_CONFIG_HOME:-$HOME/.config}/opencode; echo "$h|$h/skills" ;;
        pi)       echo "$HOME/.pi|$HOME/.pi/agent/skills" ;;
        js)       echo "$HOME/.js|$HOME/.js/skills" ;;
        # omp loads <agent dir>/skills, the agent dir being ~/.omp/agent.
        omp)      echo "$HOME/.omp|$HOME/.omp/agent/skills" ;;
        *) return 1 ;;
    esac
}
tilde() { case $1 in "$HOME"/*) echo "~/${1#"$HOME"/}" ;; *) echo "$1" ;; esac; }
# The link from $1 (a skills directory) to the universal copy: relative when
# both are under $HOME, like ../../.agents/skills/charts.
link_target() {
    # A skills directory reached through a symlinked home gets an absolute link.
    if [ "$(cd "$1" && pwd -P)" != "$1" ] || [ "$(cd "$UNIVERSAL" && pwd -P)" != "$UNIVERSAL" ]; then
        echo "$UNIVERSAL/$NAME"; return
    fi
    case $1/ in
        "$HOME"/*)
            case $UNIVERSAL/ in
                "$HOME"/*)
                    rest=${1#"$HOME"/}; up=
                    while [ -n "$rest" ]; do
                        up=../$up
                        case $rest in */*) rest=${rest#*/} ;; *) rest= ;; esac
                    done
                    echo "$up${UNIVERSAL#"$HOME"/}/$NAME"; return ;;
            esac ;;
    esac
    echo "$UNIVERSAL/$NAME"
}
# Move a directory or file that is not ours out of the way.
backup() {
    mv "$1" "$1.bak-$STAMP"
    notes="$notes
moved $(tilde "$1") to $(tilde "$1.bak-$STAMP"): it was not a link or a copy made by this script"
}

case ${AGENTS:-all} in
    all|'') agents=$ALL ;;
    none) agents= ;;
    *) agents=$(echo "$AGENTS" | tr ',' ' ') ;;
esac
for a in $agents; do agent_dirs "$a" >/dev/null || { echo "install-skill: unknown agent '$a' (known: $ALL, none)" >&2; exit 2; }; done

notes=
done_list=
skipped=

if [ "$ACTION" = install ]; then
    [ -f "$SRC/SKILL.md" ] || { echo "install-skill: no $SRC/SKILL.md (run make skill)" >&2; exit 1; }
    mkdir -p "$UNIVERSAL"
    dest=$UNIVERSAL/$NAME
    [ -L "$dest" ] && { notes="$notes
replaced the link $(tilde "$dest") -> $(readlink "$dest") with a copy"; rm -f "$dest"; }
    rm -rf "$dest.new"; cp -R "$SRC" "$dest.new"; : > "$dest.new/$MARK"
    rm -rf "$dest"; mv "$dest.new" "$dest"
    for a in $agents; do
        d=$(agent_dirs "$a"); home=${d%%|*}; dir=${d#*|}
        if [ ! -d "$home" ]; then skipped="$skipped, $a ($(tilde "$home") does not exist)"; continue; fi
        mkdir -p "$dir"; t=$dir/$NAME
        if [ -L "$t" ]; then rm -f "$t"
        elif [ -e "$t" ]; then
            if [ -f "$t/$MARK" ]; then rm -rf "$t"; else backup "$t"; fi
        fi
        if [ "${SKILL_COPY:-}" = 1 ]; then cp -R "$dest" "$t"
        else ln -s "$(link_target "$dir")" "$t"; fi
        done_list="$done_list, $(tilde "$t")"
    done
    how="linked into"; [ "${SKILL_COPY:-}" = 1 ] && how="copied into"
    printf 'installed to %s' "$(tilde "$dest")"
    if [ -n "$done_list" ]; then printf '; %s: %s' "$how" "${done_list#, }"; fi
    echo
elif [ "$ACTION" = uninstall ]; then
    for a in $agents; do
        d=$(agent_dirs "$a"); home=${d%%|*}; t=${d#*|}/$NAME
        if [ ! -d "$home" ]; then skipped="$skipped, $a ($(tilde "$home") does not exist)"; continue; fi
        if [ -L "$t" ] || [ -f "$t/$MARK" ]; then rm -rf "$t"; done_list="$done_list, $(tilde "$t")"
        elif [ -e "$t" ]; then notes="$notes
left $(tilde "$t"): it is not a link or a copy made by this script"
        fi
    done
    printf 'removed: %s' "${done_list#, }"
    [ -n "$done_list" ] || printf 'no agent links'
    # The universal copy goes only when every agent's link went with it.
    case ${AGENTS:-all} in
        all|''|none)
            if [ -e "$UNIVERSAL/$NAME" ] || [ -L "$UNIVERSAL/$NAME" ]; then
                if [ -L "$UNIVERSAL/$NAME" ] || [ -f "$UNIVERSAL/$NAME/$MARK" ] || [ -f "$UNIVERSAL/$NAME/SKILL.md" ]; then
                    rm -rf "$UNIVERSAL/$NAME"; printf '; and %s' "$(tilde "$UNIVERSAL/$NAME")"
                fi
            fi ;;
        *) printf '; kept %s' "$(tilde "$UNIVERSAL/$NAME")" ;;
    esac
    echo
else
    echo "usage: tools/install-skill.sh install|uninstall" >&2; exit 2
fi
[ -z "$skipped" ] || echo "skipped: ${skipped#, }"
[ -z "$notes" ] || printf '%s\n' "${notes#?}"
