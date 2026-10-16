#!/bin/sh
#+++
#
#	FACILITY:	HELP - OpenVMS-style help library subsystem for Linux
#
#	MODULE:		test/package.sh
#
#	ABSTRACT:	Checks what a package of the product contains.
#
#	DESCRIPTION:	CPACK makes the packages out of the same install rules
#			test/install.sh checks, so a package is expected to hold
#			exactly what an installation holds: the two utilities,
#			their libraries, and the reading library with its header
#			and package configuration.  The tarball is checked
#			always; the DEB where DPKG-DEB is found, together with
#			the fields and the scripts of its control file.
#
#			One more thing is watched: the catalogue shared by every
#			user.  CPACK stages the installation into a directory
#			of its own, and the install rule that builds the
#			catalogue once ran there, describing the staging tree
#			to the whole system.  So its file must be the same
#			after the packaging as before.
#
#			HELP_BUILD names the build directory, HELP_CPACK the
#			cpack command to package with.
#
#	AUTHOR:		StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
#
#	CREATION DATE:	 8-SEP-2026
#
#	MODIFICATION HISTORY:
#
#		 8-SEP-2026	RRL	X01-36 : Initial version.
#
#---

set -u

HELP_BUILD=${HELP_BUILD:-.}
HELP_CPACK=${HELP_CPACK:-cpack}
HELP_HELPNAME=${HELP_HELPNAME:-vhelp}
HELP_TRANNAME=${HELP_TRANNAME:-vhelptran}
HELP_ALIAS=${HELP_ALIAS:-OFF}
HELP_IDENT=${HELP_IDENT:-}
HELP_PKGVERSION=${HELP_PKGVERSION:-}
HELP_PKGPREFIX=${HELP_PKGPREFIX:-/usr/local}
HELP_SYSCACHE=${HELP_SYSCACHE:-/var/cache/help}

WRK=`mktemp -d /tmp/helppack.XXXXXX`
FAILED=0

trap 'rm -rf "$WRK"' EXIT INT TERM

#
#	Everything a package is expected to hold - the list of
#	test/install.sh, the per-configuration file of the export set
#	normalised the same way
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

if [ "$HELP_ALIAS" = ON ]
then
	EXPECTED="$EXPECTED
bin/help
share/help/help.sh"
fi

EXPECTED=`echo "$EXPECTED" | sort`

#	A listing of files, whatever produced it, brought to the form above:
#	directories dropped, the leading path up to the prefix removed
normalise ()
{
	grep -v '/$' \
	| sed "s|$1||" \
	| sed 's|^lib/cmake/HELP/HELPTargets-.*\.cmake$|lib/cmake/HELP/HELPTargets-CONFIG.cmake|' \
	| grep . | sort
}

#	The shared catalogue, as it stands before anything is packaged
if [ -f "$HELP_SYSCACHE/catalog.idx" ]
then	CAT_BEFORE=`ls -l --time-style=full-iso "$HELP_SYSCACHE/catalog.idx"`
else	CAT_BEFORE=
fi

#
#	The tarball
#
if ! "$HELP_CPACK" --config "$HELP_BUILD/CPackConfig.cmake" -G TGZ -B "$WRK/tgz" > "$WRK/log" 2>&1
then
	echo "%HELP-E-PACKAGE, cpack failed to make the tarball:"
	cat "$WRK/log"

	exit 1
fi

TARBALL="$WRK/tgz/HELP-$HELP_IDENT.tar.gz"

