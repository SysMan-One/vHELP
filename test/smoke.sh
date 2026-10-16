#!/bin/sh
#+++
#
#	FACILITY:	HELP - OpenVMS-style help library subsystem for Linux
#
#	MODULE:		test/smoke.sh
#
#	ABSTRACT:	Functional checks of the HELPTRAN librarian and of the
#			HELP browser: translation, navigation, abbreviation,
#			wildcards, the diagnostics of the translator and the
#			refusal of a damaged library.
#
#	DESCRIPTION:	The script works in a temporary directory of its own
#			and needs no privileges.  HELP_BIN names the directory
#			the images have been built in, HELP_SRC the root of
#			the source tree.
#
#	AUTHOR:		StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
#
#	CREATION DATE:	31-JUL-2026
#
#	MODIFICATION HISTORY:
#
#		11-SEP-2026	RRL	X01-36ECO1 : XDG_CACHE_HOME inherited from the shell
#					diverted the private cache away from the home the cache
#					checks point HOME at; the variable is dropped.  The
#					examples of the manuals are run with the shell's own
#					wildcards off: "/*" expanded to the root directory, /sys
#					became /SYSCACHE and the shared catalogue of the system
#					was rebuilt out of a test library at every run.
#
#		31-JUL-2026	RRL	X01-00 : Initial version.
#
#---

set -u

HELP_BIN=${HELP_BIN:-.}
HELP_SRC=${HELP_SRC:-..}

#	The images are named by HELP_HELPNAME and HELP_TRANNAME at
#	configuration time, so the check is told what they came out as
HELP=${HELP_HELP:-$HELP_BIN/vhelp}
HELPTRAN=${HELP_HELPTRAN:-$HELP_BIN/vhelptran}

#	The syntax an image prints names itself, so the check has to know
#	what the images were called; the topics of the shipped library are
#	content and do not follow a rename
HELPNAME=`basename "$HELP"`
TRANNAME=`basename "$HELPTRAN"`

#	The private cache goes to XDG_CACHE_HOME when the shell exports
#	one, and only otherwise under HOME.  The checks of the cache below
#	point HOME at a home of their own and look for the cache there, so
#	a variable inherited from the shell must not divert it elsewhere
unset XDG_CACHE_HOME

WRK=`mktemp -d /tmp/helpsmoke.XXXXXX`
FAILED=0
CHECK=0

trap 'rm -rf "$WRK"' EXIT INT TERM

#
#	ok <text>   - the check has passed
#	ko <text>   - the check has failed
#
ok ()
{
	CHECK=`expr $CHECK + 1`
	echo "%HELP-S-SMOKE, $1"
}

ko ()
{
	CHECK=`expr $CHECK + 1`
	FAILED=`expr $FAILED + 1`
	echo "%HELP-E-SMOKE, $1"
}

#
#	The source used by most of the checks
#
cat > "$WRK/src.md" <<'EOF'
<!-- ident: T-01 -->

# SUBMIT -- submit a job to a batch queue

Submits one or more jobs to a batch queue.

Format:

```
SUBMIT filespec[,...] [/qualifiers]
```

## /HOLD

Requests that the job be held until it is released.

## /AFTER -- hold the job until a given time

Requests that the job be held until the given time.

## EXAMPLES

