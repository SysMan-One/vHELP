#
#  FACILITY:  HELP - an OpenVMS-style help subsystem
#
#  ABSTRACT:  What a consumer of the package needs to carry its own
#	    documentation.
#
#  DESCRIPTION: Every product of the line ships a Markdown description of
#	    itself and has to translate it into a help library at build time.
#	    The rule, the target and the install are the same dozen lines in
#	    each of them, and have to do the same thing; three products
#	    stating them separately is three chances to state them
#	    differently, so they are stated here once.
#
#  MODIFICATION HISTORY:
#
#	 3-AUG-2026	DN	Initial revision.
#
#	 6-OCT-2026	RRL	X01-37 : help_translator() - a cross build runs a HOST
#				translator (HELP_HOST_TRANSLATOR, else vhelptran on
#				the host, else the target one under
#				CMAKE_CROSSCOMPILING_EMULATOR) instead of the target
#				image it cannot execute.  Found building yaVNA with
#				the Android NDK.
#

#
#  help_add_library(NAME target SOURCE file.md [OUTPUT file.hlb] [IDENT id]
#                   [DESTINATION dir] [ALL])
#
#  Translate the documentation of a product into a help library and install it
#  where the browser looks for one.
#
#  NAME		the target that builds the library
#  SOURCE	the Markdown source
#  OUTPUT	the library; by default the source name with .hlb, in the
#		current binary directory
#  IDENT	the identification recorded in the library; PROJECT_VERSION
#		by default
#  DESTINATION	where it is installed; the help directory by default
#  ALL		build it as part of the default target
#
#  A help library is architecture-neutral, so nothing about it stands in the
#  way of cross compiling.  The translator does: VHELPTRAN comes from the
#  HELP package that was found, so it is of that package's architecture,
#  and a package built FOR the target will not run ON the host.  A cross
#  build therefore translates with a HOST translator - see help_translator()
#  below; nothing has to be said when the host has HELP installed.
#
#      help_add_library(NAME batch_help
#                       SOURCE doc/submit.md doc/queue.md
#                       OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/batch.hlb
#                       ALL)
#
#  Several sources make one library, which is how a product with a manual
#  per command keeps them under a single facility.  FACILITY defaults to
#  the name of the project - see below - and NOFACILITY declines the root
#  for a library that is meant to carry several keys at level 1.
#
#
#  help_translator(cmd_var dep_var)
#
#  The command that translates, and what a rule that runs it depends on.
#  A native build runs HELP::helptran.  A cross build cannot - that image is
#  of the target - and takes, in this order:
#
#    HELP_HOST_TRANSLATOR	a translator of the host, named outright;
#    vhelptran on the host	searched for in PATH and /usr/local/bin, /usr/bin,
#				/bin - never in the target's prefixes;
#    the emulator		CMAKE_CROSSCOMPILING_EMULATOR running the
#				target image (qemu-user and the like).
#
#  None of them is an error, said at configure time: a cross build that
#  runs the target translator fails with "Exec format error" or a shell's
#  syntax error, which names neither the cause nor the cure.
#
#  The host translator writes the same library the target one would: the
#  format is the same on every architecture.  It should be of the same
#  delivery as the package the target links, or later.
#
function(help_translator cmd_var dep_var)
	if(NOT CMAKE_CROSSCOMPILING)
		set(${cmd_var} HELP::helptran PARENT_SCOPE)
		set(${dep_var} HELP::helptran PARENT_SCOPE)
		return()
	endif()

	set(HELP_HOST_TRANSLATOR "" CACHE FILEPATH "VHELPTRAN of the build host, for a cross build")

	if(HELP_HOST_TRANSLATOR)
		if(NOT EXISTS "${HELP_HOST_TRANSLATOR}" OR IS_DIRECTORY "${HELP_HOST_TRANSLATOR}")
			message(FATAL_ERROR "help_translator: HELP_HOST_TRANSLATOR '${HELP_HOST_TRANSLATOR}' is not a file")
		endif()

		set(${cmd_var} "${HELP_HOST_TRANSLATOR}" PARENT_SCOPE)
		set(${dep_var} "${HELP_HOST_TRANSLATOR}" PARENT_SCOPE)
		return()
	endif()

	#  The name the host's own installation was given; vhelptran unless
	#  this is the build of HELP itself with HELP_TRANNAME changed
	set(hlt_name vhelptran)

	if(HELP_TRANNAME)
		set(hlt_name ${HELP_TRANNAME} vhelptran)
	endif()

	#  The host's PATH and its standard places only: CMAKE_PREFIX_PATH and
	#  CMAKE_INSTALL_PREFIX of a cross build name the TARGET's tree, whose
	#  bin holds the target image - NO_CMAKE_FIND_ROOT_PATH alone keeps them
	find_program(HELP_HOST_TRANSLATOR_FOUND NAMES ${hlt_name}
		PATHS ENV PATH /usr/local/bin /usr/bin /bin
		NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)

	if(HELP_HOST_TRANSLATOR_FOUND)
		message(STATUS "HELP: cross build, translating with ${HELP_HOST_TRANSLATOR_FOUND} of the host")
		set(${cmd_var} "${HELP_HOST_TRANSLATOR_FOUND}" PARENT_SCOPE)
		set(${dep_var} "${HELP_HOST_TRANSLATOR_FOUND}" PARENT_SCOPE)
		return()
	endif()

	if(CMAKE_CROSSCOMPILING_EMULATOR)
		set(${cmd_var} ${CMAKE_CROSSCOMPILING_EMULATOR} $<TARGET_FILE:HELP::helptran> PARENT_SCOPE)
		set(${dep_var} HELP::helptran PARENT_SCOPE)
		return()
	endif()

	message(FATAL_ERROR
		"help_translator: a cross build cannot run the translator of the target.  "
		"Install HELP on the build host, or name its VHELPTRAN with -DHELP_HOST_TRANSLATOR=<path>, "
		"or give CMAKE_CROSSCOMPILING_EMULATOR")
