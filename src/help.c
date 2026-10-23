#define	__MODULE__	"HELP"
#define	__IDENT__	"X01-13"
#define	__REV__		"01.13.0"

/*
**++
**
**  FACILITY:	HELP - OpenVMS-style help library subsystem for Linux
**
**  MODULE:	help.c
**
**  ABSTRACT:	The HELP utility - navigation and display of a help
**		library, after the DCL HELP command of OpenVMS.
**
**  DESCRIPTION: Command syntax:
**
**		    <image> [topic [subtopic ...]] [/qualifiers]
**
**		    /LIBRARY=spec	the library to be read, or the
**					search path to look along; by
**					default the path is composed of
**					HELP_LIBRARY, $HOME/.help and the
**					directory the product was installed
**					into
**		    /[NO]PAGE		stop, or do not stop, at the end of
**					a page
**		    /[NO]PROMPT		enter, or do not enter, the
**					interactive dialogue
**		    /WIDTH=n		page width, overrides the terminal
**		    /LIST		list the whole topic tree
**		    /CATALOG		list the search path and its libraries
**		    /[NO]CACHE		use, or bypass, the catalogue cache
**		    /SYSCACHE		build the catalogue shared by every
**					user of the system - an administrative
**					action, performed by the installation
**		    /HELP		display the syntax
**
**		A key may be spelled exactly like a qualifier of the
**		utility - /LIST is both - and the qualifier wins.  A lone
**		"--" ends the qualifiers, so
**
**		    $ vhelp -- vhelp /list
**
**		asks for the topic /LIST of the topic HELP rather than for
**		a listing of the whole library.
**
**		A topic that carries a wildcard - '*' matches any run of
**		characters, '%' matches one - displays every subtopic that
**		matches it.  An unambiguous leading abbreviation of a key is
**		accepted in place of the key; an ambiguous one is treated as
**		if it were followed by an asterisk, so
**
**		    $ vhelp sub
**
**		displays SUBMIT and SUBTITLE both when the library defines
**		them both.
**
**		At the prompt an empty line moves one level up, a question
**		mark redisplays the current topic, end of file or EXIT ends
**		the session.  Paging and prompting are enabled only when the
**		output really is a terminal, so a redirected run never waits
**		for a keystroke.  Q at the paging prompt drops the rest of
**		the topic and returns to the prompt of the level the session
**		stands on, its subtopics listed again; end of file there
**		ends the session.
**
**  AUTHOR:	StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:  31-JUL-2026
**
**  MODIFICATION HISTORY:
**
**	X01-13		23-OCT-2026	RRL
**		/LIBRARY and HELP_LIBRARY tell a search path from a single
**		library by HELP$K_PATHSEP rather than by a ':' of their own -
**		on Windows (';' there) every C:\... would have been a path.
**
**	X01-12		 4-OCT-2026	CLD
**		Q at the paging prompt left ABORT of the output context
**		raised, so the dialogue's own "Subtopic?" prompt was
**		swallowed as part of the dropped topic and the next line
**		typed was looked up as a subtopic - "Q" gave NOTFOUND on
**		"<topic> Q".  The dialogue now clears it before prompting
**		and lists the subtopics of the current level once more; end
**		of file at the paging prompt ends the session.  RRL: "after
**		q show the current level".
**
**	X01-11		 1-SEP-2026	RRL
**		The set of wildcard characters is named in HELPDEF.H rather
**		than written here: '?' joined '%' as a match for any one
**		character in UTILITY$ROUTINES V.01-29, and the utility went
**		on looking a topic carrying it up as a name.
**
**	X01-10		 1-AUG-2026	RRL
**		/SYSCACHE, and the shared catalogue it writes: the
**		libraries of a system directory are harvested once, at
**		installation, and every user takes them from there.  The
**		search path carries the translation of the manual when
**		the locale asks for one, and an element ending in "..."
**		is scanned to the depth of its tree.
**
**	X01-09		 1-AUG-2026	RRL
**		A qualifier that does not fit the command array is
**		announced with %HELP-W-TOOMANYQ rather than dropped in
**		silence, and so is a topic word, with %HELP-W-TOOMANYW.
**		A key may carry blanks, so a path of nine levels can be
**		a good deal more than nine words - the vector held
**		sixteen, and a longer path was truncated without a word,
**		displaying a topic above the one asked for and reporting
**		success.  It holds sixty-four now, and says so when that
**		is not enough.
**
**	X01-08		 1-AUG-2026	RRL
**		The logging of the UTILITY$ROUTINES is directed onto the
**		standard error before anything is emitted: its default
**		destination is the standard output, which carries a paged
**		topic here.
**
**	X01-07		 1-AUG-2026	RRL
**		The verb shown to the CLI service is spelled HELP again -
**		CLI_ROUTINES X.00-11 intercepts only the dashed and
**		slashed forms of a syntax request.
**
**	X01-06		 1-AUG-2026	RRL
**		The libraries of a search path are found through a
**		catalogue, which is cached; /CATALOG and /[NO]CACHE.
**		/LIBRARY may name a search path, and when it does it is
**		the whole of it.
**
**	X01-05		 1-AUG-2026	RRL
**		The verb shown to the CLI service is no longer spelled
**		HELP: CLI_ROUTINES X.00-09 intercepts that spelling and
**		prints its own summary instead of parsing.
**
**	X01-04		 1-AUG-2026	RRL
**		The command is split before it is parsed - CLI_ROUTINES
**		X.00-07 refuses an unrecognized qualifier, and a topic is
**		routinely spelled like one.  /PAGE and /PROMPT are
**		negatable, so both forms are accepted; presence is taken
**		from __cli$present rather than from a valueless
**		__cli$get_value.
**
**	X01-03		31-JUL-2026	RRL
**		A lone "--" ends the qualifiers, so that a key spelled
**		like one - /LIST - can be asked for from the command
**		line.
**
**	X01-02		31-JUL-2026	RRL
**		Keys that carry a blank are addressed as a run of words,
**		quoted or not; the subtopic block aligns on the width in
**		characters, so a Cyrillic key keeps its column.
**
**	X01-01		31-JUL-2026	RRL
**		/LIST displays the creation time and the producer of the
**		library, after UTILITY$ROUTINES was taken to STARLET
**		X.01-09 (a quadword and a time are passed to FAO by
**		value).
**
**	X01-00		31-JUL-2026	RRL
**		Initial version.
**
**--
*/