An example of the *emphasised* and `quoted` markup, a [link](http://x.y)
and a bullet:

- the first item
- the second item

# DELETE -- delete an entry

Deletes an entry from a queue.

## /ENTRY

Names the entry to be deleted.
EOF

#
#	1. The translation itself
#
if "$HELPTRAN" "$WRK/src.md" /OUTPUT="$WRK/lib.hlb" /IDENT=T-01 > "$WRK/o.1" 2>&1 \
	&& [ -f "$WRK/lib.hlb" ]
then	ok "library created out of a Markdown source"
else	ko "library not created: `cat "$WRK/o.1"`"
fi

#
#	2. Every heading of the source became a topic, and no other one did
#
"$HELP" /LIBRARY="$WRK/lib.hlb" /LIST /NOPAGE > "$WRK/o.2" 2>/dev/null

NKEY=`sed -n 's/.*, \([0-9][0-9]*\) topic.*/\1/p' "$WRK/o.2"`
NSEEN=0

for KEY in SUBMIT /HOLD /AFTER EXAMPLES DELETE /ENTRY
do
	if grep -q "^  *$KEY\$" "$WRK/o.2"
	then	NSEEN=`expr $NSEEN + 1`
	fi
done

if [ "$NKEY" = 6 ] && [ "$NSEEN" = 6 ]
then	ok "the six headings of the source became the six topics of the library"
else	ko "the library reports $NKEY topics, $NSEEN of the six expected keys are listed"
fi

#
#	3. The identification stamped by /IDENT is read back
#
if "$HELP" /LIBRARY="$WRK/lib.hlb" /LIST /NOPAGE 2>/dev/null | grep -q 'ident T-01'
then	ok "the identification given by /IDENT is stored in the header"
else	ko "the identification given by /IDENT is lost"
fi

#
#	4. A topic of level 1
#
if "$HELP" /LIBRARY="$WRK/lib.hlb" submit /NOPAGE 2>/dev/null | grep -q 'Submits one or more jobs'
then	ok "a topic of level 1 is displayed"
else	ko "a topic of level 1 is not displayed"
fi

#
#	5. A subtopic named on the command line
#
if "$HELP" /LIBRARY="$WRK/lib.hlb" submit /AFTER /NOPAGE 2>/dev/null | grep -q 'held until the given time'
then	ok "a subtopic is reached by its full name"
else	ko "a subtopic is not reached by its full name"
fi

#
#	6. An unambiguous abbreviation of a key
#
if "$HELP" /LIBRARY="$WRK/lib.hlb" sub exa /NOPAGE 2>/dev/null | grep -q 'the first item'
then	ok "an unambiguous abbreviation selects the topic"
else	ko "an unambiguous abbreviation does not select the topic"
fi

#
#	7. The markup is rendered away
#
"$HELP" /LIBRARY="$WRK/lib.hlb" submit examples /NOPAGE > "$WRK/o.7" 2>/dev/null

if grep -q 'o the first item' "$WRK/o.7" \
	&& grep -q 'emphasised' "$WRK/o.7" \
	&& ! grep -q '`' "$WRK/o.7" \
	&& ! grep -q 'http://x.y' "$WRK/o.7"
then	ok "the inline markup is rendered into plain text"
else	ko "the inline markup survives into the display"
fi

#
#	8. A wildcard displays every match
#
NHIT=`"$HELP" /LIBRARY="$WRK/lib.hlb" submit '/*' /NOPAGE 2>/dev/null | grep -c '^    /'`

if [ "$NHIT" = 2 ]
then	ok "a wildcard displays every subtopic that matches"
else	ko "a wildcard displayed $NHIT subtopics, two are expected"
fi

#
#	9. An unknown topic is refused, with a non-zero completion
#
"$HELP" /LIBRARY="$WRK/lib.hlb" nosuchtopic /NOPAGE > "$WRK/o.9" 2>&1
STS=$?

if [ $STS -ne 0 ] && grep -q '%HELP-E-NOTFOUND' "$WRK/o.9"
then	ok "an unknown topic is refused with %HELP-E-NOTFOUND"
else	ko "an unknown topic is not refused (status $STS)"
fi

#
#	10. The library is located through HELP_LIBRARY
#
if env "HELP_LIBRARY=$WRK/lib.hlb" "$HELP" submit /NOPAGE 2>/dev/null | grep -q 'Submits one or more'
then	ok "the library is located through HELP_LIBRARY"
else	ko "the library is not located through HELP_LIBRARY"
fi

#
#	11. An explicit /LIBRARY overrides HELP_LIBRARY
#
if env "HELP_LIBRARY=$WRK/nosuch.hlb" "$HELP" /LIBRARY="$WRK/lib.hlb" submit /NOPAGE 2>/dev/null \
	| grep -q 'Submits one or more'
then	ok "/LIBRARY overrides HELP_LIBRARY"
else	ko "/LIBRARY does not override HELP_LIBRARY"
fi

#
#	12. A damaged library is refused by its checksum
#
cp "$WRK/lib.hlb" "$WRK/bad.hlb"
printf 'X' | dd of="$WRK/bad.hlb" bs=1 seek=200 conv=notrunc status=none

"$HELP" /LIBRARY="$WRK/bad.hlb" submit /NOPAGE > "$WRK/o.12" 2>&1
STS=$?

if [ $STS -ne 0 ] && grep -q '%HELP-E-BADCRC' "$WRK/o.12"
then	ok "a damaged library is refused with %HELP-E-BADCRC"
else	ko "a damaged library is not refused (status $STS)"
fi

#
#	13. A truncated library is refused before anything is dereferenced
#
head -c 400 "$WRK/lib.hlb" > "$WRK/short.hlb"

"$HELP" /LIBRARY="$WRK/short.hlb" submit /NOPAGE > "$WRK/o.13" 2>&1
STS=$?

if [ $STS -ne 0 ] && grep -q '%HELP-E-TRUNCLIB' "$WRK/o.13"
then	ok "a truncated library is refused with %HELP-E-TRUNCLIB"
else	ko "a truncated library is not refused (status $STS)"
fi

#
#	14. A file that is not a library at all
#
"$HELP" /LIBRARY="$WRK/src.md" submit /NOPAGE > "$WRK/o.14" 2>&1
STS=$?

if [ $STS -ne 0 ] && grep -q '%HELP-E-NOTHLB' "$WRK/o.14"
then	ok "a foreign file is refused with %HELP-E-NOTHLB"
else	ko "a foreign file is not refused (status $STS)"
fi

#
#	15. A missing library
#
"$HELP" /LIBRARY="$WRK/nosuch.hlb" submit /NOPAGE > "$WRK/o.15" 2>&1
STS=$?

if [ $STS -ne 0 ] && grep -q '%HELP-E-OPENIN' "$WRK/o.15"
then	ok "a missing library is refused with %HELP-E-OPENIN"
else	ko "a missing library is not refused (status $STS)"
fi

#
#	16. The subtopics are sorted by default and kept by /NOSORT
#
"$HELPTRAN" "$WRK/src.md" /OUTPUT="$WRK/ns.hlb" /NOSORT > /dev/null 2>&1

SORTED=`"$HELP" /LIBRARY="$WRK/lib.hlb" /LIST /NOPAGE 2>/dev/null | grep -m1 '/AFTER\|/HOLD'`
KEPT=`"$HELP" /LIBRARY="$WRK/ns.hlb"  /LIST /NOPAGE 2>/dev/null | grep -m1 '/AFTER\|/HOLD'`

if echo "$SORTED" | grep -q '/AFTER' && echo "$KEPT" | grep -q '/HOLD'
then	ok "the subtopics are sorted by default, /NOSORT keeps the source order"
else	ko "the ordering is wrong: sorted '$SORTED', kept '$KEPT'"
fi

#
#	17. The diagnostics of the translator
#
cat > "$WRK/bad.md" <<'EOF'
Text that precedes the first heading.

# ONE

Text of the first topic.

### THREE

A level that has been skipped.

# ONE

A key that duplicates a sibling.

#

A heading that carries no key.
EOF

"$HELPTRAN" "$WRK/bad.md" /OUTPUT="$WRK/bad2.hlb" > "$WRK/o.17" 2>&1

if grep -q '%HELP-W-NOTOP'   "$WRK/o.17" \
	&& grep -q '%HELP-W-SKIPLVL' "$WRK/o.17" \
	&& grep -q '%HELP-W-DUPKEY'  "$WRK/o.17" \
	&& grep -q '%HELP-E-NOKEY'   "$WRK/o.17"
then	ok "the translator reports NOTOP, SKIPLVL, DUPKEY and NOKEY"
else	ko "the translator is silent about a defective source"
fi

#
#	18. The <!-- key: --> directive overrides the heading text
#
cat > "$WRK/dir.md" <<'EOF'
# TOPIC

Body of the topic.

<!-- key: OVERRIDDEN -->
## Some long descriptive heading

Body of the subtopic.

<!-- ignore -->
## HIDDEN

This subtopic must not appear.

<!-- resume -->

# VISIBLE

Body.
EOF

"$HELPTRAN" "$WRK/dir.md" /OUTPUT="$WRK/dir.hlb" > /dev/null 2>&1
"$HELP" /LIBRARY="$WRK/dir.hlb" /LIST /NOPAGE > "$WRK/o.18" 2>/dev/null

if grep -q 'OVERRIDDEN' "$WRK/o.18" && grep -q 'VISIBLE' "$WRK/o.18" \
	&& ! grep -q 'HIDDEN' "$WRK/o.18"
then	ok "the key, ignore and resume directives are honoured"
else	ko "the directives are not honoured"
fi

#
#	19. Several sources make one library
#
cat > "$WRK/second.md" <<'EOF'
# ANOTHER

A topic of the second source.
EOF

"$HELPTRAN" "$WRK/src.md" "$WRK/second.md" /OUTPUT="$WRK/two.hlb" > /dev/null 2>&1

if "$HELP" /LIBRARY="$WRK/two.hlb" another /NOPAGE 2>/dev/null | grep -q 'second source' \
	&& "$HELP" /LIBRARY="$WRK/two.hlb" submit /NOPAGE 2>/dev/null | grep -q 'Submits one'
then	ok "several sources contribute to one library"
else	ko "several sources do not contribute to one library"
fi

#
#	20. /WIDTH controls the layout of the subtopic block
#
WIDE=`"$HELP" /LIBRARY="$WRK/lib.hlb" submit /NOPAGE /WIDTH=132 2>/dev/null \
	| awk '/Additional information/ {f = 1; next} f && NF {n++} END {print n + 0}'`
NARROW=`"$HELP" /LIBRARY="$WRK/lib.hlb" submit /NOPAGE /WIDTH=32 2>/dev/null \
	| awk '/Additional information/ {f = 1; next} f && NF {n++} END {print n + 0}'`

if [ "$NARROW" -gt "$WIDE" ]
then	ok "/WIDTH controls the number of the subtopic columns"
else	ko "/WIDTH does not control the layout ($WIDE line(s) wide, $NARROW narrow)"
fi

#
#	21. A redirected run neither pages nor prompts
#
echo "" | timeout 5 "$HELP" /LIBRARY="$WRK/lib.hlb" submit > /dev/null 2>&1
STS=$?

if [ $STS -eq 0 ]
then	ok "a redirected run neither pages nor prompts"
else	ko "a redirected run did not complete (status $STS)"
fi

#
#	22. An empty source produces no library
#
: > "$WRK/empty.md"

"$HELPTRAN" "$WRK/empty.md" /OUTPUT="$WRK/empty.hlb" > "$WRK/o.22" 2>&1
STS=$?

if [ $STS -ne 0 ] && grep -q '%HELP-E-EMPTYLIB' "$WRK/o.22" && [ ! -f "$WRK/empty.hlb" ]
then	ok "an empty source produces no library"
else	ko "an empty source produced a library (status $STS)"
fi

#
#	23. A failed translation leaves no half-written file behind
#
if [ ! -f "$WRK/empty.hlb.tmp" ]
then	ok "a failed translation leaves no temporary file behind"
else	ko "a temporary file has been left behind"
fi

#
#	24. The interactive dialogue, if a pseudo terminal can be had
#
if command -v script > /dev/null 2>&1
then
	printf 'submit\n/after\n\nexit\n' > "$WRK/in.24"

	script -qec "$HELP /LIBRARY=$WRK/lib.hlb /NOPAGE" /dev/null < "$WRK/in.24" > "$WRK/o.24" 2>&1

	if grep -q 'Topic?' "$WRK/o.24" \
		&& grep -q 'SUBMIT Subtopic?' "$WRK/o.24" \
		&& grep -q 'held until the given time' "$WRK/o.24"
	then	ok "the dialogue prompts, descends and returns one level up"
	else	ko "the dialogue does not work as expected"
	fi
else	ok "the dialogue check has been skipped, script(1) is absent"
fi

#
#	25. The header block of /LIST: the creation time and the producer.
#	    The time is a quadword handed to the FAO !%D directive BY VALUE
#	    (STARLET X.01-09); were it ever passed by reference again, the
#	    date printed here would be nonsense and this check would say so.
#
LIST=`"$HELP" /LIBRARY="$WRK/lib.hlb" /LIST /NOPAGE 2>/dev/null`
YEAR=`date +%Y`

if echo "$LIST" | grep -q "created *[0-9][0-9]*-[A-Z][A-Z][A-Z]-$YEAR " \
	&& echo "$LIST" | grep -qi 'helptran'
then	ok "/LIST reports the creation time and the producer of the library"
else	ko "/LIST does not report a plausible creation time"
fi

#
#	26. The extract/translate round trip settles
#
"$HELPTRAN" /EXTRACT "$WRK/lib.hlb" /OUTPUT="$WRK/r1.md"  > /dev/null 2>&1
"$HELPTRAN" "$WRK/r1.md" /OUTPUT="$WRK/r1.hlb"            > /dev/null 2>&1
"$HELPTRAN" /EXTRACT "$WRK/r1.hlb" /OUTPUT="$WRK/r2.md"   > /dev/null 2>&1
"$HELPTRAN" "$WRK/r2.md" /OUTPUT="$WRK/r2.hlb"            > /dev/null 2>&1
"$HELPTRAN" /EXTRACT "$WRK/r2.hlb" /OUTPUT="$WRK/r3.md"   > /dev/null 2>&1

if cmp -s "$WRK/r1.md" "$WRK/r2.md" && cmp -s "$WRK/r2.md" "$WRK/r3.md" \
	&& grep -q "^# SUBMIT\$" "$WRK/r1.md"
then	ok "the extract/translate round trip settles at the first pass"
else	ko "the extract/translate round trip does not settle"
fi

#
#	27. /KEY extracts one subtree and lifts it to the first level
#
"$HELPTRAN" /EXTRACT "$WRK/lib.hlb" /KEY=SUBMIT /OUTPUT="$WRK/sub.md" > /dev/null 2>&1

if grep -q "^# SUBMIT\$" "$WRK/sub.md" \
	&& grep -q "^## /AFTER\$" "$WRK/sub.md" \
	&& ! grep -q 'DELETE' "$WRK/sub.md"
then	ok "/KEY extracts one subtree, lifted to the first level"
else	ko "/KEY does not extract the subtree correctly"
fi

#
#	28. /EXTRACT refuses the qualifiers that describe a build
#
"$HELPTRAN" /EXTRACT "$WRK/lib.hlb" /LIST > "$WRK/o.28" 2>&1
STS=$?

if [ $STS -ne 0 ] && grep -q '%HELP-E-CONFQUAL' "$WRK/o.28"
then	ok "/EXTRACT with /LIST is refused with %HELP-E-CONFQUAL"
else	ko "/EXTRACT with /LIST is not refused (status $STS)"
fi

#
#	29. A key that carries a blank, as a run of words and quoted
#
cat > "$WRK/ml.md" <<'EOF'
# TOPIC

Body.

## SOURCE FORMAT

The body of the multi-word topic.

### DIRECTIVES

The body of the nested topic.
EOF

"$HELPTRAN" "$WRK/ml.md" /OUTPUT="$WRK/ml.hlb" > /dev/null 2>&1

RUN=`"$HELP" /LIBRARY="$WRK/ml.hlb" topic source format /NOPAGE 2>/dev/null | grep -c 'multi-word topic'`
QUO=`"$HELP" /LIBRARY="$WRK/ml.hlb" topic "source format" directives /NOPAGE 2>/dev/null | grep -c 'nested topic'`

if [ "$RUN" = 1 ] && [ "$QUO" = 1 ]
then	ok "a key that carries a blank is addressed as a run of words and quoted"
else	ko "a multi-word key is not addressed correctly ($RUN, $QUO)"
fi

#
#	30. Cyrillic keys: the case is folded on lookup
#
cat > "$WRK/cyr.md" <<'EOF'
# ROOT

Body.

## ААААА

Body of the first.

## ББББББ

Body of the second.

## ВВВВВВ

Body of the third.

## ГГГГГГ

Body of the fourth.
EOF

"$HELPTRAN" "$WRK/cyr.md" /OUTPUT="$WRK/cyr.hlb" > /dev/null 2>&1

if "$HELP" /LIBRARY="$WRK/cyr.hlb" root ббббб /NOPAGE 2>/dev/null | grep -q 'of the second'
then	ok "a Cyrillic key is found whatever case it is typed in"
else	ko "a Cyrillic key is not folded to the case of the library"
fi

#
#	31. The subtopic block aligns on characters, not on bytes: keys of
#	    equal width must lay out equally whichever alphabet they use
#
cat > "$WRK/lat.md" <<'EOF'
# ROOT

Body.

## AAAAA

a

## BBBBBB

b

## CCCCCC

c

## DDDDDD

d
EOF

"$HELPTRAN" "$WRK/lat.md" /OUTPUT="$WRK/lat.hlb" > /dev/null 2>&1

NLAT=`"$HELP" /LIBRARY="$WRK/lat.hlb" root /NOPAGE /WIDTH=40 2>/dev/null \
	| awk '/Additional information/ {f = 1; next} f && NF {n++} END {print n + 0}'`
NCYR=`"$HELP" /LIBRARY="$WRK/cyr.hlb" root /NOPAGE /WIDTH=40 2>/dev/null \
	| awk '/Additional information/ {f = 1; next} f && NF {n++} END {print n + 0}'`

if [ "$NLAT" = "$NCYR" ] && [ "$NLAT" = 2 ]
then	ok "the subtopic block aligns on the width in characters, not in bytes"
else	ko "the layout differs between the alphabets ($NLAT Latin, $NCYR Cyrillic)"
fi

#
#	32. The Russian edition of the manual builds and answers
#
if [ -f "$HELP_SRC/doc/helplib_ru.md" ]
then
	#	Rooted at the facility, as the installed library is - a check
	#	made against a shape the product does not ship proves nothing
	#	about the product.
	"$HELPTRAN" "$HELP_SRC/doc/helplib_ru.md" /OUTPUT="$WRK/ru.hlb" /IDENT=X01-35RU \
		/FACILITY=HELP > /dev/null 2>&1

	if "$HELP" /LIBRARY="$WRK/ru.hlb" help vhelptran "формат исходного текста" директивы /NOPAGE 2>/dev/null \
		| grep -q 'Распознаются три директивы'
	then	ok "the Russian edition builds and answers a Cyrillic path"
	else	ko "the Russian edition does not answer a Cyrillic path"
	fi
else	ko "the Russian edition of the manual is missing"
fi

#
#	33. Every qualifier the utilities accept is documented in the help
#	    library of the product.  The qualifier names are taken from the
#	    CLI tables of the sources, so a qualifier added without a topic
#	    to describe it fails here rather than in the field.
#
if [ -f "$HELP_SRC/doc/helplib.md" ] && [ -f "$HELP_SRC/src/help.c" ]
then
	"$HELPTRAN" "$HELP_SRC/doc/helplib.md" /OUTPUT="$WRK/self.hlb" /IDENT=X01-35 \
		/FACILITY=HELP > /dev/null 2>&1

	MISSING=""

	for PAIR in "VHELP:$HELP_SRC/src/help.c" "VHELPTRAN:$HELP_SRC/src/helptran.c"
	do
		VERB=`echo "$PAIR" | cut -d: -f1`
		SRC=`echo "$PAIR" | cut -d: -f2`

		QUALS=`awk '/^static[ \t]+CLI_PQDESC[ \t]+s_quals/ {f = 1; next}
			    f && /^\t\};/ {f = 0}
			    f' "$SRC" | sed -n 's/.*ASCINI("\([A-Z][A-Z]*\)").*/\1/p'`

		for Q in $QUALS
		do
			if ! "$HELP" /LIBRARY="$WRK/self.hlb" /NOPAGE -- help "$VERB" "/$Q" 2>&1 \
				| grep -q "^ *\/$Q\$"
			then	MISSING="$MISSING $VERB/$Q"
			fi
		done
	done

	if [ -z "$MISSING" ]
	then	ok "every qualifier of both utilities has a topic in the help library"
	else	ko "undocumented qualifiers:$MISSING"
	fi
else	ko "the sources needed for the documentation check are missing"
fi

#
#	34. The "--" marker reaches a key that is spelled like a qualifier
#
if "$HELP" /LIBRARY="$WRK/self.hlb" /NOPAGE -- help vhelp /list 2>/dev/null | grep -q '^      /LIST$' \
	&& "$HELP" /LIBRARY="$WRK/self.hlb" /NOPAGE help vhelp /list 2>/dev/null | grep -q 'ident X01-35'
then	ok "a lone -- ends the qualifiers, without it /LIST stays a qualifier"
else	ko "the -- marker does not work as documented"
fi

#
#	35. An argument that begins with a slash but is not a qualifier - a
#	    topic named /AFTER, an input file named by an absolute path -
#	    must not reach the CLI service, which refuses what it does not
#	    recognise.
#
"$HELP" /LIBRARY="$WRK/lib.hlb" submit /AFTER /NOPAGE > "$WRK/o.35" 2>&1

cp "$WRK/src.md" "$WRK/abs.md"

"$HELPTRAN" "$WRK/abs.md" /OUTPUT="$WRK/abs.hlb" > "$WRK/o.35b" 2>&1

if grep -q 'held until the given time' "$WRK/o.35" \
	&& ! grep -q 'Unrecognized qualifier' "$WRK/o.35" \
	&& ! grep -q 'Unrecognized qualifier' "$WRK/o.35b" \
	&& [ -f "$WRK/abs.hlb" ]
then	ok "a slash-leading topic and an absolute input path bypass the parser"
else	ko "a slash-leading argument reached the CLI service"
fi

#
#	36. Both forms of a negatable qualifier are accepted
#
"$HELP" /LIBRARY="$WRK/lib.hlb" submit /PAGE /NOPROMPT > "$WRK/o.36" 2>&1
STS=$?

"$HELPTRAN" "$WRK/src.md" /OUTPUT="$WRK/s1.hlb" /SORT   > /dev/null 2>&1
"$HELPTRAN" "$WRK/src.md" /OUTPUT="$WRK/s2.hlb" /NOSORT > /dev/null 2>&1

POS=`"$HELP" /LIBRARY="$WRK/s1.hlb" /LIST /NOPAGE 2>/dev/null | grep -m1 '/AFTER\|/HOLD'`
NEG=`"$HELP" /LIBRARY="$WRK/s2.hlb" /LIST /NOPAGE 2>/dev/null | grep -m1 '/AFTER\|/HOLD'`

if [ $STS -eq 0 ] && grep -q 'Submits one or more' "$WRK/o.36" \
	&& ! grep -q 'no documentation' "$WRK/o.36" \
	&& echo "$POS" | grep -q '/AFTER' && echo "$NEG" | grep -q '/HOLD'
then	ok "both forms of /PAGE, /PROMPT and /SORT are accepted"
else	ko "a negatable qualifier is not accepted in both forms"
fi

#
#	37. An over-long source line is reported, not silently truncated
#
{ printf '# T\n\n'; awk 'BEGIN { while (i++ < 3000) printf "x" }'; printf '\n'; } > "$WRK/long.md"

"$HELPTRAN" "$WRK/long.md" /OUTPUT="$WRK/long.hlb" > "$WRK/o.37" 2>&1

if grep -q '%HELP-W-LONGLINE' "$WRK/o.37"
then	ok "an over-long source line is reported with %HELP-W-LONGLINE"
else	ko "an over-long source line is truncated in silence"
fi

#
#	38. The authoring guide is present in the library and reachable
#
MISSING=""

for T in HEADINGS TEXT DIRECTIVES LIMITS EXAMPLE
do
	if ! "$HELP" /LIBRARY="$WRK/self.hlb" /NOPAGE help vhelptran "source format" "$T" 2>&1 \
		| grep -q "^        $T\$"
	then	MISSING="$MISSING $T"
	fi
done

if [ -z "$MISSING" ]
then	ok "the authoring guide covers headings, text, directives, limits and an example"
else	ko "the authoring guide is missing:$MISSING"
fi

#
#	39. Every spelling of a syntax request is answered by the utility
#	    itself, not by the CLI service, which would describe its own
#	    tables instead.  A bare HELP stays a topic.
#
BAD=""

for SPELL in -h --help -help /H /HELP
do
	if ! "$HELP" "$SPELL" 2>&1 | grep -q "^Usage: $HELPNAME "
	then	BAD="$BAD $SPELL"
	fi

	if ! "$HELPTRAN" "$SPELL" 2>&1 | grep -q "^Usage: $TRANNAME "
	then	BAD="$BAD helptran$SPELL"
	fi
done

if [ -z "$BAD" ] \
	&& "$HELP" /LIBRARY="$WRK/self.hlb" help vhelp /NOPAGE 2>/dev/null | grep -q '^    VHELP$' \
	&& ! "$HELP" /LIBRARY="$WRK/self.hlb" help vhelp /NOPAGE 2>&1 | grep -q 'Available commands'
then	ok "every spelling of a syntax request is answered by the utility itself"
else	ko "the syntax request is mishandled:$BAD"
fi

#
#	40. The catalogue finds a library on the search path by its topic,
#	    and the cache is written and then used
#
CATHOME="$WRK/home"
mkdir -p "$CATHOME/.help" "$WRK/site"

cp "$WRK/lib.hlb" "$WRK/site/one.hlb"

env "HOME=$CATHOME" "HELP_LIBRARY=$WRK/site" "$HELP" submit /NOPAGE > "$WRK/o.40" 2>&1

#	A catalogue written in the very second its directory changed is
#	deliberately distrusted, so the cache is proved after that second
#	has passed - which also proves the suspicion clears itself.
sleep 1

env "HOME=$CATHOME" "HELP_LIBRARY=$WRK/site" "$HELP" submit /NOPAGE > /dev/null 2>&1
env "HOME=$CATHOME" "HELP_LIBRARY=$WRK/site" "$HELP" /CATALOG /NOPAGE > "$WRK/o.40b" 2>&1

if grep -q 'Submits one or more' "$WRK/o.40" \
	&& grep -q 'from the cache' "$WRK/o.40b" \
	&& grep -q 'one.hlb' "$WRK/o.40b"
then	ok "a topic locates its library on the search path, and the cache is used"
else	ko "the catalogue does not locate a library on the search path"
fi

#
#	41. A library appearing later invalidates the cache, and the element
#	    earlier on the search path takes precedence over the later one
#
mkdir -p "$WRK/priv"

PATHLIST="$WRK/priv:$WRK/site"

env "HOME=$CATHOME" "HELP_LIBRARY=$PATHLIST" "$HELP" submit /NOPAGE > "$WRK/o.41a" 2>&1

cat > "$WRK/priv.md" <<'EOF'
# SUBMIT

The private text that must win.
EOF

"$HELPTRAN" "$WRK/priv.md" /OUTPUT="$WRK/priv/priv.hlb" > /dev/null 2>&1

env "HOME=$CATHOME" "HELP_LIBRARY=$PATHLIST" "$HELP" submit /NOPAGE > "$WRK/o.41" 2>&1

if grep -q 'Submits one or more' "$WRK/o.41a" \
	&& grep -q 'private text that must win' "$WRK/o.41"
then	ok "a library appearing later is noticed, and the earlier path element wins"
else	ko "the cache was not invalidated by a new library"
fi

#
#	42. A damaged, truncated or foreign cache is refused, not obeyed
#
CACHE=`ls -t "$CATHOME"/.cache/help/cat-*.idx 2>/dev/null | head -1`

if [ -n "$CACHE" ]
then
	printf 'XXXX' | dd of="$CACHE" bs=1 seek=64 conv=notrunc status=none

	env "HOME=$CATHOME" "HELP_LIBRARY=$PATHLIST" "$HELP" submit /NOPAGE > "$WRK/o.42" 2>&1
	STS1=$?

	head -c 96 "$CACHE" > "$WRK/short.idx" && cp "$WRK/short.idx" "$CACHE"

	env "HOME=$CATHOME" "HELP_LIBRARY=$PATHLIST" "$HELP" submit /NOPAGE > "$WRK/o.42b" 2>&1
	STS2=$?

	chmod 644 "$CACHE"

	env "HOME=$CATHOME" "HELP_LIBRARY=$PATHLIST" "$HELP" /CATALOG /NOPAGE > "$WRK/o.42c" 2>&1

	if [ $STS1 -eq 0 ] && [ $STS2 -eq 0 ] \
		&& grep -q 'private text that must win' "$WRK/o.42" \
		&& grep -q 'private text that must win' "$WRK/o.42b" \
		&& grep -q 'rebuilt' "$WRK/o.42c"
	then	ok "a damaged, truncated or loosely permitted cache is rebuilt, not obeyed"
	else	ko "a defective cache was not refused ($STS1, $STS2)"
	fi
else	ko "no cache file was written"
fi

#
#	43. A cache that is a symbolic link is refused, and its target is
#	    left alone
#
if [ -n "$CACHE" ]
then
	echo "the target must survive" > "$WRK/target"
	rm -f "$CACHE"
	ln -s "$WRK/target" "$CACHE"

	env "HOME=$CATHOME" "HELP_LIBRARY=$PATHLIST" "$HELP" submit /NOPAGE > /dev/null 2>&1

	if grep -q 'the target must survive' "$WRK/target"
	then	ok "a cache that is a symbolic link is refused, its target untouched"
	else	ko "a symbolic link cache overwrote its target"
	fi
else	ko "no cache file to replace with a link"
fi

#
#	44. /NOCACHE bypasses the cache, /LIBRARY naming one file pins the
#	    run to it whatever the search path holds
#
env "HOME=$CATHOME" "HELP_LIBRARY=$PATHLIST" "$HELP" /CATALOG /NOCACHE /NOPAGE > "$WRK/o.44" 2>&1
env "HOME=$CATHOME" "HELP_LIBRARY=$PATHLIST" "$HELP" /LIBRARY="$WRK/site/one.hlb" submit /NOPAGE \
	> "$WRK/o.44b" 2>&1

if grep -q 'not cached' "$WRK/o.44" \
	&& grep -q 'Submits one or more' "$WRK/o.44b" \
	&& ! grep -q 'private text' "$WRK/o.44b"
then	ok "/NOCACHE bypasses the cache and /LIBRARY pins the run to one file"
else	ko "/NOCACHE or /LIBRARY does not behave as documented"
fi

#
#	45. A search path named by the qualifier is the whole of it, and one
#	    that reaches nothing is reported rather than silently ignored
#
mkdir -p "$WRK/empty"

#	A path named by the qualifier is the whole of it, so this reaches
#	nothing at all whatever the installed libraries happen to be.
"$HELP" /LIBRARY="$WRK/empty" submit /NOPAGE > "$WRK/o.45" 2>&1
STS=$?

if [ $STS -ne 0 ] && grep -q '%HELP-E-NOLIBS' "$WRK/o.45"
then	ok "an empty search path is reported with %HELP-E-NOLIBS"
else	ko "an empty search path is not reported (status $STS)"
fi

#
#	46. The standard output carries data and nothing else.  Both
#	    utilities write a topic or a Markdown source there, so a
#	    diagnostic landing in it corrupts the very thing being
#	    produced - which is what a $TRACE did before the logging of
#	    the UTILITY$ROUTINES was diverted, and what a $LOG still does
#	    on a set older than V.01-15 unless the message goes through
#	    the catalogue of this facility.
#
"$HELPTRAN" /EXTRACT "$WRK/lib.hlb" 2>/dev/null > "$WRK/o.46"

{ printf '# T\n\n'; awk 'BEGIN { while (i++ < 2000) printf "x" }'; printf '\n'; } > "$WRK/noisy.md"

"$HELPTRAN" "$WRK/noisy.md" /OUTPUT="$WRK/noisy.hlb" 2>/dev/null > "$WRK/o.46b"

"$HELP" /LIBRARY="$WRK/lib.hlb" submit /NOPAGE 2>/dev/null > "$WRK/o.46c"

#	A record of either channel begins with a timestamp or with %HELP-
NOISE=`cat "$WRK/o.46" "$WRK/o.46b" "$WRK/o.46c" \
	| grep -c '^[0-9][0-9]-[0-9][0-9]-[0-9]*\|^%HELP-'`

if [ "$NOISE" = 0 ] && [ -s "$WRK/o.46" ] && [ ! -s "$WRK/o.46b" ]
then	ok "the standard output carries data alone, diagnostics go to the standard error"
else	ko "$NOISE diagnostic line(s) reached the standard output"
fi

#
#	47. A command that does not fit the arrays says so.  Dropping the
#	    tail in silence makes the utility answer about a topic above
#	    the one asked for and report success doing it.
#
I=0
INFILES=""

while [ $I -lt 70 ]
do
	printf '# T%s\n\nBody %s.\n' "$I" "$I" > "$WRK/many$I.md"
	INFILES="$INFILES $WRK/many$I.md"
	I=`expr $I + 1`
done

"$HELPTRAN" $INFILES /OUTPUT="$WRK/many.hlb" > "$WRK/o.47" 2>&1

WORDS=`awk 'BEGIN { while (i++ < 70) printf "w%d ", i }'`

"$HELP" /LIBRARY="$WRK/lib.hlb" $WORDS /NOPAGE > "$WRK/o.47b" 2>&1

if grep -q '%HELP-W-TOOMANYIN' "$WRK/o.47" && grep -q '%HELP-W-TOOMANYW' "$WRK/o.47b"
then	ok "an over-long command reports what it dropped"
else	ko "an over-long command drops its tail in silence"
fi

#
#	48. A path of several levels whose keys carry blanks reaches the
#	    topic it names.  A key of two words times nine levels is far
#	    more than nine words, which is what once truncated a legitimate
#	    path into an answer about its parent.
#
cat > "$WRK/deep.md" <<'EOF'
# ALPHA BRAVO CHARLIE DELTA

Body of level 1.

## ECHO FOXTROT GOLF

Body of level 2.

### HOTEL INDIA JULIET

Body of level 3.

#### KILO LIMA MIKE

Body of level 4.

##### NOVEMBER OSCAR PAPA

Body of level 5.

###### QUEBEC ROMEO SIERRA

Body of level 6.
EOF

"$HELPTRAN" "$WRK/deep.md" /OUTPUT="$WRK/deep.hlb" > /dev/null 2>&1

"$HELP" /LIBRARY="$WRK/deep.hlb" /NOPAGE \
	alpha bravo charlie delta echo foxtrot golf hotel india juliet \
	kilo lima mike november oscar papa quebec romeo sierra > "$WRK/o.48" 2>&1

if grep -q 'Body of level 6' "$WRK/o.48"
then	ok "a nineteen-word path of multi-word keys reaches the topic it names"
else	ko "a long path of multi-word keys does not reach its topic"
fi

#
#	49. An element ending in the ellipsis is scanned to the depth of
#	    its tree, and one without it is not.  A link to a directory is
#	    not descended into: that is how a tree comes to contain itself.
#
mkdir -p "$WRK/tree/a/b/c"
cp "$WRK/lib.hlb" "$WRK/tree/a/b/c/deep.hlb"
ln -s "$WRK/tree" "$WRK/tree/a/loop" 2>/dev/null

"$HELP" /LIBRARY="$WRK/tree" submit /NOPAGE > "$WRK/o.49" 2>&1
STS1=$?

timeout 30 "$HELP" /LIBRARY="$WRK/tree/..." submit /NOPAGE > "$WRK/o.49b" 2>&1
STS2=$?

if [ $STS1 -ne 0 ] && grep -q '%HELP-E-NOLIBS' "$WRK/o.49" \
	&& [ $STS2 -eq 0 ] && grep -q 'Submits one or more' "$WRK/o.49b"
then	ok "the ellipsis scans a tree, and a link inside it does not trap the scan"
else	ko "the recursive scan is wrong ($STS1, $STS2)"
fi

#
#	50. The shared catalogue: built by /SYSCACHE, read by a user who
#	    did not build it, and never written by an ordinary run
#
SYSC="$WRK/sysc"
mkdir -p "$SYSC" "$WRK/sysroot"

cp "$WRK/lib.hlb" "$WRK/sysroot/one.hlb"

sleep 1

env "HELP_SYSCACHE=$SYSC" "$HELP" /SYSCACHE /LIBRARY="$WRK/sysroot" > "$WRK/o.50" 2>&1

rm -rf "$WRK/fresh"
mkdir -p "$WRK/fresh"

env "HOME=$WRK/fresh" "HELP_SYSCACHE=$SYSC" "HELP_LIBRARY=$WRK/sysroot" \
	"$HELP" /CATALOG /NOPAGE > "$WRK/o.50b" 2>&1

BEFORE=`cat "$SYSC/catalog.idx" | wc -c`

env "HOME=$WRK/fresh" "HELP_SYSCACHE=$SYSC" "HELP_LIBRARY=$WRK/sysroot" \
	"$HELP" submit /NOPAGE > "$WRK/o.50c" 2>&1

AFTER=`cat "$SYSC/catalog.idx" | wc -c`

if grep -q '%HELP-I-SYSCACHE' "$WRK/o.50" \
	&& grep -q 'the shared catalogue helping' "$WRK/o.50b" \
	&& grep -q 'Submits one or more' "$WRK/o.50c" \
	&& [ "$BEFORE" = "$AFTER" ]
then	ok "the shared catalogue is built once and read by a user who did not build it"
else	ko "the shared catalogue does not work as intended"
fi

#
#	51. A shared catalogue anyone may write is refused: it would be a
#	    way of steering every user of the system to a library of one's
#	    choosing
#
chmod 666 "$SYSC/catalog.idx"

rm -rf "$WRK/fresh2"
mkdir -p "$WRK/fresh2"

env "HOME=$WRK/fresh2" "HELP_SYSCACHE=$SYSC" "HELP_LIBRARY=$WRK/sysroot" \
	"$HELP" /CATALOG /NOPAGE > "$WRK/o.51" 2>&1

chmod 644 "$SYSC/catalog.idx"

if grep -q 'rebuilt' "$WRK/o.51" && ! grep -q 'shared catalogue helping' "$WRK/o.51"
then	ok "a world-writable shared catalogue is refused"
else	ko "a world-writable shared catalogue was trusted"
fi

#
#	52. The locale puts the translation of a manual ahead of it
#
mkdir -p "$WRK/loc/xx"

cp "$WRK/lib.hlb" "$WRK/loc/one.hlb"

cat > "$WRK/tr.md" <<'EOF'
# SUBMIT

The translated text.
EOF

"$HELPTRAN" "$WRK/tr.md" /OUTPUT="$WRK/loc/xx/tr.hlb" > /dev/null 2>&1

PLAIN=`env "HELP_LIBRARY=$WRK/loc" LANG=C "$HELP" submit /NOPAGE 2>/dev/null \
	| grep -c 'Submits one or more'`
XLATED=`env "HELP_LIBRARY=$WRK/loc/xx:$WRK/loc" LANG=xx_XX.UTF-8 "$HELP" submit /NOPAGE 2>/dev/null \
	| grep -c 'The translated text'`

if [ "$PLAIN" = 1 ] && [ "$XLATED" = 1 ]
then	ok "a search path holding a translation still answers"
else	ko "the locale handling broke the search path ($PLAIN, $XLATED)"
fi

#
#	53. The cache directory is refused unless it is a private directory
#	    of this user, and is used through a descriptor thereafter - so
#	    that a name swapped underneath cannot redirect the writing.
#
mkdir -p "$WRK/hostile/evil" "$WRK/hostile/libs"
chmod 1777 "$WRK/hostile"
cp "$WRK/lib.hlb" "$WRK/hostile/libs/"

ln -s "$WRK/hostile/evil" "$WRK/hostile/link"

mkdir -p "$WRK/hostile/loose"  && chmod 777 "$WRK/hostile/loose"
mkdir -p "$WRK/hostile/proper" && chmod 700 "$WRK/hostile/proper"

for C in link loose proper
do
	env "HOME=$WRK/nohome" "HELP_CACHE=$WRK/hostile/$C" \
		"HELP_LIBRARY=$WRK/hostile/libs" "$HELP" submit /NOPAGE > /dev/null 2>&1
done

VIA_LINK=`ls -A "$WRK/hostile/evil"  | wc -l`
VIA_LOOSE=`ls -A "$WRK/hostile/loose"  | wc -l`
VIA_PROPER=`ls -A "$WRK/hostile/proper" | wc -l`

if [ "$VIA_LINK" -eq 0 ] && [ "$VIA_LOOSE" -eq 0 ] && [ "$VIA_PROPER" -gt 0 ]
then	ok "the cache is written only into a private directory of this user"
else	ko "the cache directory checks are wrong ($VIA_LINK, $VIA_LOOSE, $VIA_PROPER)"
fi

#
#	54. Every message of the catalogue is described in the manual, in
#	    both editions.  The names are taken from the source, so a
#	    message added without a line to explain it fails here rather
#	    than in front of a user who has met it.
#
if [ -f "$HELP_SRC/src/helputl.c" ]
then
	NAMES=`sed -n 's/.*{ HELP\$_[A-Z]*,[ \t]*"\([A-Z]*\)".*/\1/p' "$HELP_SRC/src/helputl.c"`
	UNDOC=""

	for M in $NAMES
	do
		case "$M" in
		NORMAL)	continue ;;		# the success code, never displayed
		esac

		for ED in "$HELP_SRC/doc/helplib.md" "$HELP_SRC/doc/helplib_ru.md"
		do
			if ! grep -q "%HELP-[SIWEF]-$M\b" "$ED"
			then	UNDOC="$UNDOC $M(`basename $ED`)"
			fi
		done
	done

	if [ -z "$UNDOC" ]
	then	ok "every message of the catalogue is described in both editions"
	else	ko "messages with no description:$UNDOC"
	fi
