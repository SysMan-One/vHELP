#define	__MODULE__	"HELPUTL"
#define	__IDENT__	"X01-04"
#define	__REV__		"01.04.0"

/*
**++
**
**  FACILITY:	HELP - OpenVMS-style help library subsystem for Linux
**
**  MODULE:	helputl.c
**
**  ABSTRACT:	Service routines shared by the HELP and HELPTRAN utilities:
**		the message catalogue of the facility and the signalling
**		routine, the paged output engine, upper-casing, terminal
**		geometry and whole-file reading.
**
**  DESCRIPTION: Every character the utilities emit leaves the process
**		through HELP$PUT_RAW, and every formatted record is built
**		by the FAO engine of the STARLET module.  Neither <stdio.h>
**		nor any of its derivatives is used anywhere in the facility;
**		the output goes to a file descriptor by WRITE(2) with a
**		partial-write loop.
**
**		Diagnostics that are of interest to the developer only go
**		to $TRACE, which a build defining NDEBUG compiles away
**		altogether; everything else - whether the reader is a user
**		or the one who wrote the program - is signalled by
**		HELP$PUTMSG out of the catalogue below, in the OpenVMS
**		format.  The division is not a matter of taste:
**		HELP$PUTMSG writes to the standard error, whereas a $LOG
**		record goes to the log descriptor - the standard output -
**		in every revision of the UTILITY$ROUTINES before V.01-15,
**		and the standard output of both utilities carries data.
**		Anything a user is meant to read therefore goes through the
**		catalogue below, e.g.
**
**		    %HELP-E-NOTFOUND, Sorry, no documentation on FOOBAR
**
**  AUTHOR:	StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:  31-JUL-2026
**
**  MODIFICATION HISTORY:
**
**	X01-04		23-OCT-2026	RRL
**		Windows x64 (MinGW-w64): help$getwinsz() asks the console
**		(GetConsoleScreenBufferInfo) rather than TIOCGWINSZ;
**		help$out_init() switches a console to UTF-8 for both ways,
**		the text of a library being UTF-8 and the console's own code
**		page (866, 1251 ...) turning Russian into mojibake;
**		help$readfile() opens O_BINARY.
**
**	X01-03		 4-OCT-2026	CLD
**		The paging prompt tells Q from end of file: Q raises
**		HELP$K_ABORT_QUIT, which drops the rest of the topic and is
**		cleared by HELP$PAGE_RESET; end of file raises
**		HELP$K_ABORT_EXIT, which the reset keeps, so ^D/^Z there
**		ends the session as on OpenVMS.  RRL: "after q show the
**		current level".
**
**	X01-02		 1-AUG-2026	RRL
**		help$log_init() uses __util$setlogfd() of UTILITY$ROUTINES
**		V.01-17: the p_cb_log_f hook it used before was withdrawn
**		in V.01-16, and __util$deflog(), the only other way, names
**		a file, seizes both standard streams and announces itself
**		on every call.
**
**	X01-01		 1-AUG-2026	RRL
**		help$log_init() - the logging of the UTILITY$ROUTINES is
**		diverted onto the standard error through p_cb_log_f.  Its
**		default destination is the standard output, which carries
**		data in both utilities of this product: a paged topic, or
**		the Markdown of an /EXTRACT.
**
**	X01-00		31-JUL-2026	RRL
**		Initial version.
**
**--
*/

#include	<stdarg.h>
#include	<stdlib.h>
#include	<string.h>
#include	<errno.h>
#include	<unistd.h>
#include	<fcntl.h>
#include	<sys/stat.h>

#ifdef	_WIN32
#include	<windows.h>
#include	<io.h>
#else
#include	<sys/ioctl.h>
#endif

#include	"helpdef.h"

