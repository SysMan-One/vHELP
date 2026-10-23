#+++
#	SPDX-License-Identifier: Apache-2.0
#
#	FACILITY:	HELP - OpenVMS-style help library subsystem for Linux
#
#	MODULE:		CMakeLists.txt
#
#	ABSTRACT:	Build of the HELP facility: the HELPTRAN librarian,
#			the HELP browser and the help library of the product
#			itself.
#
#	AUTHOR:		StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
#
#	CREATION DATE:	31-JUL-2026
#
#	MODIFICATION HISTORY:
#
#		23-OCT-2026	RRL	X01-38 : Windows x64.  The library and both utilities build
#					with MinGW-w64 (yaVNA's cmake/mingw-w64-x86_64.cmake), so
#					that yavna.exe answers HELP from its own yavna.hlb: a library
#					is read and written O_BINARY, the search path is separated by
#					';' there (HELP$K_PATHSEP), the console geometry comes from
#					GetConsoleScreenBufferInfo and the console is switched to
#					UTF-8, the image is located with GetModuleFileName.  No
#					catalogue cache on Windows, private or shared - see
#					hlbcat.c X01-01; HELP_BUILD_SYSCACHE is off for WIN32 and
#					the shell tests are replaced there by two run under
#					CMAKE_CROSSCOMPILING_EMULATOR.  A .hlb is the same file on
#					both systems.  On every system: the text of a code span is
#					taken as it stands, a backslash in it is no escape any more
#					(mdparse.c X01-01, as CommonMark has it).
#
#		16-OCT-2026	RRL	License: Apache-2.0 (LICENSE), told to CPack as well.  No new
#					version: the code and what is installed stay as in X01-37.
#
#		 6-OCT-2026	RRL	X01-37 : Cross builds.  The help library of the product and
#					every help_add_library() of a consumer are translated by a
#					translator of the HOST - HELP_HOST_TRANSLATOR, else vhelptran
#					found on the host, else the target image under
#					CMAKE_CROSSCOMPILING_EMULATOR - see help_translator() in
#					cmake/HELPHelpers.cmake.  The target image used to be run and
#					failed, the build with it.  Found building yaVNA with the
#					Android NDK.
#
#		 4-OCT-2026	CLD	X01-36ECO2 : Q at the paging prompt of HELP returns to the prompt
#					of the current level, its subtopics listed again, instead of
#					leaving the prompt swallowed and taking the next line for a
#					subtopic; end of file there ends the session.  RRL: "after q
#					show the current level".
#
#		11-SEP-2026	RRL	X01-36ECO1 : Two checks that failed for the host rather than
#					the code: the smoke check of the private cache lost it to an
#					XDG_CACHE_HOME exported by the shell, and the install check
#					asked the relocated image a topic through its whole search
#					path, where a product of the configured prefix (BATCH)
#					defines the topic HELP and answered instead.  The smoke
#					check of the manual examples ran them with the shell's
#					wildcards on, so "/*" became /sys, read as /SYSCACHE, and
#					the shared catalogue of the system was rebuilt out of a
#					test library at every run.  Tests only.
#
#		 8-SEP-2026	RRL	X01-36 : Packaging.  CPACK makes a tarball, a DEB and, where
#					RPMBUILD is found, an RPM out of the installation, plus the
#					source archive; the version of a package follows from the
#					ident.  The shared catalogue is left alone by an install into
#					a prefix other than the configured one - CPACK stages into
#					one - and is built by the package's postinst instead.  The
#					generated helpvers.h is looked for before the source tree's
#					include/, where a build made in the tree leaves a stale one.
#
#		14-AUG-2026	RRL	X01-25 : Idents brought to the Xnn-nn[ECOnn] form; the
#					product ident now names the delivery, so a library says which
#					one built it.
#
#		31-JUL-2026	RRL	X01-00 : Initial version.
#
#		 1-AUG-2026	RRL	X-04 : The warning policy is linked PRIVATE and is not
#					exported: a consumer of HELP::help was inheriting -Wall and,
#					in a strict build, -Werror, so its own untidy code would not
#					compile.
#
#		 1-AUG-2026	RRL	X-03 : The reading side is exported. HELPCOM became a
#					static library installed as libhelp.a and exported as HELP::help,
#					with StarLet asked for by FIND_DEPENDENCY rather than carried.
#
#		 1-AUG-2026	RRL	X-02 : The last references to the vendored subproject are gone -
#					StarLet is a found package and nothing in this tree describes
#					it as anything else.
#
#---

