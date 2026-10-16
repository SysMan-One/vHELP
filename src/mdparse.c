#define	__MODULE__	"MDPARSE"
#define	__IDENT__	"X01-00"
#define	__REV__		"01.00.0"

/*
**++
**
**  FACILITY:	HELP - OpenVMS-style help library subsystem for Linux
**
**  MODULE:	mdparse.c
**
**  ABSTRACT:	The Markdown reader of the HELPTRAN utility: turns a
**		Markdown file into the topic tree of a help library.
**
**  DESCRIPTION: The mapping is the natural one - a Markdown heading is a
**		help key, and the depth of the heading is the nesting level
**		of the topic, exactly as the leading digit is in an OpenVMS
**		.HLP source:
**
**		    # SUBMIT		->	1 SUBMIT
**		    ## /AFTER		->	2 /AFTER
**		    ### Examples	->	3 EXAMPLES
**
**		The key of a topic is the text of the heading with the
**		inline markup removed, truncated at the first descriptive
**		separator (" - ", " -- ", an em/en dash, ": "), trimmed and
**		upper-cased.  So
**
**		    ## /AFTER -- hold the job until a given time
**
**		yields the key /AFTER while the whole heading stays legible
**		in the source.  An explicit key is given by a directive:
**
**		    <!-- key: /AFTER -->
**
**		Two more directives are recognised, both anywhere in the
**		file: <!-- ident: X-01 --> sets the identification of the
**		library, and the pair <!-- ignore --> / <!-- resume -->
**		brackets a region of the source that is to be left out of
**		the library altogether - headings and all.  That is how a
**		document that serves both as a manual and as a help source
**		keeps its front matter out of the library.
**
**		The body of a topic is rendered into plain text: fenced code
**		blocks are copied verbatim, the strong-emphasis markers, the
**		code back-quotes and the link brackets are removed, list
**		bullets become 'o' and the horizontal rules disappear.
**		Leading and trailing blank lines of a topic are dropped, so
**		the display never opens or closes with white space.
**
**  AUTHOR:	StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:  31-JUL-2026
**
**  MODIFICATION HISTORY:
**
**	X01-00		31-JUL-2026	RRL
**		Initial version.
**
**--
*/

#include	<stdlib.h>
#include	<string.h>

#include	"helpdef.h"

/*
**  A fenced code block is shifted right by this many positions, so that
**  an example stands out of the paragraph that introduces it - the way
**  the format section of an OpenVMS help topic does.
*/
#define	MD$K_CODEIND	2

/*
**  The separators that cut the descriptive tail off a heading, longest
**  first so that " -- " is preferred over " - ".
*/
static	const char *	s_md$keysep [] = { " \xE2\x80\x94 ", " \xE2\x80\x93 ", " -- ", " - ", ": " };

