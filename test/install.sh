#!/bin/sh
#+++
#
#	FACILITY:	HELP - OpenVMS-style help library subsystem for Linux
#
#	MODULE:		test/install.sh
#
#	ABSTRACT:	Checks what an installation of the product contains.
#
#	DESCRIPTION:	The product installs two utilities, their help
#			libraries, and the reading library a consumer links
#			against - and nothing else.  The check exists
#			because the StarLet package, back when it was built
#			as a subproject of this one, once installed itself -
#			library, headers and CMake configuration - into the
#			prefix of whoever merely wanted the two utilities.
#
#			The exported package is checked as well: what it
#			imposes on a consumer is part of what is installed,
#			and this product's warning policy once travelled
#			with it, refusing to compile a consumer's own code.
#
#			HELP_BUILD names the build directory, HELP_CMAKE the
#			cmake command to install with.
#
#	AUTHOR:		StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
#
#	CREATION DATE:	 1-AUG-2026
#
#	MODIFICATION HISTORY:
#
#		11-SEP-2026	RRL	X01-36ECO1 : The relocated image proves it found the library
#					beside it through its catalogue, and answers the topic with
#					the run pinned to that library - the configured directory
#					comes first on its path, and a product installed there
#					(BATCH) defines the topic HELP and used to answer instead.
#					XDG_CACHE_HOME of the shell is dropped.
#
#		 1-AUG-2026	RRL	X01-00 : Initial version.
#
#---

set -u

HELP_BUILD=${HELP_BUILD:-.}
HELP_CMAKE=${HELP_CMAKE:-cmake}
HELP_HELPNAME=${HELP_HELPNAME:-vhelp}
HELP_TRANNAME=${HELP_TRANNAME:-vhelptran}
HELP_ALIAS=${HELP_ALIAS:-OFF}
HELP_LIBDIR=${HELP_LIBDIR:-lib}

WRK=`mktemp -d /tmp/helpinst.XXXXXX`
FAILED=0

trap 'rm -rf "$WRK"' EXIT INT TERM

if ! "$HELP_CMAKE" --install "$HELP_BUILD" --prefix "$WRK/root" > "$WRK/log" 2>&1
then
	echo "%HELP-E-INSTALL, the installation itself failed:"
	cat "$WRK/log"

	exit 1
fi

#	The per-configuration file of the export set carries the build type
#	in its name - HELPTargets-release.cmake, -debug.cmake - so the name
#	is normalised before the comparison.
#	The additional name is a symbolic link, so links count as well
FOUND=`cd "$WRK/root" && find . \( -type f -o -type l \) | sed 's|^\./||' \
	| sed 's|^lib/cmake/HELP/HELPTargets-.*\.cmake$|lib/cmake/HELP/HELPTargets-CONFIG.cmake|' \
	| sort`

#
#	Everything the product is expected to install: the two utilities,
#	their help libraries, and the reading library with the header and
#	the package configuration a consumer needs to link it.
#
EXPECTED="bin/$HELP_HELPNAME
bin/$HELP_TRANNAME
include/help/helpdef.h
include/help/helpvers.h
lib/cmake/HELP/HELPConfig.cmake
lib/cmake/HELP/HELPConfigVersion.cmake
lib/cmake/HELP/HELPHelpers.cmake
lib/cmake/HELP/HELPTargets-CONFIG.cmake
lib/cmake/HELP/HELPTargets.cmake
lib/libhelp.a
share/help/helplib.hlb
share/help/ru/helplib_ru.hlb"

#	The additional name and its snippet, when they were asked for
if [ "$HELP_ALIAS" = ON ]
then
	EXPECTED="$EXPECTED
bin/help
share/help/help.sh"
fi

EXPECTED=`echo "$EXPECTED" | sort`

if [ "$FOUND" = "$EXPECTED" ]
then	echo "%HELP-S-INSTALL, the installation holds the two utilities and their libraries"
else
	echo "%HELP-E-INSTALL, the installation holds something else than expected:"
	echo "--- found ---"
	echo "$FOUND"
	echo "--- expected ---"
	echo "$EXPECTED"

	FAILED=1