cmake_minimum_required(VERSION 3.16)

project(HELP VERSION 1.0.0 LANGUAGES C DESCRIPTION "OpenVMS-style HELP subsystem")

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

#
#	A build with no type named is a release build.  Without this the
#	compiler is given neither optimization nor NDEBUG, and the tracing
#	of the UTILITY$ROUTINES - which NDEBUG is what silences - reaches
#	the user on every run of the utilities.
#
if ( NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES )
	set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
	message(STATUS "No build type named, assuming ${CMAKE_BUILD_TYPE}")
endif()

#
#	Product identification and configuration
#
set(HELP_IDENT   "X01-38"  CACHE STRING  "Identification of the product")

#
#	The one key of level 1 the help library carries.  The catalogue of a
#	search path merges the level 1 topics of every library it finds, so
#	a library that exports its verbs puts them in a namespace shared
#	with every other library, where they collide.  One key per library,
#	naming the facility, keeps that namespace legible.
#
set(HELP_FACILITY "HELP"  CACHE STRING  "The level 1 key of the help library")
set(HELP_LIBDIR  "share/help"  CACHE STRING  "Directory the help libraries are installed into")
set(HELP_LOGNAME "HELP_LIBRARY"  CACHE STRING  "Environment variable that overrides the built-in library")
set(HELP_SYSCACHE "/var/cache/help"  CACHE STRING  "Directory of the catalogue shared by every user")

#
#	The names the images are installed under.  They are not "help" and
#	"helptran": HELP is a builtin of every Bourne-derived shell, so an
#	image of that name would never be reached from an interactive
#	session at all - the shell would answer first, about its own
#	builtins.  The V is for VMS, whose HELP this is after.
#
set(HELP_HELPNAME "vhelp"     CACHE STRING  "Name of the browser image")
set(HELP_TRANNAME "vhelptran" CACHE STRING  "Name of the librarian image")

#
#	An additional name "help", for those who would rather type it.
#	It is off by default and has to be: an image of that name is only
#	reachable from a shell that has no builtin of the same name - dash,
#	zsh, fish do not, bash and ksh do, and no ordering of PATH changes
#	that, a builtin being looked up before the path is searched.  The
#	shell snippet installed with it turns the builtin off for those who
#	want the trade.
#
option(HELP_ALIAS "Install an additional 'help' name and a shell snippet" OFF)
set(HELP_DEFLIB  "${CMAKE_INSTALL_PREFIX}/${HELP_LIBDIR}/helplib.hlb"
			CACHE STRING  "The library HELP reads when neither /LIBRARY nor HELP_LIBRARY is given")

option(HELP_STRICT  "Treat the compilation diagnostics as errors"  OFF)

#	BUILD_TESTING is the spelling CMake itself uses, so it is the one a
#	superproject reaches for; HELP_TESTS turns this product's tests off
#	on their own, leaving whatever else the tree tests alone.  The tests of
#	StarLet belong to StarLet and are run from its own build.
option(BUILD_TESTING  "Build the regression tests"                   ON)
option(HELP_TESTS   "Build the functional tests of this product"   ON)

#
#	Translation options, carried to every target of the product
#
#
#	The warning policy of this product, applied to its own sources and
#	to nothing else.
#
#	It is a plain list rather than an INTERFACE library on purpose.  A
#	library would have to be linked, and the PRIVATE dependencies of a
#	static library still reach its exported interface as $<LINK_ONLY>,
#	so the policy would follow HELP::help to whoever links against it -
#	warnings, and under HELP_STRICT a -Werror that refuses to compile
#	a consumer's own untidy code.  Warnings are this product's
#	business; a list applied target by target keeps them there.
#
set(help_warnings -Wall -Wextra -Wwrite-strings -Wno-unused-parameter)

if ( HELP_STRICT )
	list(APPEND help_warnings -Werror)
endif()