else	ko "the source needed for the message check is missing"
fi

#
#	55. A consumer reaches help_add_library() whichever way it reached
#	    this product.  The installed package pulls the helper in from
#	    HELPConfig.cmake; a consumer that used ADD_SUBDIRECTORY has no
#	    HELPConfig, and without the helper being included in the tree
#	    as well the two ways differ - the failing one saying only
#	    "Unknown CMake command".
#
if [ -f "$HELP_SRC/cmake/HELPHelpers.cmake" ]
then
	if grep -q 'include(.*cmake/HELPHelpers\.cmake)' "$HELP_SRC/CMakeLists.txt" \
		&& grep -q 'add_executable(HELP::helptran ALIAS' "$HELP_SRC/CMakeLists.txt"
	then	ok "help_add_library and HELP::helptran are defined in the tree as well"
	else	ko "a consumer using add_subdirectory would not find help_add_library"
	fi
else	ko "the helper module is missing"
fi

#
#	56. A qualifier is spelled with a slash or with the doubled dash of
#	    the GNU long form - the two prefixes CLI_ROUTINES recognises,
#	    and only those.  A spelling this utility took for a topic while
#	    the service would have taken it for a qualifier once made
#	    --LIBRARY=... select nothing at all, in silence.
#
SLASH=`"$HELP" /LIBRARY="$WRK/lib.hlb" submit /NOPAGE 2>/dev/null | grep -c 'Submits one or more'`
LONG=`"$HELP" --LIBRARY="$WRK/lib.hlb" submit --NOPAGE 2>/dev/null | grep -c 'Submits one or more'`