#include	<stdlib.h>
#include	<string.h>
#include	<unistd.h>
#include	<sys/stat.h>

#include	"helpdef.h"
#include	"cli_routines.h"

/*
**  HELP$K_DEFLIB - the library the product has been built with - and
**  HELP$K_LOGNAME - the environment variable that overrides it - are
**  fixed at configuration time, see HELPVERS.H.
*/
/*
**  Words accepted in one topic path.  A library nests HLB$K_MAXLVL deep,
**  but a key may carry blanks - SOURCE FORMAT is one key of two words -
**  so a path of nine levels can be a good deal more than nine words.  A
**  key is HLB$K_SZ_KEY bytes at most, which sets no useful bound on the
**  words inside it, so the limit is generous and its overflow is
**  reported rather than assumed harmless.
*/
#define	HELP$K_MAXWORD	64			/* Words accepted in one topic path	*/
#define	HELP$K_COLWIDTH	14			/* Minimum column of the subtopic list	*/

/*
**  The qualifier table of the utility.  An empty name closes the table.
*/
static	CLI_PQDESC	s_quals [] = {
	{ .name = {$ASCINI("LIBRARY")},	.type = CLI$K_QSTRING,	.pn = CLI$K_QUAL },
	{ .name = {$ASCINI("WIDTH")},	.type = CLI$K_NUM,	.pn = CLI$K_QUAL },
	{ .name = {$ASCINI("PAGE")},	.type = CLI$K_OPT,	.pn = CLI$K_QUAL,	.flag = CLI$M_NEGATABLE },
	{ .name = {$ASCINI("PROMPT")},	.type = CLI$K_OPT,	.pn = CLI$K_QUAL,	.flag = CLI$M_NEGATABLE },
	{ .name = {$ASCINI("LIST")},	.type = CLI$K_OPT,	.pn = CLI$K_QUAL },
	{ .name = {$ASCINI("HELP")},	.type = CLI$K_OPT,	.pn = CLI$K_QUAL },
	{ .name = {$ASCINI("CACHE")},	.type = CLI$K_OPT,	.pn = CLI$K_QUAL,	.flag = CLI$M_NEGATABLE },
	{ .name = {$ASCINI("CATALOG")},	.type = CLI$K_OPT,	.pn = CLI$K_QUAL },
	{ .name = {$ASCINI("SYSCACHE")},.type = CLI$K_OPT,	.pn = CLI$K_QUAL },
	{ .name = { .len = 0 } }
	};

static	CLI_VERB	s_verbs [] = {
	{ .name = {$ASCINI(HELP$K_HELPNAME)}, .quals = s_quals },
	{ .name = { .len = 0 } }
	};

static	const char	s_usage [] = {
	"Usage: " HELP$K_HELPNAME " [topic [subtopic ...]] [/qualifiers]\n"
	"\n"
	"    /LIBRARY=filespec  the help library to be read\n"
	"    /WIDTH=n           page width, overrides the terminal\n"
	"    /[NO]PAGE          stop, or do not stop, at the end of a page\n"
	"    /[NO]PROMPT        enter, or do not enter, the dialogue\n"
	"    /LIST              list the whole topic tree\n"
	"    /CATALOG           list the libraries on the search path\n"
	"    /[NO]CACHE         use, or bypass, the catalogue cache\n"
	"    /SYSCACHE          build the catalogue shared by every user\n"
	"    /HELP              display this text\n"
	"\n"
	"A lone -- ends the qualifiers: what follows it is a topic whatever\n"
	"it looks like, which is how a key spelled /LIST is asked for.\n"
	};