fi

#
#	The installed utilities have to run, and to find the library that
#	was installed beside them.
#
#	The relocated image is asked for its catalogue, which names every
#	library its search path holds: the copy installed beside the image
#	has to be among them.  The topic itself is then asked for with the
#	run pinned to that directory.  It cannot be asked for through the
#	search path alone: the path carries the directory the build was
#	configured for as well, ahead of the directory found beside the
#	image, and a product installed there may define the very topic
#	HELP - the BATCH product does - and answer in place of this one.
#
#	The cache of the catalogue would go to XDG_CACHE_HOME when the
#	shell exports one; the runs below are kept out of it.
#
unset XDG_CACHE_HOME

LIBDIR="$WRK/root/share/help"

if [ -x "$WRK/root/bin/$HELP_HELPNAME" ]
then
	if env "HOME=$WRK/home" "$WRK/root/bin/$HELP_HELPNAME" /CATALOG /NOCACHE /NOPAGE 2>&1 \
		| grep -q "^  $LIBDIR/helplib.hlb"
	then	echo "%HELP-S-INSTALL, the installed utility finds the installed library"
	else
		echo "%HELP-E-INSTALL, the installed utility cannot find its library"
		FAILED=1
	fi

	if env "HOME=$WRK/home" "HELP_LIBRARY=$LIBDIR" "$WRK/root/bin/$HELP_HELPNAME" help vhelptran /NOPAGE 2>&1 \
		| grep -q 'VHELPTRAN'
	then	echo "%HELP-S-INSTALL, the installed library answers for the utilities"
	else
		echo "%HELP-E-INSTALL, the installed library does not answer"
		FAILED=1
	fi
else
	echo "%HELP-E-INSTALL, no executable was installed"
	FAILED=1
fi

#
#	The additional name, when installed, must be a link to the image and
#	must answer
#
if [ "$HELP_ALIAS" = ON ]
then
	if [ -L "$WRK/root/bin/help" ] \
		&& env "HOME=$WRK/home" "HELP_LIBRARY=$LIBDIR" "$WRK/root/bin/help" help vhelptran /NOPAGE 2>&1 \
			| grep -q 'VHELPTRAN'
	then	echo "%HELP-S-INSTALL, the additional name is a link to the image and answers"
	else
		echo "%HELP-E-INSTALL, the additional name does not work"
		FAILED=1
	fi
fi

#
#	The package directory is written afresh.  CMake names one file of
#	the export set per configuration and includes every one it finds by
#	a wildcard, so an install of one configuration over another would
#	otherwise leave the entry of a configuration nobody rebuilt - and a
#	consumer would import it in silence.
#
STALE="$WRK/root/$HELP_LIBDIR/cmake/HELP/HELPTargets-nosuchconfig.cmake"

echo "a leftover of a configuration that is no longer built" > "$STALE"

if "$HELP_CMAKE" --install "$HELP_BUILD" --prefix "$WRK/root" > /dev/null 2>&1
then
	if [ -f "$STALE" ]
	then
		echo "%HELP-E-INSTALL, a stale file of the package directory survived the install"
		FAILED=1
	else	echo "%HELP-S-INSTALL, the package directory is written afresh"
	fi
else
	echo "%HELP-E-INSTALL, the second install failed"
	FAILED=1
fi

#
#	The exported package must not impose the warning policy of this
#	product on whoever links against it
#
TARGETS="$WRK/root/lib/cmake/HELP/HELPTargets.cmake"

if [ -f "$TARGETS" ]
then
	if grep -q 'COMPILE_OPTIONS' "$TARGETS"
	then
		echo "%HELP-E-INSTALL, the exported package imposes compile options on a consumer:"
		grep 'COMPILE_OPTIONS' "$TARGETS"

		FAILED=1
	else	echo "%HELP-S-INSTALL, the exported package imposes no compile options"
	fi
else
	echo "%HELP-E-INSTALL, no exported package was installed"
	FAILED=1
fi

exit $FAILED