#	and a single dash is NOT a prefix, so it stays a topic name
"$HELP" /LIBRARY="$WRK/lib.hlb" -LIBRARY=x /NOPAGE > "$WRK/o.56" 2>&1

if [ "$SLASH" = 1 ] && [ "$LONG" = 1 ] && grep -q '%HELP-E-NOTFOUND' "$WRK/o.56"
then	ok "a qualifier is spelled /NAME or --NAME, and a single dash is not a prefix"
else	ko "the qualifier prefixes are wrong ($SLASH, $LONG)"
fi

#
#	57. /IDENT outranks the <!-- ident: --> directive of the source: a
#	    qualifier is an instruction for the run at hand.  The two once
#	    agreed by accident, so the order between them was never tested
#	    and the directive quietly won.
#
cat > "$WRK/id.md" <<'EOF'
<!-- ident: FROM-SOURCE -->

# TOPIC

Body.
EOF

"$HELPTRAN" "$WRK/id.md" /OUTPUT="$WRK/id1.hlb" /IDENT=FROM-COMMAND > /dev/null 2>&1
"$HELPTRAN" "$WRK/id.md" /OUTPUT="$WRK/id2.hlb"                     > /dev/null 2>&1

WITH=`"$HELP" /LIBRARY="$WRK/id1.hlb" /LIST /NOPAGE 2>/dev/null | sed -n 's/.*ident \([^,]*\),.*/\1/p'`
WITHOUT=`"$HELP" /LIBRARY="$WRK/id2.hlb" /LIST /NOPAGE 2>/dev/null | sed -n 's/.*ident \([^,]*\),.*/\1/p'`