/*
**  The shift of a fenced code block, as a blank run.
*/
static	const char	s_md$codeind [] = { "  " };


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Test a line for being blank - empty or made of white space only.
**
**  FORMAL PARAMETERS:
**
**	a_len	Length of the line
**	a_str	Address of the line
**
**  IMPLICIT INPUTS/OUTPUTS:
**	None.
**
**  RETURN VALUE:
**	Non-zero when the line carries no printable character.
**--
*/
static	int	s_md$isblank	(
		unsigned	a_len,
	const	char *		a_str
			)
{
	for ( unsigned i = 0; i < a_len; i++ )
		{
		if ( (a_str[i] != ' ') && (a_str[i] != '\t') && (a_str[i] != '\r') )
			return	0;
		}

	return	1;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Strip the leading and the trailing white space of a line, in place
**	by adjusting the pointer and the length.
**
**  FORMAL PARAMETERS:
**
**	a_len	Length of the line, updated in place
**	a_str	Address of the line, updated in place
**
**  IMPLICIT INPUTS/OUTPUTS:
**	None.
**
**  RETURN VALUE:
**	STS$K_SUCCESS always.
**--
*/
static	int	s_md$trim	(
		unsigned *	a_len,
	const	char **		a_str
			)
{
const char *	l_ptr = *a_str;
unsigned	l_len = *a_len;

	while ( l_len && ((*l_ptr == ' ') || (*l_ptr == '\t')) )
		{
		l_ptr++;
		l_len--;
		}

	while ( l_len && ((l_ptr[l_len - 1] == ' ') || (l_ptr[l_len - 1] == '\t') || (l_ptr[l_len - 1] == '\r')) )
		l_len--;

	*a_str	= l_ptr;
	*a_len	= l_len;

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Recognise an ATX heading and return its level and its text.  A run
**	of one to HLB$K_MAXLVL number signs followed by white space opens a
**	heading; the closing run of number signs, if any, is discarded.
**
**  FORMAL PARAMETERS:
**
**	a_len	Length of the line
**	a_str	Address of the line
**	a_level	Receives the nesting level, 1 .. HLB$K_MAXLVL
**	a_txt	Receives the address of the heading text
**	a_txtlen Receives the length of the heading text
**
**  IMPLICIT INPUTS/OUTPUTS:
**	None.
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- the line is a heading;
**	STS$K_WARN	- the line is not a heading;
**	HELP$_TOODEEP	- more number signs than HLB$K_MAXLVL, the level is
**			  clamped and the heading is still returned.
**--
*/
static	int	s_md$heading	(
		unsigned	a_len,
	const	char *		a_str,
		unsigned *	a_level,
	const	char **		a_txt,
		unsigned *	a_txtlen
			)
{
const char *	l_ptr = a_str;
unsigned	l_len = a_len, l_lvl = 0;
int		l_status = STS$K_SUCCESS;

	s_md$trim(&l_len, &l_ptr);

	while ( (l_lvl < l_len) && (l_ptr[l_lvl] == '#') )
		l_lvl++;

	/* A heading needs at least one number sign and white space, or nothing, after the run */
	if ( !l_lvl || ((l_lvl < l_len) && (l_ptr[l_lvl] != ' ') && (l_ptr[l_lvl] != '\t')) )
		return	STS$K_WARN;

	*a_level = l_lvl;

	if ( l_lvl > HLB$K_MAXLVL )
		{
		*a_level = HLB$K_MAXLVL;
		l_status = HELP$_TOODEEP;
		}

	l_ptr	+= l_lvl;
	l_len	-= l_lvl;

	/* Drop the optional closing run of number signs */
	while ( l_len && (l_ptr[l_len - 1] == '#') )
		l_len--;

	s_md$trim(&l_len, &l_ptr);

	*a_txt		= l_ptr;
	*a_txtlen	= l_len;

	return	l_status;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Recognise a translator directive written as an HTML comment:
**
**	    <!-- name -->		or	<!-- name: value -->
**
**  FORMAL PARAMETERS:
**
**	a_len		Length of the line
**	a_str		Address of the line
**	a_name		Name of the directive to be matched, ASCIZ, lower case
**	a_val		Receives the address of the value, may be NULL
**	a_vallen	Receives the length of the value, may be NULL
**
**  IMPLICIT INPUTS/OUTPUTS:
**	None.
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- the line is the requested directive;
**	STS$K_WARN	- it is not.
**--
*/
static	int	s_md$directive	(
		unsigned	a_len,
	const	char *		a_str,
	const	char *		a_name,
	const	char **		a_val,
		unsigned *	a_vallen
			)
{
const char *	l_ptr = a_str;
unsigned	l_len = a_len, l_namelen = (unsigned) strlen(a_name);

	s_md$trim(&l_len, &l_ptr);

	if ( (l_len < 7) || memcmp(l_ptr, "<!--", 4) || memcmp(l_ptr + l_len - 3, "-->", 3) )
		return	STS$K_WARN;

	l_ptr	+= 4;
	l_len	-= 7;

	s_md$trim(&l_len, &l_ptr);

	if ( (l_len < l_namelen) || strncasecmp(l_ptr, a_name, l_namelen) )
		return	STS$K_WARN;

	l_ptr	+= l_namelen;
	l_len	-= l_namelen;

	s_md$trim(&l_len, &l_ptr);

	/* A value, when present, is separated from the name by a colon */
	if ( l_len && (*l_ptr == ':') )
		{
		l_ptr++;
		l_len--;

		s_md$trim(&l_len, &l_ptr);
		}
	else if ( l_len )
		return	STS$K_WARN;		/* Trailing garbage - not our directive	*/

	if ( a_val )
		*a_val = l_ptr;

	if ( a_vallen )
		*a_vallen = l_len;

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Render one line of Markdown into plain text: remove the strong
**	emphasis markers and the code back-quotes, unwrap the links, undo
**	the backslash escapes and turn a list bullet into 'o'.  The leading
**	indent of the line is preserved, so nested lists and indented code
**	keep their shape.
**
**  FORMAL PARAMETERS:
**
**	a_srclen	Length of the source line
**	a_src		Address of the source line
**	a_dstsz		Capacity of the destination
**	a_dst		Receives the rendered line
**	a_dstlen	Receives the length of the rendered line
**
**  IMPLICIT INPUTS/OUTPUTS:
**	None.
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- the line has been rendered;
**	STS$K_WARN	- the result has been truncated.
**--
*/
static	int	s_md$render	(
		unsigned	a_srclen,
	const	char *		a_src,
		unsigned	a_dstsz,
		char *		a_dst,
		unsigned *	a_dstlen
			)
{
unsigned	l_src = 0, l_dst = 0, l_ind = 0;
int		l_status = STS$K_SUCCESS;

	*a_dstlen = 0;

	/* Copy the indent verbatim, then look at the first printable character */
	while ( (l_src < a_srclen) && ((a_src[l_src] == ' ') || (a_src[l_src] == '\t')) && (l_dst < a_dstsz) )
		a_dst [l_dst++] = a_src [l_src++];

	l_ind = l_dst;

	/* A list bullet becomes the 'o' of the OpenVMS help style */
	if ( ((l_src + 1) < a_srclen) && ((a_src[l_src] == '-') || (a_src[l_src] == '*') || (a_src[l_src] == '+'))
		&& (a_src[l_src + 1] == ' ') && (l_dst < a_dstsz) )
		{
		a_dst [l_dst++] = 'o';
		l_src++;
		}

	/* A block quote marker carries no meaning in a plain text display */
	if ( (l_src < a_srclen) && (a_src[l_src] == '>') )
		{
		l_src++;

		if ( (l_src < a_srclen) && (a_src[l_src] == ' ') )
			l_src++;
		}

	while ( (l_src < a_srclen) && (l_dst < a_dstsz) )
		{
		/* A backslash escapes the next character, whatever it is */
		if ( (a_src[l_src] == '\\') && ((l_src + 1) < a_srclen) )
			{
			a_dst [l_dst++] = a_src [++l_src];
			l_src++;
			continue;
			}

		/* Code back-quotes leave the text they delimit in place */
		if ( a_src[l_src] == '`' )
			{
			l_src++;
			continue;
			}

		/* Strong emphasis, both spellings */
		if ( ((l_src + 1) < a_srclen)
			&& (((a_src[l_src] == '*') && (a_src[l_src + 1] == '*'))
			 || ((a_src[l_src] == '_') && (a_src[l_src + 1] == '_'))) )
			{
			l_src += 2;
			continue;
			}

		/* An image collapses to its alternate text, a link to "text (url)" */
		if ( (a_src[l_src] == '!') && ((l_src + 1) < a_srclen) && (a_src[l_src + 1] == '[') )
			{
			l_src++;
			continue;
			}

		if ( a_src[l_src] == '[' )
			{
		const	char *	l_close = memchr(a_src + l_src, ']', a_srclen - l_src);
		unsigned	l_txtlen;

			if ( !l_close )
				{
				a_dst [l_dst++] = a_src [l_src++];
				continue;
				}

			l_txtlen = (unsigned) (l_close - (a_src + l_src)) - 1;

			for ( unsigned i = 0; (i < l_txtlen) && (l_dst < a_dstsz); i++ )
				a_dst [l_dst++] = a_src [l_src + 1 + i];

			l_src += l_txtlen + 2;

			/* Drop the target of the link - a plain text display cannot follow it */
			if ( (l_src < a_srclen) && (a_src[l_src] == '(') )
				{
			const char *	l_paren = memchr(a_src + l_src, ')', a_srclen - l_src);

				l_src = l_paren ? (unsigned) (l_paren - a_src) + 1 : a_srclen;
				}

			continue;
			}

		a_dst [l_dst++] = a_src [l_src++];
		}

	if ( l_src < a_srclen )
		l_status = STS$K_WARN;

	/* A line that has become nothing but its indent is a blank line */
	if ( l_dst == l_ind )
		l_dst = 0;

	while ( l_dst && ((a_dst[l_dst - 1] == ' ') || (a_dst[l_dst - 1] == '\t') || (a_dst[l_dst - 1] == '\r')) )
		l_dst--;

	*a_dstlen = l_dst;

	return	l_status;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Derive the key of a topic from the text of its heading: render the
**	markup away, cut the descriptive tail at the first separator, trim
**	and collapse the internal white space.  The result is upper-cased
**	by HLB$BLD_ADDKEY, not here.
**
**	The routine is exported because MDGEN.C has to ask the very same
**	question in reverse - whether a heading built out of a key would
**	yield that key back - and the only trustworthy answer is the one
**	given by this code rather than by a second copy of its rules.
**
**  FORMAL PARAMETERS:
**
**	a_srclen	Length of the heading text
**	a_src		Address of the heading text
**	a_dstsz		Capacity of the destination
**	a_dst		Receives the key
**	a_dstlen	Receives the length of the key
**
**  IMPLICIT INPUTS/OUTPUTS:
**	None.
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- a key has been derived;
**	HELP$_LONGKEY	- the key has been truncated to <a_dstsz>;
**	HELP$_NOKEY	- nothing printable is left, no key can be made.
**--
*/
int	md$mkkey	(
		unsigned	a_srclen,
	const	char *		a_src,
		unsigned	a_dstsz,
		char *		a_dst,
		unsigned *	a_dstlen
			)
{
char		l_ren [HELP$K_SZ_LINE];
const char *	l_ptr = l_ren, *l_sep;
unsigned	l_len = 0, l_dst = 0;
int		l_status = STS$K_SUCCESS;

	*a_dstlen = 0;

	s_md$render(a_srclen, a_src, (unsigned) sizeof(l_ren), l_ren, &l_len);
	s_md$trim(&l_len, &l_ptr);

	/* Cut the descriptive tail off */
	for ( int i = 0; i < (int) $ARRSZ(s_md$keysep); i++ )
		{
		if ( (l_sep = __util$strstr((char *) l_ptr, l_len, (char *) s_md$keysep[i], strlen(s_md$keysep[i]))) )
			l_len = (unsigned) (l_sep - l_ptr);
		}

	s_md$trim(&l_len, &l_ptr);

	/* Copy collapsing the runs of white space into a single blank */
	for ( unsigned i = 0; (i < l_len) && (l_dst < a_dstsz); i++ )
		{
		if ( (l_ptr[i] != ' ') && (l_ptr[i] != '\t') )
			{
			a_dst [l_dst++] = l_ptr[i];
			continue;
			}

		if ( l_dst && (a_dst[l_dst - 1] != ' ') )
			a_dst [l_dst++] = ' ';
		}

	while ( l_dst && (a_dst[l_dst - 1] == ' ') )
		l_dst--;

	*a_dstlen = l_dst;

	if ( !l_dst )
		return	HELP$_NOKEY;

	if ( l_dst < l_len )
		l_status = HELP$_LONGKEY;

	return	l_status;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Translate a Markdown file into the topic tree of the library being
**	built.  The whole file is read into memory and scanned line by
**	line; headings open topics, everything else is rendered and
**	appended to the topic that is currently open.
**
**	Blank lines are held back and emitted only when a printable line
**	follows, which drops the leading and the trailing white space of
**	every topic without a second pass.
**
**  FORMAL PARAMETERS:
**
**	a_bld		The builder context the topics are added to
**	a_fspec		File specification of the Markdown source
**	a_errcnt	Receives the number of the diagnostics issued
**
**  IMPLICIT INPUTS/OUTPUTS:
**	Adds topics and text to the builder context.
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- the file has been translated, possibly with
**			  warnings counted in <a_errcnt>;
**	a condition of HELP$READFILE / HLB$BLD_ADDKEY otherwise, already
**	signalled.
**--
*/
int	md$translate	(
		HLB$BLD *	a_bld,
	const	char *		a_fspec,
		unsigned *	a_errcnt
			)
{
char *		l_img = NULL;
const char *	l_line, *l_eol, *l_txt, *l_val, *l_ptr;
char		l_ren [HELP$K_SZ_LINE], l_key [HLB$K_SZ_KEY];
unsigned	l_imglen = 0, l_linelen, l_txtlen, l_vallen, l_renlen, l_keylen, l_level;
unsigned	l_lineno = 0, l_pending = 0, l_errs = 0;
int		l_status, l_fence = 0, l_ignore = 0, l_skiptop = 0, l_pendkey = 0;

	*a_errcnt = 0;

	if ( !(1 & (l_status = help$readfile(a_fspec, &l_img, &l_imglen))) )
		return	l_status;

	l_key[0] = '\0';

	for ( l_line = l_img; l_line < (l_img + l_imglen); l_line = l_eol + 1 )
		{
		l_eol = memchr(l_line, '\n', (size_t) (l_img + l_imglen - l_line));

		if ( !l_eol )
			l_eol = l_img + l_imglen;

		l_linelen = (unsigned) (l_eol - l_line);
		l_lineno++;

		/*
		**  Inside an explicitly ignored region nothing at all is
		**  interpreted - only the directive that closes it.
		*/
		if ( l_ignore )
			{
			if ( 1 & s_md$directive(l_linelen, l_line, "resume", NULL, NULL) )
				l_ignore = 0;

			continue;
			}

		/*
		**  A fence toggles the verbatim mode.  Inside it nothing is
		**  interpreted - not a heading, not a directive, not a bullet.
		*/
		l_ptr	= l_line;
		l_txtlen = l_linelen;

		s_md$trim(&l_txtlen, &l_ptr);

		if ( (l_txtlen >= 3) && (!memcmp(l_ptr, "```", 3) || !memcmp(l_ptr, "~~~", 3)) )
			{
			l_fence = !l_fence;
			continue;
			}

		if ( l_fence )
			{
			if ( l_skiptop || !a_bld->curr )
				continue;

			/* Hold a blank line back until a printable one follows it */
			if ( s_md$isblank(l_linelen, l_line) )
				{
				l_pending++;
				continue;
				}

			/* Blank lines that lead a topic are dropped, not held */
			for ( ; l_pending; l_pending-- )
				{
				if ( a_bld->curr && a_bld->curr->txtlen )
					hlb$bld_addtext(a_bld, 1, "\n");
				}

			/* A carriage return of a CRLF source is not part of the text */
			l_txtlen = l_linelen;

			while ( l_txtlen && (l_line[l_txtlen - 1] == '\r') )
				l_txtlen--;

			hlb$bld_addtext(a_bld, MD$K_CODEIND, s_md$codeind);
			hlb$bld_addtext(a_bld, l_txtlen, l_line);
			hlb$bld_addtext(a_bld, 1, "\n");

			continue;
			}

		/*
		**  The directives.  IDENT and KEY carry a value, IGNORE does not.
		*/
		if ( 1 & s_md$directive(l_linelen, l_line, "ident", &l_val, &l_vallen) )
			{
			/* /IDENT was given, and it outranks the source - see HLB$BLD_CREATE */
			if ( a_bld->identset )
				continue;

			l_vallen = $MIN(l_vallen, (unsigned) sizeof(a_bld->ident) - 1);

			memcpy(a_bld->ident, l_val, l_vallen);
			a_bld->ident [l_vallen] = '\0';

			continue;
			}

		if ( 1 & s_md$directive(l_linelen, l_line, "key", &l_val, &l_vallen) )
			{
			l_keylen = $MIN(l_vallen, (unsigned) sizeof(l_key));

			memcpy(l_key, l_val, l_keylen);
			l_pendkey = (int) l_keylen;

			continue;
			}

		if ( 1 & s_md$directive(l_linelen, l_line, "ignore", NULL, NULL) )
			{
			l_ignore = 1;
			continue;
			}

		if ( 1 & s_md$directive(l_linelen, l_line, "resume", NULL, NULL) )
			continue;			/* Already outside a region	*/

		/*
		**  A heading opens a new topic.
		*/
		l_status = s_md$heading(l_linelen, l_line, &l_level, &l_txt, &l_txtlen);

		if ( 1 & l_status )
			{
			l_skiptop = 0;

			if ( $SEV(l_status) == STS$K_ERROR )
				{
				help$putmsg(HELP$_TOODEEP, 4, (fao_prm_t) a_fspec, (fao_prm_t) l_lineno,
						(fao_prm_t) l_level, (fao_prm_t) HLB$K_MAXLVL);
				l_errs++;
				}

			/* An explicit <!-- key: --> wins over the text of the heading */
			if ( l_pendkey )
				{
				l_keylen	= (unsigned) l_pendkey;
				l_pendkey	= 0;
				}
			else	{
				l_status = md$mkkey(l_txtlen, l_txt, (unsigned) sizeof(l_key), l_key, &l_keylen);

				if ( $SEV(l_status) == STS$K_WARN )
					{
					help$putmsg(HELP$_LONGKEY, 3, (fao_prm_t) a_fspec, (fao_prm_t) l_lineno,
							(fao_prm_t) sizeof(l_key));
					l_errs++;
					}

				if ( $STS_ERROR(l_status) )
					{
					help$putmsg(HELP$_NOKEY, 2, (fao_prm_t) a_fspec, (fao_prm_t) l_lineno);
					l_errs++;
					l_skiptop = 1;

					continue;
					}
				}

			/* A level that jumps over an intermediate one is reported and clamped */
			if ( l_level > ((a_bld->curr ? a_bld->curr->level : 0) + 1) )
				{
				help$putmsg(HELP$_SKIPLVL, 4, (fao_prm_t) a_fspec, (fao_prm_t) l_lineno,
						(fao_prm_t) (a_bld->curr ? a_bld->curr->level : 0), (fao_prm_t) l_level);
				l_errs++;
				}

			l_status = hlb$bld_addkey(a_bld, l_level, l_keylen, l_key, NULL);

			/*
			**  The nesting ran out and the level was clamped.  The
			**  topic exists all the same, so this is a diagnostic and
			**  not a reason to abandon the translation - it is
			**  answered before the general test for a failure below,
			**  which would otherwise take it for one.
			*/
			if ( $MSG(l_status) == $MSG(HELP$_TOODEEP) )
				{
				help$putmsg(HELP$_TOODEEP, 4, (fao_prm_t) a_fspec, (fao_prm_t) l_lineno,
						(fao_prm_t) l_level, (fao_prm_t) HLB$K_MAXLVL);
				l_errs++;
				}
			else if ( $STS_ERROR(l_status) )
				{
				free(l_img);
				*a_errcnt = l_errs;

				return	l_status;
				}

			if ( $MSG(l_status) == $MSG(HELP$_DUPKEY) )
				{
				l_key [$MIN(l_keylen, (unsigned) sizeof(l_key) - 1)] = '\0';

				help$putmsg(HELP$_DUPKEY, 3, (fao_prm_t) a_fspec, (fao_prm_t) l_lineno,
						(fao_prm_t) l_key);
				l_errs++;
				}

			l_pending = 0;

			continue;
			}

		if ( l_skiptop )
			continue;

		/*
		**  An ordinary line of the body.
		*/
		if ( !a_bld->curr )
			{
			if ( !s_md$isblank(l_linelen, l_line) )
				{
				help$putmsg(HELP$_NOTOP, 2, (fao_prm_t) a_fspec, (fao_prm_t) l_lineno);
				l_errs++;
				l_skiptop = 1;
				}

			continue;
			}

		if ( s_md$isblank(l_linelen, l_line) )
			{
			l_pending++;
			continue;
			}

		/* A line too long for the render buffer loses its tail - say so */
		if ( !(1 & s_md$render(l_linelen, l_line, (unsigned) sizeof(l_ren), l_ren, &l_renlen)) )
			{
			help$putmsg(HELP$_LONGLINE, 3, (fao_prm_t) a_fspec, (fao_prm_t) l_lineno,
					(fao_prm_t) sizeof(l_ren));
			l_errs++;
			}

		/* A horizontal rule renders to nothing and is dropped altogether */
		if ( !l_renlen )
			{
			l_pending++;
			continue;
			}

		/* Blank lines that lead a topic are dropped, not held */
		for ( ; l_pending; l_pending-- )
			{
			if ( a_bld->curr->txtlen )
				hlb$bld_addtext(a_bld, 1, "\n");
			}

		hlb$bld_addtext(a_bld, l_renlen, l_ren);
		hlb$bld_addtext(a_bld, 1, "\n");
		}

	free(l_img);

	*a_errcnt = l_errs;

	$TRACE("%s: %u lines, %u diagnostics", a_fspec, l_lineno, l_errs);

	return	STS$K_SUCCESS;
}