#
#	Identification and configuration are carried by a generated header
#	rather than by -D: the Makefile generator does not escape the '$' of
#	a macro name and MAKE expands it away before the compiler sees it.
#	The macros the header defines keep the '$' of the house style; the
#	variables here do not, so CONFIGURE_FILE can substitute them - it
#	recognises @NAME@ only for a name made of the ordinary identifier
#	characters - and so a -D on the command line needs no escaping.
#
#	__REV__ is not written, it is derived: Xaa-bb[ECOc] yields aa.bb.c,
#	the fields taken as they stand.  The one number that is written is
#	the identification above it, so the two cannot disagree.
if ( HELP_IDENT MATCHES "^X([0-9]+)-([0-9]+)(ECO([0-9]+))?$" )
	set(HELP_REV "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.")

	if ( CMAKE_MATCH_4 )
		string(APPEND HELP_REV "${CMAKE_MATCH_4}")
		set(HELP_PKG_PATCH "${CMAKE_MATCH_4}")
	else()
		string(APPEND HELP_REV "0")
		set(HELP_PKG_PATCH "0")
	endif()

	#
	#	The same three numbers for the package, without their leading
	#	zeros: DPKG and RPM compare "01" and "1" as equal, but the file
	#	they name would read HELP_01.35.0, which looks like something
	#	else.  The ident names the delivery, so the package carries the
	#	ident's numbers rather than PROJECT_VERSION, which speaks of the
	#	compatibility of the exported API and moves at its own pace.
	#
	#	Taken out of CMAKE_MATCH_n before the first REGEX REPLACE, which
	#	resets every one of them
	set(HELP_PKG_MAJOR "${CMAKE_MATCH_1}")
	set(HELP_PKG_MINOR "${CMAKE_MATCH_2}")

	string(REGEX REPLACE "^0+([0-9])" "\\1" HELP_PKG_MAJOR "${HELP_PKG_MAJOR}")
	string(REGEX REPLACE "^0+([0-9])" "\\1" HELP_PKG_MINOR "${HELP_PKG_MINOR}")
	string(REGEX REPLACE "^0+([0-9])" "\\1" HELP_PKG_PATCH "${HELP_PKG_PATCH}")
	set(HELP_PKG_VERSION "${HELP_PKG_MAJOR}.${HELP_PKG_MINOR}.${HELP_PKG_PATCH}")
else()
	message(FATAL_ERROR
		"HELP_IDENT is '${HELP_IDENT}', which is not of the form "
		"Xaa-bb[ECOc] - see include/helpvers.h.in")
endif()
configure_file(
	${CMAKE_CURRENT_SOURCE_DIR}/include/helpvers.h.in
	${CMAKE_CURRENT_BINARY_DIR}/include/helpvers.h  @ONLY)

#
#	UTILITY$ROUTINES - the StarLet package, found where it was
#	installed rather than carried inside this tree.  Build and install
#	it first:
#
#	    cmake -S <utility_routines> -B build -DCMAKE_BUILD_TYPE=Release
#	    cmake --build build
#	    cmake --install build --prefix /usr/local
#
#	and point this build at it with -DCMAKE_PREFIX_PATH=<prefix> when
#	the prefix is not one CMake searches by default.
#
find_package(StarLet 1.1 QUIET)

if ( NOT StarLet_FOUND )
	message(FATAL_ERROR
		"The StarLet package (UTILITY\$ROUTINES) was not found.\n"
		"Build and install it, then configure this product again:\n"
		"    cmake -S <utility_routines> -B b -DCMAKE_BUILD_TYPE=Release\n"
		"    cmake --build b && cmake --install b --prefix <prefix>\n"
		"    cmake -S . -B build -DCMAKE_PREFIX_PATH=<prefix>\n"
		"StarLet 1.1.0 or later is required.")
endif()

message(STATUS "Using StarLet ${StarLet_VERSION}")

#
#	The modules of the product
#
#
#	The destinations are wanted before the library is defined: its
#	INSTALL_INTERFACE include path is one of them, and a path computed
#	from a variable that is not yet set comes out as "/help".
#
include(GNUInstallDirs)