if [ "$WITH" = FROM-COMMAND ] && [ "$WITHOUT" = FROM-SOURCE ]
then	ok "/IDENT outranks the directive, and the directive still serves alone"
else	ko "the identification precedence is wrong ($WITH, $WITHOUT)"
fi

#
#	58. Every module carries one number of its own - the identification -
#	    and the rest follow from it: __IDENT__ is the topmost entry of the
#	    revision history below it, and __REV__ is Xaa-bb[ECOc] rendered as
#	    aa.bb.c.  A number written by hand is a number that drifts: all
#	    seven modules once said __REV__ "1.0.0" while their idents had
#	    moved on, and one said X-11 while its history said X.01-10.
#
BAD=""

for M in "$HELP_SRC"/src/*.c
do
	ID=`sed -n 's/^#define\t__IDENT__\t"\(.*\)"/\1/p' "$M" | head -1`
	RV=`sed -n 's/^#define\t__REV__\t\t"\(.*\)"/\1/p' "$M" | head -1`
	TOP=`sed -n 's/^\*\*\t\(X[0-9][0-9]-[0-9][0-9][A-Z0-9]*\)\t.*/\1/p' "$M" | sort -r | head -1`

	#  the form itself
	case "$ID" in
	X[0-9][0-9]-[0-9][0-9]|X[0-9][0-9]-[0-9][0-9]ECO[0-9]*)	;;
	*)	BAD="$BAD `basename $M`(form:$ID)" ; continue ;;
	esac

	#  aa.bb.c out of Xaa-bb[ECOc]
	AA=`expr "$ID" : 'X\([0-9][0-9]*\)-'`
	BB=`expr "$ID" : 'X[0-9]*-\([0-9][0-9]*\)'`
	CC=`expr "$ID" : '.*ECO\([0-9][0-9]*\)$'`
	[ -z "$CC" ] && CC=0

	[ "$RV" = "$AA.$BB.$CC" ] || BAD="$BAD `basename $M`(rev:$RV<>$AA.$BB.$CC)"
	[ "$ID" = "$TOP" ]        || BAD="$BAD `basename $M`(hist:$ID<>$TOP)"