/*
**  The message catalogue of the facility.  The order of the records is
**  irrelevant - the lookup is a linear scan over a table of two dozen
**  entries, which is cheaper than any index would be.
*/
static	const HELP$MSG	s_msgtab [] = {
	{ HELP$_NORMAL,		"NORMAL",	"normal successful completion" },
	{ HELP$_NOTFOUND,	"NOTFOUND",	"Sorry, no documentation on !AZ" },
	{ HELP$_OPENIN,		"OPENIN",	"error opening !AZ as input, errno=!SL (!AZ)" },
	{ HELP$_OPENOUT,	"OPENOUT",	"error creating !AZ as output, errno=!SL (!AZ)" },
	{ HELP$_READERR,	"READERR",	"error reading !AZ, errno=!SL (!AZ)" },
	{ HELP$_WRITERR,	"WRITERR",	"error writing !AZ, errno=!SL (!AZ)" },
	{ HELP$_NOTHLB,		"NOTHLB",	"!AZ is not a help library" },
	{ HELP$_BADVERS,	"BADVERS",	"!AZ is of format version !UL, !UL is required" },
	{ HELP$_BADCRC,		"BADCRC",	"checksum error in !AZ, computed !XL, expected !XL" },
	{ HELP$_TRUNCLIB,	"TRUNCLIB",	"!AZ is truncated, !UL bytes of !UL are present" },
	{ HELP$_NOMEM,		"NOMEM",	"cannot allocate !UL bytes, errno=!SL (!AZ)" },
	{ HELP$_TOODEEP,	"TOODEEP",	"!AZ:!UL, heading level !UL exceeds the maximum of !UL" },
	{ HELP$_SKIPLVL,	"SKIPLVL",	"!AZ:!UL, heading level goes !UL -> !UL, intermediate levels assumed" },
	{ HELP$_NOKEY,		"NOKEY",	"!AZ:!UL, heading carries no key, topic ignored" },
	{ HELP$_LONGKEY,	"LONGKEY",	"!AZ:!UL, key truncated to !UL characters" },
	{ HELP$_DUPKEY,		"DUPKEY",	"!AZ:!UL, key !AZ is already defined at this level" },
	{ HELP$_NOTOP,		"NOTOP",	"!AZ:!UL, text precedes the first heading, ignored" },
	{ HELP$_EMPTYLIB,	"EMPTYLIB",	"no topics found, library not created" },
	{ HELP$_CREATED,	"CREATED",	"!AZ created, !UL topic!%S, !UL bytes of text" },
	{ HELP$_IVQUAL,		"IVQUAL",	"illegal value !AZ for the qualifier /!AZ" },
	{ HELP$_EXTRACTED,	"EXTRACTED",	"!AZ extracted from !AZ, !UL topic!%S" },
	{ HELP$_CONFQUAL,	"CONFQUAL",	"/!AZ and /!AZ cannot be given together" },
	{ HELP$_LONGLINE,	"LONGLINE",	"!AZ:!UL, line truncated to !UL bytes" },
	{ HELP$_NOLIBS,		"NOLIBS",	"no help library found on the search path !AZ" },
	{ HELP$_CACHERR,	"CACHERR",	"catalogue cache !AZ unusable, !AZ" },
	{ HELP$_TOOMANY,	"TOOMANY",	"more than !UL elements on the search path, the rest ignored" },
	{ HELP$_TOOMANYT,	"TOOMANYT",	"more than !UL topics in the catalogue, the rest ignored" },
	{ HELP$_DIAGS,		"DIAGS",	"!UL diagnostic!%S issued during the translation" },
	{ HELP$_TOOMANYIN,	"TOOMANYIN",	"more than !UL input files, the rest ignored" },
	{ HELP$_TOOMANYQ,	"TOOMANYQ",	"more than !UL qualifiers, the rest ignored" },
	{ HELP$_TOOMANYW,	"TOOMANYW",	"more than !UL words in the topic, the rest ignored" },
	{ HELP$_SYSCACHE,	"SYSCACHE",	"!AZ written, !UL topic!%S out of !UL file!%S" },
	{ HELP$_QUEFAIL,	"QUEFAIL",	"the key !AZ could not be queued, it will not be released" },
	{ HELP$_BADRUN,		"BADRUN",	"key !UL names the text run !UL:!UL, outside the heap of !UL bytes" }
	};

/*
**  Severity letters, indexed by $SEV() of a condition value.
*/
static	const char *	s_sevchar [STS$K_MAX] = {
	STS$C__WARN, STS$C__SUCCESS, STS$C__ERROR, STS$C__INFO,
	STS$C__FATAL, STS$C__UNDEF, STS$C__UNDEF, STS$C__UNDEF
	};

/*
**  The prompt shown by the pager, and the answers that stop the output.
*/
static	const char	s_pagemsg [] = { "                Press RETURN to continue (Q to quit) ... " };