#
#	The modules are gathered into a library rather than into an object
#	collection, so that the reading side - HLB$OPEN and the routines
#	that navigate a topic and display it - can be linked by another
#	product instead of that product spawning the HELP image.
#
#	The writing side travels in the same library.  Splitting it would
#	buy a consumer a few kilobytes of archive it does not link anyway,
#	the linker taking only the objects it needs, and would cost a
#	division of HLB.C that the format does not call for.
#
#
#	Identification is not left to discipline: every module carries one
#	number of its own - __IDENT__ - and __REV__ must follow from it by
#	the rule Xaa-bb[ECOc] -> aa.bb.c.  Checking it here rather than only
#	in the test suite means a mismatch stops the build that introduced
#	it, not the next run of CTEST.
#
set(help_common  src/helputl.c src/hlb.c src/mdparse.c src/mdgen.c src/hlbcat.c)
set(help_modules ${help_common} src/help.c src/helptran.c)

foreach ( m ${help_modules} )
	file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/${m} help_id
			REGEX "^#define[ \t]+__IDENT__" LIMIT_COUNT 1)
	file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/${m} help_rev
			REGEX "^#define[ \t]+__REV__"   LIMIT_COUNT 1)

	string(REGEX REPLACE "^.*\"(.*)\".*$" "\\1" help_id "${help_id}")
	string(REGEX REPLACE "^.*\"(.*)\".*$" "\\1" help_rev "${help_rev}")

	if ( NOT help_id MATCHES "^X([0-9]+)-([0-9]+)(ECO([0-9]+))?$" )
		message(FATAL_ERROR "${m}: __IDENT__ is '${help_id}', not of the form Xaa-bb[ECOc]")
	endif()

	set(help_want "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.")

	if ( CMAKE_MATCH_4 )
		string(APPEND help_want "${CMAKE_MATCH_4}")
	else()
		string(APPEND help_want "0")
	endif()

	if ( NOT help_rev STREQUAL help_want )
		message(FATAL_ERROR
			"${m}: __REV__ is '${help_rev}' but __IDENT__ '${help_id}' yields '${help_want}'")
	endif()
endforeach()

add_library(helpcom STATIC ${help_common})

#
#	The target is named HELPCOM because HELP is a reserved target name
#	in CMake; the archive and the exported name are HELP all the same,
#	so a consumer writes HELP::help whether this product was installed
#	or is drawn in by ADD_SUBDIRECTORY.
#
set_target_properties(helpcom PROPERTIES OUTPUT_NAME help EXPORT_NAME help)

add_library(HELP::help ALIAS helpcom)

#	The binary directory comes first: a build once made in the source
#	tree leaves its helpvers.h beside helpvers.h.in, and a later build
#	elsewhere would take that stale one - its ident, its catalogue
#	directory - over the one it has just generated, in silence
target_include_directories(helpcom PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
	$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/help>)
#	StarLet is PUBLIC - HELPDEF.H includes its headers, so a consumer
#	needs it too; the warning policy is PRIVATE, being ours alone
target_link_libraries(helpcom PUBLIC StarLet::starlet)
target_compile_options(helpcom PRIVATE ${help_warnings})

add_executable(helptran src/helptran.c)
set_target_properties(helptran PROPERTIES OUTPUT_NAME ${HELP_TRANNAME})

#	The same name a consumer of the installed package sees, so that
#	help_add_library() works whichever way this product was reached
add_executable(HELP::helptran ALIAS helptran)

#
#	And the helper a consumer uses to translate its own documentation.
#	The installed package pulls it in from HELPConfig.cmake; a consumer
#	that reached this product by ADD_SUBDIRECTORY has no HELPConfig to
#	pull anything from, so it is included here as well.  Without this
#	the two ways of using the package differ, and the one that fails
#	does so with "Unknown CMake command", which names neither the cause
#	nor the cure.
#
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/HELPHelpers.cmake)
target_link_libraries(helptran PRIVATE helpcom StarLet::starlet)
target_compile_options(helptran PRIVATE ${help_warnings})

#	'help' is a reserved target name in CMake, hence the target is
#	named helpexe; the image itself is named by HELP_HELPNAME
add_executable(helpexe src/help.c)
set_target_properties(helpexe PROPERTIES OUTPUT_NAME ${HELP_HELPNAME})
target_link_libraries(helpexe PRIVATE helpcom StarLet::starlet)
target_compile_options(helpexe PRIVATE ${help_warnings})

#
#	The help library of the product itself, built out of its own
#	documentation by the utility that has just been compiled
#
#	natively the image just compiled; a cross build cannot run that
#	one and takes a translator of the host, see help_translator()
help_translator(help_tran help_trandep)