done

if [ -z "$BAD" ]
then	ok "every module's ident is well formed, tops its history and yields its rev"
else	ko "identification is out of step:$BAD"
fi

#
#	59. /FACILITY roots a library at one key of level 1, and the library
#	    this product installs is rooted at its own.  The catalogue of a
#	    search path merges the level 1 topics of every library it finds,
#	    so a library that exports its verbs there collides with the next
#	    product's; one key per library is what keeps that root legible.
#
cat > "$WRK/fac.md" <<'EOF'
Text before the first heading.

# SUBMIT

Submit a job.

## /AFTER

Hold it.

# DELETE

Delete an entry.
EOF

"$HELPTRAN" "$WRK/fac.md" /OUTPUT="$WRK/fac.hlb" /FACILITY=BATCH > /dev/null 2>&1

TREE=`"$HELP" /LIBRARY="$WRK/fac.hlb" /LIST /NOPAGE 2>/dev/null`
#	The first four lines of /LIST are the header - the file, its ident
#	and its creator - and only what follows is the tree.  Counting by
#	the name of the creator would tie this check to the name the images
#	were built under, which is a setting.
ROOTS=`echo "$TREE" | sed -n '5,$p' | sed -n 's/^  \([^ ].*\)$/\1/p' | wc -l`

#	the sole level 1 key is the facility, the verbs sit below it, and the
#	preamble became the text of the facility topic
if [ "$ROOTS" = 1 ] \
	&& echo "$TREE" | grep -q '^  BATCH$' \
	&& echo "$TREE" | grep -q '^    SUBMIT$' \
	&& echo "$TREE" | grep -q '^      /AFTER$' \
	&& "$HELP" /LIBRARY="$WRK/fac.hlb" batch /NOPAGE 2>/dev/null | grep -q 'Text before the first heading'