if [ -f "$TARBALL" ]
then
	echo "%HELP-S-PACKAGE, the tarball is named after the product and the ident"

	FOUND=`tar tzf "$TARBALL" | normalise "^HELP-$HELP_IDENT/"`

	if [ "$FOUND" = "$EXPECTED" ]
	then	echo "%HELP-S-PACKAGE, the tarball holds what an installation holds"
	else
		echo "%HELP-E-PACKAGE, the tarball holds something else than expected:"
		echo "--- found ---"
		echo "$FOUND"
		echo "--- expected ---"
		echo "$EXPECTED"

		FAILED=1
	fi

	#	The image unpacked anywhere has to find the library beside it
	mkdir -p "$WRK/unpack" && tar xzf "$TARBALL" -C "$WRK/unpack"

	if env "HOME=$WRK/home" "$WRK/unpack/HELP-$HELP_IDENT/bin/$HELP_HELPNAME" \
		/LIBRARY="$WRK/unpack/HELP-$HELP_IDENT/share/help" help vhelptran /NOPAGE 2>&1 | grep -q 'VHELPTRAN'
	then	echo "%HELP-S-PACKAGE, the unpacked utility answers from the unpacked library"
	else
		echo "%HELP-E-PACKAGE, the unpacked utility does not answer"
		FAILED=1
	fi
else
	echo "%HELP-E-PACKAGE, no HELP-$HELP_IDENT.tar.gz was made; the directory holds:"
	ls "$WRK/tgz"

	FAILED=1
fi

#
#	The DEB, where the tool that builds it is found
#
if command -v dpkg-deb > /dev/null 2>&1
then
	if ! "$HELP_CPACK" --config "$HELP_BUILD/CPackConfig.cmake" -G DEB -B "$WRK/deb" > "$WRK/log" 2>&1
	then
		echo "%HELP-E-PACKAGE, cpack failed to make the DEB:"
		cat "$WRK/log"

		FAILED=1
	else
		DEB=`ls "$WRK/deb"/*.deb 2>/dev/null | head -n 1`

		if [ -n "$DEB" ]
		then
			FOUND=`dpkg-deb -c "$DEB" | awk '{ print $NF }' | normalise "^\\.$HELP_PKGPREFIX/"`

			if [ "$FOUND" = "$EXPECTED" ]
			then	echo "%HELP-S-PACKAGE, the DEB holds what an installation holds, under $HELP_PKGPREFIX"
			else
				echo "%HELP-E-PACKAGE, the DEB holds something else than expected:"
				echo "--- found ---"
				echo "$FOUND"
				echo "--- expected ---"
				echo "$EXPECTED"

				FAILED=1
			fi

			VERSION=`dpkg-deb -f "$DEB" Version`

			if [ "$VERSION" = "$HELP_PKGVERSION" ]
			then	echo "%HELP-S-PACKAGE, the DEB carries version $VERSION, as the ident $HELP_IDENT has it"
			else
				echo "%HELP-E-PACKAGE, the DEB carries version '$VERSION', the ident $HELP_IDENT calls for $HELP_PKGVERSION"
				FAILED=1
			fi

			if [ -n "`dpkg-deb -f "$DEB" Maintainer`" ]
			then	echo "%HELP-S-PACKAGE, the DEB names a maintainer"
			else
				echo "%HELP-E-PACKAGE, the DEB names no maintainer"
				FAILED=1
			fi

			#	The scripts that build and remove the shared catalogue,
			#	and they have to be executable - DPKG-DEB lists such
			#	with an asterisk
			for script in postinst postrm
			do
				if dpkg-deb -I "$DEB" | grep -q "\* *$script"
				then	echo "%HELP-S-PACKAGE, the DEB carries an executable $script"
				else
					echo "%HELP-E-PACKAGE, the DEB carries no executable $script"
					FAILED=1
				fi
			done
		else
			echo "%HELP-E-PACKAGE, no DEB was made; the directory holds:"
			ls "$WRK/deb"

			FAILED=1
		fi
	fi
else
	echo "%HELP-I-PACKAGE, dpkg-deb is not found, the DEB is not checked"
fi

#
#	The shared catalogue of the system is not the packaging's to touch
#
if [ -f "$HELP_SYSCACHE/catalog.idx" ]
then	CAT_AFTER=`ls -l --time-style=full-iso "$HELP_SYSCACHE/catalog.idx"`
else	CAT_AFTER=
fi

if [ "$CAT_BEFORE" = "$CAT_AFTER" ]
then	echo "%HELP-S-PACKAGE, the shared catalogue was left alone"
else
	echo "%HELP-E-PACKAGE, the packaging rewrote the shared catalogue $HELP_SYSCACHE/catalog.idx"
	FAILED=1
fi

exit $FAILED
