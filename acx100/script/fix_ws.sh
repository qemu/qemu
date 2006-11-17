#!/bin/sh
# Whitespace fixer

temp=$$.$RANDOM

begin8sp_tab=$'s/^        /\t/'
beginchar7sp_chartab=$'s/^\\([^ \t]\\)       /\\1\t/'
tab8sp_tabtab=$'s/\t        /\t\t/g'
begin17sptab_tab=$'s/^ \\{1,7\\}\t/\t/'
tab17sptab_tabtab=$'s/\t \\{1,7\\}\t/\t\t/g'
trailingws_=$'s/[ \t]*$//'
# Fixes whitespace in strings:
# printk(KERN_WARNING "ISILoad:Card%d rejected load header:\n"...);
wscr_cr=$'s/[ \t]*\\\\n/\\\\n/'

find -type f \
| while read name; do
    if test "C" = "${name/*.[ch]/C}" \
	 -o "SH" = "${name/*.sh/SH}"; then
	echo "Formatting $name as C or sh file" >&2
	cat "$name" \
	| sed "$begin8sp_tab" \
	| sed "$begin17sptab_tab" \
	| sed -e "$tab8sp_tabtab" -e "$tab8sp_tabtab" -e "$tab8sp_tabtab" \
	      -e "$tab8sp_tabtab" -e "$tab8sp_tabtab" -e "$tab8sp_tabtab" \
	| sed -e "$tab17sptab_tabtab" -e "$tab17sptab_tabtab" \
	      -e "$tab17sptab_tabtab" -e "$tab17sptab_tabtab" \
	      -e "$tab17sptab_tabtab" -e "$tab17sptab_tabtab" \
	| sed "$trailingws_" \
	| sed -e "$wscr_cr" -e "$wscr_cr" -e "$wscr_cr" -e "$wscr_cr" \
	      -e "$wscr_cr" -e "$wscr_cr" -e "$wscr_cr" -e "$wscr_cr" \

    else
	echo "Removing trailing spaces from $name" >&2
	cat "$name" \
	| sed "$trailingws_" \

    fi >"$name.$temp"

    # Conserve mode:
    cat "$name.$temp" >"$name"
    rm "$name.$temp"
done
