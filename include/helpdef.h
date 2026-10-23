#ifndef	__HELPDEF$H__
#define	__HELPDEF$H__	1

/*
**++
**
**  FACILITY:	HELP - OpenVMS-style help library subsystem for Linux
**
**  MODULE:	helpdef.h
**
**  ABSTRACT:	Public definitions shared by all modules of the HELP facility:
**		the on-disk layout of a help library (.HLB), the in-memory
**		builder objects, the condition values of the facility and
**		the prototypes of the HLB$, MD$ and HELP$ routines.
**
**  DESCRIPTION: A help library is a flat, self-contained, read-only file:
**
**		+-----------------------------------------------------+
**		|  HLB$HDR	fixed header, magic/version/CRC	      |
**		+-----------------------------------------------------+
**		|  HLB$KEY [keycnt]  key index, fixed-size records,    |
**		|		     stored in pre-order of the topic  |
**		|		     tree; PARENT/CHILD/NEXT are the   |
**		|		     indices of the neighbours	      |
**		+-----------------------------------------------------+
**		|  text heap	     the rendered plain text of every |
**		|		     topic, referenced by TXTOFF/     |
**		|		     TXTLEN of the key record	      |
**		+-----------------------------------------------------+
**
**		The whole file is read into a single memory block by
**		HLB$OPEN, so navigation costs no I/O at all.
**
**  AUTHOR:	StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:  31-JUL-2026
**
**  MODIFICATION HISTORY:
**
**	X01-02		23-OCT-2026	RRL
**		Windows x64 (MinGW-w64, for yaVNA's yavna.exe): HELP$K_PATHSEP,
**		the separator of a search path - ';' there, ':' being part of
**		C:\...; HELP$K_NULLDEV; O_BINARY, 0 where the C RTL has no
**		text mode - a library opened without it on Windows has its
**		CR/LF translated and its reading stopped at the first ^Z.
**
**	X01-01		 4-OCT-2026	CLD
**		HELP$K_ABORT_QUIT and HELP$K_ABORT_EXIT, the two values of
**		ABORT of HELP$OUT: Q at the paging prompt stops the topic,
**		end of file stops the session.  RRL: "after q show the
**		current level".
**
**	X.01-00		31-JUL-2026	CLAUDE
**		Initial version.
**
**--
*/

#ifndef	__MODULE__
#define	__MODULE__	"HELPDEF"
#endif

#ifndef	__IDENT__
#define	__IDENT__	"X01-02"
#endif

#ifndef	__REV__
#define	__REV__		"01.02.0"
#endif

#ifndef	__FAC__
#define	__FAC__		"HELP"
#endif

#include	<stdint.h>
#include	<stddef.h>
#include	<fcntl.h>

#include	"helpvers.h"

#include	"utility_routines.h"
#include	"starlet.h"
#include	"cli_routines.h"