/*
**  A run of blanks long enough for any indent the display can ask for.
*/
static	const char	s_blanks [] = { "                                                                " };


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Write a memory block to a file descriptor, restarting the transfer
**	on a partial write and on EINTR.  This is the only place in the
**	whole facility where WRITE(2) is called.
**
**  FORMAL PARAMETERS:
**
**	a_chan	Descriptor to write to
**	a_len	Number of bytes to be written
**	a_buf	Address of the data
**
**  IMPLICIT INPUTS/OUTPUTS:
**	None.
**
**  RETURN VALUE:
**	STS$K_SUCCESS - the whole block has been written;
**	STS$K_ERROR   - WRITE(2) failed, ERRNO is set.
**--
*/
static	int	s_help$write	(
		int		a_chan,
		unsigned	a_len,
		const char *	a_buf
			)
{
ssize_t		l_wrt;
unsigned	l_rest = a_len;
const char *	l_ptr = a_buf;

	while ( l_rest )
		{
		l_wrt = write(a_chan, l_ptr, l_rest);

		if ( l_wrt > 0 )
			{
			l_ptr	+= l_wrt;
			l_rest	-= (unsigned) l_wrt;
			continue;
			}

		/* An interrupted transfer is retried, anything else is fatal for the record */
		if ( (l_wrt < 0) && (errno == EINTR) )
			continue;

		return	help$putmsg(HELP$_WRITERR, 3, (fao_prm_t) "the output stream",
					(fao_prm_t) errno, (fao_prm_t) strerror(errno));
		}

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Look a condition value up in the message catalogue of the facility.
**	Only the facility and the message number take part in the match, so
**	that a code signalled with a different severity is still found.
**
**  FORMAL PARAMETERS:
**
**	a_sts	Condition value to be found
**	a_msg	Receives the address of the catalogue record
**
**  IMPLICIT INPUTS/OUTPUTS:
**	Reads the module-static S_MSGTAB.
**
**  RETURN VALUE:
**	STS$K_SUCCESS - <a_msg> holds the address of the record;
**	STS$K_WARN    - no such message in the catalogue.
**--
*/
int	help$getmsg	(
		unsigned	a_sts,
	const	HELP$MSG **	a_msg
			)
{
	if ( !a_msg )
		return	STS$K_WARN;

	for ( int i = 0; i < (int) $ARRSZ(s_msgtab); i++ )
		{
		if ( ($FAC(s_msgtab[i].sts) != $FAC(a_sts)) || ($MSG(s_msgtab[i].sts) != $MSG(a_sts)) )
			continue;

		*a_msg = &s_msgtab[i];

		return	STS$K_SUCCESS;
		}

	return	STS$K_WARN;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Signal a condition in the OpenVMS message format:
**
**	    %<facility>-<severity>-<identification>, <text>
**
**	The text is the FAO control string of the catalogue record, the
**	parameters are taken from the argument list.  The record goes to
**	the standard error stream so that it never contaminates the help
**	text a caller may be capturing from the standard output.
**
**  FORMAL PARAMETERS:
**
**	a_sts		Condition value to be signalled
**	a_prmcnt	Number of the FAO parameters that follow, 0 .. HELP$K_MAXPRM
**	...		The FAO parameters, each cast to FAO_PRM_T by the caller
**
**  IMPLICIT INPUTS/OUTPUTS:
**	Writes to the standard error stream.
**
**  RETURN VALUE:
**	The condition value <a_sts>, so that the routine can be used as
**	the operand of a RETURN statement.
**--
*/
int	help$putmsg	(
		unsigned	a_sts,
		int		a_prmcnt,
				...
			)
{
va_list		l_ap;
const HELP$MSG *l_msg = NULL;
fao_prm_t	l_prm [HELP$K_MAXPRM];
char		l_txt [HELP$K_SZ_MSG], l_rec [HELP$K_SZ_MSG];
fao_desc_t	l_txtdsc = $FAO_DESC(l_txt), l_recdsc = $FAO_DESC(l_rec);
unsigned short	l_txtlen = 0, l_reclen = 0;
int		l_cnt = a_prmcnt;

	/* Collect the parameters, silently clamping an over-long list */
	if ( l_cnt > HELP$K_MAXPRM )
		l_cnt = HELP$K_MAXPRM;

	if ( l_cnt < 0 )
		l_cnt = 0;

	va_start(l_ap, a_prmcnt);

	for ( int i = 0; i < l_cnt; i++ )
		l_prm[i] = va_arg(l_ap, fao_prm_t);

	va_end(l_ap);

	/* Format the body of the message from the catalogue */
	if ( 1 & help$getmsg(a_sts, &l_msg) )
		__util$faol(l_msg->fao, &l_txtlen, &l_txtdsc, l_prm, l_cnt);
	else	__util$fao("undocumented condition value !XL", &l_txtlen, &l_txtdsc, (fao_prm_t) a_sts);

	/* ... and wrap it into the standard %FAC-S-IDENT, text envelope */
	__util$fao("%!AZ-!AZ-!AZ, !AD\n", &l_reclen, &l_recdsc,
		(fao_prm_t) __FAC__,
		(fao_prm_t) s_sevchar[$SEV(a_sts) & 0x7],
		(fao_prm_t) (l_msg ? l_msg->ident : "UNKNOWN"),
		(fao_prm_t) l_txtlen, (fao_prm_t) l_txt);

	s_help$write(STDERR_FILENO, l_reclen, l_rec);

	return	(int) a_sts;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Point the logging of the UTILITY$ROUTINES at the standard error.
**	Both utilities call it before anything else is emitted: the
**	standard output carries what they were run to produce - the text
**	of a topic, the Markdown of an /EXTRACT - and a log or a trace
**	record among it would be indistinguishable from the documentation.
**
**	The set has offered three ways of saying this, and only the last
**	of them says it plainly:
**
**	    P_CB_LOG_F, a diversion hook, honoured by the tracing alone
**		until V.01-15 and withdrawn in V.01-16;
**	    __UTIL$DEFLOG, which names a file rather than a descriptor and
**		seizes both standard streams by DUP2 - right for a daemon,
**		which wants neither, and wrong here.  It also announces
**		itself with a record of its own on every call;
**	    __UTIL$SETLOGFD, of V.01-17, which sets the descriptor and
**		does nothing else.
**
**	Hence the requirement on the version, recorded in the README.
**
**  FORMAL PARAMETERS:
**	None.
**
**  IMPLICIT INPUTS/OUTPUTS:
**	Sets the log descriptor of the UTILITY$ROUTINES.
**
**  RETURN VALUE:
**	STS$K_SUCCESS always - a utility that cannot redirect its logging
**	still has work to do.
**--
*/
int	help$log_init	(void)
{
	__util$setlogfd(STDERR_FILENO);

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Ask the terminal for its geometry.  A descriptor that is not a
**	terminal, or a driver that reports a degenerate window, yields the
**	built-in defaults.
**
**  FORMAL PARAMETERS:
**
**	a_chan		Descriptor to be interrogated
**	a_width		Receives the width of the page, characters
**	a_height	Receives the height of the page, lines
**
**  IMPLICIT INPUTS/OUTPUTS:
**	None.
**
**  RETURN VALUE:
**	STS$K_SUCCESS - the geometry has been taken from the driver;
**	STS$K_WARN    - the defaults have been substituted.
**--
*/
int	help$getwinsz	(
		int		a_chan,
		unsigned *	a_width,
		unsigned *	a_height
			)
{
#ifdef	_WIN32
CONSOLE_SCREEN_BUFFER_INFO l_csbi = {0};
unsigned	l_col, l_row;
#else
struct winsize	l_ws = {0};
#endif

	if ( a_width )
		*a_width = HELP$K_DEFWIDTH;

	if ( a_height )
		*a_height = HELP$K_DEFPAGE;

#ifdef	_WIN32
	/* The window, not the buffer: the buffer is thousands of lines tall */
	if ( !GetConsoleScreenBufferInfo((HANDLE) _get_osfhandle(a_chan), &l_csbi) )
		return	STS$K_WARN;

	l_col = (unsigned) (l_csbi.srWindow.Right - l_csbi.srWindow.Left + 1);
	l_row = (unsigned) (l_csbi.srWindow.Bottom - l_csbi.srWindow.Top + 1);

	if ( a_width && (l_col >= HELP$K_MINWIDTH) )
		*a_width = $MIN(l_col, HELP$K_MAXWIDTH);

	if ( a_height && (l_row > 4) )
		*a_height = l_row;
#else
	if ( ioctl(a_chan, TIOCGWINSZ, &l_ws) )
		return	STS$K_WARN;

	if ( a_width && l_ws.ws_col >= HELP$K_MINWIDTH )
		*a_width = $MIN(l_ws.ws_col, HELP$K_MAXWIDTH);

	if ( a_height && (l_ws.ws_row > 4) )
		*a_height = l_ws.ws_row;
#endif

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Initialize an output context.  Paging is requested by the caller
**	but is granted only when the output really is a terminal - a run
**	whose output is redirected into a file or a pipe must never stop
**	waiting for a keystroke.
**
**  FORMAL PARAMETERS:
**
**	a_out		The context to be initialized
**	a_chan		Descriptor the text is written to
**	a_inchan	Descriptor the paging answers are read from
**	a_page		!= 0 - paging is requested
**	a_width		Page width, 0 - ask the terminal
**
**  IMPLICIT INPUTS/OUTPUTS:
**	None.
**
**  RETURN VALUE:
**	STS$K_SUCCESS always.
**--
*/
int	help$out_init	(
		HELP$OUT *	a_out,
		int		a_chan,
		int		a_inchan,
		int		a_page,
		unsigned	a_width
			)
{
unsigned	l_width = HELP$K_DEFWIDTH, l_height = HELP$K_DEFPAGE;

	memset(a_out, 0, sizeof(HELP$OUT));

	a_out->chan	= a_chan;
	a_out->inchan	= a_inchan;
	a_out->tty	= isatty(a_chan) && isatty(a_inchan);

#ifdef	_WIN32
	/* The text is UTF-8; a console shows it in its own code page unless told otherwise */
	if ( isatty(a_chan) )
		SetConsoleOutputCP(CP_UTF8);

	if ( isatty(a_inchan) )
		SetConsoleCP(CP_UTF8);
#endif

	help$getwinsz(a_chan, &l_width, &l_height);

	/* An explicit /WIDTH overrides the driver, within sane bounds */
	if ( a_width )
		l_width = $MIN($MAX(a_width, HELP$K_MINWIDTH), HELP$K_MAXWIDTH);

	a_out->width	= l_width;
	a_out->height	= l_height;
	a_out->page	= a_page && a_out->tty;
	a_out->lineno	= 0;
	a_out->abort	= 0;

	$TRACE("chan=%d, tty=%d, page=%d, width=%u, height=%u", a_chan, a_out->tty, a_out->page, l_width, l_height);

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Reset the line counter of the pager.  Called before every new topic
**	so that the prompt appears at a page boundary of the topic, not of
**	the whole session.  A Q given at the paging prompt is forgotten
**	here; an end of file is not - it stops the session, not a topic.
**
**  FORMAL PARAMETERS:
**
**	a_out	The output context
**
**  IMPLICIT INPUTS/OUTPUTS:
**	None.
**
**  RETURN VALUE:
**	STS$K_SUCCESS always.
**--
*/
int	help$page_reset	(
		HELP$OUT *	a_out
			)
{
	a_out->lineno	= 0;

	if ( a_out->abort != HELP$K_ABORT_EXIT )
		a_out->abort = 0;

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Read a single line from a descriptor, one character at a time, so
**	that nothing is consumed past the line terminator.  The terminator
**	itself is not stored.
**
**  FORMAL PARAMETERS:
**
**	a_chan		Descriptor to read from
**	a_bufsz		Capacity of the buffer
**	a_buf		Receives the line
**	a_buflen	Receives the length of the line
**
**  IMPLICIT INPUTS/OUTPUTS:
**	None.
**
**  RETURN VALUE:
**	STS$K_SUCCESS - a line has been read;
**	STS$K_WARN    - end of file has been reached before any character;
**	STS$K_ERROR   - READ(2) failed.
**--
*/
int	help$readline	(
		int		a_chan,
		unsigned	a_bufsz,
		char *		a_buf,
		unsigned *	a_buflen
			)
{
ssize_t		l_red;
unsigned	l_len = 0;
char		l_chr;

	*a_buflen = 0;

	while ( l_len < (a_bufsz - 1) )
		{
		l_red = read(a_chan, &l_chr, 1);

		if ( (l_red < 0) && (errno == EINTR) )
			continue;

		if ( l_red < 0 )
			return	help$putmsg(HELP$_READERR, 3, (fao_prm_t) "the input stream",
						(fao_prm_t) errno, (fao_prm_t) strerror(errno));

		if ( !l_red )
			break;			/* End-of-file, ^Z at the terminal	*/

		if ( l_chr == '\n' )
			{
			a_buf[l_len]	= '\0';
			*a_buflen	= l_len;

			return	STS$K_SUCCESS;
			}

		a_buf[l_len++] = l_chr;
		}

	a_buf[l_len]	= '\0';
	*a_buflen	= l_len;

	return	l_len ? STS$K_SUCCESS : STS$K_WARN;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Emit the paging prompt and wait for the answer.  An answer that
**	starts with Q or with q raises ABORT of the context to
**	HELP$K_ABORT_QUIT, end of file (or a read error) to
**	HELP$K_ABORT_EXIT; either makes HELP$PUT_RAW discard the rest of
**	the output until HELP$PAGE_RESET, which clears the first only.
**
**  FORMAL PARAMETERS:
**
**	a_out	The output context
**
**  IMPLICIT INPUTS/OUTPUTS:
**	Updates LINENO and ABORT of the context.
**
**  RETURN VALUE:
**	STS$K_SUCCESS - the output may continue;
**	STS$K_WARN    - the user asked to stop.
**--
*/
static	int	s_help$pause	(
		HELP$OUT *	a_out
			)
{
char		l_ans [16];
unsigned	l_anslen = 0;
int		l_status;

	s_help$write(a_out->chan, (unsigned) sizeof(s_pagemsg) - 1, s_pagemsg);

	l_status = help$readline(a_out->inchan, (unsigned) sizeof(l_ans), l_ans, &l_anslen);

	/*
	**  Erase the prompt: a carriage return alone only moves the cursor,
	**  the text stays on the screen until something overwrites it - and
	**  the line that follows is usually shorter.  The prompt carries no
	**  tab, so its width on the screen is its length.
	*/
	s_help$write(a_out->chan, 1, "\r");
	help$put_indent(a_out, (unsigned) sizeof(s_pagemsg) - 1);
	s_help$write(a_out->chan, 1, "\r");

	a_out->lineno = 0;

	if ( !(1 & l_status) )
		{
		a_out->abort = HELP$K_ABORT_EXIT;

		return	STS$K_WARN;
		}

	if ( l_anslen && ((l_ans[0] == 'Q') || (l_ans[0] == 'q')) )
		{
		a_out->abort = HELP$K_ABORT_QUIT;

		return	STS$K_WARN;
		}

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Write a block of text to the output context, honouring the pager.
**	The block is split at the line terminators; after every HEIGHT-1
**	lines the prompt is issued.
**
**  FORMAL PARAMETERS:
**
**	a_out	The output context
**	a_len	Length of the block, bytes
**	a_str	Address of the block
**
**  IMPLICIT INPUTS/OUTPUTS:
**	Updates LINENO and ABORT of the context.
**
**  RETURN VALUE:
**	STS$K_SUCCESS - the block has been written;
**	STS$K_WARN    - the output has been aborted by the user;
**	STS$K_ERROR   - a write error, already signalled.
**--
*/
int	help$put_raw	(
		HELP$OUT *	a_out,
		unsigned	a_len,
	const	char *		a_str
			)
{
const char *	l_ptr = a_str, *l_eol;
unsigned	l_rest = a_len, l_chunk;
int		l_status;

	if ( a_out->abort )
		return	STS$K_WARN;

	while ( l_rest )
		{
		/* A run up to and including the next line terminator, or the tail */
		if ( (l_eol = memchr(l_ptr, '\n', l_rest)) )
			l_chunk = (unsigned) (l_eol - l_ptr) + 1;
		else	l_chunk = l_rest;

		if ( !(1 & (l_status = s_help$write(a_out->chan, l_chunk, l_ptr))) )
			return	l_status;

		l_ptr	+= l_chunk;
		l_rest	-= l_chunk;

		if ( !l_eol )
			continue;

		a_out->lineno++;

		if ( !a_out->page || (a_out->lineno < (a_out->height - 1)) )
			continue;

		if ( !(1 & s_help$pause(a_out)) )
			return	STS$K_WARN;
		}

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Format a record with the FAO engine and write it to the output
**	context.  The assembly buffer of the context is used, so the
**	routine is not reentrant with respect to a single context - which
**	is exactly how the utilities use it.
**
**  FORMAL PARAMETERS:
**
**	a_out		The output context
**	a_fao		FAO control string
**	a_prmcnt	Number of the FAO parameters that follow
**	...		The FAO parameters, each cast to FAO_PRM_T by the caller
**
**  IMPLICIT INPUTS/OUTPUTS:
**	Uses and overwrites the BUF field of the context.
**
**  RETURN VALUE:
**	As of HELP$PUT_RAW.
**--
*/
int	help$put	(
		HELP$OUT *	a_out,
	const	char *		a_fao,
		int		a_prmcnt,
				...
			)
{
va_list		l_ap;
fao_prm_t	l_prm [HELP$K_MAXPRM];
fao_desc_t	l_dsc;
unsigned short	l_outlen = 0;
int		l_cnt = $MIN($MAX(a_prmcnt, 0), HELP$K_MAXPRM);

	va_start(l_ap, a_prmcnt);

	for ( int i = 0; i < l_cnt; i++ )
		l_prm[i] = va_arg(l_ap, fao_prm_t);

	va_end(l_ap);

	l_dsc.dsc$w_length	= (unsigned short) sizeof(a_out->buf);
	l_dsc.dsc$b_dtype	= 0;
	l_dsc.dsc$b_class	= 0;
	l_dsc.dsc$a_pointer	= a_out->buf;

	__util$faol(a_fao, &l_outlen, &l_dsc, l_prm, l_cnt);

	return	help$put_raw(a_out, l_outlen, a_out->buf);
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Write a block of help text with every line shifted right by a fixed
**	number of blanks, the way OpenVMS HELP indents a topic by its
**	nesting level.  An empty line is emitted empty, without trailing
**	blanks.
**
**  FORMAL PARAMETERS:
**
**	a_out		The output context
**	a_indent	Number of the leading blanks
**	a_len		Length of the text, bytes
**	a_txt		Address of the text
**
**  IMPLICIT INPUTS/OUTPUTS:
**	None.
**
**  RETURN VALUE:
**	As of HELP$PUT_RAW.
**--
*/
int	help$put_text	(
		HELP$OUT *	a_out,
		unsigned	a_indent,
		unsigned	a_len,
	const	char *		a_txt
			)
{
const char *	l_ptr = a_txt, *l_eol;
unsigned	l_rest = a_len, l_line, l_pad = $MIN(a_indent, (unsigned) sizeof(s_blanks) - 1);
int		l_status = STS$K_SUCCESS;

	while ( l_rest )
		{
		if ( (l_eol = memchr(l_ptr, '\n', l_rest)) )
			l_line = (unsigned) (l_eol - l_ptr);
		else	l_line = l_rest;

		/* Blank lines carry no padding - trailing blanks are noise */
		if ( l_line && l_pad )
			{
			if ( !(1 & (l_status = help$put_raw(a_out, l_pad, s_blanks))) )
				return	l_status;
			}

		if ( !(1 & (l_status = help$put_raw(a_out, l_line + (l_eol ? 1 : 0), l_ptr))) )
			return	l_status;

		l_ptr	+= l_line + (l_eol ? 1 : 0);
		l_rest	-= l_line + (l_eol ? 1 : 0);
		}

	return	l_status;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Emit a run of blanks.  Used to indent the key lines and the
**	subtopic block of a display; the text of a topic is indented by
**	HELP$PUT_TEXT, which does it line by line.
**
**  FORMAL PARAMETERS:
**
**	a_out		The output context
**	a_indent	Number of the blanks
**
**  IMPLICIT INPUTS/OUTPUTS:
**	None.
**
**  RETURN VALUE:
**	As of HELP$PUT_RAW.
**--
*/
int	help$put_indent	(
		HELP$OUT *	a_out,
		unsigned	a_indent
			)
{
	if ( !a_indent )
		return	STS$K_SUCCESS;

	return	help$put_raw(a_out, $MIN(a_indent, (unsigned) sizeof(s_blanks) - 1), s_blanks);
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Flush the output context.  Kept as a separate entry point although
**	the writes are unbuffered, so that a buffered implementation can be
**	dropped in without touching the callers.
**
**  FORMAL PARAMETERS:
**
**	a_out	The output context
**
**  IMPLICIT INPUTS/OUTPUTS:
**	None.
**
**  RETURN VALUE:
**	STS$K_SUCCESS always.
**--
*/
int	help$flush	(
		HELP$OUT *	a_out
			)
{
	(void) a_out;

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Copy a string upper-casing it, so that both the keys of a library
**	and the topics typed by the user reach the lookup in the one case
**	it works in.
**
**	Latin is folded by the arithmetic of ASCII.  Cyrillic is folded as
**	UTF-8: the lower case of the Russian alphabet occupies D0 B0..BF
**	and D1 80..8F, its upper case D0 90..9F and D0 A0..AF, and the
**	stray D1 91 pairs with D0 81.  Both cases are two bytes wide, so
**	the fold never changes the length - which is what lets a key be
**	folded in place inside a fixed field.
**
**	Any other multi-byte sequence is copied through untouched.
**
**  FORMAL PARAMETERS:
**
**	a_srclen	Length of the source, bytes
**	a_src		Address of the source
**	a_dstsz		Capacity of the destination, bytes
**	a_dst		Address of the destination
**	a_dstlen	Receives the length actually stored
**
**  IMPLICIT INPUTS/OUTPUTS:
**	None.
**
**  RETURN VALUE:
**	STS$K_SUCCESS - the whole source has been converted;
**	STS$K_WARN    - the result has been truncated to <a_dstsz>.
**--
*/
int	help$upcase	(
		unsigned	a_srclen,
	const	char *		a_src,
		unsigned	a_dstsz,
		char *		a_dst,
		unsigned *	a_dstlen
			)
{
unsigned char	l_lead, l_trail;
unsigned	l_src = 0, l_dst = 0;

	while ( (l_src < a_srclen) && (l_dst < a_dstsz) )
		{
		l_lead = (unsigned char) a_src [l_src];

		/*
		**  A two-byte sequence of the Cyrillic block - it needs two
		**  free positions in the destination, or it is left for the
		**  truncation test below.
		*/
		if ( ((l_lead == 0xD0) || (l_lead == 0xD1)) && ((l_src + 1) < a_srclen) )
			{
			if ( (l_dst + 1) >= a_dstsz )
				break;

			l_trail = (unsigned char) a_src [l_src + 1];

			if ( (l_lead == 0xD0) && (l_trail >= 0xB0) && (l_trail <= 0xBF) )
				{
				/* а .. п -> А .. П */
				a_dst [l_dst++] = (char) 0xD0;
				a_dst [l_dst++] = (char) (l_trail - 0x20);
				}
			else if ( (l_lead == 0xD1) && (l_trail >= 0x80) && (l_trail <= 0x8F) )
				{
				/* р .. я -> Р .. Я, the lead byte changes as well */
				a_dst [l_dst++] = (char) 0xD0;
				a_dst [l_dst++] = (char) (l_trail + 0x20);
				}
			else if ( (l_lead == 0xD1) && (l_trail == 0x91) )
				{
				/* ё -> Ё, which sits apart from the rest of the alphabet */
				a_dst [l_dst++] = (char) 0xD0;
				a_dst [l_dst++] = (char) 0x81;
				}
			else	{
				a_dst [l_dst++] = (char) l_lead;
				a_dst [l_dst++] = (char) l_trail;
				}

			l_src += 2;

			continue;
			}

		a_dst [l_dst++] = ((l_lead >= 'a') && (l_lead <= 'z')) ? (char) (l_lead - ('a' - 'A')) : (char) l_lead;
		l_src++;
		}

	if ( a_dstlen )
		*a_dstlen = l_dst;

	return	(l_src == a_srclen) ? STS$K_SUCCESS : STS$K_WARN;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Return the width a string occupies on the screen, in characters.
**	A UTF-8 continuation byte - one of 80..BF - carries no width of its
**	own, so counting the bytes that are not continuations gives the
**	number of characters.  That is what the column layout of the
**	subtopic block has to align on; the byte length would push a
**	Cyrillic key half a column to the left.
**
**	Combining marks and the double-width blocks of East Asia are not
**	accounted for; neither appears in a help key.
**
**  FORMAL PARAMETERS:
**
**	a_len	Length of the string, bytes
**	a_str	Address of the string
**
**  IMPLICIT INPUTS/OUTPUTS:
**	None.
**
**  RETURN VALUE:
**	The width of the string in characters.
**--
*/
unsigned	help$dsplen	(
		unsigned	a_len,
	const	char *		a_str
			)
{
unsigned	l_width = 0;

	for ( unsigned i = 0; i < a_len; i++ )
		{
		if ( 0x80 != (((unsigned char) a_str[i]) & 0xC0) )
			l_width++;
		}

	return	l_width;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Count the qualifiers of a table whose name begins with a given
**	prefix, optionally among those carrying a flag.  The rule is the
**	one the CLI service itself applies: a leading abbreviation matches,
**	and more than one match is an ambiguity rather than a failure.
**
**  FORMAL PARAMETERS:
**
**	a_quals		The qualifier table, an empty name closes it
**	a_name		The prefix
**	a_len		Length of the prefix
**	a_flag		0, or a flag every candidate must carry
**
**  IMPLICIT INPUTS/OUTPUTS:
**	None.
**
**  RETURN VALUE:
**	The number of the qualifiers that match.
**--
*/
static	int	s_help$match	(
	const	CLI_PQDESC *	a_quals,
	const	char *		a_name,
		unsigned	a_len,
		unsigned	a_flag
			)
{
int		l_cnt = 0;

	for ( const CLI_PQDESC *l_qual = a_quals; $ASCLEN(&l_qual->name); l_qual++ )
		{
		if ( a_flag && !(l_qual->flag & a_flag) )
			continue;

		if ( a_len > (unsigned) $ASCLEN(&l_qual->name) )
			continue;

		if ( !strncasecmp(a_name, $ASCPTR(&l_qual->name), a_len) )
			l_cnt++;
		}

	return	l_cnt;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Decide whether a command line argument is a qualifier of the
**	utility or one of its ordinary arguments - a topic for HELP, an
**	input file for HELPTRAN.
**
**	The distinction has to be made before the CLI service is called,
**	not after.  Both utilities routinely receive arguments that begin
**	with a slash and are not qualifiers at all: the keys of a help
**	library are named /AFTER or /EXTRACT, and an input file is named
**	/usr/share/doc/foo.md.  Since CLI_ROUTINES X.00-07 an argument
**	that begins with a slash and matches no qualifier is refused
**	outright, so such an argument must never reach the parser.
**
**	An argument is a qualifier when its name, up to the '=' of its
**	value, is a leading abbreviation of one in the table, or - for a
**	negatable qualifier - of one prefixed with NO.  An abbreviation
**	that matches several is still a qualifier: the ambiguity is the
**	CLI service's to report, not ours to hide.
**
**  FORMAL PARAMETERS:
**
**	a_quals	The qualifier table of the utility
**	a_arg	The argument, ASCIZ
**
**  IMPLICIT INPUTS/OUTPUTS:
**	None.
**
**  RETURN VALUE:
**	Non-zero when the argument is a qualifier of the utility.
**--
*/
int	help$isqual	(
	const	CLI_PQDESC *	a_quals,
	const	char *		a_arg
			)
{
const char *	l_val;
unsigned	l_len;

	/*
	**  The two prefixes CLI_ROUTINES X.00-14 recognises, and only those:
	**  a slash, or the doubled dash of the GNU long form.  A single dash
	**  ceased to be a prefix in that revision, and this routine has to
	**  say exactly what the service would say - a spelling accepted here
	**  and refused there, or the other way about, would behave
	**  differently depending on where in the command it stood.
	*/
	if ( *a_arg == '/' )
		a_arg++;
	else if ( (a_arg[0] == '-') && (a_arg[1] == '-') )
		a_arg += 2;
	else	return	0;

	l_val	= strchr(a_arg, '=');
	l_len	= l_val ? (unsigned) (l_val - a_arg) : (unsigned) strlen(a_arg);

	if ( !l_len )
		return	0;

	if ( s_help$match(a_quals, a_arg, l_len, 0) )
		return	1;

	/* /NO<name> selects the negated form of a negatable qualifier */
	if ( (l_len > 2) && !strncasecmp(a_arg, "NO", 2)
		&& s_help$match(a_quals, a_arg + 2, l_len - 2, CLI$M_NEGATABLE) )
		return	1;

	return	0;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Decide whether an argument asks for the command syntax.  The
**	spellings are those CLI_ROUTINES X.00-11 recognises - /HELP, /H,
**	-help, --help, -h - because the request has to be answered before
**	the parser is called, and a spelling answered here but not there,
**	or the other way round, would behave differently depending on
**	where in the command it stood.
**
**	A bare HELP is not one of them: it is the name of a topic, and of
**	a very likely one.  The service made the same correction in that
**	revision, for the same reason.
**
**  FORMAL PARAMETERS:
**
**	a_arg	The argument, ASCIZ
**
**  IMPLICIT INPUTS/OUTPUTS:
**	None.
**
**  RETURN VALUE:
**	Non-zero when the argument asks for the syntax.
**--
*/
int	help$ishelp	(
	const	char *		a_arg
			)
{
const char *	l_ptr = a_arg;

	if ( !l_ptr )
		return	0;

	/* Without a dash or a slash the argument is an ordinary one */
	if ( (*l_ptr != '-') && (*l_ptr != '/') )
		return	0;

	l_ptr++;

	if ( *l_ptr == '-' )
		l_ptr++;

	if ( ((l_ptr[0] == 'h') || (l_ptr[0] == 'H')) && !l_ptr[1] )
		return	1;

	return	(!strncasecmp(l_ptr, "HELP", 4)) && !l_ptr[4];
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Copy an ASCIZ string into a buffer of a known capacity, always
**	terminating the result.  STRNCPY(3) is deliberately avoided: it
**	leaves the target unterminated on truncation, and a truncated file
**	specification is a reference to the wrong file, not a cosmetic
**	defect - so the caller is told about it.
**
**  FORMAL PARAMETERS:
**
**	a_dstsz	Capacity of the destination, including the terminator
**	a_dst	Address of the destination
**	a_src	Address of the source, ASCIZ
**
**  IMPLICIT INPUTS/OUTPUTS:
**	None.
**
**  RETURN VALUE:
**	STS$K_SUCCESS - the whole source has been copied;
**	STS$K_WARN    - the result has been truncated.
**--
*/
int	help$strcpy	(
		unsigned	a_dstsz,
		char *		a_dst,
	const	char *		a_src
			)
{
unsigned	l_len;

	if ( !a_dstsz )
		return	STS$K_WARN;

	l_len = (unsigned) strnlen(a_src, a_dstsz);

	memcpy(a_dst, a_src, $MIN(l_len, a_dstsz - 1));
	a_dst [$MIN(l_len, a_dstsz - 1)] = '\0';

	return	(l_len < a_dstsz) ? STS$K_SUCCESS : STS$K_WARN;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Read a whole file into a freshly allocated, zero-terminated block.
**	The zero byte past the data lets the Markdown scanner treat the
**	image as a C string without a bounds test on every character.
**
**  FORMAL PARAMETERS:
**
**	a_fspec		File specification
**	a_buf		Receives the address of the allocated block
**	a_buflen	Receives the length of the data, without the zero byte
**
**  IMPLICIT INPUTS/OUTPUTS:
**	Allocates memory to be released by the caller with FREE(3).
**
**  RETURN VALUE:
**	STS$K_SUCCESS or a HELP$_OPENIN / HELP$_READERR / HELP$_NOMEM
**	condition, already signalled.
**--
*/
int	help$readfile	(
	const	char *		a_fspec,
		char **		a_buf,
		unsigned *	a_buflen
			)
{
struct stat	l_st = {0};
char *		l_buf;
ssize_t		l_red;
size_t		l_rest, l_off = 0;
int		l_fd;

	*a_buf		= NULL;
	*a_buflen	= 0;

	if ( 0 > (l_fd = open(a_fspec, O_RDONLY | O_BINARY)) )
		return	help$putmsg(HELP$_OPENIN, 3, (fao_prm_t) a_fspec, (fao_prm_t) errno, (fao_prm_t) strerror(errno));

	if ( fstat(l_fd, &l_st) )
		{
		close(l_fd);

		return	help$putmsg(HELP$_READERR, 3, (fao_prm_t) a_fspec, (fao_prm_t) errno, (fao_prm_t) strerror(errno));
		}

	if ( !(l_buf = calloc(1, (size_t) l_st.st_size + 1)) )
		{
		close(l_fd);

		return	help$putmsg(HELP$_NOMEM, 3, (fao_prm_t) (l_st.st_size + 1), (fao_prm_t) errno, (fao_prm_t) strerror(errno));
		}

	/* READ(2) is free to return short - loop until the file is exhausted */
	for ( l_rest = (size_t) l_st.st_size; l_rest; )
		{
		l_red = read(l_fd, l_buf + l_off, l_rest);

		if ( (l_red < 0) && (errno == EINTR) )
			continue;

		if ( l_red <= 0 )
			break;

		l_off	+= (size_t) l_red;
		l_rest	-= (size_t) l_red;
		}

	if ( l_rest )
		{
		free(l_buf);
		close(l_fd);

		return	help$putmsg(HELP$_READERR, 3, (fao_prm_t) a_fspec, (fao_prm_t) errno, (fao_prm_t) strerror(errno));
		}

	close(l_fd);

	l_buf[l_off]	= '\0';
	*a_buf		= l_buf;
	*a_buflen	= (unsigned) l_off;

	$TRACE("%s: %u bytes", a_fspec, *a_buflen);

	return	STS$K_SUCCESS;
}
