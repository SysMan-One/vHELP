#+++
#	FACILITY:	HELP - OpenVMS-style help library subsystem for Linux
#	MODULE:		test/CMakeLists.txt
#	ABSTRACT:	Functional tests of the HELP facility.
#	AUTHOR:		StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
#	CREATION DATE:	31-JUL-2026
#
#	MODIFICATION HISTORY:
#
#		23-OCT-2026	RRL	X01-38 : WIN32 - win_tran and win_show under the
#					emulator instead of the shell tests.
#
#		 8-SEP-2026	RRL	X01-36 : A check of what a package contains, see
#					test/package.sh.
#---

#
#	Windows: the shell scripts below do not run there.  Two checks instead,
#	run under CMAKE_CROSSCOMPILING_EMULATOR (wine64) from the build
#	directory - a path is given relative to it, an absolute one beginning
#	with '/' being a qualifier to these utilities: the librarian translates
#	the manual, and the browser reads a topic out of the library the host
#	translator made (a .hlb is the same file on both systems).
#
if ( WIN32 )
	file(RELATIVE_PATH help_mdrel ${PROJECT_BINARY_DIR} ${PROJECT_SOURCE_DIR}/doc/helplib.md)

	add_test(NAME win_tran
		COMMAND helptran ${help_mdrel} /OUTPUT=wintest.hlb /IDENT=WINTEST
		WORKING_DIRECTORY ${PROJECT_BINARY_DIR})
	set_tests_properties(win_tran PROPERTIES PASS_REGULAR_EXPRESSION "HELP-I-CREATED")

	add_test(NAME win_show
		COMMAND helpexe /LIBRARY=helplib.hlb HELP LIBRARY
		WORKING_DIRECTORY ${PROJECT_BINARY_DIR})
	set_tests_properties(win_show PROPERTIES PASS_REGULAR_EXPRESSION "LIBRARY" FAIL_REGULAR_EXPRESSION "HELP-[EWF]-")

	return()
endif()

add_test(NAME smoke
	COMMAND ${CMAKE_COMMAND} -E env
		HELP_BIN=$<TARGET_FILE_DIR:helpexe>
		HELP_HELP=$<TARGET_FILE:helpexe>
		HELP_HELPTRAN=$<TARGET_FILE:helptran>
		HELP_SRC=${CMAKE_CURRENT_SOURCE_DIR}/..
		HELP_BUILD_DIR=${PROJECT_BINARY_DIR}
		sh ${CMAKE_CURRENT_SOURCE_DIR}/smoke.sh)

#	What an installation contains - it once acquired the whole of the
#	subproject as a CMake package, see test/install.sh
add_test(NAME install
	COMMAND ${CMAKE_COMMAND} -E env
		HELP_BUILD=${PROJECT_BINARY_DIR}
		HELP_CMAKE=${CMAKE_COMMAND}
		HELP_HELPNAME=${HELP_HELPNAME}
		HELP_TRANNAME=${HELP_TRANNAME}
		HELP_LIBDIR=${CMAKE_INSTALL_LIBDIR}
		HELP_ALIAS=$<IF:$<BOOL:${HELP_ALIAS}>,ON,OFF>
		sh ${CMAKE_CURRENT_SOURCE_DIR}/install.sh)

#
#	What a package contains - the same things, made by CPACK.  The
#	packaging is defined for a top-level build only, so the check is too
#
if ( CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR )
	add_test(NAME package
		COMMAND ${CMAKE_COMMAND} -E env
			HELP_BUILD=${PROJECT_BINARY_DIR}
			HELP_CPACK=${CMAKE_CPACK_COMMAND}
			HELP_HELPNAME=${HELP_HELPNAME}
			HELP_TRANNAME=${HELP_TRANNAME}
			HELP_ALIAS=$<IF:$<BOOL:${HELP_ALIAS}>,ON,OFF>
			HELP_IDENT=${HELP_IDENT}
			HELP_PKGVERSION=${HELP_PKG_VERSION}
			HELP_PKGPREFIX=${CMAKE_INSTALL_PREFIX}
			HELP_SYSCACHE=${HELP_SYSCACHE}
			sh ${CMAKE_CURRENT_SOURCE_DIR}/package.sh)
endif()
