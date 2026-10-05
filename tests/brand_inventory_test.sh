#!/bin/sh
# Phase 4.5: every tracked file that still contains a legacy identifier must be classified in
# docs/architecture/brand_identifiers.tsv (longest prefix wins). Read-only.
root=${1:-.}
reg="$root/docs/architecture/brand_identifiers.tsv"
[ -f "$reg" ] || { echo "missing $reg"; exit 1; }
fail=0
files=$(cd "$root" && git ls-files | while read -r f; do
	[ -f "$root/$f" ] && grep -qiIE 'flux|hico|synthesiscore' "$root/$f" 2>/dev/null && echo "$f"
	case "$f" in *[Ff]lux*|*[Hh]i[Cc]o*|*[Ss]ynthesis[Cc]ore*|*synthesiscore*) echo "$f" ;; esac
done | sort -u)
for f in $files; do
	best=""
	while IFS='	' read -r prefix class reason; do
		case "$prefix" in ''|'#'*) continue ;; esac
		case "$f" in "$prefix"*) [ ${#prefix} -gt ${#best} ] && best=$prefix ;; esac
	done <"$reg"
	[ -n "$best" ] || { echo "UNCLASSIFIED: $f"; fail=1; }
done
# Every class used must be one of the allowed classes.
bad=$(grep -v '^#' "$reg" | awk -F'\t' 'NF{print $2}' | grep -vxE 'compatibility|technical identifier|historical documentation|migration documentation|test fixture|intentional legacy reference')
[ -z "$bad" ] || { echo "invalid class: $bad"; fail=1; }
[ $fail -eq 0 ] && echo "brand_inventory_test: all legacy identifier occurrences classified"
exit $fail