then	ok "/FACILITY roots a library at one key, with the verbs below it"
else	ko "/FACILITY does not root the library ($ROOTS keys of level 1)"
fi

#	and the library of this product obeys the same rule
OWN=`"$HELP" /LIBRARY="$HELP_BIN/helplib.hlb" /LIST /NOPAGE 2>/dev/null \
	| sed -n 's/^  \([A-Z][A-Z]*\)$/\1/p' | wc -l`

if [ "$OWN" = 1 ]
then	ok "the library of this product carries one key of level 1"
else	ko "the library of this product carries $OWN keys of level 1"
fi

#
#	61. help_add_library() carries the facility root through, and takes
#	    several sources into one library - a product with a manual per
#	    command keeps them under a single key that way.  The helper is
#	    exercised as a consumer sees it, through the installed package.
#
if command -v cmake > /dev/null 2>&1
then
	CONS="$WRK/consumer"
	mkdir -p "$CONS/doc"

	cmake --install "$HELP_BUILD_DIR" --prefix "$CONS/pkg" > /dev/null 2>&1

	printf '# SUBMIT\n\nSubmit a job.\n' > "$CONS/doc/submit.md"
	printf '# QUEUE\n\nManage a queue.\n'  > "$CONS/doc/queue.md"

	cat > "$CONS/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.16)