add_custom_command(
	OUTPUT  ${CMAKE_CURRENT_BINARY_DIR}/helplib.hlb
	COMMAND ${help_tran} ${CMAKE_CURRENT_SOURCE_DIR}/doc/helplib.md
		/OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/helplib.hlb
		/IDENT=${HELP_IDENT}
		/FACILITY=${HELP_FACILITY}
	DEPENDS ${help_trandep} ${CMAKE_CURRENT_SOURCE_DIR}/doc/helplib.md
	COMMENT "Translating doc/helplib.md into helplib.hlb")

add_custom_command(
	OUTPUT  ${CMAKE_CURRENT_BINARY_DIR}/helplib_ru.hlb
	COMMAND ${help_tran} ${CMAKE_CURRENT_SOURCE_DIR}/doc/helplib_ru.md
		/OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/helplib_ru.hlb
		/IDENT=${HELP_IDENT}RU
		/FACILITY=${HELP_FACILITY}
	DEPENDS ${help_trandep} ${CMAKE_CURRENT_SOURCE_DIR}/doc/helplib_ru.md
	COMMENT "Translating doc/helplib_ru.md into helplib_ru.hlb")

add_custom_target(helplib ALL DEPENDS
	${CMAKE_CURRENT_BINARY_DIR}/helplib.hlb
	${CMAKE_CURRENT_BINARY_DIR}/helplib_ru.hlb)