#ifdef	__cplusplus
extern "C" {
#endif

/*
**  Facility number of the HELP subsystem, see $DEFFAC/$DEFMSG/$DEFSEV
**  in UTILITY_ROUTINES.H for the layout of a condition value.
*/
#define	HELP$K_FACNO		0x0110

#define	$HELPSTS(msg, sev)	( $DEFFAC(HELP$K_FACNO) | $DEFMSG(msg) | $DEFSEV(sev) )

/*
**  Message numbers of the facility.  Ordered - hence an enumeration;
**  the text and the identification of every message live in the
**  catalogue S_MSGTAB (HELPUTL.C).
*/
enum	{
	HELP$K_MSG_NORMAL	= 1,		/* Normal successful completion		*/
	HELP$K_MSG_NOTFOUND,			/* No documentation on the topic	*/
	HELP$K_MSG_OPENIN,			/* Error opening an input file		*/
	HELP$K_MSG_OPENOUT,			/* Error creating an output file	*/
	HELP$K_MSG_READERR,			/* Error reading a file			*/
	HELP$K_MSG_WRITERR,			/* Error writing a file			*/
	HELP$K_MSG_NOTHLB,			/* Not a help library			*/
	HELP$K_MSG_BADVERS,			/* Unsupported library format version	*/
	HELP$K_MSG_BADCRC,			/* Checksum error in a library		*/
	HELP$K_MSG_TRUNCLIB,			/* Library file is truncated		*/
	HELP$K_MSG_NOMEM,			/* Memory allocation failure		*/
	HELP$K_MSG_TOODEEP,			/* Heading level exceeds the maximum	*/
	HELP$K_MSG_SKIPLVL,			/* Heading level skipped		*/
	HELP$K_MSG_NOKEY,			/* Empty heading - no key can be made	*/
	HELP$K_MSG_LONGKEY,			/* Key truncated to HLB$K_SZ_KEY	*/
	HELP$K_MSG_DUPKEY,			/* Duplicate key at the same level	*/
	HELP$K_MSG_NOTOP,			/* Text before the first heading	*/
	HELP$K_MSG_EMPTYLIB,			/* No topics at all			*/
	HELP$K_MSG_CREATED,			/* Library has been created		*/
	HELP$K_MSG_IVQUAL,			/* Illegal value of a qualifier		*/
	HELP$K_MSG_EXTRACTED,			/* Library has been extracted		*/
	HELP$K_MSG_CONFQUAL,			/* Conflicting qualifiers		*/
	HELP$K_MSG_LONGLINE,			/* Source line truncated		*/
	HELP$K_MSG_NOLIBS,			/* No libraries on the search path	*/
	HELP$K_MSG_CACHERR,			/* Catalogue cache unusable		*/
	HELP$K_MSG_TOOMANY,			/* Too many libraries on the path	*/
	HELP$K_MSG_TOOMANYT,			/* Too many topics in the catalogue	*/
	HELP$K_MSG_DIAGS,			/* Diagnostics issued while translating	*/
	HELP$K_MSG_TOOMANYIN,			/* Too many input files on the command	*/
	HELP$K_MSG_TOOMANYQ,			/* Too many qualifiers on the command	*/
	HELP$K_MSG_TOOMANYW,			/* Too many words in a topic path	*/
	HELP$K_MSG_SYSCACHE,			/* Shared catalogue written		*/
	HELP$K_MSG_QUEFAIL,			/* A key could not be queued		*/
	HELP$K_MSG_BADRUN,			/* Text run lies outside the heap	*/

	HELP$K_MSG_MAX				/* End-Of-List				*/
	};

#define	HELP$_NORMAL	$HELPSTS(HELP$K_MSG_NORMAL,	STS$K_SUCCESS)
#define	HELP$_NOTFOUND	$HELPSTS(HELP$K_MSG_NOTFOUND,	STS$K_ERROR)
#define	HELP$_OPENIN	$HELPSTS(HELP$K_MSG_OPENIN,	STS$K_ERROR)
#define	HELP$_OPENOUT	$HELPSTS(HELP$K_MSG_OPENOUT,	STS$K_ERROR)
#define	HELP$_READERR	$HELPSTS(HELP$K_MSG_READERR,	STS$K_ERROR)
#define	HELP$_WRITERR	$HELPSTS(HELP$K_MSG_WRITERR,	STS$K_ERROR)
#define	HELP$_NOTHLB	$HELPSTS(HELP$K_MSG_NOTHLB,	STS$K_ERROR)
#define	HELP$_BADVERS	$HELPSTS(HELP$K_MSG_BADVERS,	STS$K_ERROR)
#define	HELP$_BADCRC	$HELPSTS(HELP$K_MSG_BADCRC,	STS$K_ERROR)
#define	HELP$_TRUNCLIB	$HELPSTS(HELP$K_MSG_TRUNCLIB,	STS$K_ERROR)
#define	HELP$_NOMEM	$HELPSTS(HELP$K_MSG_NOMEM,	STS$K_FATAL)
#define	HELP$_TOODEEP	$HELPSTS(HELP$K_MSG_TOODEEP,	STS$K_ERROR)
#define	HELP$_SKIPLVL	$HELPSTS(HELP$K_MSG_SKIPLVL,	STS$K_WARN)
#define	HELP$_NOKEY	$HELPSTS(HELP$K_MSG_NOKEY,	STS$K_ERROR)
#define	HELP$_LONGKEY	$HELPSTS(HELP$K_MSG_LONGKEY,	STS$K_WARN)
#define	HELP$_DUPKEY	$HELPSTS(HELP$K_MSG_DUPKEY,	STS$K_WARN)
#define	HELP$_NOTOP	$HELPSTS(HELP$K_MSG_NOTOP,	STS$K_WARN)
#define	HELP$_EMPTYLIB	$HELPSTS(HELP$K_MSG_EMPTYLIB,	STS$K_ERROR)
#define	HELP$_CREATED	$HELPSTS(HELP$K_MSG_CREATED,	STS$K_INFO)
#define	HELP$_IVQUAL	$HELPSTS(HELP$K_MSG_IVQUAL,	STS$K_ERROR)
#define	HELP$_EXTRACTED	$HELPSTS(HELP$K_MSG_EXTRACTED,	STS$K_INFO)
#define	HELP$_CONFQUAL	$HELPSTS(HELP$K_MSG_CONFQUAL,	STS$K_ERROR)
#define	HELP$_LONGLINE	$HELPSTS(HELP$K_MSG_LONGLINE,	STS$K_WARN)
#define	HELP$_NOLIBS	$HELPSTS(HELP$K_MSG_NOLIBS,	STS$K_ERROR)
#define	HELP$_CACHERR	$HELPSTS(HELP$K_MSG_CACHERR,	STS$K_WARN)
#define	HELP$_TOOMANY	$HELPSTS(HELP$K_MSG_TOOMANY,	STS$K_WARN)
#define	HELP$_TOOMANYT	$HELPSTS(HELP$K_MSG_TOOMANYT,	STS$K_WARN)
#define	HELP$_DIAGS	$HELPSTS(HELP$K_MSG_DIAGS,	STS$K_INFO)
#define	HELP$_TOOMANYIN	$HELPSTS(HELP$K_MSG_TOOMANYIN,	STS$K_WARN)
#define	HELP$_TOOMANYQ	$HELPSTS(HELP$K_MSG_TOOMANYQ,	STS$K_WARN)
#define	HELP$_TOOMANYW	$HELPSTS(HELP$K_MSG_TOOMANYW,	STS$K_WARN)
#define	HELP$_SYSCACHE	$HELPSTS(HELP$K_MSG_SYSCACHE,	STS$K_INFO)
#define	HELP$_QUEFAIL	$HELPSTS(HELP$K_MSG_QUEFAIL,	STS$K_ERROR)
#define	HELP$_BADRUN	$HELPSTS(HELP$K_MSG_BADRUN,	STS$K_ERROR)

/*
**  Sizes and limits of the subsystem.
*/
#define	HLB$K_MAGIC	0x24424C48UL		/* 'HLB$' - little endian on-disk order	*/
#define	HLB$K_VERSION	1			/* Format version			*/
#define	HLB$K_REVISION	0			/* Format revision			*/

#define	HLB$K_SZ_KEY	64			/* Maximum length of a key		*/
#define	HLB$K_SZ_IDENT	16			/* Library identification string	*/
#define	HLB$K_SZ_CREATOR 96			/* Producer + source file spec		*/
#define	HLB$K_MAXLVL	9			/* Maximum nesting level of topics	*/

#define	HELP$K_SZ_LINE	1024			/* Maximum length of a source line	*/
#define	HELP$K_SZ_PATH	512			/* Maximum length of a file spec	*/
#define	HELP$K_SZ_MSG	512			/* Maximum length of a message		*/
#define	HELP$K_SZ_OUT	4096			/* Size of the output buffer		*/
#define	HELP$K_MAXPRM	16			/* Maximum number of FAO parameters	*/

#define	HELP$K_DEFWIDTH	80			/* Default terminal width		*/
#define	HELP$K_DEFPAGE	24			/* Default terminal height		*/
#define	HELP$K_MINWIDTH	32			/* Sanity floor for /WIDTH		*/
#define	HELP$K_MAXWIDTH	512			/* Sanity ceiling for /WIDTH		*/

#define	HELP$K_SZ_PATHL	4096			/* Maximum length of a search path	*/
#define	HELP$K_MAXLIB	64			/* Maximum libraries on a search path	*/
#define	HELP$K_MAXIN	64			/* Maximum input files on a command	*/
#define	HELP$K_MAXQUAL	64			/* Maximum qualifiers on a command	*/
#define	HELP$K_MAXCATKEY 4096			/* Maximum topics in a catalogue	*/
#define	HELP$K_LIBTYPE	".hlb"			/* File type of a help library		*/

#ifdef	_WIN32
#define	HELP$K_PATHSEP	';'			/* Separator of a search path - ':' is in C: (drive)	*/
#define	HELP$K_NULLDEV	"NUL"			/* Where the quiet output goes		*/
#else
#define	HELP$K_PATHSEP	':'			/* Separator of a search path, as in PATH	*/
#define	HELP$K_NULLDEV	"/dev/null"		/* Where the quiet output goes		*/
#endif

#ifndef	O_BINARY
#define	O_BINARY	0			/* No text mode to be asked out of	*/
#endif

/*
**  The characters that make a topic a wildcard rather than a name.  They
**  are the ones __UTIL$PATTERN_MATCH acts on, and are named here so that
**  the two cannot drift apart: a character the matcher honours but this
**  set omits is never offered to it, and a topic carrying it is looked up
**  as a name and not found.
*/
#define	HELP$K_WILDCARD	"*%?"			/* Any run, any one, any one		*/
#define	HELP$K_MAXDEPTH	8			/* Depth a recursive scan descends to	*/
#define	HELP$K_RECURSE	"..."			/* Element suffix asking for a subtree	*/
#define	HELP$K_SYSFILE	"catalog.idx"		/* The shared catalogue, in HELP$K_SYSCACHE */

#define	CAT$K_MAGIC	0x54414348UL		/* 'HCAT' - little endian on-disk order	*/
#define	CAT$K_VERSION	1			/* Format version of the cache		*/

#pragma	pack	(push)
#pragma	pack	(1)

/*
**  The fixed header of a help library.  Every numeric field is stored in
**  the native byte order of the producing host; a library carried to a
**  host of the opposite endianness is rejected by the magic check, which
**  is exactly the intent - the file is a build artefact, not an exchange
**  format.
*/
typedef	struct	hlb_hdr_t
{
	uint32_t	magic;			/* HLB$K_MAGIC				*/
	uint16_t	vers;			/* HLB$K_VERSION			*/
	uint16_t	rev;			/* HLB$K_REVISION			*/
	uint32_t	hdrsz;			/* sizeof(HLB$HDR)			*/
	uint32_t	keysz;			/* sizeof(HLB$KEY)			*/
	uint32_t	keycnt;			/* Number of the key records		*/
	uint32_t	keyoff;			/* Byte offset of the key index		*/
	uint32_t	txtoff;			/* Byte offset of the text heap		*/
	uint32_t	txtsz;			/* Size of the text heap, bytes		*/
	uint32_t	crc;			/* CRC-32 over the index and the heap	*/
	uint64_t	credat;			/* Creation time, seconds of the epoch	*/
	char		ident [HLB$K_SZ_IDENT];	/* Library identification, ASCIZ	*/
	char		creator [HLB$K_SZ_CREATOR];/* Producer and source, ASCIZ	*/
} HLB$HDR;

/*
**  A single key (topic) record of the index.  The records are stored in
**  the pre-order of the topic tree, so a whole subtree occupies a
**  contiguous run - which makes /LIST and the wildcard scan a plain
**  sequential pass over the array.
*/
typedef	struct	hlb_key_t
{
	uint32_t	txtoff;			/* Offset of the text in the heap	*/
	uint32_t	txtlen;			/* Length of the text, bytes		*/
	int32_t		parent;			/* Index of the parent, -1 at level 1	*/
	int32_t		child;			/* Index of the first subtopic, -1 - none */
	int32_t		next;			/* Index of the next sibling, -1 - none	*/
	uint16_t	level;			/* Nesting level, 1 .. HLB$K_MAXLVL	*/
	uint16_t	keylen;			/* Significant length of KEY		*/
	char		key [HLB$K_SZ_KEY];	/* Key, upper case, blank padded	*/
} HLB$KEY;

/*
**  A single element of a search path as the catalogue remembers it: a
**  help library, or a directory that was scanned for libraries.  MTIME
**  and SIZE are what the cache is validated against - a directory whose
**  contents changed reports a new modification time, so a library added
**  or removed is noticed without opening anything.
**
**  An element that did not exist is recorded all the same, with ABSENT
**  set: its later appearance has to invalidate the cache just as a
**  change to an existing one does.  A user creating $HOME/.help for the
**  first time would otherwise not be noticed until something else on the
**  path happened to change.
*/
typedef	struct	cat_ent_t
{
	uint64_t	mtime;			/* Modification time, seconds		*/
	uint64_t	size;			/* Size, bytes; 0 for a directory	*/
	uint16_t	isdir;			/* != 0 - a directory, not a library	*/
	uint16_t	absent;			/* != 0 - the element did not exist	*/
	char		fspec [HELP$K_SZ_PATH];	/* File specification, ASCIZ		*/
} CAT$ENT;

/*
**  A topic of the catalogue: a key of level 1 and the library holding it.
*/
typedef	struct	cat_key_t
{
	uint16_t	keylen;			/* Significant length of KEY		*/
	uint16_t	lib;			/* Index into the entry array		*/
	char		key [HLB$K_SZ_KEY];	/* Key, upper case			*/
} CAT$KEY;

/*
**  The fixed header of a catalogue cache.
*/
typedef	struct	cat_hdr_t
{
	uint32_t	magic;			/* CAT$K_MAGIC				*/
	uint16_t	vers;			/* CAT$K_VERSION			*/
	uint16_t	rev;			/* Reserved				*/
	uint32_t	hdrsz;			/* sizeof(CAT$HDR)			*/
	uint32_t	entsz;			/* sizeof(CAT$ENT)			*/
	uint32_t	keysz;			/* sizeof(CAT$KEY)			*/
	uint32_t	entcnt;			/* Number of the entries		*/
	uint32_t	keycnt;			/* Number of the topics			*/
	uint32_t	crc;			/* CRC-32 over the two arrays		*/
	uint64_t	credat;			/* Time the cache was built		*/
	char		path [HELP$K_SZ_PATHL];	/* The search path it was built from	*/
} CAT$HDR;

#pragma	pack	(pop)

/*
**  A catalogue of the libraries reachable through a search path.
*/
typedef	struct	hlb_cat_t
{
	CAT$ENT *	ents;			/* The path elements			*/
	unsigned	entcnt;			/* Number of them			*/

	CAT$KEY *	keys;			/* The topics				*/
	unsigned	keycnt;			/* Number of them			*/

	int		cached;			/* != 0 - loaded from the cache		*/
	int		shared;			/* != 0 - the shared catalogue was read	*/
	int		entovf;			/* != 0 - HELP$K_MAXLIB was reached	*/
	int		keyovf;			/* != 0 - HELP$K_MAXCATKEY was reached	*/
	int		dirty;			/* != 0 - the cache wants rewriting	*/

	uint64_t	credat;			/* Time the catalogue was written	*/

	char		path [HELP$K_SZ_PATHL];	/* The search path			*/

	/*
	**  The cache is reached through a descriptor of the directory
	**  holding it, not through its name: the directory is validated
	**  once, and every operation afterwards is relative to the very
	**  object that was validated.  A name would have to be resolved
	**  anew each time, and between two resolutions it can be made to
	**  mean something else - which is the whole of the danger in a
	**  directory anyone may write to.
	*/
	int		cachefd;		/* The directory, -1 - no caching	*/
	char		cachename [64];		/* The file within it			*/
	char		cache [HELP$K_SZ_PATH];	/* Full specification, for display	*/
} HLB$CAT;

/*
**  An opened help library.  The whole file lives in a single allocation
**  addressed by BASE; HDR, KEYS and TXT are the projections of it.
*/
typedef	struct	hlb_lib_t
{
	char *		base;			/* The image of the file		*/
	size_t		size;			/* Size of the image, bytes		*/

	HLB$HDR *	hdr;			/* -> the header			*/
	HLB$KEY *	keys;			/* -> the key index			*/
	char *		txt;			/* -> the text heap			*/

	unsigned	keycnt;			/* Number of keys, from the header	*/

	char		fspec [HELP$K_SZ_PATH];	/* File spec the library came from	*/
} HLB$LIB;

/*
**  A key of a library being built.  The builder keeps every key on a
**  __QUEUE in creation order (so that the text can still be appended to
**  the current key) and, in parallel, on the PARENT/CHILD/NEXT tree that
**  is later flattened into the index by HLB$WRITE.
*/
typedef	struct	hlb_bkey_t
{
	ENTRY		links;			/* $INSQTAIL/$REMQHEAD links - keep first */

	struct hlb_bkey_t *parent;		/* -> the parent key, NULL at level 1	*/
	struct hlb_bkey_t *child;		/* -> the first subtopic		*/
	struct hlb_bkey_t *next;		/* -> the next sibling			*/

	int		index;			/* Index assigned by the flattening pass */

	unsigned	level;			/* Nesting level, 1 .. HLB$K_MAXLVL	*/
	unsigned	keylen;			/* Significant length of KEY		*/
	char		key [HLB$K_SZ_KEY];	/* Key, upper case			*/

	char *		txt;			/* Text of the topic, may be NULL	*/
	unsigned	txtlen;			/* Used bytes of TXT			*/
	unsigned	txtsz;			/* Allocated bytes of TXT		*/
} HLB$BKEY;

/*
**  The context of a library under construction.
*/
typedef	struct	hlb_bld_t
{
	__QUEUE		keyque;			/* All the keys, in creation order	*/

	HLB$BKEY *	root;			/* -> the first key of level 1		*/
	HLB$BKEY *	curr;			/* -> the key the text is appended to	*/

	unsigned	keycnt;			/* Number of the keys created		*/
	unsigned	txtsz;			/* Total size of the texts		*/

	int		sort;			/* != 0 - keep the subtopics sorted	*/
	int		identset;		/* != 0 - IDENT came from the command	*/
	unsigned	lvlbase;		/* Levels the source is pushed down by	*/

	char		ident [HLB$K_SZ_IDENT];	/* Value of /IDENT			*/
	char		creator [HLB$K_SZ_CREATOR];/* Producer and source		*/
} HLB$BLD;

/*
**  The values of ABORT of the output context.  Q at the paging prompt
**  discards the rest of the topic only - the dialogue goes on at the
**  level it stands on; end of file (^D, ^Z) there ends the session.
*/
enum	{
	HELP$K_ABORT_QUIT = 1,			/* Q - the rest of the topic		*/
	HELP$K_ABORT_EXIT			/* EOF - the whole session		*/
	};

/*
**  The output context of the HELP utility: where the text goes, how wide
**  the page is and how many lines are left before the paging prompt.
*/
typedef	struct	help_out_t
{
	int		chan;			/* Output file descriptor		*/
	int		inchan;			/* Descriptor the prompts are read from	*/

	int		tty;			/* != 0 - the output is a terminal	*/
	int		page;			/* != 0 - paging is enabled		*/

	unsigned	width;			/* Page width, characters		*/
	unsigned	height;			/* Page height, lines			*/
	unsigned	lineno;			/* Lines emitted since the last prompt	*/

	int		abort;			/* HELP$K_ABORT_*, 0 - not stopped	*/

	char		buf [HELP$K_SZ_OUT];	/* Assembly buffer of a single record	*/
} HELP$OUT;

/*
**  The message catalogue record of the facility.  Unlike EMSG_RECORD of
**  UTILITY_ROUTINES the text is an FAO control string, not a printf one,
**  so that the whole subsystem stays free of <stdio.h>.
*/
typedef	struct	help_msg_t
{
	unsigned	sts;			/* Condition value			*/
	const char *	ident;			/* Message identification, e.g. NOTFOUND */
	const char *	fao;			/* FAO control string of the text	*/
} HELP$MSG;

/*
**  HELPUTL.C - output, paging and message signalling
*/
int	help$out_init	(HELP$OUT *a_out, int a_chan, int a_inchan, int a_page, unsigned a_width);
int	help$put	(HELP$OUT *a_out, const char *a_fao, int a_prmcnt, ...);
int	help$put_raw	(HELP$OUT *a_out, unsigned a_len, const char *a_str);
int	help$put_text	(HELP$OUT *a_out, unsigned a_indent, unsigned a_len, const char *a_txt);
int	help$put_indent	(HELP$OUT *a_out, unsigned a_indent);
int	help$flush	(HELP$OUT *a_out);
int	help$page_reset	(HELP$OUT *a_out);
int	help$putmsg	(unsigned a_sts, int a_prmcnt, ...);
int	help$getmsg	(unsigned a_sts, const HELP$MSG **a_msg);
int	help$upcase	(unsigned a_srclen, const char *a_src, unsigned a_dstsz, char *a_dst, unsigned *a_dstlen);
unsigned help$dsplen	(unsigned a_len, const char *a_str);
int	help$getwinsz	(int a_chan, unsigned *a_width, unsigned *a_height);
int	help$readline	(int a_chan, unsigned a_bufsz, char *a_buf, unsigned *a_buflen);
int	help$readfile	(const char *a_fspec, char **a_buf, unsigned *a_buflen);
int	help$isqual	(const CLI_PQDESC *a_quals, const char *a_arg);
int	help$ishelp	(const char *a_arg);
int	help$log_init	(void);
int	help$strcpy	(unsigned a_dstsz, char *a_dst, const char *a_src);

/*
**  HLB.C - the help library service, reader side
*/
int	hlb$open	(const char *a_fspec, HLB$LIB **a_lib);
int	hlb$close	(HLB$LIB *a_lib);
int	hlb$getkey	(HLB$LIB *a_lib, int a_index, HLB$KEY **a_key);
int	hlb$gettext	(HLB$LIB *a_lib, int a_index, unsigned *a_len, const char **a_txt);
int	hlb$lookup	(HLB$LIB *a_lib, int a_parent, unsigned a_keylen, const char *a_key, int *a_index);
int	hlb$lookup_run	(HLB$LIB *a_lib, int a_parent, int a_wordcnt, char **a_words, int *a_used, int *a_index);
int	hlb$search	(HLB$LIB *a_lib, int a_parent, const char *a_pattern, int a_from, int *a_index);
int	hlb$firstchild	(HLB$LIB *a_lib, int a_parent, int *a_index);
int	hlb$path	(HLB$LIB *a_lib, int a_index, unsigned a_bufsz, char *a_buf, unsigned *a_buflen);

/*
**  HLB.C - the help library service, writer side
*/
int	hlb$bld_create	(HLB$BLD **a_bld, const char *a_ident, const char *a_creator, int a_sort);
int	hlb$bld_delete	(HLB$BLD *a_bld);
int	hlb$bld_addkey	(HLB$BLD *a_bld, unsigned a_level, unsigned a_keylen, const char *a_key, HLB$BKEY **a_bkey);
int	hlb$bld_facility (HLB$BLD *a_bld, unsigned a_faclen, const char *a_fac);
int	hlb$bld_addtext	(HLB$BLD *a_bld, unsigned a_len, const char *a_txt);
int	hlb$bld_write	(HLB$BLD *a_bld, const char *a_fspec);

/*
**  HLBCAT.C - the catalogue of the libraries on a search path
*/
int	cat$path	(unsigned a_bufsz, char *a_buf);
int	cat$open	(const char *a_path, int a_usecache, HLB$CAT **a_cat);
int	cat$syscache	(const char *a_path, HLB$CAT **a_cat);
int	cat$open_store	(const char *a_dir, HLB$CAT **a_cat);
int	cat$close	(HLB$CAT *a_cat);
int	cat$lookup	(HLB$CAT *a_cat, unsigned a_keylen, const char *a_key, unsigned a_bufsz, char *a_fspec);
int	cat$firstlib	(HLB$CAT *a_cat, unsigned a_bufsz, char *a_fspec);
int	cat$nextkey	(HLB$CAT *a_cat, int a_from, int *a_index);

/*
**  MDPARSE.C - the Markdown reader
*/
int	md$translate	(HLB$BLD *a_bld, const char *a_fspec, unsigned *a_errcnt);
int	md$mkkey	(unsigned a_srclen, const char *a_src, unsigned a_dstsz, char *a_dst, unsigned *a_dstlen);

/*
**  MDGEN.C - the Markdown writer, the reverse of MDPARSE.C
*/
int	md$generate	(HLB$LIB *a_lib, int a_root, HELP$OUT *a_out, unsigned *a_cnt);

#ifdef	__cplusplus
}
#endif

#endif	/* __HELPDEF$H__ */
