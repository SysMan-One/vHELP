#define	__MODULE__	"MDGEN"
#define	__IDENT__	"X01-00"
#define	__REV__		"01.00.0"

/*
**++
**
**  FACILITY:	HELP - OpenVMS-style help library subsystem for Linux
**
**  MODULE:	mdgen.c
**
**  ABSTRACT:	The Markdown writer of the HELPTRAN utility: turns the
**		topic tree of a help library back into a Markdown source,
**		the way LIBRARY/EXTRACT recovers a module from an OpenVMS
**		library.
**
**  DESCRIPTION: The generator is the exact reverse of MDPARSE.C and is
**		written to one requirement: the round trip must settle.  A
**		library extracted, translated and extracted again has to
**		yield the identical text.  The first translation is lossy -
**		the descriptive tail of a heading, the emphasis markers and
**		the fences do not survive into the library - but from the
**		second pass on nothing changes any more, and that is the
**		property the tests hold the module to.
**
**		Settling costs two things.  A heading is written bare, out
**		of the key alone; when the reader would not give that key
**		back - because the key carries a separator such as " - ",
**		or leads with a number sign - an explicit <!-- key: -->
**		directive precedes it.  The question is put to MD$MKKEY,
**		the reader's own routine, so that the two halves cannot
**		drift apart.
**
**		And the text is escaped: every character the reader
**		consumes - the backslash, the back-quote, the bracket of a
**		link, a doubled emphasis marker, the bang of an image and,
**		at the head of a line, a number sign or the opening of a
**		comment - is protected with a backslash, which the reader
**		strips again.
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

#define	MDGEN$K_SZ_WORK	(4 * HELP$K_SZ_LINE)	/* Assembly buffer of one escaped line	*/


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Escape one line of topic text so that the reader gives it back
**	unchanged.  Only the constructs the reader actually consumes are
**	protected; a lone asterisk or underscore, which the reader passes
**	through, is left alone so that the extracted source stays legible.
**
**  FORMAL PARAMETERS:
**
**	a_srclen	Length of the line, bytes
**	a_src		Address of the line
**	a_dstsz		Capacity of the destination
**	a_dst		Receives the escaped line
**	a_dstlen	Receives the length of the escaped line
**
**  IMPLICIT INPUTS/OUTPUTS:
**	None.
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- the line has been escaped;
**	STS$K_WARN	- the result has been truncated.
**--
*/
static	int	s_mdgen$escape	(
		unsigned	a_srclen,
	const	char *		a_src,
		unsigned	a_dstsz,
		char *		a_dst,
		unsigned *	a_dstlen
			)
{
unsigned	l_src = 0, l_dst = 0, l_ind = 0;

	*a_dstlen = 0;

	/* The indent is copied first - the reader treats it the same way */
	while ( (l_src < a_srclen) && ((a_src[l_src] == ' ') || (a_src[l_src] == '\t')) && (l_dst < a_dstsz) )
		a_dst [l_dst++] = a_src [l_src++];

	l_ind = l_src;

	/*
	**  At the head of the text a number sign would open a heading, a
	**  comment would open a directive and a fence would open a verbatim
	**  block - each has to lose its meaning.
	*/
	if ( (l_src < a_srclen) && (l_dst < (a_dstsz - 1)) )
		{
		if ( (a_src[l_src] == '#')
			|| ((a_srclen - l_src >= 3) && (!memcmp(a_src + l_src, "```", 3)
							|| !memcmp(a_src + l_src, "~~~", 3)))
			|| ((a_srclen - l_src >= 4) && !memcmp(a_src + l_src, "<!--", 4)) )
			a_dst [l_dst++] = '\\';
		}

	while ( (l_src < a_srclen) && (l_dst < (a_dstsz - 1)) )
		{
		switch ( a_src [l_src] )
			{
			case '\\':			/* Introduces an escape		*/
			case '`':			/* Delimits a code span		*/
			case '[':			/* Opens a link			*/
				a_dst [l_dst++] = '\\';
				break;

			case '_':			/* Doubled - strong emphasis	*/
				if ( ((l_src + 1) < a_srclen) && (a_src[l_src + 1] == '_') )
					a_dst [l_dst++] = '\\';
				break;

			case '*':			/* Doubled - strong emphasis;	*/
			case '-':			/* at the head of the text and	*/
			case '+':			/* followed by a blank - a list	*/
				if ( ((l_src + 1) < a_srclen) && (a_src[l_src + 1] == a_src[l_src])
					&& (a_src[l_src] == '*') )
					a_dst [l_dst++] = '\\';
				else if ( (l_src == l_ind) && ((l_src + 1) < a_srclen) && (a_src[l_src + 1] == ' ') )
					a_dst [l_dst++] = '\\';
				break;

			case '>':			/* Leads a block quote		*/
				if ( l_src == l_ind )
					a_dst [l_dst++] = '\\';
				break;

			case '!':			/* Followed by '[' - an image	*/
				if ( ((l_src + 1) < a_srclen) && (a_src[l_src + 1] == '[') )
					a_dst [l_dst++] = '\\';
				break;

			default:
				break;
			}

		a_dst [l_dst++] = a_src [l_src++];
		}

	*a_dstlen = l_dst;

	return	(l_src == a_srclen) ? STS$K_SUCCESS : STS$K_WARN;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Write the heading of a topic, preceded by an explicit key directive
**	whenever the reader would not derive that key from the heading text
**	on its own.
**
**  FORMAL PARAMETERS:
**
**	a_out	The output context
**	a_key	The key record of the topic
**	a_level	The nesting level to be written, 1 .. HLB$K_MAXLVL
**
**  IMPLICIT INPUTS/OUTPUTS:
**	Writes to the output context.
**
**  RETURN VALUE:
**	STS$K_SUCCESS always.
**--
*/
static	int	s_mdgen$heading	(
		HELP$OUT *	a_out,
		HLB$KEY *	a_key,
		unsigned	a_level
			)
{
char		l_back [HLB$K_SZ_KEY];
unsigned	l_backlen = 0;
int		l_status;

	/* Would the reader recover this very key out of a bare heading? */
	l_status = md$mkkey(a_key->keylen, a_key->key, (unsigned) sizeof(l_back), l_back, &l_backlen);

	if ( !(1 & l_status) || (l_backlen != a_key->keylen) || memcmp(l_back, a_key->key, l_backlen) )
		/* '!' opens an FAO directive - a literal one is written as '!!' */
		help$put(a_out, "<!!-- key: !AD -->\n", 2, (fao_prm_t) a_key->keylen, (fao_prm_t) a_key->key);

	for ( unsigned i = 0; i < a_level; i++ )
		help$put(a_out, "#", 0);

	help$put(a_out, " !AD\n\n", 2, (fao_prm_t) a_key->keylen, (fao_prm_t) a_key->key);

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Write the topic tree of a library, or one subtree of it, as a
**	Markdown source.
**
**	The key index is stored in pre-order, so a subtree is the run that
**	begins at its root and ends at the first key that is not deeper
**	than the root - which makes the whole traversal a single forward
**	scan with no stack of its own.  When a subtree is extracted its
**	root is lifted to level one, so that the result is a source in its
**	own right rather than a fragment.
**
**  FORMAL PARAMETERS:
**
**	a_lib	The library
**	a_root	Index of the topic to be extracted, -1 - the whole library
**	a_out	The output context
**	a_cnt	Receives the number of the topics written, may be NULL
**
**  IMPLICIT INPUTS/OUTPUTS:
**	Writes to the output context.
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- the source has been written;
**	STS$K_WARN	- the root is out of range, nothing written.
**--
*/
int	md$generate	(
		HLB$LIB *	a_lib,
		int		a_root,
		HELP$OUT *	a_out,
		unsigned *	a_cnt
			)
{
HLB$KEY *	l_key, *l_root = NULL;
const char *	l_txt = NULL, *l_ptr, *l_eol;
char *		l_work;
unsigned	l_txtlen = 0, l_linelen, l_worklen, l_cnt = 0, l_shift = 0;
int		l_first = 0, l_last = (int) a_lib->keycnt;

	if ( a_cnt )
		*a_cnt = 0;

	if ( a_root >= 0 )
		{
		if ( !(1 & hlb$getkey(a_lib, a_root, &l_root)) )
			return	STS$K_WARN;

		l_first	= a_root;
		l_shift	= l_root->level - 1;

		/* The subtree ends where the indentation returns to that of its root */
		for ( l_last = a_root + 1; l_last < (int) a_lib->keycnt; l_last++ )
			{
			if ( !(1 & hlb$getkey(a_lib, l_last, &l_key)) || (l_key->level <= l_root->level) )
				break;
			}
		}

	if ( !(l_work = calloc(1, MDGEN$K_SZ_WORK)) )
		return	help$putmsg(HELP$_NOMEM, 3, (fao_prm_t) MDGEN$K_SZ_WORK, (fao_prm_t) 0,
					(fao_prm_t) "cannot allocate the work area");

	/* The identification belongs to the library as a whole, not to a subtree */
	if ( (a_root < 0) && a_lib->hdr->ident[0] )
		help$put(a_out, "<!!-- ident: !AZ -->\n\n", 1, (fao_prm_t) a_lib->hdr->ident);

	for ( int i = l_first; i < l_last; i++ )
		{
		if ( !(1 & hlb$getkey(a_lib, i, &l_key)) )
			break;

		s_mdgen$heading(a_out, l_key, l_key->level - l_shift);
		l_cnt++;

		if ( !(1 & hlb$gettext(a_lib, i, &l_txtlen, &l_txt)) )
			continue;

		for ( l_ptr = l_txt; l_ptr < (l_txt + l_txtlen); l_ptr += l_linelen + 1 )
			{
			l_eol = memchr(l_ptr, '\n', (size_t) (l_txt + l_txtlen - l_ptr));

			if ( !l_eol )
				l_eol = l_txt + l_txtlen;

			l_linelen = (unsigned) (l_eol - l_ptr);

			s_mdgen$escape(l_linelen, l_ptr, MDGEN$K_SZ_WORK, l_work, &l_worklen);

			help$put_raw(a_out, l_worklen, l_work);
			help$put_raw(a_out, 1, "\n");
			}

		help$put_raw(a_out, 1, "\n");
		}

	free(l_work);

	if ( a_cnt )
		*a_cnt = l_cnt;

	$TRACE("root=%d, keys %d..%d, %u topic(s) written", a_root, l_first, l_last, l_cnt);

	return	STS$K_SUCCESS;
}