#
#	Installation
#
#	VHELPTRAN is exported besides being installed: a product that carries
#	its own documentation has to run it at build time, and an imported
#	executable is how it gets at one without knowing where the package
#	was installed.  See help_add_library() in HELPConfig.cmake.
install(TARGETS helpexe RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(TARGETS helptran
	EXPORT		HELPTargets
	RUNTIME		DESTINATION ${CMAKE_INSTALL_BINDIR})

#
#	The reading API, so that another product may answer for its own
#	help instead of spawning the HELP image.
#
#	StarLet is not carried along: it is a package in its own right and
#	the config generated below asks for it with FIND_DEPENDENCY.  One
#	copy of UTILITY$ROUTINES in a system is what keeps two revisions of
#	it out of one image - the hazard being real rather than theoretical,
#	its structures being laid out in its headers and its consumers
#	embedding them.
#
#	HELP_OPTIONS is deliberately not among the exported targets: it
#	carries the warning policy of this product, which a consumer must
#	not inherit
install(TARGETS helpcom
	EXPORT		HELPTargets
	ARCHIVE		DESTINATION ${CMAKE_INSTALL_LIBDIR}
	INCLUDES	DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/help)

install(FILES
	${CMAKE_CURRENT_SOURCE_DIR}/include/helpdef.h
	${CMAKE_CURRENT_BINARY_DIR}/include/helpvers.h
	DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/help)

include(CMakePackageConfigHelpers)

write_basic_package_version_file(
	"${CMAKE_CURRENT_BINARY_DIR}/HELPConfigVersion.cmake"
	VERSION		${PROJECT_VERSION}
	COMPATIBILITY	SameMajorVersion)

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/HELPConfig.cmake"
"#
#  HELP - an OpenVMS-style help subsystem.  Defines HELP::help, the library
#  that reads and navigates a help library.  HELPDEF.H draws in the headers
#  of UTILITY$ROUTINES, so that package is required as well.
#
include(CMakeFindDependencyMacro)
find_dependency(StarLet 1.0)
include(\"\${CMAKE_CURRENT_LIST_DIR}/HELPTargets.cmake\")
include(\"\${CMAKE_CURRENT_LIST_DIR}/HELPHelpers.cmake\")

")

#
#	The package directory is emptied before it is written.  CMake names
#	one file of the export set per build configuration -
#	HELPTargets-release.cmake, -debug.cmake - and HELPTargets.cmake
#	includes every one it finds by a wildcard.  Installing Debug over an
#	earlier Release therefore leaves both, and a consumer built in
#	Release imports the entry of a configuration that is no longer
#	there: it names an image nobody rebuilt, or one that was renamed
#	since, and does so in silence.  Nothing else in this product needs
#	the same treatment - every other installed file is written afresh
#	under a name that does not vary - so the sweep is confined to this
#	one directory rather than made a rule of the whole prefix.
#
install(CODE "
	set(help_pkgdir \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/cmake/HELP\")

	if ( EXISTS \"\${help_pkgdir}\" )
		file(REMOVE_RECURSE \"\${help_pkgdir}\")
		message(STATUS \"Removed the previous \${help_pkgdir}\")
	endif()
")

install(FILES
	"${CMAKE_CURRENT_SOURCE_DIR}/cmake/HELPHelpers.cmake"
	"${CMAKE_CURRENT_BINARY_DIR}/HELPConfig.cmake"
	"${CMAKE_CURRENT_BINARY_DIR}/HELPConfigVersion.cmake"
	DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HELP)

install(EXPORT HELPTargets
	FILE		HELPTargets.cmake
	NAMESPACE	HELP::
	DESTINATION	${CMAKE_INSTALL_LIBDIR}/cmake/HELP)
#
#	The English library goes on the default search path.  The Russian
#	one is the same manual in another language, so it defines the same
#	topics; on one path it would merely hide, or be hidden by, its
#	counterpart.  It is installed one level down instead, and is
#	selected by naming that directory:
#
#	    $ HELP_LIBRARY=${CMAKE_INSTALL_PREFIX}/${HELP_LIBDIR}/ru help
#
if ( HELP_ALIAS )
	#	A symbolic link rather than a second copy: the two are the
	#	same image, and one of them being stale would be a puzzle
	#	CMAKE_INSTALL_PREFIX is left for the install script to expand,
	#	so that "cmake --install --prefix ..." puts the link where it
	#	put the image rather than where the build was configured for
	install(CODE "
		file(CREATE_LINK \"${HELP_HELPNAME}\"
			\"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/help\"
			SYMBOLIC)
		message(STATUS \"Additional name: \${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/help\")
	")

	configure_file(
		${CMAKE_CURRENT_SOURCE_DIR}/share_help.sh.in
		${CMAKE_CURRENT_BINARY_DIR}/help.sh  @ONLY)

	install(FILES ${CMAKE_CURRENT_BINARY_DIR}/help.sh DESTINATION ${HELP_LIBDIR})
endif()

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/helplib.hlb    DESTINATION ${HELP_LIBDIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/helplib_ru.hlb DESTINATION ${HELP_LIBDIR}/ru)

#
#	The catalogue shared by every user of the system, harvested here so
#	that nobody's first run has to open the installed libraries itself.
#
#	It is not part of the installation proper: it is a derived thing,
#	rebuilt at will by HELP/SYSCACHE, and a staged or unprivileged
#	install must not fail for want of it.  Hence the best-effort call,
#	skipped outright when DESTDIR is in play - the paths recorded in it
#	would be those of the staging tree, which nothing will ever read.
#
#	Skipped as well when the prefix of the install is not the prefix
#	the build was configured for.  That is "cmake --install --prefix",
#	and it is CPACK, which stages into a directory of its own by
#	changing the prefix rather than by setting DESTDIR: the image and
#	the libraries named below are those of the configured prefix, so
#	the catalogue would describe an installation other than the one
#	being made - or, when there is none there yet, fail to be built at
#	all.  A package builds it from its postinst, on the system it lands
#	in; see packaging/postinst.in.
#
#	Windows has no shared catalogue (hlbcat.c X01-01), and a cross build
#	must not make directories of the build host's /var/cache.
if ( WIN32 )
	option(HELP_BUILD_SYSCACHE "Build the shared catalogue when installing" OFF)
else()
	option(HELP_BUILD_SYSCACHE "Build the shared catalogue when installing" ON)
endif()

if ( HELP_BUILD_SYSCACHE )
	install(CODE "
	if ( DEFINED ENV{DESTDIR} AND NOT \"\$ENV{DESTDIR}\" STREQUAL \"\" )
		message(STATUS \"DESTDIR is set, the shared catalogue is left to HELP/SYSCACHE\")
	elseif ( NOT \"\${CMAKE_INSTALL_PREFIX}\" STREQUAL \"${CMAKE_INSTALL_PREFIX}\" )
		message(STATUS \"Installing into \${CMAKE_INSTALL_PREFIX} rather than ${CMAKE_INSTALL_PREFIX}, the shared catalogue is left to HELP/SYSCACHE\")
	else()
		file(MAKE_DIRECTORY \"${HELP_SYSCACHE}\")

		execute_process(
			COMMAND \"${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/${HELP_HELPNAME}\"
				/SYSCACHE
				/LIBRARY=${CMAKE_INSTALL_PREFIX}/${HELP_LIBDIR}
			RESULT_VARIABLE help_rc
			ERROR_VARIABLE  help_err
			OUTPUT_QUIET)

		if ( NOT help_rc EQUAL 0 )
			message(STATUS \"The shared catalogue was not built: \${help_err}\")
			message(STATUS \"Run '${HELP_HELPNAME} /SYSCACHE' with the right to write ${HELP_SYSCACHE}\")
		else()
			message(STATUS \"Shared catalogue: ${HELP_SYSCACHE}/catalog.idx\")
		endif()
	endif()
	")
endif()

#
#	Functional tests
#
if ( BUILD_TESTING )
	enable_testing()

	if ( HELP_TESTS )
		add_subdirectory(test)
	endif()
endif()

#
#	Packaging
#
#	CPACK makes a tarball of the installation, a DEB, and an RPM where
#	RPMBUILD is found, besides an archive of the sources:
#
#	    cpack --config build/CPackConfig.cmake
#	    cpack --config build/CPackSourceConfig.cmake
#
#	A consumer that reached this product by ADD_SUBDIRECTORY wants the
#	targets, not the packaging: its own CPack configuration must not be
#	joined by ours.  PROJECT_IS_TOP_LEVEL would say it in one word, but
#	it arrived in CMake 3.21 and the minimum here is 3.16.
#
#	The version of a package follows from the ident - see HELP_PKG_VERSION
#	above - and the archives are named <product>-<ident>, as the rule of
#	the house has them.  A DEB and an RPM are named as DPKG and RPM name
#	theirs, so that a repository takes them as they come.
#
if ( CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR )
	set(HELP_CONTACT "StarLet Squad and Ruslan R. Laishev <polylab240@gmail.com>"
			CACHE STRING  "Maintainer named in the packages")

	set(CPACK_PACKAGE_NAME			"${PROJECT_NAME}")
	set(CPACK_PACKAGE_VENDOR		"StarLet Squad")
	set(CPACK_PACKAGE_CONTACT		"${HELP_CONTACT}")
	set(CPACK_PACKAGE_VERSION_MAJOR		"${HELP_PKG_MAJOR}")
	set(CPACK_PACKAGE_VERSION_MINOR		"${HELP_PKG_MINOR}")
	set(CPACK_PACKAGE_VERSION_PATCH		"${HELP_PKG_PATCH}")
	set(CPACK_PACKAGE_VERSION		"${HELP_PKG_VERSION}")
	set(CPACK_PACKAGE_DESCRIPTION_SUMMARY	"${PROJECT_DESCRIPTION}")
	set(CPACK_PACKAGE_DESCRIPTION
"An OpenVMS-style help subsystem for Linux: ${HELP_TRANNAME}, a librarian
that translates Markdown into a help library, and ${HELP_HELPNAME}, a browser
that navigates it.  The reading side is a library, libhelp, so that
another product may answer for its own help; building against it
needs the StarLet package (UTILITY\$ROUTINES) as well.")
	set(CPACK_PACKAGE_FILE_NAME		"${PROJECT_NAME}-${HELP_IDENT}")
	set(CPACK_PACKAGE_CHECKSUM		SHA256)
	set(CPACK_STRIP_FILES			ON)
	set(CPACK_VERBATIM_VARIABLES		ON)
	set(CPACK_RESOURCE_FILE_LICENSE		"${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")

	#
	#	The prefix a system package is written into: the one the build
	#	was configured for, since that is what the images carry.  A
	#	package meant for a distribution is configured with
	#	-DCMAKE_INSTALL_PREFIX=/usr and gets /usr here.  The tarball
	#	does not take it - see packaging/CPackProject.cmake.in.
	#
	set(HELP_PKGPREFIX "${CMAKE_INSTALL_PREFIX}")

	configure_file(
		${CMAKE_CURRENT_SOURCE_DIR}/packaging/CPackProject.cmake.in
		${CMAKE_CURRENT_BINARY_DIR}/packaging/CPackProject.cmake  @ONLY)
	configure_file(
		${CMAKE_CURRENT_SOURCE_DIR}/packaging/postinst.in
		${CMAKE_CURRENT_BINARY_DIR}/packaging/postinst  @ONLY)
	configure_file(
		${CMAKE_CURRENT_SOURCE_DIR}/packaging/postrm.in
		${CMAKE_CURRENT_BINARY_DIR}/packaging/postrm  @ONLY)

	set(CPACK_PROJECT_CONFIG_FILE "${CMAKE_CURRENT_BINARY_DIR}/packaging/CPackProject.cmake")

	#	The generators: the tarball always, the system packages where
	#	the tool that builds them is found
	set(help_generators TGZ)

	find_program(HELP_DPKG_DEB dpkg-deb)
	find_program(HELP_RPMBUILD rpmbuild)

	if ( HELP_DPKG_DEB )
		list(APPEND help_generators DEB)
	endif()

	if ( HELP_RPMBUILD )
		list(APPEND help_generators RPM)
	endif()

	set(CPACK_GENERATOR ${help_generators})

	#
	#	DEB.  The images are linked statically against libhelp and
	#	libstarlet, so the only dependency they have is the C library,
	#	which DPKG-SHLIBDEPS works out; the scripts build and remove
	#	the shared catalogue, which is not a file of the package.
	#
	set(CPACK_DEBIAN_FILE_NAME			DEB-DEFAULT)
	set(CPACK_DEBIAN_PACKAGE_SECTION		utils)
	set(CPACK_DEBIAN_PACKAGE_PRIORITY		optional)
	set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS		ON)
	set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION ON)
	set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
		"${CMAKE_CURRENT_BINARY_DIR}/packaging/postinst"
		"${CMAKE_CURRENT_BINARY_DIR}/packaging/postrm")

	#
	#	RPM.  The directories of the prefix belong to the filesystem
	#	package of the system, so the package must not claim them or it
	#	conflicts with it.  Untested for want of RPMBUILD on the build
	#	host of this revision.
	#
	set(CPACK_RPM_FILE_NAME				RPM-DEFAULT)
	set(CPACK_RPM_PACKAGE_GROUP			"Applications/System")
	set(CPACK_RPM_PACKAGE_LICENSE			"Apache-2.0")
	set(CPACK_RPM_POST_INSTALL_SCRIPT_FILE		"${CMAKE_CURRENT_BINARY_DIR}/packaging/postinst")
	set(CPACK_RPM_POSTUN_SCRIPT_FILE		"${CMAKE_CURRENT_BINARY_DIR}/packaging/postrm")
	set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
		"${HELP_PKGPREFIX}"
		"${HELP_PKGPREFIX}/${CMAKE_INSTALL_BINDIR}"
		"${HELP_PKGPREFIX}/${CMAKE_INSTALL_LIBDIR}"
		"${HELP_PKGPREFIX}/${CMAKE_INSTALL_INCLUDEDIR}"
		"${HELP_PKGPREFIX}/${CMAKE_INSTALL_DATADIR}")

	#
	#	The sources.  What a build leaves behind is left out, including
	#	the leavings of a build made in the source tree itself, and the
	#	packages of an earlier run.
	#
	set(CPACK_SOURCE_GENERATOR		TGZ)
	set(CPACK_SOURCE_PACKAGE_FILE_NAME	"${PROJECT_NAME}-${HELP_IDENT}-src")
	set(CPACK_SOURCE_IGNORE_FILES
		"/\\.git/"
		"/build/"
		"/_CPack_Packages/"
		"/CMakeFiles/"
		"/Testing/"
		"/CMakeCache\\.txt$"
		"/cmake_install\\.cmake$"
		"/CTestTestfile\\.cmake$"
		"/CPack.*Config\\.cmake$"
		"/HELPConfig.*\\.cmake$"
		"/install_manifest\\.txt$"
		"/Makefile$"
		"/include/helpvers\\.h$"
		"/packaging/postinst$"
		"/packaging/postrm$"
		"/packaging/CPackProject\\.cmake$"
		"/${HELP_HELPNAME}$"
		"/${HELP_TRANNAME}$"
		"\\.hlb$"
		"\\.a$"
		"\\.o$"
		"\\.tar\\.gz$"
		"\\.deb$"
		"\\.rpm$"
		"\\.sha256$"
		"\\.swp$"
		"~$")

	include(CPack)
endif()