/*
**  The synthetic verb put in front of the arguments for the CLI service.
*/
/*
**  The verb the CLI service is shown.  CLI_ROUTINES X.00-09 intercepted
**  any argument spelled HELP and printed a summary of its own tables, so
**  a utility of that very name could not be parsed at all; X.00-11
**  narrowed the interception to the dashed and slashed forms, and the
**  natural name is usable again.
*/
static	char	s_clivrb [] = { HELP$K_HELPNAME };


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Display the list of the subtopics of a key in columns, the way the
**	"Additional information available" block of OpenVMS HELP looks.
**	The column is as wide as the longest key of the chain, never
**	narrower than HELP$K_COLWIDTH, and the number of columns follows
**	the width of the page.
**
**  FORMAL PARAMETERS:
**
**	a_out		The output context
**	a_lib		The library
**	a_parent	Index of the parent, -1 - the top level
**	a_indent	Indent of the block, characters
**	a_header	Heading line of the block, ASCIZ
**
**  IMPLICIT INPUTS/OUTPUTS:
**	Writes to the output context.
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- the list has been displayed;
**	STS$K_WARN	- the key has no subtopics, nothing displayed.
**--
*/
static	int	s_help$subtopics	(
		HELP$OUT *	a_out,
		HLB$LIB *	a_lib,
		int		a_parent,
		unsigned	a_indent,
	const	char *		a_header
			)
{
HLB$KEY *	l_key;
unsigned	l_col = HELP$K_COLWIDTH, l_ncol, l_slot = 0;
int		l_first, l_scan;

	if ( !(1 & hlb$firstchild(a_lib, a_parent, &l_first)) )
		return	STS$K_WARN;

	/*
	**  First pass - the width of the widest key of the chain.  The width
	**  is counted in characters, not in bytes: a Cyrillic key is two
	**  bytes per character and would push the column layout apart.
	*/
	for ( l_scan = l_first; l_scan >= 0; l_scan = l_key->next )
		{
		if ( !(1 & hlb$getkey(a_lib, l_scan, &l_key)) )
			break;

		l_col = $MAX(l_col, help$dsplen(l_key->keylen, l_key->key) + 2);
		}

	l_ncol = (a_out->width > (a_indent + l_col)) ? ((a_out->width - a_indent) / l_col) : 1;

	help$put(a_out, "\n", 0);
	help$put_indent(a_out, a_indent);
	help$put(a_out, "!AZ\n\n", 1, (fao_prm_t) a_header);

	/* Second pass - the keys themselves, l_ncol of them per line */
	for ( l_scan = l_first; l_scan >= 0; l_scan = l_key->next )
		{
		if ( !(1 & hlb$getkey(a_lib, l_scan, &l_key)) )
			break;

		if ( !l_slot )
			help$put_indent(a_out, a_indent);

		help$put(a_out, "!AD", 2, (fao_prm_t) l_key->keylen, (fao_prm_t) l_key->key);

		/* Pad only when another key follows on the same line - no trailing blanks */
		if ( (l_key->next >= 0) && ((l_slot + 1) < l_ncol) )
			help$put_indent(a_out, l_col - help$dsplen(l_key->keylen, l_key->key));

		if ( ++l_slot >= l_ncol )
			{
			help$put(a_out, "\n", 0);
			l_slot = 0;
			}
		}

	if ( l_slot )
		help$put(a_out, "\n", 0);

	help$put(a_out, "\n", 0);

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Display the topics of a catalogue in columns - the root display of
**	a session that spans several libraries, in place of the level 1
**	chain of a single one.
**
**  FORMAL PARAMETERS:
**
**	a_out	The output context
**	a_cat	The catalogue
**
**  IMPLICIT INPUTS/OUTPUTS:
**	Writes to the output context.
**
**  RETURN VALUE:
**	STS$K_SUCCESS always.
**--
*/
static	int	s_help$catroot	(
		HELP$OUT *	a_out,
		HLB$CAT *	a_cat
			)
{
unsigned	l_col = HELP$K_COLWIDTH, l_ncol, l_slot = 0;
int		l_scan = -1, l_next;

	/* First pass - the width of the widest topic of the catalogue */
	while ( 1 & cat$nextkey(a_cat, l_scan, &l_scan) )
		l_col = $MAX(l_col, help$dsplen(a_cat->keys[l_scan].keylen, a_cat->keys[l_scan].key) + 2);

	l_ncol = (a_out->width > (2 + l_col)) ? ((a_out->width - 2) / l_col) : 1;

	help$page_reset(a_out);

	help$put(a_out, "\n", 0);
	help$put_indent(a_out, 2);
	help$put(a_out, "!AZ\n\n", 1, (fao_prm_t) "Information available:");

	for ( l_scan = -1; 1 & cat$nextkey(a_cat, l_scan, &l_scan); )
		{
		if ( !l_slot )
			help$put_indent(a_out, 2);

		help$put(a_out, "!AD", 2, (fao_prm_t) a_cat->keys[l_scan].keylen,
				(fao_prm_t) a_cat->keys[l_scan].key);

		/* Pad only when another topic follows on the same line */
		if ( (1 & cat$nextkey(a_cat, l_scan, &l_next)) && ((l_slot + 1) < l_ncol) )
			help$put_indent(a_out, l_col - help$dsplen(a_cat->keys[l_scan].keylen,
									a_cat->keys[l_scan].key));

		if ( ++l_slot >= l_ncol )
			{
			help$put(a_out, "\n", 0);
			l_slot = 0;
			}
		}

	if ( l_slot )
		help$put(a_out, "\n", 0);

	help$put(a_out, "\n", 0);

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	List the libraries of a catalogue and the topics each answers for -
**	the display of /CATALOG.  A topic hidden by a library earlier on
**	the search path is marked, so that a shadowed library is visible as
**	such rather than merely absent from the answers.
**
**  FORMAL PARAMETERS:
**
**	a_out	The output context
**	a_cat	The catalogue
**
**  IMPLICIT INPUTS/OUTPUTS:
**	Writes to the output context.
**
**  RETURN VALUE:
**	STS$K_SUCCESS always.
**--
*/
static	int	s_help$catalog	(
		HELP$OUT *	a_out,
		HLB$CAT *	a_cat
			)
{
int		l_hidden;

	help$put(a_out, "\nSearch path: !AZ\n", 1, (fao_prm_t) a_cat->path);

	if ( a_cat->cache[0] )
		help$put(a_out, "Catalogue:   !AZ (!AZ!AZ)\n", 3, (fao_prm_t) a_cat->cache,
				(fao_prm_t) (a_cat->cached ? "from the cache" : "rebuilt"),
				(fao_prm_t) (a_cat->shared ? ", the shared catalogue helping" : ""));
	else	help$put(a_out, "Catalogue:   not cached\n", 0);

	for ( unsigned i = 0; i < a_cat->entcnt; i++ )
		{
		/* A directory, or an element that is not there, is not a library */
		if ( a_cat->ents[i].isdir || a_cat->ents[i].absent )
			continue;

		help$put(a_out, "\n  !AZ\n", 1, (fao_prm_t) a_cat->ents[i].fspec);

		for ( unsigned k = 0; k < a_cat->keycnt; k++ )
			{
			if ( a_cat->keys[k].lib != i )
				continue;

			/* Is an earlier library answering for this very topic? */
			l_hidden = 0;

			for ( unsigned j = 0; j < k; j++ )
				{
				if ( (a_cat->keys[j].keylen == a_cat->keys[k].keylen)
					&& !memcmp(a_cat->keys[j].key, a_cat->keys[k].key,
							a_cat->keys[k].keylen) )
					{
					l_hidden = 1;
					break;
					}
				}

			help$put(a_out, "    !AD!AZ\n", 3, (fao_prm_t) a_cat->keys[k].keylen,
					(fao_prm_t) a_cat->keys[k].key,
					(fao_prm_t) (l_hidden ? "   (hidden)" : ""));
			}
		}

	help$put(a_out, "\n", 0);

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Display a topic: the chain of the keys from level 1 down to the
**	topic, each on its own line and indented by its level, then the
**	text of the topic, then the list of its subtopics.
**
**  FORMAL PARAMETERS:
**
**	a_out	The output context
**	a_lib	The library
**	a_index	Index of the topic, -1 - the root of the library
**
**  IMPLICIT INPUTS/OUTPUTS:
**	Resets the pager of the output context.
**
**  RETURN VALUE:
**	STS$K_SUCCESS always.
**--
*/
static	int	s_help$show	(
		HELP$OUT *	a_out,
		HLB$LIB *	a_lib,
		int		a_index
			)
{
HLB$KEY *	l_key;
const char *	l_txt = NULL;
int		l_chain [HLB$K_MAXLVL + 1];
unsigned	l_txtlen = 0, l_indent = 0, l_level = 0;
int		l_depth = 0, l_scan = a_index;

	help$page_reset(a_out);

	if ( a_index < 0 )
		{
		/* The root - the library itself has no text of its own */
		s_help$subtopics(a_out, a_lib, -1, 2, "Information available:");

		return	STS$K_SUCCESS;
		}

	/* Collect the chain of the ancestors, bottom-up */
	while ( (l_scan >= 0) && (l_depth < (int) $ARRSZ(l_chain)) )
		{
		if ( !(1 & hlb$getkey(a_lib, l_scan, &l_key)) )
			break;

		l_chain [l_depth++] = l_scan;
		l_scan = l_key->parent;
		}

	help$put(a_out, "\n", 0);

	/* ... and emit it top-down, each key one level deeper than the previous */
	for ( int i = l_depth - 1; i >= 0; i-- )
		{
		hlb$getkey(a_lib, l_chain[i], &l_key);

		l_level		= l_key->level;
		l_indent	= 2 * l_level;

		help$put_indent(a_out, l_indent);
		help$put(a_out, "!AD\n\n", 2, (fao_prm_t) l_key->keylen, (fao_prm_t) l_key->key);
		}

	if ( 1 & hlb$gettext(a_lib, a_index, &l_txtlen, &l_txt) )
		help$put_text(a_out, l_indent + 2, l_txtlen, l_txt);

	s_help$subtopics(a_out, a_lib, a_index, l_indent, "Additional information available:");

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Display every subtopic of a key whose name matches a pattern.  Used
**	both for an explicit wildcard and for an abbreviation that turned
**	out to be ambiguous.
**
**  FORMAL PARAMETERS:
**
**	a_out		The output context
**	a_lib		The library
**	a_parent	Index of the parent, -1 - the top level
**	a_pattern	The pattern, ASCIZ
**	a_cnt		Receives the number of the topics displayed
**
**  IMPLICIT INPUTS/OUTPUTS:
**	Writes to the output context.
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- at least one topic has been displayed;
**	STS$K_WARN	- nothing matched.
**--
*/
static	int	s_help$wildcard	(
		HELP$OUT *	a_out,
		HLB$LIB *	a_lib,
		int		a_parent,
	const	char *		a_pattern,
		unsigned *	a_cnt
			)
{
HLB$KEY *	l_key;
unsigned	l_cnt = 0;
int		l_scan = -1;

	while ( 1 & hlb$search(a_lib, a_parent, a_pattern, l_scan, &l_scan) )
		{
		s_help$show(a_out, a_lib, l_scan);
		l_cnt++;

		if ( !(1 & hlb$getkey(a_lib, l_scan, &l_key)) || (l_key->next < 0) )
			break;

		l_scan = l_key->next;
		}

	if ( a_cnt )
		*a_cnt = l_cnt;

	return	l_cnt ? STS$K_SUCCESS : STS$K_WARN;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Resolve a sequence of topic names against the library, starting at
**	a given parent, and display what has been found.  Every name but
**	the last selects the parent of the next one; the last one is the
**	topic being asked for.
**
**  FORMAL PARAMETERS:
**
**	a_out		The output context
**	a_lib		The library
**	a_parent	Index of the topic the search starts at, -1 - the root
**	a_wordcnt	Number of the names
**	a_words		The names, ASCIZ
**	a_cur		Receives the index of the topic that has been
**			displayed, so that the dialogue can continue from it
**
**  IMPLICIT INPUTS/OUTPUTS:
**	Writes to the output context.
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- something has been displayed;
**	HELP$_NOTFOUND	- nothing matched, already signalled.
**--
*/
static	int	s_help$navigate	(
		HELP$OUT *	a_out,
		HLB$LIB *	a_lib,
		int		a_parent,
		int		a_wordcnt,
		char **		a_words,
		int *		a_cur
			)
{
char		l_pattern [HLB$K_SZ_KEY + 2], l_path [HELP$K_SZ_PATH];
unsigned	l_cnt = 0, l_pathlen = 0;
int		l_parent = a_parent, l_index = -1, l_status, l_wild, l_used = 1;

	for ( int i = 0; i < a_wordcnt; )
		{
		l_wild	= (NULL != strpbrk(a_words[i], HELP$K_WILDCARD));
		l_status = STS$K_WARN;
		l_used	= 1;

		if ( !l_wild )
			{
			/* A key may carry blanks, so a run of words is offered at once */
			l_status = hlb$lookup_run(a_lib, l_parent, a_wordcnt - i, &a_words[i], &l_used, &l_index);

			if ( !l_used )
				l_used = 1;

			if ( $STS_ERROR(l_status) )
				{
				/* Name the whole path, not just the leaf, so the diagnostic is usable */
				hlb$path(a_lib, l_parent, (unsigned) sizeof(l_path), l_path, &l_pathlen);

				if ( l_pathlen && ((l_pathlen + strlen(a_words[i]) + 2) < sizeof(l_path)) )
					{
					l_path [l_pathlen++] = ' ';
					strcpy(l_path + l_pathlen, a_words[i]);
					}
				else	help$strcpy((unsigned) sizeof(l_path), l_path, a_words[i]);

				return	help$putmsg(HELP$_NOTFOUND, 1, (fao_prm_t) l_path);
				}
			}

		/*
		**  An unambiguous match either becomes the parent of the names
		**  that follow, or - when it consumes the last of them - is the
		**  topic being asked for.
		*/
		if ( !l_wild && (l_status == STS$K_SUCCESS) )
			{
			i += l_used;

			if ( i < a_wordcnt )
				{
				l_parent = l_index;
				continue;
				}

			s_help$show(a_out, a_lib, l_index);

			if ( a_cur )
				*a_cur = l_index;

			return	STS$K_SUCCESS;
			}

		/*
		**  A wildcard, or an ambiguous abbreviation - which is treated
		**  as if an asterisk had been appended to it.
		*/
		help$strcpy((unsigned) sizeof(l_pattern) - 1, l_pattern, a_words[i]);

		if ( !l_wild )
			strcat(l_pattern, "*");

		if ( i < (a_wordcnt - 1) )
			{
			/* In the middle of a path a wildcard selects the first match */
			if ( !(1 & hlb$search(a_lib, l_parent, l_pattern, -1, &l_index)) )
				return	help$putmsg(HELP$_NOTFOUND, 1, (fao_prm_t) a_words[i]);

			l_parent = l_index;
			i++;

			continue;
			}

		if ( !(1 & s_help$wildcard(a_out, a_lib, l_parent, l_pattern, &l_cnt)) )
			return	help$putmsg(HELP$_NOTFOUND, 1, (fao_prm_t) a_words[i]);

		if ( a_cur )
			*a_cur = l_parent;

		return	STS$K_SUCCESS;
		}

	/* No names at all - display the topic the dialogue currently stands on */
	s_help$show(a_out, a_lib, l_parent);

	if ( a_cur )
		*a_cur = l_parent;

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Resolve a request in a session that may span several libraries.
**
**	When a library is already open the request is offered to it first,
**	which keeps a subtopic named like a topic of another library from
**	pulling the session out from under the user.  Only when the open
**	library has no answer - or when none is open - does the catalogue
**	decide which library to turn to, and the library it names is opened
**	in place of the current one.
**
**  FORMAL PARAMETERS:
**
**	a_out		The output context
**	a_cat		The catalogue, NULL when a single library was named
**	a_lib		The library in use, replaced when the session moves
**	a_cur		Index of the current topic, updated
**	a_wordcnt	Number of the names
**	a_words		The names, ASCIZ
**
**  IMPLICIT INPUTS/OUTPUTS:
**	May close the current library and open another.
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- something has been displayed;
**	HELP$_NOTFOUND	- nothing matched, already signalled.
**--
*/
static	int	s_help$session	(
		HELP$OUT *	a_out,
		HLB$CAT *	a_cat,
		HLB$LIB **	a_lib,
		int *		a_cur,
		int		a_wordcnt,
		char **		a_words
			)
{
HLB$LIB *	l_new = NULL;
char		l_fspec [HELP$K_SZ_PATH];
int		l_status;

	/* The open library answers first, from wherever the session stands */
	if ( *a_lib )
		{
		l_status = s_help$navigate(a_out, *a_lib, *a_cur, a_wordcnt, a_words, a_cur);

		if ( 1 & l_status )
			return	l_status;

		/* At depth the failure is the answer - do not leap to another library */
		if ( *a_cur >= 0 )
			return	l_status;
		}

	if ( !a_cat )
		return	HELP$_NOTFOUND;

	if ( !(1 & cat$lookup(a_cat, (unsigned) strlen(a_words[0]), a_words[0],
				(unsigned) sizeof(l_fspec), l_fspec)) )
		return	help$putmsg(HELP$_NOTFOUND, 1, (fao_prm_t) a_words[0]);

	/* The library the catalogue names is already open - it has answered */
	if ( *a_lib && !strcmp((*a_lib)->fspec, l_fspec) )
		return	help$putmsg(HELP$_NOTFOUND, 1, (fao_prm_t) a_words[0]);

	if ( !(1 & (l_status = hlb$open(l_fspec, &l_new))) )
		return	l_status;

	if ( *a_lib )
		hlb$close(*a_lib);

	*a_lib	= l_new;
	*a_cur	= -1;

	return	s_help$navigate(a_out, l_new, -1, a_wordcnt, a_words, a_cur);
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Split a line into blank-separated words, in place: the separators
**	are overwritten with the string terminator and the words are
**	returned as pointers into the original buffer.
**
**	A run enclosed in quotes, single or double, is one word however
**	many blanks it holds - which is how a key that carries a blank is
**	named at the prompt.  On the command line the shell has already
**	done the same job, so both routes reach the lookup alike.
**
**  FORMAL PARAMETERS:
**
**	a_line		The line, ASCIZ, modified in place
**	a_maxword	Capacity of the word vector
**	a_words		Receives the addresses of the words
**	a_wordcnt	Receives the number of the words
**
**  IMPLICIT INPUTS/OUTPUTS:
**	None.
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- the whole line has been split;
**	STS$K_WARN	- it holds more words than the vector takes, and
**			  the rest have been dropped.
**--
*/
static	int	s_help$split	(
		char *		a_line,
		int		a_maxword,
		char **		a_words,
		int *		a_wordcnt
			)
{
char *		l_ptr = a_line;
char		l_quote;
int		l_cnt = 0;

	while ( *l_ptr && (l_cnt < a_maxword) )
		{
		while ( (*l_ptr == ' ') || (*l_ptr == '\t') )
			l_ptr++;

		if ( !*l_ptr )
			break;

		/* A quoted run is one word, blanks and all */
		if ( (*l_ptr == '"') || (*l_ptr == '\'') )
			{
			l_quote = *l_ptr++;

			a_words [l_cnt++] = l_ptr;

			while ( *l_ptr && (*l_ptr != l_quote) )
				l_ptr++;

			if ( *l_ptr )
				*l_ptr++ = '\0';

			continue;
			}

		a_words [l_cnt++] = l_ptr;

		while ( *l_ptr && (*l_ptr != ' ') && (*l_ptr != '\t') )
			l_ptr++;

		if ( *l_ptr )
			*l_ptr++ = '\0';
		}

	*a_wordcnt = l_cnt;

	/* Anything left over means the vector filled before the line ended */
	while ( (*l_ptr == ' ') || (*l_ptr == '\t') )
		l_ptr++;

	return	*l_ptr ? STS$K_WARN : STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Redisplay the level the dialogue stands on after Q at the paging
**	prompt dropped the rest of a display: the list of the subtopics of
**	the current topic, the topics of the library at its root, or the
**	catalogue when no library is open.  Called once per stop, so a Q
**	given inside this list itself does not bring it back again.
**
**  FORMAL PARAMETERS:
**
**	a_out	The output context
**	a_cat	The catalogue, NULL when a single library was named
**	a_lib	The library in use, NULL - the catalogue root
**	a_cur	Index of the current topic, -1 - the root
**
**  IMPLICIT INPUTS/OUTPUTS:
**	Resets the pager of the output context.
**
**  RETURN VALUE:
**	STS$K_SUCCESS always.
**--
*/
static	int	s_help$resume	(
		HELP$OUT *	a_out,
		HLB$CAT *	a_cat,
		HLB$LIB *	a_lib,
		int		a_cur
			)
{
HLB$KEY *	l_key;

	help$page_reset(a_out);

	if ( !a_lib )
		{
		if ( a_cat )
			s_help$catroot(a_out, a_cat);

		return	STS$K_SUCCESS;
		}

	if ( a_cur < 0 )
		s_help$subtopics(a_out, a_lib, -1, 2, "Information available:");
	else if ( 1 & hlb$getkey(a_lib, a_cur, &l_key) )
		s_help$subtopics(a_out, a_lib, a_cur, 2 * l_key->level, "Additional information available:");

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	The interactive dialogue.  The prompt names the topic the session
**	currently stands on, an empty answer moves one level up, a question
**	mark redisplays the topic, EXIT or end of file terminates.  So does
**	end of file at the paging prompt of the display before; Q there
**	brings the dialogue back to the current level, its subtopics
**	listed again.
**
**  FORMAL PARAMETERS:
**
**	a_out	The output context
**	a_cat	The catalogue, NULL when a single library was named
**	a_lib	The library in use, updated as the session moves between them
**	a_cur	Index of the topic the dialogue starts at, -1 - the root
**
**  IMPLICIT INPUTS/OUTPUTS:
**	Reads the input descriptor of the output context.
**
**  RETURN VALUE:
**	STS$K_SUCCESS always.
**--
*/
static	int	s_help$dialogue	(
		HELP$OUT *	a_out,
		HLB$CAT *	a_cat,
		HLB$LIB **	a_lib,
		int		a_cur
			)
{
HLB$KEY *	l_key;
char		l_line [HELP$K_SZ_LINE], l_path [HELP$K_SZ_PATH];
char *		l_words [HELP$K_MAXWORD];
unsigned	l_linelen = 0, l_pathlen = 0;
int		l_cur = a_cur, l_wordcnt = 0;

	while ( 1 )
		{
		/*
		**  The last display may have been stopped at the paging prompt.
		**  Until the stop is cleared every write is discarded - the
		**  prompt below included, and the next line typed would then go
		**  in as a subtopic of a prompt never seen.  The redisplay may
		**  meet end of file at its own paging prompt, so it goes first.
		*/
		if ( a_out->abort == HELP$K_ABORT_QUIT )
			s_help$resume(a_out, a_cat, *a_lib, l_cur);

		if ( a_out->abort == HELP$K_ABORT_EXIT )
			break;

		help$page_reset(a_out);

		l_path[0] = '\0';
		l_pathlen = 0;

		if ( *a_lib )
			hlb$path(*a_lib, l_cur, (unsigned) sizeof(l_path), l_path, &l_pathlen);

		if ( l_pathlen )
			help$put(a_out, "!AZ Subtopic? ", 1, (fao_prm_t) l_path);
		else	help$put(a_out, "Topic? ", 0);

		if ( !(1 & help$readline(a_out->inchan, (unsigned) sizeof(l_line), l_line, &l_linelen)) )
			{
			help$put(a_out, "\n", 0);
			break;
			}

		if ( !(1 & s_help$split(l_line, HELP$K_MAXWORD, l_words, &l_wordcnt)) )
			help$putmsg(HELP$_TOOMANYW, 1, (fao_prm_t) HELP$K_MAXWORD);

		if ( !l_wordcnt )
			{
			/*
			**  An empty answer goes one level up.  At the top of a
			**  library it returns to the catalogue - the library is
			**  closed, and the root of the session is the merged
			**  list of every library on the search path.
			*/
			if ( !*a_lib )
				break;

			if ( !(1 & hlb$getkey(*a_lib, l_cur, &l_key)) )
				break;

			l_cur = l_key->parent;

			if ( (l_cur < 0) && a_cat )
				{
				hlb$close(*a_lib);
				*a_lib = NULL;

				s_help$catroot(a_out, a_cat);

				continue;
				}

			s_help$show(a_out, *a_lib, l_cur);

			continue;
			}

		if ( !strcasecmp(l_words[0], "EXIT") || !strcasecmp(l_words[0], "QUIT") )
			break;

		if ( !strcmp(l_words[0], "?") )
			{
			if ( *a_lib )
				s_help$show(a_out, *a_lib, l_cur);
			else	s_help$catroot(a_out, a_cat);

			continue;
			}

		s_help$session(a_out, a_cat, a_lib, &l_cur, l_wordcnt, l_words);
		}

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	List the whole topic tree of a library, one topic per line and
**	indented by its nesting level.
**
**  FORMAL PARAMETERS:
**
**	a_out	The output context
**	a_lib	The library
**
**  IMPLICIT INPUTS/OUTPUTS:
**	Writes to the output context.
**
**  RETURN VALUE:
**	STS$K_SUCCESS always.
**--
*/
static	int	s_help$list	(
		HELP$OUT *	a_out,
		HLB$LIB *	a_lib
			)
{
HLB$KEY *	l_key;

	/*
	**  The header block, an analogue of LIBRARY/LIST/FULL.  The creation
	**  time is a quadword and is passed to !%D BY VALUE, per STARLET
	**  X.01-09.
	*/
	help$put(a_out, "\n  !AZ\n", 1, (fao_prm_t) a_lib->fspec);

	help$put(a_out, "  ident !AZ, created !%D, !UL topic!%S\n", 3,
			(fao_prm_t) (a_lib->hdr->ident[0] ? a_lib->hdr->ident : "(none)"),
			(fao_prm_t) a_lib->hdr->credat,
			(fao_prm_t) a_lib->keycnt);

	if ( a_lib->hdr->creator[0] )
		help$put(a_out, "  !AZ\n", 1, (fao_prm_t) a_lib->hdr->creator);

	help$put(a_out, "\n", 0);

	for ( unsigned i = 0; i < a_lib->keycnt; i++ )
		{
		if ( !(1 & hlb$getkey(a_lib, (int) i, &l_key)) )
			break;

		help$put_indent(a_out, (unsigned) l_key->level * 2);
		help$put(a_out, "!AD\n", 2, (fao_prm_t) l_key->keylen, (fao_prm_t) l_key->key);
		}

	help$put(a_out, "\n", 0);

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Decide whether the run is pinned to one library.  It is when
**	/LIBRARY names a single library - that is how a caller says "this
**	library and no other", and the catalogue takes no part.  A name
**	that turns out not to exist still pins: naming the wrong file is
**	an error to report, not a reason to answer out of some other
**	library.
**
**	/LIBRARY naming a directory, a directory with the ellipsis of a
**	recursive scan, or a list of elements, is not pinning: it is a
**	search path, and it is then the whole of it.
**
**  FORMAL PARAMETERS:
**
**	a_clictx	The parse context
**	a_bufsz		Capacity of the buffer
**	a_buf		Receives the file specification, ASCIZ
**
**  IMPLICIT INPUTS/OUTPUTS:
**	None.
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- the run is pinned to the library named in <a_buf>;
**	STS$K_WARN	- it is not; <a_buf> holds the search path the
**			  qualifier named, or is empty when it named none.
**--
*/
static	int	s_help$findlib	(
		CLI_CTX *	a_clictx,
		unsigned	a_bufsz,
		char *		a_buf
			)
{
struct stat	l_st = {0};
ASC		l_val = {0};
char		l_tmp [ASC$K_SZ + 1] = {0};
unsigned	l_len, l_rec;

	a_buf[0] = '\0';

	if ( !(1 & __cli$present(a_clictx, &s_quals[0], NULL))
		|| !(1 & __cli$get_value(a_clictx, &s_quals[0], &l_val)) )
		return	STS$K_WARN;

	/* ASC2STR knows nothing about the capacity of the target - stage through a full-size buffer */
	__util$asc2str(&l_val, l_tmp);

	/*
	**  A value that carries the separator, or that names a directory, is
	**  a search path.  Anything else is meant as a single library - a
	**  name that does not exist included, so that naming the wrong file
	**  is reported rather than quietly answered out of another library.
	*/
	l_len = (unsigned) strlen(l_tmp);
	l_rec = (unsigned) strlen(HELP$K_RECURSE);

	if ( !strchr(l_tmp, HELP$K_PATHSEP)
		&& !((l_len > l_rec) && !strcmp(l_tmp + l_len - l_rec, HELP$K_RECURSE))
		&& !(!stat(l_tmp, &l_st) && S_ISDIR(l_st.st_mode)) )
		{
		help$strcpy(a_bufsz, a_buf, l_tmp);

		return	STS$K_SUCCESS;
		}

	/*
	**  A search path given on the command line is the whole of it.  A
	**  qualifier is an instruction for this one run and is meant to be
	**  obeyed literally; the environment variable, which is a standing
	**  preference, merely leads the default path instead.
	*/
	help$strcpy(a_bufsz, a_buf, l_tmp);

	return	STS$K_WARN;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	The entry point of the HELP utility: parse the command, open the
**	library, display the topic that has been asked for and, unless
**	/NOPROMPT has been given, enter the dialogue.
**
**  FORMAL PARAMETERS:
**
**	a_argc, a_argv	The command line
**
**  IMPLICIT INPUTS/OUTPUTS:
**	Reads the library, writes to the standard output.
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
HLB$LIB *	l_lib = NULL;
HLB$CAT *	l_cat = NULL;
HELP$OUT	l_out;
ASC		l_val = {0};
char *		l_argv [1 + HELP$K_MAXQUAL], *l_words [HELP$K_MAXWORD];
char		l_libspec [HELP$K_SZ_PATH] = {0}, l_num [ASC$K_SZ + 1] = {0};
char		l_path [HELP$K_SZ_PATHL] = {0};
unsigned	l_width = 0, l_state = 0;
int		l_argc = 1, l_wordcnt = 0, l_status, l_page = 1, l_prompt = 1, l_list = 0, l_cur = -1;
int		l_sep = a_argc, l_usage = 0, l_cache = 1, l_catalog = 0, l_qovf = 0, l_wovf = 0;
int		l_syscache = 0;

	/* The standard output carries a paged topic here - keep the logging off it */
	help$log_init();

	/*
	**  Several keys of a library - /LIST among them - are spelled exactly
	**  like a qualifier of this utility, and a qualifier wins.  A lone
	**  "--" ends the qualifiers: what follows it is a topic whatever it
	**  looks like, and the CLI service is not shown it at all.
	*/
	for ( int i = 1; i < a_argc; i++ )
		{
		if ( !strcmp(a_argv[i], "--") )
			{
			l_sep = i;
			break;
			}
		}

	/*
	**  The command is split before it is parsed.  The CLI service is shown
	**  the verb and the qualifiers of this utility, and nothing else: a
	**  topic is routinely spelled like one - /AFTER, /EXTRACT - and since
	**  CLI_ROUTINES X.00-07 an unrecognized qualifier is refused outright.
	*/
	l_argv [0] = s_clivrb;

	for ( int i = 1; i < l_sep; i++ )
		{
		/*
		**  A request for the syntax is answered here, not by the CLI
		**  service: the service would print a summary of its own
		**  tables, which describes the parser rather than the utility.
		**  Every spelling the service knows is caught; a bare HELP is
		**  not one of them, being the name of a topic here.
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
			**  silence: a command whose tail went missing would else
			**  answer about some topic above the one that was asked
			**  for, and report success doing it.
			*/
			if ( l_argc < (int) $ARRSZ(l_argv) )
				l_argv [l_argc++] = a_argv [i];
			else if ( !l_qovf++ )
				help$putmsg(HELP$_TOOMANYQ, 1, (fao_prm_t) HELP$K_MAXQUAL);
			}
		else if ( l_wordcnt < HELP$K_MAXWORD )
			l_words [l_wordcnt++] = a_argv [i];
		else if ( !l_wovf++ )
			help$putmsg(HELP$_TOOMANYW, 1, (fao_prm_t) HELP$K_MAXWORD);
		}

	/* Past the "--" marker everything is a topic, without being looked at */
	for ( int i = l_sep + 1; i < a_argc; i++ )
		{
		if ( l_wordcnt < HELP$K_MAXWORD )
			l_words [l_wordcnt++] = a_argv [i];
		else if ( !l_wovf++ )
			help$putmsg(HELP$_TOOMANYW, 1, (fao_prm_t) HELP$K_MAXWORD);
		}

	if ( !(1 & __cli$parse(s_verbs, CLI$M_OPSIGNAL, l_argc, l_argv, (void **) &l_clictx)) )
		return	EXIT_FAILURE;

	/* A negatable qualifier reports which form it was given in */
	if ( 1 & __cli$present(l_clictx, &s_quals[2], &l_state) )
		l_page = !(l_state & CLI$M_NEGATED);

	if ( 1 & __cli$present(l_clictx, &s_quals[3], &l_state) )
		l_prompt = !(l_state & CLI$M_NEGATED);

	if ( 1 & __cli$present(l_clictx, &s_quals[4], NULL) )
		l_list = 1;

	if ( 1 & __cli$present(l_clictx, &s_quals[6], &l_state) )
		l_cache = !(l_state & CLI$M_NEGATED);

	if ( 1 & __cli$present(l_clictx, &s_quals[7], NULL) )
		l_catalog = 1;

	if ( 1 & __cli$present(l_clictx, &s_quals[8], NULL) )
		l_syscache = 1;

	if ( (1 & __cli$present(l_clictx, &s_quals[1], NULL))
		&& (1 & __cli$get_value(l_clictx, &s_quals[1], &l_val)) )
		{
		__util$asc2str(&l_val, l_num);
		l_width = (unsigned) strtoul(l_num, NULL, 0);

		if ( !l_width )
			{
			help$putmsg(HELP$_IVQUAL, 2, (fao_prm_t) l_num, (fao_prm_t) "WIDTH");
			__cli$cleanup(l_clictx);

			return	EXIT_FAILURE;
			}
		}

	help$out_init(&l_out, STDOUT_FILENO, STDIN_FILENO, l_page, l_width);

	if ( l_usage )
		{
		help$put_raw(&l_out, (unsigned) sizeof(s_usage) - 1, s_usage);
		__cli$cleanup(l_clictx);

		return	EXIT_SUCCESS;
		}

	/*
	**  /SYSCACHE harvests a search path and writes the catalogue every
	**  user of the system reads.  It answers no question and displays no
	**  topic, so it is done before a library is opened for one.
	*/
	if ( l_syscache )
		{
		/*
		**  S_HELP$FINDLIB fills the buffer whichever way it answers -
		**  with a library when the run is pinned to one, with a search
		**  path when the qualifier named one - so it is the buffer that
		**  is tested, not the status.
		*/
		s_help$findlib(l_clictx, (unsigned) sizeof(l_libspec), l_libspec);

		if ( l_libspec[0] )
			help$strcpy((unsigned) sizeof(l_path), l_path, l_libspec);
		else	cat$path((unsigned) sizeof(l_path), l_path);

		l_status = cat$syscache(l_path, &l_cat);

		if ( 1 & l_status )
			{
		unsigned	l_libs = 0;

			/* The entries also count the directories that were scanned */
			for ( unsigned i = 0; i < l_cat->entcnt; i++ )
				{
				if ( !l_cat->ents[i].isdir && !l_cat->ents[i].absent )
					l_libs++;
				}

			help$putmsg(HELP$_SYSCACHE, 3, (fao_prm_t) l_cat->cache,
					(fao_prm_t) l_cat->keycnt, (fao_prm_t) l_libs);
			}

		cat$close(l_cat);
		__cli$cleanup(l_clictx);

		return	(1 & l_status) ? EXIT_SUCCESS : EXIT_FAILURE;
		}

	/*
	**  An explicit /LIBRARY naming one file is answered by that file and
	**  nothing else - it is how a caller pins the answer down.  Anything
	**  else goes through the catalogue of the search path.
	*/
	if ( 1 & s_help$findlib(l_clictx, (unsigned) sizeof(l_libspec), l_libspec) )
		{
		if ( !(1 & (l_status = hlb$open(l_libspec, &l_lib))) )
			{
			__cli$cleanup(l_clictx);

			return	EXIT_FAILURE;
			}
		}
	else	{
		/* A path named by the qualifier is the whole of it; otherwise compose one */
		if ( l_libspec[0] )
			help$strcpy((unsigned) sizeof(l_path), l_path, l_libspec);
		else	cat$path((unsigned) sizeof(l_path), l_path);

		if ( !(1 & (l_status = cat$open(l_path, l_cache, &l_cat))) )
			{
			__cli$cleanup(l_clictx);

			return	EXIT_FAILURE;
			}
		}

	if ( l_catalog )
		{
		if ( l_cat )
			s_help$catalog(&l_out, l_cat);
		else	s_help$list(&l_out, l_lib);

		hlb$close(l_lib);
		cat$close(l_cat);
		__cli$cleanup(l_clictx);

		return	EXIT_SUCCESS;
		}

	/*
	**  /LIST without a library of its own lists every library of the
	**  catalogue in turn, which is what the qualifier means: everything
	**  reachable, not everything in one file.
	*/
	if ( l_list )
		{
		if ( !l_cat )
			s_help$list(&l_out, l_lib);
		else	{
			for ( unsigned i = 0; i < l_cat->entcnt; i++ )
				{
				if ( l_cat->ents[i].isdir || l_cat->ents[i].absent )
					continue;

				if ( !(1 & hlb$open(l_cat->ents[i].fspec, &l_lib)) )
					continue;

				s_help$list(&l_out, l_lib);

				hlb$close(l_lib);
				l_lib = NULL;
				}
			}

		hlb$close(l_lib);
		cat$close(l_cat);
		__cli$cleanup(l_clictx);

		return	EXIT_SUCCESS;
		}

	if ( l_wordcnt )
		l_status = s_help$session(&l_out, l_cat, &l_lib, &l_cur, l_wordcnt, l_words);
	else if ( l_cat )
		{
		s_help$catroot(&l_out, l_cat);
		l_status = STS$K_SUCCESS;
		}
	else	l_status = s_help$navigate(&l_out, l_lib, -1, 0, NULL, &l_cur);

	/* The dialogue makes sense at a terminal only */
	if ( l_prompt && l_out.tty )
		s_help$dialogue(&l_out, l_cat, &l_lib, l_cur);

	help$flush(&l_out);

	hlb$close(l_lib);
	cat$close(l_cat);
	__cli$cleanup(l_clictx);

	return	(1 & l_status) ? EXIT_SUCCESS : EXIT_FAILURE;
}
