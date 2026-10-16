#define	__MODULE__	"HELPTRAN"
#define	__IDENT__	"X01-07"
#define	__REV__		"01.07.0"

/*
**++
**
**  FACILITY:	HELP - OpenVMS-style help library subsystem for Linux
**
**  MODULE:	helptran.c
**
**  ABSTRACT:	The HELPTRAN utility - the librarian of the facility.  It
**		translates one or several Markdown files into a single help
**		library, the way LIBRARY/HELP/CREATE builds an .HLB on
**		OpenVMS.
**
**  DESCRIPTION: Command syntax:
**
**		    <image> file.md [file.md ...] [/qualifiers]
**
**		    /OUTPUT=filespec	the library to be created; by
**					default the first input file with
**					the type replaced by .hlb
**		    /IDENT=string	identification stamped into the
**					library header
**		    /FACILITY=name	root the library at one key of that
**					name, with everything the sources
**					hold one level below it
**		    /[NO]SORT		sort the subtopics by the key, or
**					keep the order of the source
**		    /LIST		list the topic tree afterwards
**		    /HELP		display the syntax
**
**		and, in the reverse direction:
**
**		    <image> /EXTRACT file.hlb [/KEY=path] [/OUTPUT=file.md]
**
**		which recovers a Markdown source out of a library, the way
**		LIBRARY/EXTRACT recovers a module on OpenVMS.  The recovery
**		is lossy on its first pass - the descriptive tail of a
**		heading and the emphasis markers live in the source only,
**		not in the library - but from the second pass on the round
**		trip settles, and the tests hold it to that.
**
**		The qualifiers are parsed by the CLI service of the
**		UTILITY$ROUTINES; the input files are taken from the
**		arguments that are not qualifiers, so their number is not
**		limited to the eight positional parameters of the CLI.
**
**  AUTHOR:	StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:  31-JUL-2026
**
**  MODIFICATION HISTORY:
**
**	X01-07		21-AUG-2026	RRL
**		/FACILITY - a library rooted at one key of level 1, so
**		that the catalogue of a search path is a list of
**		facilities rather than of everybody's verbs at once.
**
**	X01-06		 1-AUG-2026	RRL
**		An input file or a qualifier that does not fit the command
**		arrays is still dropped, but is announced with
**		%HELP-W-TOOMANYIN or %HELP-W-TOOMANYQ - once - rather than
**		in silence.  Seventy source files used to produce a library
**		of the first sixty-four and no word about the rest.
**
**	X01-05		 1-AUG-2026	RRL
**		The logging of the UTILITY$ROUTINES is directed onto the
**		standard error before anything is emitted.  Its default
**		destination is the standard output, which carries the
**		Markdown of an /EXTRACT - a $TRACE, active in every Debug
**		build, landed in the middle of it.
**
**	X01-04		 1-AUG-2026	RRL
**		/HELP is answered by the utility, not by the CLI service:
**		CLI_ROUTINES X.00-09 intercepts it and prints a summary
**		of its own tables instead.
**
**	X01-03		 1-AUG-2026	RRL
**		The command is split before it is parsed - CLI_ROUTINES
**		X.00-07 refuses an unrecognized qualifier, and an input
**		file named by an absolute path begins with a slash just
**		as a qualifier does.  /SORT is negatable.
**
**	X01-02		31-JUL-2026	RRL
**		/EXTRACT and /KEY - recovery of a Markdown source out of
**		a library.
**
**	X01-01		31-JUL-2026	RRL
**		/LIST displays the creation time of the library, after
**		UTILITY$ROUTINES was taken to STARLET X.01-09.
**
**	X01-00		31-JUL-2026	RRL
**		Initial version.
**
**--
*/

#include	<stdlib.h>
#include	<string.h>
#include	<errno.h>
#include	<unistd.h>
#include	<fcntl.h>

#include	"helpdef.h"
#include	"cli_routines.h"

