#define	__MODULE__	"HLB"
#define	__IDENT__	"X01-00"
#define	__REV__		"01.00.0"

/*
**++
**
**  FACILITY:	HELP - OpenVMS-style help library subsystem for Linux
**
**  MODULE:	hlb.c
**
**  ABSTRACT:	The help library service.  The reader half opens a .HLB
**		file, validates it and navigates the topic tree; the writer
**		half accumulates topics in memory and flattens them into a
**		.HLB file.
**
**  DESCRIPTION: The writer keeps every key on a __QUEUE in creation order
**		- that is the order the text is appended in - and, at the
**		same time, on a PARENT/CHILD/NEXT tree.  Subtopics are
**		inserted into the sibling chain in ascending order of the
**		key, which is what OpenVMS HELP shows under "Additional
**		information available"; /NOSORT keeps the order of the
**		source instead.
**
**		HLB$BLD_WRITE walks the tree in pre-order, assigns the
**		indices, materialises the key array and the text heap in
**		memory, computes the CRC over both and emits the three
**		sections in one pass.  Because the walk is pre-order, the
**		whole subtree of a key occupies a contiguous run of the
**		index - which is what makes the wildcard scan and /LIST a
**		plain sequential pass.
**
**		The reader reads the entire file into one allocation.  A
**		help library is a small object - the OpenVMS HELPLIB.HLB is
**		under two megabytes - and holding it whole removes any I/O
**		from the navigation.
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
#include	<errno.h>
#include	<time.h>
#include	<unistd.h>
#include	<fcntl.h>
#include	<sys/stat.h>

#include	"helpdef.h"

#define	HLB$K_TXTCHUNK	4096			/* Growth step of a topic's text buffer	*/


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Compare two keys as OpenVMS HELP does - case has already been
**	folded by the producer and by the consumer, so a plain byte
**	comparison over the shorter length, then over the lengths, gives
**	the ordering.
**
**  FORMAL PARAMETERS:
**
**	a_len1, a_key1	The first key
**	a_len2, a_key2	The second key
**
**  IMPLICIT INPUTS/OUTPUTS:
**	None.
**
**  RETURN VALUE:
**	< 0, 0, > 0 - the first key sorts before, equal to, after the second.
**--
*/
static	int	s_hlb$keycmp	(
		unsigned	a_len1,
	const	char *		a_key1,
		unsigned	a_len2,
	const	char *		a_key2
			)
{
int	l_diff;

	if ( (l_diff = memcmp(a_key1, a_key2, $MIN(a_len1, a_len2))) )
		return	l_diff;

	return	(int) a_len1 - (int) a_len2;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Create the context of a help library under construction.
**
**  FORMAL PARAMETERS:
**
**	a_bld		Receives the address of the context
**	a_ident		Library identification string, may be NULL
**	a_creator	Producer and source description, may be NULL
**	a_sort		!= 0 - keep the subtopics of a key sorted by the key
**
**  IMPLICIT INPUTS/OUTPUTS:
**	Allocates memory to be released by HLB$BLD_DELETE.
**
**  RETURN VALUE:
**	STS$K_SUCCESS or HELP$_NOMEM, already signalled.
**--
*/
int	hlb$bld_create	(
		HLB$BLD **	a_bld,
	const	char *		a_ident,
	const	char *		a_creator,
		int		a_sort
			)
{
HLB$BLD *	l_bld;

	*a_bld = NULL;

	if ( !(l_bld = calloc(1, sizeof(HLB$BLD))) )
		return	help$putmsg(HELP$_NOMEM, 3, (fao_prm_t) sizeof(HLB$BLD), (fao_prm_t) errno,
					(fao_prm_t) strerror(errno));

	l_bld->sort = a_sort;

	/*
	**  An identification given on the command line outranks the one
	**  written in the source: a qualifier is an instruction for this one
	**  run, a directive is a property of a file that may be translated
	**  by anybody for any purpose.
	*/
	if ( a_ident && *a_ident )
		{
		help$strcpy((unsigned) sizeof(l_bld->ident), l_bld->ident, a_ident);

		l_bld->identset = 1;
		}

	if ( a_creator )
		help$strcpy((unsigned) sizeof(l_bld->creator), l_bld->creator, a_creator);

	*a_bld = l_bld;

	$TRACE("bld=%p, ident='%s', sort=%d", (void *) l_bld, l_bld->ident, a_sort);

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Root the library being built at its facility: create the one key of
**	level 1 that names it, and push everything the sources contribute
**	one level down beneath it.
**
**	The catalogue of a search path merges the level 1 topics of every
**	library it finds, so what a library puts there is what the whole
**	system sees.  A library that exports its verbs - SUBMIT, DELETE,
**	PURGE - puts them in a namespace it shares with every other
**	library, where they collide with the verbs of the next product and
**	hide, or are hidden by, them.  One key per library, naming the
**	facility, is what keeps that namespace legible: BATCH, HELP, DCL.
**
**	Text that precedes the first heading of a source has nowhere to go
**	in a library that is not rooted; here it has, and becomes the text
**	of the facility topic itself.
**
**  FORMAL PARAMETERS:
**
**	a_bld		The builder context, before any topic is added
**	a_faclen	Length of the facility name
**	a_fac		The facility name, any case
**
**  IMPLICIT INPUTS/OUTPUTS:
**	Sets LVLBASE of the context, so every later key sinks by one.
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- the root has been created;
**	STS$K_WARN	- the context already holds topics, nothing done;
**	HELP$_NOMEM	- allocation failure, already signalled.
**--
*/
int	hlb$bld_facility	(
		HLB$BLD *	a_bld,
		unsigned	a_faclen,
	const	char *		a_fac
			)
{
int		l_status;

	/* The root has to be the first key, or it would not be the root */
	if ( a_bld->keycnt )
		return	STS$K_WARN;

	if ( !(1 & (l_status = hlb$bld_addkey(a_bld, 1, a_faclen, a_fac, NULL))) )
		return	l_status;

	a_bld->lvlbase = 1;

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Release a builder context together with every key and every text
**	buffer that has been accumulated in it.
**
**  FORMAL PARAMETERS:
**
**	a_bld	The context, may be NULL
**
**  IMPLICIT INPUTS/OUTPUTS:
**	Empties the key queue of the context.
**
**  RETURN VALUE:
**	STS$K_SUCCESS always.
**--
*/
int	hlb$bld_delete	(
		HLB$BLD *	a_bld
			)
{
HLB$BKEY *	l_bkey = NULL;
unsigned	l_qcnt = 0;

	if ( !a_bld )
		return	STS$K_SUCCESS;

	/*
	**  $REMQHEAD reports an empty queue by a zero count, not by a failure
	**  status, and it insists on a place to put that count - so the count
	**  is what terminates the loop.
	*/
	while ( (1 & $REMQHEAD(&a_bld->keyque, &l_bkey, &l_qcnt)) && l_qcnt )
		{
		if ( l_bkey->txt )
			free(l_bkey->txt);

		free(l_bkey);
		}

	free(a_bld);

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Link a key into the sibling chain of its parent, either at the
**	sorted position or at the tail, depending on the SORT attribute of
**	the builder.
**
**  FORMAL PARAMETERS:
**
**	a_bld	The builder context
**	a_bkey	The key to be linked, PARENT is already set
**
**  IMPLICIT INPUTS/OUTPUTS:
**	Updates the CHILD/NEXT chains of the tree and, at level 1, the
**	ROOT field of the context.
**
**  RETURN VALUE:
**	STS$K_SUCCESS always.
**--
*/
static	int	s_hlb$link	(
		HLB$BLD *	a_bld,
		HLB$BKEY *	a_bkey
			)
{
HLB$BKEY *	l_prev = NULL, *l_scan;
HLB$BKEY **	l_anchor = a_bkey->parent ? &a_bkey->parent->child : &a_bld->root;

	for ( l_scan = *l_anchor; l_scan; l_prev = l_scan, l_scan = l_scan->next )
		{
		if ( !a_bld->sort )
			continue;		/* /NOSORT - always run to the tail	*/

		if ( 0 < s_hlb$keycmp(l_scan->keylen, l_scan->key, a_bkey->keylen, a_bkey->key) )
			break;			/* The insertion point has been found	*/
		}

	a_bkey->next = l_scan;

	if ( l_prev )
		l_prev->next = a_bkey;
	else	*l_anchor = a_bkey;

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Create a topic in the library being built and make it the current
**	one, so that the text that follows is appended to it.
**
**	The nesting level requested by the caller is clamped to one more
**	than the level of the deepest open topic: a source that jumps from
**	level 1 straight to level 3 gets a level 2 topic, and the caller
**	is expected to have signalled HELP$_SKIPLVL.
**
**  FORMAL PARAMETERS:
**
**	a_bld		The builder context
**	a_level		Requested nesting level, 1 .. HLB$K_MAXLVL
**	a_keylen	Length of the key
**	a_key		Address of the key, any case
**	a_bkey		Receives the address of the created key, may be NULL
**
**  IMPLICIT INPUTS/OUTPUTS:
**	Updates CURR, KEYCNT and the key queue of the context.
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- the topic has been created;
**	HELP$_TOODEEP	- the nesting limit was reached and the level was
**			  clamped, the topic has still been created;
**	HELP$_DUPKEY	- a sibling with the same key already exists, the
**			  topic has still been created;
**	HELP$_NOMEM	- allocation failure, already signalled.
**--
*/
int	hlb$bld_addkey	(
		HLB$BLD *	a_bld,
		unsigned	a_level,
		unsigned	a_keylen,
	const	char *		a_key,
		HLB$BKEY **	a_bkey
			)
{
HLB$BKEY *	l_bkey, *l_parent, *l_scan;
unsigned	l_level = a_level, l_keylen = 0, l_qcnt = 0;
int		l_status = STS$K_SUCCESS;

	if ( a_bkey )
		*a_bkey = NULL;

	/*
	**  A library rooted at its facility carries the whole source one
	**  level down, under the single key of level 1 that names the
	**  facility - see HLB$BLD_FACILITY.
	*/
	l_level += a_bld->lvlbase;

	/* Clamp the level to one below the deepest currently open topic */
	if ( l_level > ((a_bld->curr ? a_bld->curr->level : 0) + 1) )
		l_level = (a_bld->curr ? a_bld->curr->level : 0) + 1;

	if ( l_level < 1 )
		l_level = 1;

	/*
	**  The nesting has run out.  This is worth a word even though the
	**  reader of the source already checks the depth: a facility root
	**  spends one level of the nine, so a source that fits on its own
	**  does not fit under a root, and the topic that overflows is
	**  silently flattened onto its parent.
	*/
	if ( l_level > HLB$K_MAXLVL )
		{
		l_level  = HLB$K_MAXLVL;
		l_status = HELP$_TOODEEP;
		}

	/* Walk up from the current topic to the parent of the new one */
	for ( l_parent = a_bld->curr; l_parent && (l_parent->level >= l_level); l_parent = l_parent->parent )
		;

	if ( !(l_bkey = calloc(1, sizeof(HLB$BKEY))) )
		return	help$putmsg(HELP$_NOMEM, 3, (fao_prm_t) sizeof(HLB$BKEY), (fao_prm_t) errno,
					(fao_prm_t) strerror(errno));

	help$upcase(a_keylen, a_key, (unsigned) sizeof(l_bkey->key), l_bkey->key, &l_keylen);

	l_bkey->keylen	= l_keylen;
	l_bkey->level	= l_level;
	l_bkey->parent	= l_parent;
	l_bkey->index	= -1;

	/* Report - but do not reject - a key that duplicates a sibling */
	for ( l_scan = l_parent ? l_parent->child : a_bld->root; l_scan; l_scan = l_scan->next )
		{
		if ( !s_hlb$keycmp(l_scan->keylen, l_scan->key, l_bkey->keylen, l_bkey->key) )
			{
			l_status = HELP$_DUPKEY;
			break;
			}
		}

	s_hlb$link(a_bld, l_bkey);

	/* The queue owns the key for the purpose of releasing it later on */
	if ( !(1 & $INSQTAIL(&a_bld->keyque, l_bkey, &l_qcnt)) )
		{
		l_bkey->key [$MIN(l_keylen, (unsigned) sizeof(l_bkey->key) - 1)] = '\0';

		help$putmsg(HELP$_QUEFAIL, 1, (fao_prm_t) l_bkey->key);
		}

	a_bld->curr = l_bkey;
	a_bld->keycnt++;

	if ( a_bkey )
		*a_bkey = l_bkey;

	$TRACE("level=%u, key='%.*s', keycnt=%u", l_level, (int) l_keylen, l_bkey->key, a_bld->keycnt);

	return	l_status;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Append a block of text to the current topic of the builder,
**	growing the buffer of the topic in HLB$K_TXTCHUNK steps.
**
**  FORMAL PARAMETERS:
**
**	a_bld	The builder context
**	a_len	Length of the block, bytes
**	a_txt	Address of the block
**
**  IMPLICIT INPUTS/OUTPUTS:
**	Updates the text buffer of the current topic and TXTSZ of the
**	context.
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- the block has been appended;
**	STS$K_WARN	- there is no current topic, the block is dropped;
**	HELP$_NOMEM	- allocation failure, already signalled.
**--
*/
int	hlb$bld_addtext	(
		HLB$BLD *	a_bld,
		unsigned	a_len,
	const	char *		a_txt
			)
{
HLB$BKEY *	l_bkey = a_bld->curr;
char *		l_new;
unsigned	l_need;

	if ( !l_bkey )
		return	STS$K_WARN;

	if ( !a_len )
		return	STS$K_SUCCESS;

	l_need = l_bkey->txtlen + a_len;

	if ( l_need > l_bkey->txtsz )
		{
		l_need = ((l_need + HLB$K_TXTCHUNK) / HLB$K_TXTCHUNK) * HLB$K_TXTCHUNK;

		if ( !(l_new = realloc(l_bkey->txt, l_need)) )
			return	help$putmsg(HELP$_NOMEM, 3, (fao_prm_t) l_need, (fao_prm_t) errno,
						(fao_prm_t) strerror(errno));

		l_bkey->txt	= l_new;
		l_bkey->txtsz	= l_need;
		}

	memcpy(l_bkey->txt + l_bkey->txtlen, a_txt, a_len);

	l_bkey->txtlen	+= a_len;
	a_bld->txtsz	+= a_len;

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Assign the index of every key of a sibling chain and of every
**	subtree below it, walking the tree in pre-order.  The recursion
**	descends into the subtopics only, so its depth never exceeds
**	HLB$K_MAXLVL.
**
**  FORMAL PARAMETERS:
**
**	a_bkey	The first key of the chain, may be NULL
**	a_index	The index counter, updated in place
**
**  IMPLICIT INPUTS/OUTPUTS:
**	Sets the INDEX field of every key visited.
**
**  RETURN VALUE:
**	STS$K_SUCCESS always.
**--
*/
static	int	s_hlb$number	(
		HLB$BKEY *	a_bkey,
		int *		a_index
			)
{
	for ( HLB$BKEY *l_scan = a_bkey; l_scan; l_scan = l_scan->next )
		{
		l_scan->index = (*a_index)++;

		s_hlb$number(l_scan->child, a_index);
		}

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Materialise a sibling chain and everything below it into the key
**	array and the text heap of the library image.
**
**  FORMAL PARAMETERS:
**
**	a_bkey		The first key of the chain, may be NULL
**	a_keys		The key array being filled
**	a_heap		The text heap being filled
**	a_txtoff	The heap high-water mark, updated in place
**
**  IMPLICIT INPUTS/OUTPUTS:
**	None.
**
**  RETURN VALUE:
**	STS$K_SUCCESS always.
**--
*/
static	int	s_hlb$emit	(
		HLB$BKEY *	a_bkey,
		HLB$KEY *	a_keys,
		char *		a_heap,
		unsigned *	a_txtoff
			)
{
HLB$KEY *	l_key;

	for ( HLB$BKEY *l_scan = a_bkey; l_scan; l_scan = l_scan->next )
		{
		l_key = &a_keys [l_scan->index];

		l_key->parent	= l_scan->parent ? l_scan->parent->index : -1;
		l_key->child	= l_scan->child  ? l_scan->child->index  : -1;
		l_key->next	= l_scan->next   ? l_scan->next->index   : -1;
		l_key->level	= (uint16_t) l_scan->level;
		l_key->keylen	= (uint16_t) l_scan->keylen;

		memcpy(l_key->key, l_scan->key, l_scan->keylen);

		l_key->txtoff	= *a_txtoff;
		l_key->txtlen	= l_scan->txtlen;

		if ( l_scan->txtlen )
			memcpy(a_heap + *a_txtoff, l_scan->txt, l_scan->txtlen);

		*a_txtoff += l_scan->txtlen;

		s_hlb$emit(l_scan->child, a_keys, a_heap, a_txtoff);
		}

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Flatten the accumulated topic tree into a help library file.  The
**	file is written to a temporary name in the same directory and is
**	renamed over the target only after the last byte has reached the
**	file system, so that a failed run never leaves a half-written
**	library behind.
**
**  FORMAL PARAMETERS:
**
**	a_bld	The builder context
**	a_fspec	File specification of the library to be created
**
**  IMPLICIT INPUTS/OUTPUTS:
**	Creates the file <a_fspec>.
**
**  RETURN VALUE:
**	STS$K_SUCCESS or a HELP$_EMPTYLIB / HELP$_NOMEM / HELP$_OPENOUT /
**	HELP$_WRITERR condition, already signalled.
**--
*/
int	hlb$bld_write	(
		HLB$BLD *	a_bld,
	const	char *		a_fspec
			)
{
HLB$HDR		l_hdr = {0};
HLB$KEY *	l_keys = NULL;
char *		l_heap = NULL;
char		l_tmp [HELP$K_SZ_PATH];
unsigned short	l_tmplen = 0;
fao_desc_t	l_tmpdsc = $FAO_DESC(l_tmp);
unsigned	l_txtoff = 0, l_keysz;
int		l_fd = -1, l_index = 0, l_status = STS$K_SUCCESS;

	if ( !a_bld->keycnt )
		return	help$putmsg(HELP$_EMPTYLIB, 0);

	/* Assign the indices in pre-order and allocate the image */
	s_hlb$number(a_bld->root, &l_index);

	l_keysz = a_bld->keycnt * (unsigned) sizeof(HLB$KEY);

	if ( !(l_keys = calloc(1, l_keysz)) )
		return	help$putmsg(HELP$_NOMEM, 3, (fao_prm_t) l_keysz, (fao_prm_t) errno, (fao_prm_t) strerror(errno));

	if ( !(l_heap = calloc(1, a_bld->txtsz ? a_bld->txtsz : 1)) )
		{
		free(l_keys);

		return	help$putmsg(HELP$_NOMEM, 3, (fao_prm_t) a_bld->txtsz, (fao_prm_t) errno,
					(fao_prm_t) strerror(errno));
		}

	s_hlb$emit(a_bld->root, l_keys, l_heap, &l_txtoff);

	/* Compose the header; the CRC covers the index and the heap */
	l_hdr.magic	= HLB$K_MAGIC;
	l_hdr.vers	= HLB$K_VERSION;
	l_hdr.rev	= HLB$K_REVISION;
	l_hdr.hdrsz	= (uint32_t) sizeof(HLB$HDR);
	l_hdr.keysz	= (uint32_t) sizeof(HLB$KEY);
	l_hdr.keycnt	= a_bld->keycnt;
	l_hdr.keyoff	= (uint32_t) sizeof(HLB$HDR);
	l_hdr.txtoff	= (uint32_t) sizeof(HLB$HDR) + l_keysz;
	l_hdr.txtsz	= l_txtoff;
	l_hdr.credat	= (uint64_t) time(NULL);

	help$strcpy((unsigned) sizeof(l_hdr.ident), l_hdr.ident, a_bld->ident);
	help$strcpy((unsigned) sizeof(l_hdr.creator), l_hdr.creator, a_bld->creator);

	l_hdr.crc	= __util$crc32c(0, l_keys, l_keysz);
	l_hdr.crc	= __util$crc32c(l_hdr.crc, l_heap, l_txtoff);

	/* Write into a sibling temporary file, then rename over the target */
	__util$fao("!AZ.tmp", &l_tmplen, &l_tmpdsc, (fao_prm_t) a_fspec);
	l_tmp [$MIN(l_tmplen, (unsigned short) sizeof(l_tmp) - 1)] = '\0';

	if ( 0 > (l_fd = open(l_tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644)) )
		{
		l_status = help$putmsg(HELP$_OPENOUT, 3, (fao_prm_t) l_tmp, (fao_prm_t) errno,
					(fao_prm_t) strerror(errno));
		}
	else if ( (sizeof(HLB$HDR) != (size_t) write(l_fd, &l_hdr, sizeof(HLB$HDR)))
		|| (l_keysz != (unsigned) write(l_fd, l_keys, l_keysz))
		|| (l_txtoff && (l_txtoff != (unsigned) write(l_fd, l_heap, l_txtoff))) )
		{
		l_status = help$putmsg(HELP$_WRITERR, 3, (fao_prm_t) l_tmp, (fao_prm_t) errno,
					(fao_prm_t) strerror(errno));
		}

	if ( l_fd >= 0 )
		{
		if ( close(l_fd) && (1 & l_status) )
			l_status = help$putmsg(HELP$_WRITERR, 3, (fao_prm_t) l_tmp, (fao_prm_t) errno,
						(fao_prm_t) strerror(errno));
		}

	if ( (1 & l_status) && rename(l_tmp, a_fspec) )
		l_status = help$putmsg(HELP$_WRITERR, 3, (fao_prm_t) a_fspec, (fao_prm_t) errno,
					(fao_prm_t) strerror(errno));

	if ( !(1 & l_status) )
		unlink(l_tmp);

	free(l_keys);
	free(l_heap);

	$TRACE("%s: keycnt=%u, txtsz=%u, crc=%#x, status=%#x", a_fspec, a_bld->keycnt, l_txtoff, l_hdr.crc, l_status);

	return	l_status;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Open a help library: read the file whole, check the magic, the
**	format version, the internal consistency of the section table and
**	the checksum, and project the header, the index and the heap onto
**	the image.
**
**  FORMAL PARAMETERS:
**
**	a_fspec	File specification of the library
**	a_lib	Receives the address of the library context
**
**  IMPLICIT INPUTS/OUTPUTS:
**	Allocates memory to be released by HLB$CLOSE.
**
**  RETURN VALUE:
**	STS$K_SUCCESS or a HELP$_OPENIN / HELP$_NOTHLB / HELP$_BADVERS /
**	HELP$_TRUNCLIB / HELP$_BADCRC / HELP$_NOMEM condition, already
**	signalled.
**--
*/
int	hlb$open	(
	const	char *		a_fspec,
		HLB$LIB **	a_lib
			)
{
HLB$LIB *	l_lib;
HLB$HDR *	l_hdr;
char *		l_buf = NULL;
unsigned	l_buflen = 0, l_crc, l_vers, l_need, l_want;
int		l_status;

	*a_lib = NULL;

	if ( !(1 & (l_status = help$readfile(a_fspec, &l_buf, &l_buflen))) )
		return	l_status;

	if ( !(l_lib = calloc(1, sizeof(HLB$LIB))) )
		{
		free(l_buf);

		return	help$putmsg(HELP$_NOMEM, 3, (fao_prm_t) sizeof(HLB$LIB), (fao_prm_t) errno,
					(fao_prm_t) strerror(errno));
		}

	l_lib->base	= l_buf;
	l_lib->size	= l_buflen;
	l_lib->hdr	= l_hdr = (HLB$HDR *) l_buf;

	help$strcpy((unsigned) sizeof(l_lib->fspec), l_lib->fspec, a_fspec);

	/* The magic also catches a library produced on a host of the opposite endianness */
	if ( (l_buflen < sizeof(HLB$HDR)) || (l_hdr->magic != HLB$K_MAGIC) )
		{
		hlb$close(l_lib);

		return	help$putmsg(HELP$_NOTHLB, 1, (fao_prm_t) a_fspec);
		}

	if ( (l_hdr->vers != HLB$K_VERSION) || (l_hdr->hdrsz != sizeof(HLB$HDR)) || (l_hdr->keysz != sizeof(HLB$KEY)) )
		{
		/* The header lives in the image - read it out before the image goes */
		l_vers = l_hdr->vers;

		hlb$close(l_lib);

		return	help$putmsg(HELP$_BADVERS, 3, (fao_prm_t) a_fspec, (fao_prm_t) l_vers,
					(fao_prm_t) HLB$K_VERSION);
		}

	/* Every section must lie inside the file - checked before any of them is touched */
	if ( ((size_t) l_hdr->keyoff + (size_t) l_hdr->keycnt * sizeof(HLB$KEY) > l_buflen)
		|| ((size_t) l_hdr->txtoff + (size_t) l_hdr->txtsz > l_buflen) )
		{
		l_need = l_hdr->txtoff + l_hdr->txtsz;

		hlb$close(l_lib);

		return	help$putmsg(HELP$_TRUNCLIB, 3, (fao_prm_t) a_fspec, (fao_prm_t) l_buflen,
					(fao_prm_t) l_need);
		}

	l_lib->keys	= (HLB$KEY *) (l_buf + l_hdr->keyoff);
	l_lib->txt	= l_buf + l_hdr->txtoff;
	l_lib->keycnt	= l_hdr->keycnt;

	l_crc = __util$crc32c(0, l_lib->keys, (size_t) l_hdr->keycnt * sizeof(HLB$KEY));
	l_crc = __util$crc32c(l_crc, l_lib->txt, l_hdr->txtsz);

	if ( l_crc != l_hdr->crc )
		{
		l_want = l_hdr->crc;

		hlb$close(l_lib);

		return	help$putmsg(HELP$_BADCRC, 3, (fao_prm_t) a_fspec, (fao_prm_t) l_crc, (fao_prm_t) l_want);
		}

	*a_lib = l_lib;

	$TRACE("%s: keycnt=%u, txtsz=%u, ident='%s'", a_fspec, l_lib->keycnt, l_hdr->txtsz, l_hdr->ident);

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Close a help library and release its image.
**
**  FORMAL PARAMETERS:
**
**	a_lib	The library context, may be NULL
**
**  IMPLICIT INPUTS/OUTPUTS:
**	None.
**
**  RETURN VALUE:
**	STS$K_SUCCESS always.
**--
*/
int	hlb$close	(
		HLB$LIB *	a_lib
			)
{
	if ( !a_lib )
		return	STS$K_SUCCESS;

	if ( a_lib->base )
		free(a_lib->base);

	free(a_lib);

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Return the address of a key record by its index, refusing an index
**	that is out of the range of the library.
**
**  FORMAL PARAMETERS:
**
**	a_lib	The library context
**	a_index	Index of the key
**	a_key	Receives the address of the key record
**
**  IMPLICIT INPUTS/OUTPUTS:
**	None.
**
**  RETURN VALUE:
**	STS$K_SUCCESS - <a_key> holds the address;
**	STS$K_WARN    - the index is out of range.
**--
*/
int	hlb$getkey	(
		HLB$LIB *	a_lib,
		int		a_index,
		HLB$KEY **	a_key
			)
{
	if ( (a_index < 0) || (a_index >= (int) a_lib->keycnt) )
		return	STS$K_WARN;

	*a_key = &a_lib->keys [a_index];

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Return the text of a topic.  The text is not zero-terminated - it
**	is a run inside the heap - so the length is returned with it.
**
**  FORMAL PARAMETERS:
**
**	a_lib	The library context
**	a_index	Index of the key
**	a_len	Receives the length of the text
**	a_txt	Receives the address of the text
**
**  IMPLICIT INPUTS/OUTPUTS:
**	None.
**
**  RETURN VALUE:
**	STS$K_SUCCESS - the topic has a text;
**	STS$K_WARN    - the index is out of range or the topic has no text.
**--
*/
int	hlb$gettext	(
		HLB$LIB *	a_lib,
		int		a_index,
		unsigned *	a_len,
	const	char **		a_txt
			)
{
HLB$KEY *	l_key;

	*a_len = 0;
	*a_txt = NULL;

	if ( !(1 & hlb$getkey(a_lib, a_index, &l_key)) )
		return	STS$K_WARN;

	if ( !l_key->txtlen )
		return	STS$K_WARN;

	/* The run must lie inside the heap - a corrupted index is not trusted */
	if ( ((size_t) l_key->txtoff + l_key->txtlen) > a_lib->hdr->txtsz )
		return	help$putmsg(HELP$_BADRUN, 4, (fao_prm_t) a_index, (fao_prm_t) l_key->txtoff,
					(fao_prm_t) l_key->txtlen, (fao_prm_t) a_lib->hdr->txtsz);

	*a_len = l_key->txtlen;
	*a_txt = a_lib->txt + l_key->txtoff;

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Return the index of the first subtopic of a key, or of the first
**	topic of level 1 when the parent is -1.
**
**  FORMAL PARAMETERS:
**
**	a_lib		The library context
**	a_parent	Index of the parent, -1 - the top level
**	a_index		Receives the index of the first subtopic
**
**  IMPLICIT INPUTS/OUTPUTS:
**	None.
**
**  RETURN VALUE:
**	STS$K_SUCCESS - <a_index> holds the index;
**	STS$K_WARN    - the key has no subtopics.
**--
*/
int	hlb$firstchild	(
		HLB$LIB *	a_lib,
		int		a_parent,
		int *		a_index
			)
{
HLB$KEY *	l_key;

	*a_index = -1;

	if ( a_parent < 0 )
		{
		/* The top level chain always starts at index 0 - the pre-order guarantees it */
		if ( !a_lib->keycnt )
			return	STS$K_WARN;

		*a_index = 0;

		return	STS$K_SUCCESS;
		}

	if ( !(1 & hlb$getkey(a_lib, a_parent, &l_key)) || (l_key->child < 0) )
		return	STS$K_WARN;

	*a_index = l_key->child;

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Find a subtopic of a key by its name.  An exact match wins; failing
**	that, a leading abbreviation is accepted, the way the OpenVMS HELP
**	accepts one.  When several subtopics share the abbreviation the
**	first of them is returned together with a warning, which lets the
**	caller fall back to a wildcard display of all the candidates.
**
**  FORMAL PARAMETERS:
**
**	a_lib		The library context
**	a_parent	Index of the parent, -1 - the top level
**	a_keylen	Length of the name being looked up
**	a_key		Address of the name, any case
**	a_index		Receives the index of the subtopic
**
**  IMPLICIT INPUTS/OUTPUTS:
**	None.
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- an unambiguous match, <a_index> holds its index;
**	STS$K_WARN	- the abbreviation is ambiguous, <a_index> holds
**			  the index of the first candidate;
**	STS$K_ERROR	- no match at all.
**--
*/
int	hlb$lookup	(
		HLB$LIB *	a_lib,
		int		a_parent,
		unsigned	a_keylen,
	const	char *		a_key,
		int *		a_index
			)
{
HLB$KEY *	l_key;
char		l_ups [HLB$K_SZ_KEY];
unsigned	l_upslen = 0;
int		l_scan, l_first = -1, l_cnt = 0;

	*a_index = -1;

	help$upcase(a_keylen, a_key, (unsigned) sizeof(l_ups), l_ups, &l_upslen);

	if ( !l_upslen )
		return	STS$K_ERROR;

	if ( !(1 & hlb$firstchild(a_lib, a_parent, &l_scan)) )
		return	STS$K_ERROR;

	for ( ; l_scan >= 0; l_scan = l_key->next )
		{
		if ( !(1 & hlb$getkey(a_lib, l_scan, &l_key)) )
			break;

		if ( (l_key->keylen == l_upslen) && !memcmp(l_key->key, l_ups, l_upslen) )
			{
			*a_index = l_scan;

			return	STS$K_SUCCESS;		/* An exact match ends the search */
			}

		if ( (l_key->keylen > l_upslen) && !memcmp(l_key->key, l_ups, l_upslen) )
			{
			if ( l_first < 0 )
				l_first = l_scan;

			l_cnt++;
			}
		}

	if ( l_first < 0 )
		return	STS$K_ERROR;

	*a_index = l_first;

	return	(l_cnt == 1) ? STS$K_SUCCESS : STS$K_WARN;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Find a subtopic by a run of words, taking the longest run that
**	matches.  A key is allowed to carry blanks - SOURCE FORMAT is one
**	key, not two - and the command line has already been cut into words
**	by the shell or by the prompt reader, so the two have to be brought
**	back together here.
**
**	The run is tried longest first: of "HELPTRAN SOURCE FORMAT" the
**	three-word and the two-word runs fail, the one-word run selects
**	HELPTRAN, and the caller then offers "SOURCE FORMAT", which matches
**	whole.  Longest-first is what makes a specific key win over the
**	shorter one it begins with.
**
**	Only an unambiguous match is accepted for a run of more than one
**	word: an abbreviation that fans out is meaningful for a single word
**	- the caller turns it into a wildcard display - but not for a run,
**	where it would silently swallow the words that follow.
**
**  FORMAL PARAMETERS:
**
**	a_lib		The library context
**	a_parent	Index of the parent, -1 - the top level
**	a_wordcnt	Number of the words available
**	a_words		The words, ASCIZ
**	a_used		Receives the number of the words consumed
**	a_index		Receives the index of the subtopic
**
**  IMPLICIT INPUTS/OUTPUTS:
**	None.
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- an unambiguous match;
**	STS$K_WARN	- a single word matched ambiguously, <a_index> holds
**			  the first candidate and <a_used> is one;
**	STS$K_ERROR	- no run matched.
**--
*/
int	hlb$lookup_run	(
		HLB$LIB *	a_lib,
		int		a_parent,
		int		a_wordcnt,
		char **		a_words,
		int *		a_used,
		int *		a_index
			)
{
char		l_join [HLB$K_SZ_KEY];
unsigned	l_len, l_wordlen;
int		l_status = STS$K_ERROR;

	*a_used		= 0;
	*a_index	= -1;

	for ( int l_run = $MIN(a_wordcnt, HLB$K_MAXLVL); l_run >= 1; l_run-- )
		{
		/* Join the run with a single blank; a run that cannot fit is not a key */
		for ( int i = 0, l_len2 = 0; ; i++ )
			{
			if ( i >= l_run )
				{
				l_len = (unsigned) l_len2;
				break;
				}

			l_wordlen = (unsigned) strlen(a_words[i]);

			if ( ((unsigned) l_len2 + l_wordlen + (i ? 1 : 0)) > sizeof(l_join) )
				{
				l_len = 0;
				break;
				}

			if ( i )
				l_join [l_len2++] = ' ';

			memcpy(l_join + l_len2, a_words[i], l_wordlen);
			l_len2 += (int) l_wordlen;
			}

		if ( !l_len )
			continue;

		l_status = hlb$lookup(a_lib, a_parent, l_len, l_join, a_index);

		if ( $STS_ERROR(l_status) )
			continue;

		/* An ambiguous match is accepted for a single word only */
		if ( (l_run > 1) && (l_status != STS$K_SUCCESS) )
			continue;

		*a_used = l_run;

		return	l_status;
		}

	*a_index = -1;

	return	STS$K_ERROR;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Find the next subtopic of a key whose name matches a wildcard
**	pattern.  The scan is resumable: <a_from> is the index the search
**	starts at, -1 meaning the first subtopic.
**
**  FORMAL PARAMETERS:
**
**	a_lib		The library context
**	a_parent	Index of the parent, -1 - the top level
**	a_pattern	Pattern, ASCIZ, any case; the wildcards are those of
**			HELP$K_WILDCARD - '*' for any run of characters,
**			'%' and '?' for any one of them
**	a_from		Index to resume at, -1 - start from the beginning
**	a_index		Receives the index of the matching subtopic
**
**  IMPLICIT INPUTS/OUTPUTS:
**	None.
**
**  RETURN VALUE:
**	STS$K_SUCCESS - a match has been found;
**	STS$K_WARN    - the chain is exhausted.
**--
*/
int	hlb$search	(
		HLB$LIB *	a_lib,
		int		a_parent,
	const	char *		a_pattern,
		int		a_from,
		int *		a_index
			)
{
HLB$KEY *	l_key;
char		l_pat [HLB$K_SZ_KEY + 1], l_key_z [HLB$K_SZ_KEY + 1];
unsigned	l_patlen = 0;
int		l_scan = a_from;

	*a_index = -1;

	help$upcase((unsigned) strnlen(a_pattern, HLB$K_SZ_KEY), a_pattern, HLB$K_SZ_KEY, l_pat, &l_patlen);
	l_pat [l_patlen] = '\0';

	if ( (l_scan < 0) && !(1 & hlb$firstchild(a_lib, a_parent, &l_scan)) )
		return	STS$K_WARN;

	for ( ; l_scan >= 0; l_scan = l_key->next )
		{
		if ( !(1 & hlb$getkey(a_lib, l_scan, &l_key)) )
			break;

		memcpy(l_key_z, l_key->key, l_key->keylen);
		l_key_z [l_key->keylen] = '\0';

		if ( !__util$pattern_match(l_key_z, l_pat) )
			continue;

		*a_index = l_scan;

		return	STS$K_SUCCESS;
		}

	return	STS$K_WARN;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Compose the full path of a topic - the keys from level 1 down to
**	the topic itself, separated by a blank - as it appears in the
**	prompt of the HELP utility.
**
**  FORMAL PARAMETERS:
**
**	a_lib		The library context
**	a_index		Index of the topic
**	a_bufsz		Capacity of the buffer
**	a_buf		Receives the path, ASCIZ
**	a_buflen	Receives the length of the path, may be NULL
**
**  IMPLICIT INPUTS/OUTPUTS:
**	None.
**
**  RETURN VALUE:
**	STS$K_SUCCESS - the path has been composed;
**	STS$K_WARN    - the index is out of range, the buffer holds an
**			empty string.
**--
*/
int	hlb$path	(
		HLB$LIB *	a_lib,
		int		a_index,
		unsigned	a_bufsz,
		char *		a_buf,
		unsigned *	a_buflen
			)
{
HLB$KEY *	l_key;
int		l_chain [HLB$K_MAXLVL + 1];
unsigned	l_off = 0;
int		l_depth = 0, l_scan = a_index;

	a_buf[0] = '\0';

	if ( a_buflen )
		*a_buflen = 0;

	/* Collect the chain bottom-up, then emit it top-down */
	while ( (l_scan >= 0) && (l_depth < (int) $ARRSZ(l_chain)) )
		{
		if ( !(1 & hlb$getkey(a_lib, l_scan, &l_key)) )
			break;

		l_chain [l_depth++] = l_scan;
		l_scan = l_key->parent;
		}

	if ( !l_depth )
		return	STS$K_WARN;

	for ( int i = l_depth - 1; i >= 0; i-- )
		{
		hlb$getkey(a_lib, l_chain[i], &l_key);

		if ( (l_off + l_key->keylen + 2) > a_bufsz )
			break;

		if ( l_off )
			a_buf [l_off++] = ' ';

		memcpy(a_buf + l_off, l_key->key, l_key->keylen);
		l_off += l_key->keylen;
		}

	a_buf [l_off] = '\0';

	if ( a_buflen )
		*a_buflen = l_off;

	return	STS$K_SUCCESS;
}
