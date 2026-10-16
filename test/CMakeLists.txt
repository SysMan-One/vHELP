#+++
#	FACILITY:	HELP - OpenVMS-style help library subsystem for Linux
#	MODULE:		test/CMakeLists.txt
#	ABSTRACT:	Functional tests of the HELP facility.
#	AUTHOR:		StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
#	CREATION DATE:	31-JUL-2026
#
#	MODIFICATION HISTORY:
#
#		 8-SEP-2026	RRL	X01-36 : A check of what a package contains, see
#					test/package.sh.
#---

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