project(BATCH VERSION 3.1.0 LANGUAGES C)
include(GNUInstallDirs)
find_package(HELP REQUIRED)
help_add_library(NAME batch_help
                 SOURCE doc/submit.md doc/queue.md
                 OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/batch.hlb
                 ALL)
help_add_library(NAME flat_help SOURCE doc/submit.md
                 OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/flat.hlb NOFACILITY ALL)
EOF

	if cmake -S "$CONS" -B "$CONS/b" -DCMAKE_PREFIX_PATH="$CONS/pkg" > /dev/null 2>&1 \
		&& cmake --build "$CONS/b" > /dev/null 2>&1
	then
		ROOTED=`"$HELP" /LIBRARY="$CONS/b/batch.hlb" /LIST /NOPAGE 2>/dev/null`
		FLAT=`"$HELP" /LIBRARY="$CONS/b/flat.hlb" /LIST /NOPAGE 2>/dev/null`

		#	the facility defaults to the name of the project, both
		#	sources land under it, and NOFACILITY declines the root
		if echo "$ROOTED" | grep -q '^  BATCH$' \
			&& echo "$ROOTED" | grep -q '^    SUBMIT$' \
			&& echo "$ROOTED" | grep -q '^    QUEUE$' \
			&& echo "$FLAT" | grep -q '^  SUBMIT$'
		then	ok "help_add_library roots at the project, joins sources, and NOFACILITY opts out"
		else	ko "help_add_library does not carry the facility through"
		fi
	else	ko "a consumer of help_add_library does not configure or build"
	fi
else	ok "cmake is absent, the helper check is skipped"
fi

#
#	62. Every character the matcher of the UTILITY$ROUTINES honours is
#	    one this utility offers it.  The set is named once, in
#	    HELPDEF.H; when '?' joined '%' in the library the utility went
#	    on looking a topic carrying it up as a name, and found nothing.
#
STAR=`"$HELP" /LIBRARY="$WRK/lib.hlb" 'SUB*' /NOPAGE 2>/dev/null | grep -c 'Submits one or more'`
PCT=`"$HELP" /LIBRARY="$WRK/lib.hlb" 'SUBMI%' /NOPAGE 2>/dev/null | grep -c 'Submits one or more'`
QUE=`"$HELP" /LIBRARY="$WRK/lib.hlb" 'SUBMI?' /NOPAGE 2>/dev/null | grep -c 'Submits one or more'`

#	the set of the utility must not be narrower than the library's
WILD=`sed -n 's/^#define\tHELP\$K_WILDCARD\t"\(.*\)".*/\1/p' "$HELP_SRC/include/helpdef.h"`

if [ "$STAR" = 1 ] && [ "$PCT" = 1 ] && [ "$QUE" = 1 ] \
	&& [ "$WILD" = '*%?' ]
then	ok "the wildcards of the library are all offered to it by the utility"
else	ko "a wildcard is not honoured ($STAR, $PCT, $QUE, set '$WILD')"
fi

#
#	63. Every example of the manuals that asks this product's own
#	    library a question is run, and must be answered.  The examples
#	    went stale the moment the library was rooted at its facility:
#	    the topics moved one level down and every one of them asked
#	    after a topic of level 1 that had become a level 2.
#
STALE=""
TRIED=0

for ED in "$HELP_SRC/doc/helplib.md" "$HELP_SRC/doc/helplib_ru.md"
do
	case "$ED" in
	*_ru.md) LIB="$WRK/ru.hlb" ;;
	*)	 LIB="$WRK/self.hlb" ;;
	esac

	[ -f "$LIB" ] || continue

	#	the examples that name no library of their own - those are the
	#	ones this product's own library must answer
	sed -n 's/^\$ vhelp \(.*\)$/\1/p' "$ED" | grep -v '/[Ll][Ii][Bb][Rr][Aa][Rr][Yy]=' \
		| grep -v '^/SYSCACHE' | grep -v '|' | sort -u > "$WRK/ex.txt"

	#	An example is passed to eval with the wildcard it carries -
	#	and a wildcard is the shell's as well as this product's: "/*"
	#	would become every entry of the root directory, /sys among
	#	them, which the utility reads as /SYSCACHE and rebuilds the
	#	shared catalogue of the system out of the test library.  The
	#	shell is told to leave the wildcards alone for the duration
	set -f

	while read -r EX
	do
		#	an example may name a topic of a library it invented for
		#	the purpose - SUBMIT, COPY - which this one does not hold
		case "$EX" in
		submit*|sub\**|copy*)	continue ;;
		esac

		TRIED=`expr $TRIED + 1`

		#	eval, because an example may carry quotes of its own
		if ! eval "\"$HELP\" /LIBRARY=\"$LIB\" /NOPAGE $EX" 2>&1 \
			| grep -qv '%HELP-E-NOTFOUND'
		then	STALE="$STALE [$EX]"
		fi
	done < "$WRK/ex.txt"

	set +f
done

if [ -z "$STALE" ] && [ $TRIED -gt 0 ]
then	ok "the $TRIED example(s) of the manuals are answered by the library they describe"
else	ko "examples that no longer work:$STALE"
fi

#
#	64. Every environment variable the utilities read is named in both
#	    manuals.  A variable that steers the search or the cache and is
#	    documented nowhere is a setting only its author knows about.
#
UNDOC=""

for V in `grep -rhoE 'getenv\("[^"]+"\)' "$HELP_SRC"/src/*.c \
		| sed 's/getenv("//; s/")//' | sort -u`
do
	for ED in "$HELP_SRC/doc/helplib.md" "$HELP_SRC/doc/helplib_ru.md"
	do
		grep -qF "$V" "$ED" || UNDOC="$UNDOC $V(`basename $ED`)"
	done
done

if [ -z "$UNDOC" ]
then	ok "every environment variable the utilities read is documented in both editions"
else	ko "environment variables with no description:$UNDOC"
fi

#
#	64b. And none of them carries a '$'.  A shell cannot set such a
#	     variable at all - NAME=value is refused for it and only ENV(1)
#	     will do - so the name is unusable by the very people it is
#	     meant for.  The macros of the sources keep the '$'; what the
#	     environment is asked for does not.
#
DOLLAR=`grep -rhoE 'getenv\("[^"]+"\)' "$HELP_SRC"/src/*.c \
		| sed 's/getenv("//; s/")//' | grep '[$]' | sort -u | tr '\n' ' '`

if [ -z "$DOLLAR" ]
then	ok "no environment variable of the utilities carries a dollar"
else	ko "environment variables a shell cannot set: $DOLLAR"
fi

#
#	65. Every setting of the build is named in the README.  A cache
#	    variable nobody wrote down is one only its author can find.
#
UNDOC=""

for O in `grep -oE "^(option|set)\(HELP_[A-Z_]+" "$HELP_SRC/CMakeLists.txt" \
		| sed 's/^[a-z]*(//' | sort -u`
do
	grep -qF "$O" "$HELP_SRC/README.md" || UNDOC="$UNDOC $O"
done

if [ -z "$UNDOC" ]
then	ok "every setting of the build is named in the README"
else	ko "settings named nowhere:$UNDOC"
fi

echo ""
echo "%HELP-I-SMOKE, $CHECK check(s) performed, $FAILED failure(s)"

exit `[ $FAILED -eq 0 ]; echo $?`