/*
**  The qualifier table of the utility.  An empty name closes the table.
*/
static	CLI_PQDESC	s_quals [] = {
	{ .name = {$ASCINI("OUTPUT")},	.type = CLI$K_QSTRING,	.pn = CLI$K_QUAL },
	{ .name = {$ASCINI("IDENT")},	.type = CLI$K_QSTRING,	.pn = CLI$K_QUAL },
	{ .name = {$ASCINI("SORT")},	.type = CLI$K_OPT,	.pn = CLI$K_QUAL,	.flag = CLI$M_NEGATABLE },
	{ .name = {$ASCINI("LIST")},	.type = CLI$K_OPT,	.pn = CLI$K_QUAL },
	{ .name = {$ASCINI("HELP")},	.type = CLI$K_OPT,	.pn = CLI$K_QUAL },
	{ .name = {$ASCINI("EXTRACT")},	.type = CLI$K_OPT,	.pn = CLI$K_QUAL },
	{ .name = {$ASCINI("KEY")},	.type = CLI$K_QSTRING,	.pn = CLI$K_QUAL },
	{ .name = {$ASCINI("FACILITY")},.type = CLI$K_QSTRING,	.pn = CLI$K_QUAL },
	{ .name = { .len = 0 } }
	};

static	CLI_VERB	s_verbs [] = {
	{ .name = {$ASCINI(HELP$K_TRANNAME)}, .quals = s_quals },
	{ .name = { .len = 0 } }
	};

/*
**  The syntax summary, emitted by /HELP and by a command that carries no
**  input file at all.
*/
static	const char	s_usage [] = {
	"Usage: " HELP$K_TRANNAME " file.md [file.md ...] [/qualifiers]\n"
	"\n"
	"    /OUTPUT=filespec   the help library to be created,\n"
	"                       by default the first input with the type .hlb\n"
	"    /IDENT=string      identification stamped into the library header\n"
	"    /FACILITY=name     root the library at one key of that name, with\n"
	"                       everything the sources hold one level below\n"
	"    /[NO]SORT          sort the subtopics, or keep the order of the source\n"
	"    /LIST              list the topic tree of the created library\n"
	"    /HELP              display this text\n"
	"\n"
	"Usage: " HELP$K_TRANNAME " /EXTRACT file.hlb [/KEY=\"topic subtopic\"] [/OUTPUT=file.md]\n"
	"\n"
	"    /EXTRACT           read a library and write its Markdown source\n"
	"    /KEY=path          extract that topic and its subtopics only\n"
	"    /OUTPUT=filespec   where the source goes, by default the standard output\n"
	};