endfunction()

function(help_add_library)
	cmake_parse_arguments(HLB "ALL;NOFACILITY" "NAME;OUTPUT;IDENT;DESTINATION;FACILITY" "SOURCE" ${ARGN})

	if(NOT HLB_NAME OR NOT HLB_SOURCE)
		message(FATAL_ERROR "help_add_library: NAME and SOURCE are required")
	endif()

	if(NOT TARGET HELP::helptran AND NOT CMAKE_CROSSCOMPILING)
		message(FATAL_ERROR
			"help_add_library: the HELP package defines no VHELPTRAN to translate with")
	endif()

	help_translator(hlb_tran hlb_trandep)

	#
	#  One source names the library after itself.  Several cannot - the
	#  first of them is no more the name of the whole than the last - so
	#  the caller is asked rather than guessed at.
	#
	if(NOT HLB_OUTPUT)
		list(LENGTH HLB_SOURCE hlb_nsrc)

		if(hlb_nsrc GREATER 1)
			message(FATAL_ERROR
				"help_add_library: ${hlb_nsrc} sources were given, so OUTPUT has to name "
				"the library they are translated into")
		endif()

		get_filename_component(HLB_OUTPUT "${HLB_SOURCE}" NAME_WE)
		set(HLB_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${HLB_OUTPUT}.hlb")
	endif()

	if(NOT HLB_IDENT)
		set(HLB_IDENT "${PROJECT_VERSION}")
	endif()

	#
	#  The catalogue of a search path merges the level 1 topics of every
	#  library it finds, so a library should carry exactly one - the name
	#  of its facility - or its verbs land in a namespace shared with
	#  every other product and collide there.  Hence the default: the
	#  name of the project, as IDENT defaults to its version.
	#
	#  A caller who means to put several keys at level 1 says NOFACILITY.
	#  An empty FACILITY will not do: CMAKE_PARSE_ARGUMENTS leaves the
	#  variable undefined for a keyword given an empty value exactly as
	#  for a keyword not given at all, so the two cannot be told apart.
	#
	if(HLB_NOFACILITY)
		if(HLB_FACILITY)
			message(FATAL_ERROR
				"help_add_library: FACILITY and NOFACILITY are both given")
		endif()

		set(hlb_facqual "")
	else()
		if(NOT HLB_FACILITY)
			set(HLB_FACILITY "${PROJECT_NAME}")
		endif()

		set(hlb_facqual "/FACILITY=${HLB_FACILITY}")
	endif()

	if(NOT HLB_DESTINATION)
		set(HLB_DESTINATION "share/help")

		if(CMAKE_INSTALL_DATADIR)
			set(HLB_DESTINATION "${CMAKE_INSTALL_DATADIR}/help")
		endif()
	endif()

	set(hlb_sources "")

	foreach(hlb_src ${HLB_SOURCE})
		get_filename_component(hlb_src "${hlb_src}" ABSOLUTE)
		list(APPEND hlb_sources "${hlb_src}")
	endforeach()

	#
	#  The value of a qualifier is given unquoted, and could as well be
	#  quoted: VERBATIM escapes an argument rather than passing quotes
	#  through, and a space in a path does not split it either way - a
	#  CMake command list is separated by semicolons, not by blanks.  The
	#  one value that would differ is a path holding a semicolon, which
	#  CMake cannot carry through most of its own interfaces regardless.
	#
	add_custom_command(OUTPUT "${HLB_OUTPUT}"
			   COMMAND ${hlb_tran} ${hlb_sources}
				   /OUTPUT=${HLB_OUTPUT} /IDENT=${HLB_IDENT} ${hlb_facqual}
			   DEPENDS ${hlb_sources} ${hlb_trandep}
			   COMMENT "Translating ${HLB_OUTPUT}"
			   VERBATIM)

	if(HLB_ALL)
		add_custom_target(${HLB_NAME} ALL DEPENDS "${HLB_OUTPUT}")
	else()
		add_custom_target(${HLB_NAME} DEPENDS "${HLB_OUTPUT}")
	endif()

	install(FILES "${HLB_OUTPUT}" DESTINATION "${HLB_DESTINATION}")
endfunction()