/*
**  The synthetic verb put in front of the arguments for the CLI service.
*/
static	char	s_verb [] = { HELP$K_TRANNAME };


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Compose the default output specification: the first input file with
**	its type replaced by .hlb.  A name that carries no type simply gets
**	one appended.
**
**  FORMAL PARAMETERS:
**
**	a_input		The first input file specification
**	a_bufsz		Capacity of the buffer
**	a_buf		Receives the output specification, ASCIZ
**
**  IMPLICIT INPUTS/OUTPUTS:
**	None.
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- the specification has been composed;
**	STS$K_WARN	- it has been truncated to the buffer.
**--
*/
static	int	s_helptran$defout	(
	const	char *		a_input,
		unsigned	a_bufsz,
		char *		a_buf
			)
{
const char *	l_dot, *l_sep;
unsigned	l_len = (unsigned) strlen(a_input);

	/* The type is what follows the last dot of the last component of the path */
	l_sep = strrchr(a_input, '/');
	l_dot = strrchr(a_input, '.');

	if ( l_dot && (!l_sep || (l_dot > l_sep)) )
		l_len = (unsigned) (l_dot - a_input);

	if ( (l_len + 5) > a_bufsz )
		{
		l_len = a_bufsz - 5;

		memcpy(a_buf, a_input, l_len);
		memcpy(a_buf + l_len, ".hlb", 5);

		return	STS$K_WARN;
		}

	memcpy(a_buf, a_input, l_len);
	memcpy(a_buf + l_len, ".hlb", 5);

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	List the topic tree of a library, one topic per line, indented by
**	its nesting level.  The key index is stored in pre-order, so a
**	plain sequential scan yields the tree already in the right order.
**
**  FORMAL PARAMETERS:
**
**	a_out	The output context
**	a_lib	The library to be listed
**
**  IMPLICIT INPUTS/OUTPUTS:
**	Writes to the output context.
**
**  RETURN VALUE:
**	STS$K_SUCCESS always.
**--
*/
static	int	s_helptran$list	(
		HELP$OUT *	a_out,
		HLB$LIB *	a_lib
			)
{
HLB$KEY *	l_key;

	/* The creation time is a quadword, passed to !%D BY VALUE - STARLET X.01-09 */
	help$put(a_out, "\nTopics of !AZ (ident !AZ), created !%D, !UL topic!%S:\n\n", 4,
		(fao_prm_t) a_lib->fspec,
		(fao_prm_t) (a_lib->hdr->ident[0] ? a_lib->hdr->ident : "(none)"),
		(fao_prm_t) a_lib->hdr->credat,
		(fao_prm_t) a_lib->keycnt);

	for ( unsigned i = 0; i < a_lib->keycnt; i++ )
		{
		if ( !(1 & hlb$getkey(a_lib, (int) i, &l_key)) )
			break;

		help$put(a_out, "!4UL  ", 1, (fao_prm_t) i);
		help$put_indent(a_out, (unsigned) (l_key->level - 1) * 2);
		help$put(a_out, "!UL !AD  (!UL byte!%S)\n", 4,
			(fao_prm_t) l_key->level,
			(fao_prm_t) l_key->keylen, (fao_prm_t) l_key->key,
			(fao_prm_t) l_key->txtlen);
		}

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Resolve a topic path - the keys from level 1 down, separated by
**	blanks - against a library.
**
**  FORMAL PARAMETERS:
**
**	a_lib	The library
**	a_path	The path, ASCIZ; an empty one selects the whole library
**	a_index	Receives the index of the topic, -1 for the whole library
**
**  IMPLICIT INPUTS/OUTPUTS:
**	None.
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- the topic has been found;
**	HELP$_NOTFOUND	- it has not, already signalled.
**--
*/
static	int	s_helptran$resolve	(
		HLB$LIB *	a_lib,
	const	char *		a_path,
		int *		a_index
			)
{
char		l_work [HELP$K_SZ_PATH];
char *		l_words [HLB$K_MAXLVL], *l_ptr;
int		l_parent = -1, l_index = -1, l_wordcnt = 0, l_used = 0, l_pos = 0;

	*a_index = -1;

	help$strcpy((unsigned) sizeof(l_work), l_work, a_path);

	/* Cut the path into words, in place */
	for ( l_ptr = l_work; *l_ptr && (l_wordcnt < (int) $ARRSZ(l_words)); )
		{
		while ( (*l_ptr == ' ') || (*l_ptr == '\t') )
			l_ptr++;

		if ( !*l_ptr )
			break;

		l_words [l_wordcnt++] = l_ptr;

		while ( *l_ptr && (*l_ptr != ' ') && (*l_ptr != '\t') )
			l_ptr++;

		if ( *l_ptr )
			*l_ptr++ = '\0';
		}

	/* A key may carry blanks, so the words are consumed a run at a time */
	while ( l_pos < l_wordcnt )
		{
		if ( $STS_ERROR(hlb$lookup_run(a_lib, l_parent, l_wordcnt - l_pos, &l_words[l_pos], &l_used, &l_index)) )
			return	help$putmsg(HELP$_NOTFOUND, 1, (fao_prm_t) a_path);

		l_parent = l_index;
		l_pos	+= l_used;
		}

	*a_index = l_parent;

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Perform an extraction: open the library, resolve the topic asked
**	for by /KEY and write the Markdown source of it.
**
**  FORMAL PARAMETERS:
**
**	a_libspec	File specification of the library
**	a_key		Value of /KEY, an empty string - the whole library
**	a_outspec	File specification of the source, empty - the standard output
**
**  IMPLICIT INPUTS/OUTPUTS:
**	Creates the output file when one is named.
**
**  RETURN VALUE:
**	STS$K_SUCCESS or a condition of HLB$OPEN / HELP$_OPENOUT /
**	HELP$_NOTFOUND, already signalled.
**--
*/
static	int	s_helptran$extract	(
	const	char *		a_libspec,
	const	char *		a_key,
	const	char *		a_outspec
			)
{
HLB$LIB *	l_lib = NULL;
HELP$OUT	l_out;
unsigned	l_cnt = 0;
int		l_fd = STDOUT_FILENO, l_index = -1, l_status;

	if ( !(1 & (l_status = hlb$open(a_libspec, &l_lib))) )
		return	l_status;

	if ( !(1 & (l_status = s_helptran$resolve(l_lib, a_key, &l_index))) )
		{
		hlb$close(l_lib);

		return	l_status;
		}

	if ( *a_outspec && (0 > (l_fd = open(a_outspec, O_WRONLY | O_CREAT | O_TRUNC, 0644))) )
		{
		hlb$close(l_lib);

		return	help$putmsg(HELP$_OPENOUT, 3, (fao_prm_t) a_outspec, (fao_prm_t) errno,
					(fao_prm_t) strerror(errno));
		}

	/* Paging an extraction would corrupt it - the source is data, not a display */
	help$out_init(&l_out, l_fd, STDIN_FILENO, 0, 0);

	l_status = md$generate(l_lib, l_index, &l_out, &l_cnt);

	help$flush(&l_out);

	if ( l_fd != STDOUT_FILENO )
		{
		close(l_fd);

		help$putmsg(HELP$_EXTRACTED, 3, (fao_prm_t) a_outspec, (fao_prm_t) a_libspec, (fao_prm_t) l_cnt);
		}

	hlb$close(l_lib);

	return	l_status;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	The entry point of the HELPTRAN utility: parse the command, build
**	the library from every input file in turn and write it out.
**
**  FORMAL PARAMETERS:
**
**	a_argc, a_argv	The command line
**
**  IMPLICIT INPUTS/OUTPUTS:
**	Creates the output file.
**
**  RETURN VALUE:
**	EXIT_SUCCESS or EXIT_FAILURE.
**--
*/
int	main	(
		int		a_argc,
		char **		a_argv
		)
{
CLI_CTX *	l_clictx = NULL;
HLB$BLD *	l_bld = NULL;
HLB$LIB *	l_lib = NULL;
HELP$OUT	l_out;
ASC		l_val = {0};
char *		l_argv [1 + HELP$K_MAXQUAL], *l_files [HELP$K_MAXIN];
char		l_outspec [HELP$K_SZ_PATH] = {0}, l_ident [HLB$K_SZ_IDENT] = {0}, l_creator [HLB$K_SZ_CREATOR];
char		l_key [HELP$K_SZ_PATH] = {0}, l_tmp [ASC$K_SZ + 1] = {0};
char		l_fac [HLB$K_SZ_KEY] = {0};
unsigned short	l_crelen = 0;
fao_desc_t	l_credsc = $FAO_DESC(l_creator);
unsigned	l_errs = 0, l_toterrs = 0, l_state = 0;
int		l_argc = 1, l_status, l_sort = 1, l_list = 0, l_extract = 0, l_nfiles = 0, l_first = -1;
int		l_usage = 0, l_fovf = 0, l_qovf = 0;

	/* The standard output carries the Markdown of an /EXTRACT - keep the logging off it */
	help$log_init();

	help$out_init(&l_out, STDOUT_FILENO, STDIN_FILENO, 0, 0);

	/*
	**  The CLI service matches the leading token against the verb table,
	**  so a synthetic verb is put in front of the real arguments.
	*/
	/*
	**  The command is split before it is parsed.  The CLI service is shown
	**  the verb and the qualifiers of this utility, and nothing else: an
	**  input file is routinely named by an absolute path, which begins
	**  with a slash just as a qualifier does, and since CLI_ROUTINES
	**  X.00-07 an unrecognized qualifier is refused outright.
	*/
	l_argv [0] = s_verb;

	for ( int i = 1; i < a_argc; i++ )
		{
		/*
		**  A request for the syntax is answered here, not by the CLI
		**  service: the service would print a summary of its own
		**  tables, which describes the parser rather than the utility.
		**  A bare HELP is not caught, being the name of an input file
		**  as far as this utility knows.
		*/
		if ( help$ishelp(a_argv[i]) )
			{
			l_usage = 1;
			continue;
			}

		if ( help$isqual(s_quals, a_argv[i]) )
			{
			/*
			**  An argument that does not fit is dropped, but never in
			**  silence: a command whose tail went missing has to say so,
			**  once, rather than quietly do less than it was asked.
			*/
			if ( l_argc < (int) $ARRSZ(l_argv) )
				l_argv [l_argc++] = a_argv [i];
			else if ( !l_qovf++ )
				help$putmsg(HELP$_TOOMANYQ, 1, (fao_prm_t) HELP$K_MAXQUAL);

			continue;
			}

		if ( l_nfiles < (int) $ARRSZ(l_files) )
			l_files [l_nfiles++] = a_argv [i];
		else if ( !l_fovf++ )
			help$putmsg(HELP$_TOOMANYIN, 1, (fao_prm_t) HELP$K_MAXIN);

		if ( l_first < 0 )
			l_first = l_nfiles - 1;
		}

	if ( !(1 & (l_status = __cli$parse(s_verbs, CLI$M_OPSIGNAL, l_argc, l_argv, (void **) &l_clictx))) )
		return	EXIT_FAILURE;

	/* /HELP, or a command without a single input file, is answered with the syntax */
	if ( l_usage || !l_nfiles )
		{
		help$put_raw(&l_out, (unsigned) sizeof(s_usage) - 1, s_usage);
		__cli$cleanup(l_clictx);

		return	l_nfiles ? EXIT_SUCCESS : EXIT_FAILURE;
		}

	/* A negatable qualifier reports which form it was given in */
	if ( 1 & __cli$present(l_clictx, &s_quals[2], &l_state) )
		l_sort = !(l_state & CLI$M_NEGATED);

	if ( 1 & __cli$present(l_clictx, &s_quals[3], NULL) )
		l_list = 1;

	/* ASC2STR knows nothing about the capacity of the target - stage through a full-size buffer */
	if ( (1 & __cli$present(l_clictx, &s_quals[1], NULL))
		&& (1 & __cli$get_value(l_clictx, &s_quals[1], &l_val)) )
		{
		__util$asc2str(&l_val, l_tmp);
		help$strcpy((unsigned) sizeof(l_ident), l_ident, l_tmp);
		}

	if ( 1 & __cli$present(l_clictx, &s_quals[5], NULL) )
		l_extract = 1;

	if ( (1 & __cli$present(l_clictx, &s_quals[6], NULL))
		&& (1 & __cli$get_value(l_clictx, &s_quals[6], &l_val)) )
		{
		__util$asc2str(&l_val, l_tmp);
		help$strcpy((unsigned) sizeof(l_key), l_key, l_tmp);
		}

	if ( (1 & __cli$present(l_clictx, &s_quals[7], NULL))
		&& (1 & __cli$get_value(l_clictx, &s_quals[7], &l_val)) )
		{
		__util$asc2str(&l_val, l_tmp);
		help$strcpy((unsigned) sizeof(l_fac), l_fac, l_tmp);
		}

	if ( (1 & __cli$present(l_clictx, &s_quals[0], NULL))
		&& (1 & __cli$get_value(l_clictx, &s_quals[0], &l_val)) )
		{
		__util$asc2str(&l_val, l_tmp);
		help$strcpy((unsigned) sizeof(l_outspec), l_outspec, l_tmp);
		}
	else if ( !l_extract )
		s_helptran$defout(l_files[l_first], (unsigned) sizeof(l_outspec), l_outspec);

	/*
	**  An extraction reads a library and writes a source; the qualifiers
	**  that describe how a library is to be built have no meaning there.
	*/
	if ( l_extract )
		{
		if ( !l_sort || l_list || l_ident[0] || l_fac[0] )
			{
			help$putmsg(HELP$_CONFQUAL, 2, (fao_prm_t) "EXTRACT",
					(fao_prm_t) (l_list ? "LIST" : (l_fac[0] ? "FACILITY"
						: (l_sort ? "IDENT" : "NOSORT"))));
			__cli$cleanup(l_clictx);

			return	EXIT_FAILURE;
			}

		l_status = s_helptran$extract(l_files[l_first], l_key, l_outspec);

		__cli$cleanup(l_clictx);

		return	(1 & l_status) ? EXIT_SUCCESS : EXIT_FAILURE;
		}

	/* The creator stamp records who produced the library and out of what */
	__util$fao("!AZ !AZ from !AZ", &l_crelen, &l_credsc, (fao_prm_t) HELP$K_TRANNAME,
			(fao_prm_t) __IDENT__, (fao_prm_t) l_files[l_first]);
	l_creator [$MIN(l_crelen, (unsigned short) sizeof(l_creator) - 1)] = '\0';

	if ( !(1 & (l_status = hlb$bld_create(&l_bld, l_ident, l_creator, l_sort))) )
		{
		__cli$cleanup(l_clictx);

		return	EXIT_FAILURE;
		}

	/*
	**  The facility root, when one was asked for, before a single topic
	**  of a source is read - see HLB$BLD_FACILITY.
	*/
	if ( l_fac[0] && !(1 & (l_status = hlb$bld_facility(l_bld, (unsigned) strlen(l_fac), l_fac))) )
		{
		hlb$bld_delete(l_bld);
		__cli$cleanup(l_clictx);

		return	EXIT_FAILURE;
		}

	/* Every input file contributes its topics to the one library */
	for ( int i = 0; i < l_nfiles; i++ )
		{
		if ( !(1 & (l_status = md$translate(l_bld, l_files[i], &l_errs))) )
			{
			hlb$bld_delete(l_bld);
			__cli$cleanup(l_clictx);

			return	EXIT_FAILURE;
			}

		l_toterrs += l_errs;
		}

	l_status = hlb$bld_write(l_bld, l_outspec);

	if ( 1 & l_status )
		help$putmsg(HELP$_CREATED, 3, (fao_prm_t) l_outspec, (fao_prm_t) l_bld->keycnt,
				(fao_prm_t) l_bld->txtsz);

	hlb$bld_delete(l_bld);

	/* /LIST reads the library back, which also proves it can be opened */
	if ( (1 & l_status) && l_list )
		{
		if ( 1 & hlb$open(l_outspec, &l_lib) )
			{
			s_helptran$list(&l_out, l_lib);
			hlb$close(l_lib);
			}
		}

	__cli$cleanup(l_clictx);

	/*
	**  A count the user is meant to read goes through the message
	**  catalogue, not through $LOG: a $LOG record lands on the standard
	**  output, which carries the Markdown of an /EXTRACT here.
	*/
	if ( l_toterrs )
		help$putmsg(HELP$_DIAGS, 1, (fao_prm_t) l_toterrs);

	return	(1 & l_status) ? EXIT_SUCCESS : EXIT_FAILURE;
}
