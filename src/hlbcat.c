#define	__MODULE__	"HLBCAT"
#define	__IDENT__	"X01-01"
#define	__REV__		"01.01.0"

/*
**++
**
**  FACILITY:	HELP - OpenVMS-style help library subsystem for Linux
**
**  MODULE:	hlbcat.c
**
**  ABSTRACT:	The catalogue of the help libraries reachable through a
**		search path, and the cache that spares the utility from
**		rebuilding it on every run.
**
**  DESCRIPTION: A search path is a list of elements separated by a colon,
**		in the manner of PATH.  An element is either a help library
**		or a directory, which is scanned for files of the type
**		.hlb; a directory is read in the collating order of the
**		names, so a catalogue does not depend on the order the file
**		system happens to return.
**
**		Building the catalogue means opening every library and
**		harvesting its topics of level 1 - the only ones needed to
**		decide which library answers a question.  When a topic is
**		defined by more than one library the first on the path
**		wins, as a command found earlier on PATH does.
**
**		Opening every library on every run of HELP would be wasted
**		work, so the catalogue is cached.  The cache is validated
**		against the modification time and the size of every element
**		it was built from - including the directories, whose time
**		changes when a library is added to them or removed - and is
**		rebuilt whenever anything disagrees.  A cache that cannot
**		be read, written or trusted is not an error: it is dropped
**		and the catalogue is built directly.
**
**		A modification time is only good to the second on many file
**		systems, so an element changed in the very second the cache
**		was written would carry the time the cache already records
**		and would pass the test for ever after.  An element whose
**		time is not strictly older than the cache is therefore not
**		trusted at all: the catalogue is rebuilt, and the rebuild
**		carries a later time, so the suspicion lasts one second and
**		clears itself.
**
**		The cache lives in the first of these that can be had:
**
**		    $HELP_CACHE			named outright
**		    $XDG_CACHE_HOME/help/	the usual place
**		    $HOME/.cache/help/		when XDG_CACHE_HOME is unset
**		    /tmp/help-<uid>/		when there is no home at all
**
**		The last of these is a directory anyone can write to, so
**		the cache is treated as hostile there: the directory and
**		the file must be owned by this user and readable by no one
**		else, the file must be a regular file rather than a link to
**		one, and a cache that fails any of those tests is refused
**		rather than repaired.  The worst a hostile cache could do
**		is name a library that does not answer the question asked -
**		the library itself is opened, checksummed and validated the
**		usual way - but a stale answer to a question about, say, a
**		system command is a poor thing to hand anybody.
**
**		The tests are made once, on a descriptor of the directory,
**		and everything afterwards is done relative to it - OPENAT,
**		RENAMEAT, UNLINKAT, FCHMODAT.  Testing a name and then
**		using that name again would leave the interval between the
**		two open to anyone who can rename in the parent directory:
**		the name would be resolved a second time, and there is no
**		saying it still means what it meant.  A descriptor cannot
**		be redirected once opened, so the object inspected is the
**		object used.  Whether the parent carries the sticky bit
**		then stops mattering, which is the point: the arrangement
**		is not to be trusted, only the descriptor is.
**
**  AUTHOR:	StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:   1-AUG-2026
**
**  MODIFICATION HISTORY:
**
**	X01-01		23-OCT-2026	RRL
**		Windows x64 (MinGW-w64, for yaVNA's yavna.exe).  The search
**		path is separated by HELP$K_PATHSEP (';' there).  There is no
**		cache on Windows, private or shared: the checks that make
**		one safe - a descriptor of the directory, OPENAT and the rest
**		relative to it, the owner and the mode - have no counterpart
**		in its C RTL, and a cache that cannot be trusted is not had
**		at all, as the description above says; the catalogue is
**		built on every run, which for the handful of libraries of a
**		Windows installation costs nothing worth saving.
**		VHELP/SYSCACHE says so (HELP$_CACHERR).  The image is
**		located with GetModuleFileName, its directory ending at the
**		last '\' or '/'; the null device is NUL.
**
**	X01-00		 1-AUG-2026	RRL
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
#include	<dirent.h>
#include	<sys/stat.h>
#include	<sys/types.h>

#ifdef	_WIN32
#include	<windows.h>
#endif

#include	"helpdef.h"

#define	CAT$K_SEP	HELP$K_PATHSEP		/* Separator of the search path		*/

/*
**  S_CAT$SCANDIR calls it and stands ahead of it, the two being written in
**  the order a reader follows rather than the order the compiler needs.
*/
static	int	s_cat$reuse	(HLB$CAT *a_cat, HLB$CAT *a_store, const char *a_fspec, int a_isdir);


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Append one element to a search path being composed, unless it is
**	already there.  A path that would overflow is left as it stands -
**	an element that cannot be recorded is better dropped than half
**	written.
**
**  FORMAL PARAMETERS:
**
**	a_bufsz	Capacity of the path buffer
**	a_buf	The path, ASCIZ, extended in place
**	a_elem	The element to be appended, ASCIZ
**
**  IMPLICIT INPUTS/OUTPUTS:
**	None.
**
**  RETURN VALUE:
**	STS$K_SUCCESS - the element is on the path, now or already;
**	STS$K_WARN    - there is no room for it.
**--
*/
static	int	s_cat$append	(
		unsigned	a_bufsz,
		char *		a_buf,
	const	char *		a_elem
			)
{
const char *	l_scan;
unsigned	l_len = (unsigned) strlen(a_buf), l_elen = (unsigned) strlen(a_elem);

	if ( !l_elen )
		return	STS$K_SUCCESS;

	/* An element already on the path is not repeated */
	for ( l_scan = a_buf; *l_scan; )
		{
	const char *	l_end = strchr(l_scan, CAT$K_SEP);
	unsigned	l_this = l_end ? (unsigned) (l_end - l_scan) : (unsigned) strlen(l_scan);

		if ( (l_this == l_elen) && !memcmp(l_scan, a_elem, l_elen) )
			return	STS$K_SUCCESS;

		l_scan += l_this + (l_end ? 1 : 0);
		}

	if ( (l_len + l_elen + 2) > a_bufsz )
		return	STS$K_WARN;

	if ( l_len )
		a_buf [l_len++] = CAT$K_SEP;

	memcpy(a_buf + l_len, a_elem, l_elen + 1);

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Name the language the user asked for, out of the environment: the
**	first component of LC_MESSAGES, LC_ALL or LANG, whichever is set
**	first, with the territory and the encoding cut off - so that
**	ru_RU.UTF-8 yields ru, which is the name of the directory a
**	translation is installed into.
**
**	The C and POSIX locales name no language: they are the absence of
**	a request, not a request for English.
**
**  FORMAL PARAMETERS:
**
**	a_buf	Receives the language, ASCIZ
**	a_bufsz	Capacity of the buffer
**
**  IMPLICIT INPUTS/OUTPUTS:
**	Reads the environment.
**
**  RETURN VALUE:
**	STS$K_SUCCESS - a language has been named;
**	STS$K_WARN    - none has.
**--
*/
static	int	s_cat$language	(
		char *		a_buf,
		unsigned	a_bufsz
			)
{
const char *	l_env = NULL;
unsigned	l_len = 0;

	a_buf[0] = '\0';

	if ( !((l_env = getenv("LC_MESSAGES")) && *l_env)
		&& !((l_env = getenv("LC_ALL")) && *l_env)
		&& !((l_env = getenv("LANG")) && *l_env) )
		return	STS$K_WARN;

	/* The language ends where the territory or the encoding begins */
	while ( (l_len < (a_bufsz - 1)) && l_env[l_len]
		&& (l_env[l_len] != '_') && (l_env[l_len] != '.') && (l_env[l_len] != '@') )
		{
		a_buf [l_len] = l_env [l_len];
		l_len++;
		}

	a_buf [l_len] = '\0';

	if ( !l_len || !strcmp(a_buf, "C") || !strcmp(a_buf, "POSIX") )
		return	STS$K_WARN;

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Append the library directory of the running image to a search path:
**	the directory the executable lives in, one level up, then the
**	library directory the product was configured with - so an image at
**	<somewhere>/bin/<image> finds <somewhere>/share/help whatever prefix
**	it was configured for.
**
**	The image is located through /proc/self/exe, which is what Linux
**	offers and what this product is for.  A system without it simply
**	contributes nothing here, and the built-in path stands alone.
**
**  FORMAL PARAMETERS:
**
**	a_bufsz	Capacity of the path buffer
**	a_buf	The path, ASCIZ, extended in place
**
**  IMPLICIT INPUTS/OUTPUTS:
**	None.
**
**  RETURN VALUE:
**	STS$K_SUCCESS - the directory has been appended;
**	STS$K_WARN    - the image could not be located.
**--
*/
static	int	s_cat$imagedir	(
		unsigned	a_bufsz,
		char *		a_buf
			)
{
char		l_exe [HELP$K_SZ_PATH], l_dir [HELP$K_SZ_PATH];
char		l_langdir [HELP$K_SZ_PATH], l_lang [32];
unsigned short	l_len = 0;
fao_desc_t	l_dsc = $FAO_DESC(l_dir), l_langdsc = $FAO_DESC(l_langdir);
#ifdef	_WIN32
DWORD		l_red;
char		*l_sep;

	if ( !(l_red = GetModuleFileNameA(NULL, l_exe, (DWORD) sizeof(l_exe))) || (l_red >= sizeof(l_exe)) )
		return	STS$K_WARN;

	l_exe [l_red] = '\0';

	/* Strip the file name, then the 'bin' component it lives in; either slash separates there */
	for ( int i = 0; i < 2; i++ )
		{
		if ( !(l_sep = strrchr(l_exe, '\\')) || (strrchr(l_exe, '/') > l_sep) )
			l_sep = strrchr(l_exe, '/');

		if ( !l_sep )
			return	STS$K_WARN;

		*l_sep = '\0';
		}
#else
ssize_t		l_red;

	if ( 0 >= (l_red = readlink("/proc/self/exe", l_exe, sizeof(l_exe) - 1)) )
		return	STS$K_WARN;

	l_exe [l_red] = '\0';

	/* Strip the file name, then the 'bin' component it lives in */
	if ( !strrchr(l_exe, '/') )
		return	STS$K_WARN;

	*strrchr(l_exe, '/') = '\0';

	if ( !strrchr(l_exe, '/') )
		return	STS$K_WARN;

	*strrchr(l_exe, '/') = '\0';
#endif

	__util$fao("!AZ/!AZ", &l_len, &l_dsc, (fao_prm_t) l_exe, (fao_prm_t) HELP$K_LIBDIR);
	l_dir [$MIN(l_len, (unsigned short) sizeof(l_dir) - 1)] = '\0';

	/* The translation first here as well, for the same reason */
	if ( 1 & s_cat$language(l_lang, (unsigned) sizeof(l_lang)) )
		{
		__util$fao("!AZ/!AZ", &l_len, &l_langdsc, (fao_prm_t) l_dir, (fao_prm_t) l_lang);
		l_langdir [$MIN(l_len, (unsigned short) sizeof(l_langdir) - 1)] = '\0';

		s_cat$append(a_bufsz, a_buf, l_langdir);
		}

	return	s_cat$append(a_bufsz, a_buf, l_dir);
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Compose the search path of the utility:
**
**	    the translation of HELP_LIBRARY, which may name a library, a
**	    directory, or a list of either;
**	    the private library directory of the user, $HOME/.help;
**	    the translation of the manual into the language of the locale,
**	    when the locale names one and the directory is there;
**	    the directory the product was configured to install into;
**	    the same directory found relative to the running image, which
**	    is what answers when the product has been moved.
**
**	The order is the order of precedence: a topic defined by a private
**	library hides the one the product was shipped with, which is what
**	lets a user correct or extend the documentation without touching
**	what was installed.
**
**  FORMAL PARAMETERS:
**
**	a_bufsz	Capacity of the buffer
**	a_buf	Receives the search path, ASCIZ
**
**  IMPLICIT INPUTS/OUTPUTS:
**	Reads the environment.
**
**  RETURN VALUE:
**	STS$K_SUCCESS always - whether anything on the path exists is
**	decided by CAT$OPEN.
**--
*/
int	cat$path	(
		unsigned	a_bufsz,
		char *		a_buf
			)
{
const char *	l_env;
char		l_dir [HELP$K_SZ_PATH], l_langdir [HELP$K_SZ_PATH], l_lang [32];
unsigned short	l_len = 0;
fao_desc_t	l_dsc = $FAO_DESC(l_dir), l_langdsc = $FAO_DESC(l_langdir);

	a_buf[0] = '\0';

	if ( (l_env = getenv(HELP$K_LOGNAME)) && *l_env )
		s_cat$append(a_bufsz, a_buf, l_env);

	if ( (l_env = getenv("HOME")) && *l_env )
		{
		__util$fao("!AZ/.help", &l_len, &l_dsc, (fao_prm_t) l_env);
		l_dir [$MIN(l_len, (unsigned short) sizeof(l_dir) - 1)] = '\0';

		s_cat$append(a_bufsz, a_buf, l_dir);
		}

	/* The library the product was built with names the directory it lives in */
	help$strcpy((unsigned) sizeof(l_dir), l_dir, HELP$K_DEFLIB);

	if ( strrchr(l_dir, '/') )
		{
		*strrchr(l_dir, '/') = '\0';

		/*
		**  The translation of the manual, when the locale asks for one,
		**  ahead of the manual itself.  A translation defines the same
		**  topics as its original, so the two cannot share a place on
		**  the path - one would merely hide the other, and which of
		**  them is wanted is a question only the user can answer.
		*/
		if ( 1 & s_cat$language(l_lang, (unsigned) sizeof(l_lang)) )
			{
			__util$fao("!AZ/!AZ", &l_len, &l_langdsc, (fao_prm_t) l_dir, (fao_prm_t) l_lang);
			l_langdir [$MIN(l_len, (unsigned short) sizeof(l_langdir) - 1)] = '\0';

			s_cat$append(a_bufsz, a_buf, l_langdir);
			}

		s_cat$append(a_bufsz, a_buf, l_dir);
		}

	/*
	**  And the same directory found relative to the running image.  The
	**  built-in path above is fixed when the product is configured, so it
	**  is wrong whenever the product is installed under a different
	**  prefix, staged through DESTDIR, or simply moved - and a copy of
	**  the tree carried to another machine is a normal thing to do.
	*/
	s_cat$imagedir(a_bufsz, a_buf);

	$TRACE("search path '%s'", a_buf);

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Open a directory and satisfy oneself about it, returning the
**	descriptor rather than the name.  Everything done afterwards is
**	done relative to that descriptor, so that what was inspected and
**	what is used are the same object - a name has to be resolved anew
**	on every call, and between two resolutions it can be made to mean
**	something else.  In a directory anyone may write to that is not a
**	theoretical concern.
**
**	O_NOFOLLOW refuses a symbolic link in the last component outright,
**	and O_DIRECTORY refuses anything that is not a directory, so both
**	are settled before the first FSTAT.
**
**  FORMAL PARAMETERS:
**
**	a_dir		The directory
**	a_private	!= 0 - it must belong to this user and be closed to
**			everyone else; == 0 - it may be read by all, but must
**			be writable by its owner alone
**	a_fd		Receives the descriptor
**
**  IMPLICIT INPUTS/OUTPUTS:
**	Opens a descriptor for the caller to close.
**
**  RETURN VALUE:
**	STS$K_SUCCESS - the directory is usable and <a_fd> holds it;
**	STS$K_WARN    - it is not.
**--
*/
static	int	s_cat$opendir	(
	const	char *		a_dir,
		int		a_private,
		int *		a_fd
			)
{
#ifdef	_WIN32
	/* Nothing to hold a directory by - see the history, X01-01 */
	*a_fd = -1;

	return	STS$K_WARN;
#else
struct stat	l_st = {0};
int		l_fd;

	*a_fd = -1;

	if ( 0 > (l_fd = open(a_dir, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)) )
		return	STS$K_WARN;

	if ( fstat(l_fd, &l_st) || !S_ISDIR(l_st.st_mode) )
		{
		close(l_fd);

		return	STS$K_WARN;
		}

	/*
	**  A directory anyone may write to holds nothing worth trusting: the
	**  file inside it can be replaced between one run and the next.
	*/
	if ( l_st.st_mode & (S_IWGRP | S_IWOTH) )
		{
		close(l_fd);

		return	STS$K_WARN;
		}

	if ( a_private && (l_st.st_uid != getuid()) )
		{
		close(l_fd);

		return	STS$K_WARN;
		}

	*a_fd = l_fd;

	return	STS$K_SUCCESS;
#endif
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Locate the directory the catalogue cache lives in, creating it when
**	it is absent.  See the description of the module for the order the
**	candidates are tried in and for the checks the shared one is put
**	through.
**
**  FORMAL PARAMETERS:
**
**	a_bufsz	Capacity of the buffer
**	a_buf	Receives the directory, ASCIZ, empty when there is none
**
**  IMPLICIT INPUTS/OUTPUTS:
**	May create a directory.
**
**  RETURN VALUE:
**	STS$K_SUCCESS - the directory is usable;
**	STS$K_WARN    - no directory could be had, caching is off.
**--
*/
static	int	s_cat$cachedir	(
		unsigned	a_bufsz,
		char *		a_buf,
		int *		a_fd
			)
{
const char *	l_env;
char		l_dir [HELP$K_SZ_PATH];
unsigned short	l_len = 0;
fao_desc_t	l_dsc = $FAO_DESC(l_dir);

	a_buf[0] = '\0';
	*a_fd	 = -1;

#ifdef	_WIN32
	/* No private cache on Windows, quietly - see the history, X01-01 */
	(void) l_env; (void) l_len; (void) l_dsc; (void) l_dir;

	return	STS$K_WARN;
#else
	if ( (l_env = getenv("HELP_CACHE")) && *l_env )
		help$strcpy((unsigned) sizeof(l_dir), l_dir, l_env);
	else if ( (l_env = getenv("XDG_CACHE_HOME")) && (*l_env == '/') )
		{
		__util$fao("!AZ/help", &l_len, &l_dsc, (fao_prm_t) l_env);
		l_dir [$MIN(l_len, (unsigned short) sizeof(l_dir) - 1)] = '\0';
		}
	else if ( (l_env = getenv("HOME")) && (*l_env == '/') )
		{
		__util$fao("!AZ/.cache/help", &l_len, &l_dsc, (fao_prm_t) l_env);
		l_dir [$MIN(l_len, (unsigned short) sizeof(l_dir) - 1)] = '\0';

		/* The intermediate .cache may be absent; its own mode is not ours to choose */
		{
		char	l_up [HELP$K_SZ_PATH];

			help$strcpy((unsigned) sizeof(l_up), l_up, l_dir);

			if ( strrchr(l_up, '/') )
				{
				*strrchr(l_up, '/') = '\0';

				if ( mkdir(l_up, 0700) && (errno != EEXIST) )
					return	STS$K_WARN;
				}
		}
		}
	else	{
		/* No home at all - a service account, say.  The shared directory it is */
		__util$fao("/tmp/help-!UL", &l_len, &l_dsc, (fao_prm_t) (unsigned) getuid());
		l_dir [$MIN(l_len, (unsigned short) sizeof(l_dir) - 1)] = '\0';
		}

	if ( mkdir(l_dir, 0700) && (errno != EEXIST) )
		return	help$putmsg(HELP$_CACHERR, 2, (fao_prm_t) l_dir, (fao_prm_t) strerror(errno));

	/*
	**  Whoever owns the directory owns everything in it.  A directory
	**  that is not ours, that others may write to, or that turns out not
	**  to be a directory at all - a symbolic link laid there before us -
	**  is not used.  From here on it is the descriptor that is used, so
	**  no later resolution of the name can substitute another object.
	*/
	if ( !(1 & s_cat$opendir(l_dir, 1, a_fd)) )
		return	help$putmsg(HELP$_CACHERR, 2, (fao_prm_t) l_dir,
					(fao_prm_t) "it is not a private directory of this user");

	help$strcpy(a_bufsz, a_buf, l_dir);

	return	STS$K_SUCCESS;
#endif
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Compose the name of the cache file of a search path.  The name
**	carries the checksum of the path, so that two different paths - a
**	run with HELP_LIBRARY set and one without - do not share a cache
**	and invalidate one another turn by turn.
**
**  FORMAL PARAMETERS:
**
**	a_cat	The catalogue; its CACHEFD, CACHENAME and CACHE are set
**
**  IMPLICIT INPUTS/OUTPUTS:
**	Opens the descriptor of the cache directory.
**
**  RETURN VALUE:
**	STS$K_SUCCESS - the cache file may be used;
**	STS$K_WARN    - caching is off.
**--
*/
static	int	s_cat$cachefile	(
		HLB$CAT *	a_cat
			)
{
char		l_dir [HELP$K_SZ_PATH], l_spec [HELP$K_SZ_PATH];
unsigned short	l_len = 0;
fao_desc_t	l_dsc = $FAO_DESC(l_spec);
int		l_status;

	a_cat->cache[0]		= '\0';
	a_cat->cachename[0]	= '\0';

	if ( !(1 & (l_status = s_cat$cachedir((unsigned) sizeof(l_dir), l_dir, &a_cat->cachefd))) )
		return	l_status;

	/* The name alone - it is opened relative to the descriptor */
	__util$fao("cat-!XL.idx", &l_len, &l_dsc,
			(fao_prm_t) __util$crc32c(0, a_cat->path, strlen(a_cat->path)));

	l_spec [$MIN(l_len, (unsigned short) sizeof(l_spec) - 1)] = '\0';

	help$strcpy((unsigned) sizeof(a_cat->cachename), a_cat->cachename, l_spec);

	/* And the whole of it, for the display of /CATALOG */
	__util$fao("!AZ/!AZ", &l_len, &l_dsc, (fao_prm_t) l_dir, (fao_prm_t) a_cat->cachename);
	l_spec [$MIN(l_len, (unsigned short) sizeof(l_spec) - 1)] = '\0';

	help$strcpy((unsigned) sizeof(a_cat->cache), a_cat->cache, l_spec);

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Name the directory of the shared catalogue - the one built once by
**	VHELP/SYSCACHE, at installation or afterwards, and read by every
**	user of the system.  Unlike a private cache it is not keyed by a
**	search path: it is a store of libraries already harvested, and any
**	path that names one of them takes it from there.
**
**	The file within it is HELP$K_SYSFILE, opened relative to a
**	descriptor of this directory - see S_CAT$OPENDIR.
**
**  FORMAL PARAMETERS:
**
**	a_bufsz	Capacity of the buffer
**	a_buf	Receives the directory, ASCIZ
**
**  IMPLICIT INPUTS/OUTPUTS:
**	None.
**
**  RETURN VALUE:
**	STS$K_SUCCESS always.
**--
*/
static	int	s_cat$sysdir	(
		unsigned	a_bufsz,
		char *		a_buf
			)
{
const char *	l_env = getenv("HELP_SYSCACHE");

	return	help$strcpy(a_bufsz, a_buf, (l_env && *l_env) ? l_env : HELP$K_SYSCACHE);
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Record one element of the search path in the catalogue together
**	with the modification time and the size the cache is validated
**	against.
**
**  FORMAL PARAMETERS:
**
**	a_cat	The catalogue
**	a_fspec	File specification of the element
**	a_isdir	!= 0 - the element is a directory
**	a_index	Receives the index of the entry, may be NULL
**
**  IMPLICIT INPUTS/OUTPUTS:
**	Extends the entry array of the catalogue.
**
**  RETURN VALUE:
**	STS$K_SUCCESS - the element has been recorded;
**	STS$K_WARN    - it does not exist, or the array is full.
**--
*/
static	int	s_cat$addent	(
		HLB$CAT *	a_cat,
	const	char *		a_fspec,
		int		a_isdir,
		int *		a_index
			)
{
struct stat	l_st = {0};
CAT$ENT *	l_ent;

	if ( a_index )
		*a_index = -1;

	/* The array is full - say so once, rather than drop the rest in silence */
	if ( a_cat->entcnt >= HELP$K_MAXLIB )
		{
		a_cat->entovf = 1;

		return	STS$K_WARN;
		}

	l_ent = &a_cat->ents [a_cat->entcnt];

	memset(l_ent, 0, sizeof(CAT$ENT));

	l_ent->isdir	= (uint16_t) (a_isdir != 0);

	help$strcpy((unsigned) sizeof(l_ent->fspec), l_ent->fspec, a_fspec);

	/*
	**  An element that does not exist is recorded as absent rather than
	**  dropped, so that its appearance invalidates the cache.
	*/
	if ( stat(a_fspec, &l_st) )
		l_ent->absent = 1;
	else	{
		l_ent->mtime	= (uint64_t) l_st.st_mtime;
		l_ent->size	= a_isdir ? 0 : (uint64_t) l_st.st_size;
		}

	if ( a_index )
		*a_index = (int) a_cat->entcnt;

	a_cat->entcnt++;

	return	l_ent->absent ? STS$K_WARN : STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Open a library and record its topics of level 1 in the catalogue.
**	A file that is not a library, or whose checksum disagrees, is
**	passed over in silence: a directory on the search path may hold
**	anything, and a scan is not the place to complain about it.
**
**  FORMAL PARAMETERS:
**
**	a_cat	The catalogue
**	a_fspec	File specification of the library
**	a_lib	Index of the entry the library was recorded as
**
**  IMPLICIT INPUTS/OUTPUTS:
**	Extends the key array of the catalogue.
**
**  RETURN VALUE:
**	STS$K_SUCCESS - the library has been harvested;
**	STS$K_WARN    - it could not be, and has been passed over.
**--
*/
static	int	s_cat$harvest	(
		HLB$CAT *	a_cat,
	const	char *		a_fspec,
		int		a_lib
			)
{
HLB$LIB *	l_lib = NULL;
HLB$KEY *	l_key;
CAT$KEY *	l_cat;
int		l_scan, l_quiet;

	/*
	**  A library that cannot be opened is not this routine's business to
	**  announce, so the signalling of HLB$OPEN is suppressed around it.
	*/
	l_quiet = dup(STDERR_FILENO);

	if ( l_quiet >= 0 )
		{
	int	l_null = open(HELP$K_NULLDEV, O_WRONLY);

		if ( l_null >= 0 )
			{
			dup2(l_null, STDERR_FILENO);
			close(l_null);
			}
		}

	if ( !(1 & hlb$open(a_fspec, &l_lib)) )
		l_lib = NULL;

	if ( l_quiet >= 0 )
		{
		dup2(l_quiet, STDERR_FILENO);
		close(l_quiet);
		}

	if ( !l_lib )
		return	STS$K_WARN;

	for ( l_scan = 0; (l_scan >= 0) && (1 & hlb$getkey(l_lib, l_scan, &l_key)); l_scan = l_key->next )
		{
		if ( a_cat->keycnt >= HELP$K_MAXCATKEY )
			{
			a_cat->keyovf = 1;
			break;
			}

		l_cat = &a_cat->keys [a_cat->keycnt++];

		memset(l_cat, 0, sizeof(CAT$KEY));

		l_cat->keylen	= l_key->keylen;
		l_cat->lib	= (uint16_t) a_lib;

		memcpy(l_cat->key, l_key->key, l_key->keylen);
		}

	$TRACE("%s: %u topic(s) of level 1", a_fspec, l_lib->keycnt ? 1 : 0);

	hlb$close(l_lib);

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Compare two directory entries by name, for QSORT(3).  A directory
**	is read in the order the file system chooses, which is no order at
**	all; sorting makes a catalogue depend on what is on the disk rather
**	than on how it got there.
**
**  FORMAL PARAMETERS:
**
**	a_p1, a_p2	The names being compared
**
**  IMPLICIT INPUTS/OUTPUTS:
**	None.
**
**  RETURN VALUE:
**	< 0, 0, > 0 as STRCMP(3).
**--
*/
static	int	s_cat$namecmp	(
	const	void *		a_p1,
	const	void *		a_p2
			)
{
	return	strcmp(*(const char * const *) a_p1, *(const char * const *) a_p2);
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Scan a directory for help libraries and harvest each of them.
**
**	A depth greater than zero carries the scan into the subdirectories
**	as well, one level less at each turn.  A subdirectory is entered
**	only when it is a directory in its own right, not a link to one:
**	a link is the way a directory tree comes to contain itself, and a
**	scan that followed one would not come back.
**
**  FORMAL PARAMETERS:
**
**	a_cat	The catalogue
**	a_dir	The directory
**	a_depth	Levels below this one to descend into, 0 - none
**	a_store	A store of already harvested libraries, may be NULL
**
**  IMPLICIT INPUTS/OUTPUTS:
**	Extends the catalogue.
**
**  RETURN VALUE:
**	STS$K_SUCCESS - the directory has been scanned;
**	STS$K_WARN    - it could not be opened.
**--
*/
static	int	s_cat$scandir	(
		HLB$CAT *	a_cat,
	const	char *		a_dir,
		int		a_depth,
		HLB$CAT *	a_store
			)
{
char *		l_subs [HELP$K_MAXLIB];
unsigned	l_subcnt = 0;
DIR *		l_dirp;
struct dirent *	l_dent;
char *		l_names [HELP$K_MAXLIB];
char		l_spec [HELP$K_SZ_PATH];
unsigned short	l_len = 0;
fao_desc_t	l_dsc = $FAO_DESC(l_spec);
unsigned	l_typelen = (unsigned) strlen(HELP$K_LIBTYPE), l_namecnt = 0, l_nlen;
int		l_lib;

	if ( !(l_dirp = opendir(a_dir)) )
		return	STS$K_WARN;

	while ( (l_dent = readdir(l_dirp)) )
		{
		l_nlen = (unsigned) strlen(l_dent->d_name);

		/* A library of this directory */
		if ( (l_nlen > l_typelen) && !strcasecmp(l_dent->d_name + l_nlen - l_typelen, HELP$K_LIBTYPE) )
			{
			if ( l_namecnt >= $ARRSZ(l_names) )
				continue;

			if ( !(l_names [l_namecnt] = strdup(l_dent->d_name)) )
				break;

			l_namecnt++;

			continue;
			}

		/* A subdirectory, when the scan was asked to descend */
		if ( (a_depth <= 0) || (l_subcnt >= $ARRSZ(l_subs)) )
			continue;

		if ( !strcmp(l_dent->d_name, ".") || !strcmp(l_dent->d_name, "..") )
			continue;

		if ( !(l_subs [l_subcnt] = strdup(l_dent->d_name)) )
			break;

		l_subcnt++;
		}

	closedir(l_dirp);

	qsort(l_names, l_namecnt, sizeof(l_names[0]), s_cat$namecmp);
	qsort(l_subs, l_subcnt, sizeof(l_subs[0]), s_cat$namecmp);

	for ( unsigned i = 0; i < l_namecnt; i++ )
		{
		__util$fao("!AZ/!AZ", &l_len, &l_dsc, (fao_prm_t) a_dir, (fao_prm_t) l_names[i]);
		l_spec [$MIN(l_len, (unsigned short) sizeof(l_spec) - 1)] = '\0';

		/*
		**  Reading a directory is cheap; opening a library is not - it
		**  is read whole and checksummed - so it is the library that is
		**  taken from the store, never the directory holding it.
		*/
		if ( !(1 & s_cat$reuse(a_cat, a_store, l_spec, 0))
			&& (1 & s_cat$addent(a_cat, l_spec, 0, &l_lib)) )
			s_cat$harvest(a_cat, l_spec, l_lib);

		free(l_names[i]);
		}

	/* The subtrees, each recorded so that a change in one is noticed */
	for ( unsigned i = 0; i < l_subcnt; i++ )
		{
	struct stat	l_st = {0};

		__util$fao("!AZ/!AZ", &l_len, &l_dsc, (fao_prm_t) a_dir, (fao_prm_t) l_subs[i]);
		l_spec [$MIN(l_len, (unsigned short) sizeof(l_spec) - 1)] = '\0';

		/* LSTAT, not STAT - a link to a directory is not descended into (Windows: no LSTAT, nor links in its C RTL) */
#ifdef	_WIN32
		if ( !stat(l_spec, &l_st) && S_ISDIR(l_st.st_mode) )
#else
		if ( !lstat(l_spec, &l_st) && S_ISDIR(l_st.st_mode) )
#endif
			{
			s_cat$addent(a_cat, l_spec, 1, NULL);
			s_cat$scandir(a_cat, l_spec, a_depth - 1, a_store);
			}

		free(l_subs[i]);
		}

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Take an element out of a store of already harvested ones, when the
**	store holds it and the file system still agrees with what it says.
**
**	This is what makes the shared catalogue worth writing: the
**	libraries of a system directory are opened once, by whoever built
**	it, and every user of the system takes them from there.  The store
**	is trusted no further than the file system lets it be - the
**	modification time and the size of the element must be what the
**	store recorded, and the suspicion rule of S_CAT$VALID applies here
**	as well.
**
**  FORMAL PARAMETERS:
**
**	a_cat	The catalogue being built
**	a_store	The store, may be NULL
**	a_fspec	The library wanted
**	a_isdir	Reserved, a directory is never taken from a store
**
**  IMPLICIT INPUTS/OUTPUTS:
**	Extends the catalogue when the element is taken.
**
**  RETURN VALUE:
**	STS$K_SUCCESS - the element has been taken from the store;
**	STS$K_WARN    - it has not, and has to be scanned.
**--
*/
static	int	s_cat$reuse	(
		HLB$CAT *	a_cat,
		HLB$CAT *	a_store,
	const	char *		a_fspec,
		int		a_isdir
			)
{
struct stat	l_st = {0};
CAT$ENT *	l_ent;
int		l_new;

	if ( !a_store || !a_store->entcnt )
		return	STS$K_WARN;

	for ( unsigned i = 0; i < a_store->entcnt; i++ )
		{
		l_ent = &a_store->ents [i];

		if ( strcmp(l_ent->fspec, a_fspec) || l_ent->isdir || l_ent->absent )
			continue;

		/* What the store says must still be what the file system says */
		if ( stat(a_fspec, &l_st) )
			return	STS$K_WARN;

		if ( ((uint64_t) l_st.st_mtime != l_ent->mtime)
			|| ((uint64_t) l_st.st_size != l_ent->size)
			|| ((uint64_t) l_st.st_mtime >= a_store->credat) )
			return	STS$K_WARN;

		if ( !(1 & s_cat$addent(a_cat, a_fspec, a_isdir, &l_new)) )
			return	STS$K_WARN;

		/* The topics this element answered for, as the store recorded them */
		for ( unsigned k = 0; k < a_store->keycnt; k++ )
			{
			if ( a_store->keys[k].lib != i )
				continue;

			if ( a_cat->keycnt >= HELP$K_MAXCATKEY )
				{
				a_cat->keyovf = 1;
				break;
				}

			a_cat->keys [a_cat->keycnt] = a_store->keys [k];
			a_cat->keys [a_cat->keycnt].lib = (uint16_t) l_new;
			a_cat->keycnt++;
			}

		$TRACE("%s taken from the store", a_fspec);

		return	STS$K_SUCCESS;
		}

	return	STS$K_WARN;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Build a catalogue by walking a search path: every element that is a
**	directory is scanned, every element that is a file is harvested.
**
**  FORMAL PARAMETERS:
**
**	a_cat	The catalogue, its arrays already allocated
**	a_path	The search path
**
**  IMPLICIT INPUTS/OUTPUTS:
**	Fills the catalogue.
**
**  RETURN VALUE:
**	STS$K_SUCCESS always.
**--
*/
static	int	s_cat$build	(
		HLB$CAT *	a_cat,
	const	char *		a_path,
		HLB$CAT *	a_store
			)
{
struct stat	l_st = {0};
const char *	l_scan, *l_end;
char		l_elem [HELP$K_SZ_PATH];
unsigned	l_len, l_step = 0, l_reclen = (unsigned) strlen(HELP$K_RECURSE);
int		l_lib, l_depth;

	a_cat->entcnt	= 0;
	a_cat->keycnt	= 0;
	a_cat->credat	= 0;

	for ( l_scan = a_path; *l_scan; l_scan += l_step + (l_end ? 1 : 0) )
		{
		l_end	= strchr(l_scan, CAT$K_SEP);
		l_step	= l_end ? (unsigned) (l_end - l_scan) : (unsigned) strlen(l_scan);
		l_len	= l_step;

		if ( !l_len || (l_len >= sizeof(l_elem)) )
			continue;

		memcpy(l_elem, l_scan, l_len);
		l_elem [l_len] = '\0';

		/*
		**  The element is trimmed below, so the step through the path
		**  is taken from the length as it was found - L_STEP - and not
		**  from L_LEN, which the trimming shortens.
		*/

		/*
		**  An element that ends in "..." asks for the whole tree below
		**  it, after the manner of the [dir...] of a VMS file
		**  specification.  The default path carries no such element:
		**  a scan that walked into every subdirectory of the install
		**  directory would gather the translations of a manual along
		**  with the manual, and each would merely hide the other.
		*/
		l_depth = 0;

		if ( (l_len > l_reclen) && !strcmp(l_elem + l_len - l_reclen, HELP$K_RECURSE) )
			{
			l_depth = HELP$K_MAXDEPTH;
			l_len  -= l_reclen;

			/* The separator before the ellipsis belongs to it */
			while ( l_len && (l_elem[l_len - 1] == '/') )
				l_len--;

			l_elem [l_len ? l_len : 1] = '\0';
			}

		/*
		**  An element that does not exist is still recorded - see
		**  S_CAT$ADDENT - so it is classified before it is stated.
		*/
		if ( stat(l_elem, &l_st) )
			{
			s_cat$addent(a_cat, l_elem, 0, NULL);

			continue;
			}

		if ( S_ISDIR(l_st.st_mode) )
			{
			/* The directory itself is recorded, so that a library added to it is noticed */
			s_cat$addent(a_cat, l_elem, 1, NULL);
			s_cat$scandir(a_cat, l_elem, l_depth, a_store);

			continue;
			}

		if ( 1 & s_cat$reuse(a_cat, a_store, l_elem, 0) )
			continue;

		if ( 1 & s_cat$addent(a_cat, l_elem, 0, &l_lib) )
			s_cat$harvest(a_cat, l_elem, l_lib);
		}

	/*
	**  A catalogue that did not fit answers fewer questions than the
	**  search path holds answers to, which the user has to be told: the
	**  alternative is a topic that is simply not found, for no visible
	**  reason.
	*/
	if ( a_cat->entovf )
		help$putmsg(HELP$_TOOMANY, 1, (fao_prm_t) HELP$K_MAXLIB);

	if ( a_cat->keyovf )
		help$putmsg(HELP$_TOOMANYT, 1, (fao_prm_t) HELP$K_MAXCATKEY);

	$TRACE("%u element(s), %u topic(s)", a_cat->entcnt, a_cat->keycnt);

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Test a catalogue loaded from the cache against the file system: the
**	modification time and the size of every element it was built from
**	must be what they were.  A directory reports a new time when a
**	library is added to it or removed, so the test covers the arrival
**	of a library as well as the change of one.
**
**  FORMAL PARAMETERS:
**
**	a_cat	The catalogue
**
**  IMPLICIT INPUTS/OUTPUTS:
**	None.
**
**  RETURN VALUE:
**	STS$K_SUCCESS - the catalogue still describes the file system;
**	STS$K_WARN    - it does not and has to be rebuilt.
**--
*/
static	int	s_cat$valid	(
		HLB$CAT *	a_cat
			)
{
struct stat	l_st = {0};
CAT$ENT *	l_ent;

	for ( unsigned i = 0; i < a_cat->entcnt; i++ )
		{
		l_ent = &a_cat->ents [i];

		if ( stat(l_ent->fspec, &l_st) )
			{
			/* Still absent is still valid; it disappearing is not */
			if ( l_ent->absent )
				continue;

			return	STS$K_WARN;
			}

		/* An element that was absent and now exists invalidates the catalogue */
		if ( l_ent->absent )
			return	STS$K_WARN;

		if ( (!!l_ent->isdir) != (!!S_ISDIR(l_st.st_mode)) )
			return	STS$K_WARN;

		if ( (uint64_t) l_st.st_mtime != l_ent->mtime )
			return	STS$K_WARN;

		if ( !l_ent->isdir && ((uint64_t) l_st.st_size != l_ent->size) )
			return	STS$K_WARN;

		/*
		**  An element whose time is not strictly older than the cache
		**  may have changed within the second the cache was written -
		**  a change the file system cannot distinguish from no change
		**  at all.  Such a cache is not trusted.
		*/
		if ( (uint64_t) l_st.st_mtime >= a_cat->credat )
			return	STS$K_WARN;
		}

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Read a catalogue from its cache file.  The file is opened without
**	following a symbolic link and is refused unless it is a regular
**	file belonging to this user and readable by nobody else - the cache
**	may live in a directory anyone can write to.
**
**  FORMAL PARAMETERS:
**
**	a_cat	The catalogue, its arrays already allocated
**
**  IMPLICIT INPUTS/OUTPUTS:
**	Fills the catalogue.
**
**  RETURN VALUE:
**	STS$K_SUCCESS - the catalogue has been read and is consistent;
**	STS$K_WARN    - it has not, for whatever reason.
**--
*/
static	int	s_cat$load	(
		HLB$CAT *	a_cat,
		int		a_dirfd,
	const	char *		a_file,
		int		a_checkpath
			)
{
CAT$HDR		l_hdr = {0};
struct stat	l_st = {0};
size_t		l_entsz, l_keysz;
uint32_t	l_crc;
int		l_fd;

	if ( (a_dirfd < 0) || !a_file || !a_file[0] )
		return	STS$K_WARN;

#ifdef	_WIN32
	(void) l_hdr; (void) l_st; (void) l_entsz; (void) l_keysz; (void) l_crc; (void) l_fd; (void) a_checkpath;

	return	STS$K_WARN;			/* A_DIRFD is never had there, see S_CAT$OPENDIR */
#else
	/* Relative to the directory that was inspected, not to its name */
	if ( 0 > (l_fd = openat(a_dirfd, a_file, O_RDONLY | O_NOFOLLOW | O_CLOEXEC)) )
		return	STS$K_WARN;

	/*
	**  A private cache must belong to this user and be readable by no
	**  one else.  The shared catalogue is another matter: it is meant to
	**  be read by everyone, so what is asked of it is that no one but
	**  its owner may write it - a file anyone could rewrite would be a
	**  way of steering every user of the system to a library of one's
	**  choosing.  Being owned by root or by the owner of the directory
	**  is left to the file system to arrange; what is refused here is
	**  the group- or world-writable case.
	*/
	if ( fstat(l_fd, &l_st) || !S_ISREG(l_st.st_mode) )
		{
		close(l_fd);

		return	STS$K_WARN;
		}

	if ( a_checkpath )
		{
		if ( (l_st.st_uid != getuid()) || (l_st.st_mode & (S_IRWXG | S_IRWXO)) )
			{
			close(l_fd);

			return	STS$K_WARN;
			}
		}
	else if ( l_st.st_mode & (S_IWGRP | S_IWOTH) )
		{
		close(l_fd);

		return	STS$K_WARN;
		}

	if ( sizeof(CAT$HDR) != (size_t) read(l_fd, &l_hdr, sizeof(CAT$HDR)) )
		{
		close(l_fd);

		return	STS$K_WARN;
		}

	/* The header must agree with this build before a single byte of it is trusted */
	if ( (l_hdr.magic != CAT$K_MAGIC) || (l_hdr.vers != CAT$K_VERSION)
		|| (l_hdr.hdrsz != sizeof(CAT$HDR)) || (l_hdr.entsz != sizeof(CAT$ENT))
		|| (l_hdr.keysz != sizeof(CAT$KEY))
		|| (l_hdr.entcnt > HELP$K_MAXLIB) || (l_hdr.keycnt > HELP$K_MAXCATKEY)
		|| (a_checkpath && strncmp(l_hdr.path, a_cat->path, sizeof(l_hdr.path))) )
		{
		close(l_fd);

		return	STS$K_WARN;
		}

	l_entsz = (size_t) l_hdr.entcnt * sizeof(CAT$ENT);
	l_keysz = (size_t) l_hdr.keycnt * sizeof(CAT$KEY);

	if ( (l_entsz != (size_t) read(l_fd, a_cat->ents, l_entsz))
		|| (l_keysz != (size_t) read(l_fd, a_cat->keys, l_keysz)) )
		{
		close(l_fd);

		return	STS$K_WARN;
		}

	close(l_fd);

	l_crc = __util$crc32c(0, a_cat->ents, l_entsz);
	l_crc = __util$crc32c(l_crc, a_cat->keys, l_keysz);

	if ( l_crc != l_hdr.crc )
		return	STS$K_WARN;

	a_cat->entcnt	= l_hdr.entcnt;
	a_cat->keycnt	= l_hdr.keycnt;
	a_cat->credat	= l_hdr.credat;

	/* Every ASCIZ field of the cache is terminated before anything reads it */
	for ( unsigned i = 0; i < a_cat->entcnt; i++ )
		a_cat->ents[i].fspec [sizeof(a_cat->ents[i].fspec) - 1] = '\0';

	for ( unsigned i = 0; i < a_cat->keycnt; i++ )
		{
		if ( (a_cat->keys[i].keylen > HLB$K_SZ_KEY) || (a_cat->keys[i].lib >= a_cat->entcnt) )
			return	STS$K_WARN;
		}

	$TRACE("%s: %u element(s), %u topic(s)", a_file, a_cat->entcnt, a_cat->keycnt);

	return	STS$K_SUCCESS;
#endif
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Write a catalogue to its cache file.  The file is created with a
**	unique temporary name, written, and renamed over the target, so a
**	reader never sees a half-written cache and two utilities running at
**	once cannot interleave their writes.
**
**  FORMAL PARAMETERS:
**
**	a_cat	The catalogue
**
**  IMPLICIT INPUTS/OUTPUTS:
**	Creates the cache file.
**
**  RETURN VALUE:
**	STS$K_SUCCESS - the cache has been written;
**	STS$K_WARN    - it has not; the catalogue is usable regardless.
**--
*/
static	int	s_cat$save	(
		HLB$CAT *	a_cat,
		int		a_dirfd,
	const	char *		a_file,
		mode_t		a_mode
			)
{
CAT$HDR		l_hdr = {0};
char		l_tmp [HELP$K_SZ_PATH];
unsigned short	l_len = 0;
fao_desc_t	l_dsc = $FAO_DESC(l_tmp);
size_t		l_entsz = (size_t) a_cat->entcnt * sizeof(CAT$ENT);
size_t		l_keysz = (size_t) a_cat->keycnt * sizeof(CAT$KEY);
int		l_fd, l_status = STS$K_SUCCESS;

	if ( (a_dirfd < 0) || !a_file || !a_file[0] )
		return	STS$K_WARN;

#ifdef	_WIN32
	(void) l_hdr; (void) l_tmp; (void) l_len; (void) l_dsc; (void) l_entsz; (void) l_keysz; (void) l_fd; (void) l_status; (void) a_mode;

	return	STS$K_WARN;			/* A_DIRFD is never had there, see S_CAT$OPENDIR */
#else
	__util$fao("!AZ.!UL", &l_len, &l_dsc, (fao_prm_t) a_file, (fao_prm_t) (unsigned) getpid());
	l_tmp [$MIN(l_len, (unsigned short) sizeof(l_tmp) - 1)] = '\0';

	/* O_EXCL refuses a name that already exists, symbolic link included */
	if ( 0 > (l_fd = openat(a_dirfd, l_tmp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, a_mode)) )
		return	STS$K_WARN;

	l_hdr.magic	= CAT$K_MAGIC;
	l_hdr.vers	= CAT$K_VERSION;
	l_hdr.hdrsz	= (uint32_t) sizeof(CAT$HDR);
	l_hdr.entsz	= (uint32_t) sizeof(CAT$ENT);
	l_hdr.keysz	= (uint32_t) sizeof(CAT$KEY);
	l_hdr.entcnt	= a_cat->entcnt;
	l_hdr.keycnt	= a_cat->keycnt;
	/*
	**  The time is taken after the catalogue has been built, not before:
	**  it is the instant the contents were known to be current, and the
	**  suspicion rule of S_CAT$VALID measures against it.
	*/
	a_cat->credat	= (uint64_t) time(NULL);
	l_hdr.credat	= a_cat->credat;

	help$strcpy((unsigned) sizeof(l_hdr.path), l_hdr.path, a_cat->path);

	l_hdr.crc	= __util$crc32c(0, a_cat->ents, l_entsz);
	l_hdr.crc	= __util$crc32c(l_hdr.crc, a_cat->keys, l_keysz);

	if ( (sizeof(CAT$HDR) != (size_t) write(l_fd, &l_hdr, sizeof(CAT$HDR)))
		|| (l_entsz != (size_t) write(l_fd, a_cat->ents, l_entsz))
		|| (l_keysz && (l_keysz != (size_t) write(l_fd, a_cat->keys, l_keysz))) )
		l_status = STS$K_WARN;

	if ( close(l_fd) )
		l_status = STS$K_WARN;

	/* The shared catalogue is read by everyone, so its mode is asked for */
	if ( (1 & l_status) && fchmodat(a_dirfd, l_tmp, a_mode, 0) )
		l_status = STS$K_WARN;

	if ( (1 & l_status) && renameat(a_dirfd, l_tmp, a_dirfd, a_file) )
		l_status = STS$K_WARN;

	if ( !(1 & l_status) )
		unlinkat(a_dirfd, l_tmp, 0);

	$TRACE("%s: status=%#x", a_file, l_status);

	return	l_status;
#endif
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Read a store of harvested libraries - the shared catalogue - into a
**	context of its own.  It is not validated against a search path,
**	having been built from a different one; every element of it is
**	tested separately as it is taken, by S_CAT$REUSE.
**
**  FORMAL PARAMETERS:
**
**	a_file	File specification of the store
**	a_cat	Receives the address of the store
**
**  IMPLICIT INPUTS/OUTPUTS:
**	Allocates memory to be released by CAT$CLOSE.
**
**  RETURN VALUE:
**	STS$K_SUCCESS - the store has been read;
**	STS$K_WARN    - it has not, for whatever reason.
**--
*/
int	cat$open_store	(
	const	char *		a_dir,
		HLB$CAT **	a_cat
			)
{
HLB$CAT *	l_cat;

	*a_cat = NULL;

	if ( !(l_cat = calloc(1, sizeof(HLB$CAT))) )
		return	STS$K_WARN;

	l_cat->cachefd = -1;
	l_cat->ents = calloc(HELP$K_MAXLIB, sizeof(CAT$ENT));
	l_cat->keys = calloc(HELP$K_MAXCATKEY, sizeof(CAT$KEY));

	/*
	**  The shared catalogue is meant to be read by everyone, so its
	**  directory need not be private - but nobody save its owner may
	**  write it, or it would be a way of steering every user of the
	**  system to a library of one's choosing.
	*/
	if ( !l_cat->ents || !l_cat->keys
		|| !(1 & s_cat$opendir(a_dir, 0, &l_cat->cachefd))
		|| !(1 & s_cat$load(l_cat, l_cat->cachefd, HELP$K_SYSFILE, 0)) )
		{
		cat$close(l_cat);

		return	STS$K_WARN;
		}

	*a_cat = l_cat;

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Build the catalogue of a search path from scratch and write it to
**	the shared file, for every user of the system to read.  This is an
**	administrative action - it needs the right to write the directory,
**	which an ordinary run neither has nor wants - and is what the
**	installation of the product performs on its own libraries.
**
**  FORMAL PARAMETERS:
**
**	a_path	The search path to harvest
**	a_cat	Receives the catalogue, for the caller to report on
**
**  IMPLICIT INPUTS/OUTPUTS:
**	Creates the shared catalogue.
**
**  RETURN VALUE:
**	STS$K_SUCCESS or a HELP$_NOLIBS / HELP$_CACHERR condition, already
**	signalled.
**--
*/
int	cat$syscache	(
	const	char *		a_path,
		HLB$CAT **	a_cat
			)
{
HLB$CAT *	l_cat;
char		l_sysdir [HELP$K_SZ_PATH];

	*a_cat = NULL;

	if ( !(l_cat = calloc(1, sizeof(HLB$CAT))) )
		return	help$putmsg(HELP$_NOMEM, 3, (fao_prm_t) sizeof(HLB$CAT), (fao_prm_t) errno,
					(fao_prm_t) strerror(errno));

	l_cat->cachefd = -1;
	l_cat->ents = calloc(HELP$K_MAXLIB, sizeof(CAT$ENT));
	l_cat->keys = calloc(HELP$K_MAXCATKEY, sizeof(CAT$KEY));

	if ( !l_cat->ents || !l_cat->keys )
		{
		cat$close(l_cat);

		return	help$putmsg(HELP$_NOMEM, 3, (fao_prm_t) sizeof(CAT$KEY) * HELP$K_MAXCATKEY,
					(fao_prm_t) errno, (fao_prm_t) strerror(errno));
		}

	help$strcpy((unsigned) sizeof(l_cat->path), l_cat->path, a_path);
	help$strcpy((unsigned) sizeof(l_cat->cachename), l_cat->cachename, HELP$K_SYSFILE);

	s_cat$sysdir((unsigned) sizeof(l_sysdir), l_sysdir);

	{
	unsigned short	l_len = 0;
	fao_desc_t	l_dsc = $FAO_DESC(l_cat->cache);

		__util$fao("!AZ/!AZ", &l_len, &l_dsc, (fao_prm_t) l_sysdir, (fao_prm_t) HELP$K_SYSFILE);
		l_cat->cache [$MIN(l_len, (unsigned short) sizeof(l_cat->cache) - 1)] = '\0';
	}

	if ( !(1 & s_cat$opendir(l_sysdir, 0, &l_cat->cachefd)) )
		{
#ifdef	_WIN32
		help$putmsg(HELP$_CACHERR, 2, (fao_prm_t) l_sysdir,
				(fao_prm_t) "there is no shared catalogue on Windows");
#else
		help$putmsg(HELP$_CACHERR, 2, (fao_prm_t) l_sysdir,
				(fao_prm_t) "it is not a directory writable by its owner alone");
#endif

		cat$close(l_cat);

		return	HELP$_CACHERR;
		}

	/* Nothing is taken from a store here: the point is to harvest afresh */
	s_cat$build(l_cat, l_cat->path, NULL);

	if ( !l_cat->keycnt )
		{
		help$putmsg(HELP$_NOLIBS, 1, (fao_prm_t) l_cat->path);

		cat$close(l_cat);

		return	HELP$_NOLIBS;
		}

	/* Readable by everyone, writable by whoever was allowed to make it */
	if ( !(1 & s_cat$save(l_cat, l_cat->cachefd, l_cat->cachename, 0644)) )
		{
		help$putmsg(HELP$_CACHERR, 2, (fao_prm_t) l_cat->cache, (fao_prm_t) strerror(errno));

		cat$close(l_cat);

		return	HELP$_CACHERR;
		}

	*a_cat = l_cat;

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Open the catalogue of a search path: read it from the cache when
**	the cache still describes the file system, build it otherwise, and
**	write the result back for the next run.
**
**  FORMAL PARAMETERS:
**
**	a_path		The search path
**	a_usecache	!= 0 - the cache may be read and written
**	a_cat		Receives the address of the catalogue
**
**  IMPLICIT INPUTS/OUTPUTS:
**	May create the cache directory and file.
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- the catalogue holds at least one topic;
**	HELP$_NOLIBS	- nothing was found, already signalled;
**	HELP$_NOMEM	- allocation failure, already signalled.
**--
*/
int	cat$open	(
	const	char *		a_path,
		int		a_usecache,
		HLB$CAT **	a_cat
			)
{
HLB$CAT *	l_cat, *l_store = NULL;
char		l_sysdir [HELP$K_SZ_PATH] = {0};

	*a_cat = NULL;

	if ( !(l_cat = calloc(1, sizeof(HLB$CAT))) )
		return	help$putmsg(HELP$_NOMEM, 3, (fao_prm_t) sizeof(HLB$CAT), (fao_prm_t) errno,
					(fao_prm_t) strerror(errno));

	l_cat->cachefd = -1;
	l_cat->ents = calloc(HELP$K_MAXLIB, sizeof(CAT$ENT));
	l_cat->keys = calloc(HELP$K_MAXCATKEY, sizeof(CAT$KEY));

	if ( !l_cat->ents || !l_cat->keys )
		{
		cat$close(l_cat);

		return	help$putmsg(HELP$_NOMEM, 3, (fao_prm_t) sizeof(CAT$KEY) * HELP$K_MAXCATKEY,
					(fao_prm_t) errno, (fao_prm_t) strerror(errno));
		}

	help$strcpy((unsigned) sizeof(l_cat->path), l_cat->path, a_path);

	if ( a_usecache )
		s_cat$cachefile(l_cat);

	/* The private cache is used only when it still describes what is on the disk */
	if ( (1 & s_cat$load(l_cat, l_cat->cachefd, l_cat->cachename, 1)) && (1 & s_cat$valid(l_cat)) )
		l_cat->cached = 1;
	else	{
		/*
		**  Failing it, the shared catalogue is consulted - not as a
		**  cache of this search path, which it knows nothing about, but
		**  as a store of libraries somebody has already opened.  A
		**  first run on a fresh account pays for its own libraries
		**  only, the system's having been harvested at installation.
		*/
		if ( a_usecache && (1 & s_cat$sysdir((unsigned) sizeof(l_sysdir), l_sysdir)) )
			{
			if ( (1 & cat$open_store(l_sysdir, &l_store)) )
				l_cat->shared = 1;
			}

		s_cat$build(l_cat, l_cat->path, l_store);

		cat$close(l_store);

		l_cat->dirty = 1;

		if ( a_usecache )
			s_cat$save(l_cat, l_cat->cachefd, l_cat->cachename, 0600);
		}

	if ( !l_cat->keycnt )
		{
		help$putmsg(HELP$_NOLIBS, 1, (fao_prm_t) l_cat->path);

		cat$close(l_cat);

		return	HELP$_NOLIBS;
		}

	*a_cat = l_cat;

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Release a catalogue.
**
**  FORMAL PARAMETERS:
**
**	a_cat	The catalogue, may be NULL
**
**  IMPLICIT INPUTS/OUTPUTS:
**	None.
**
**  RETURN VALUE:
**	STS$K_SUCCESS always.
**--
*/
int	cat$close	(
		HLB$CAT *	a_cat
			)
{
	if ( !a_cat )
		return	STS$K_SUCCESS;

	if ( a_cat->ents )
		free(a_cat->ents);

	if ( a_cat->keys )
		free(a_cat->keys);

	if ( a_cat->cachefd >= 0 )
		close(a_cat->cachefd);

	free(a_cat);

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Find the library that answers for a topic of level 1.  An exact
**	match wins; failing that, a leading abbreviation is accepted, the
**	way HLB$LOOKUP accepts one inside a library.  When several
**	libraries define the topic the first on the search path wins - the
**	catalogue is built in the order of the path, so the first match in
**	the key array is that one.
**
**  FORMAL PARAMETERS:
**
**	a_cat		The catalogue
**	a_keylen	Length of the topic name
**	a_key		The topic name, any case
**	a_bufsz		Capacity of the buffer
**	a_fspec		Receives the library specification, ASCIZ
**
**  IMPLICIT INPUTS/OUTPUTS:
**	None.
**
**  RETURN VALUE:
**	STS$K_SUCCESS - a library has been found;
**	STS$K_WARN    - no topic of the catalogue matches.
**--
*/
int	cat$lookup	(
		HLB$CAT *	a_cat,
		unsigned	a_keylen,
	const	char *		a_key,
		unsigned	a_bufsz,
		char *		a_fspec
			)
{
char		l_ups [HLB$K_SZ_KEY];
unsigned	l_upslen = 0;
int		l_first = -1;

	a_fspec[0] = '\0';

	help$upcase(a_keylen, a_key, (unsigned) sizeof(l_ups), l_ups, &l_upslen);

	if ( !l_upslen )
		return	STS$K_WARN;

	for ( unsigned i = 0; i < a_cat->keycnt; i++ )
		{
		if ( a_cat->keys[i].keylen < l_upslen )
			continue;

		if ( memcmp(a_cat->keys[i].key, l_ups, l_upslen) )
			continue;

		/* An exact match ends the search wherever it is found */
		if ( a_cat->keys[i].keylen == l_upslen )
			{
			help$strcpy(a_bufsz, a_fspec, a_cat->ents [a_cat->keys[i].lib].fspec);

			return	STS$K_SUCCESS;
			}

		if ( l_first < 0 )
			l_first = (int) i;
		}

	if ( l_first < 0 )
		return	STS$K_WARN;

	help$strcpy(a_bufsz, a_fspec, a_cat->ents [a_cat->keys[l_first].lib].fspec);

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Return the specification of the first library of the catalogue.
**	Used when a topic has to be shown from somewhere and the catalogue
**	has not been asked about anything in particular.
**
**  FORMAL PARAMETERS:
**
**	a_cat	The catalogue
**	a_bufsz	Capacity of the buffer
**	a_fspec	Receives the library specification, ASCIZ
**
**  IMPLICIT INPUTS/OUTPUTS:
**	None.
**
**  RETURN VALUE:
**	STS$K_SUCCESS - a library has been returned;
**	STS$K_WARN    - the catalogue holds none.
**--
*/
int	cat$firstlib	(
		HLB$CAT *	a_cat,
		unsigned	a_bufsz,
		char *		a_fspec
			)
{
	a_fspec[0] = '\0';

	for ( unsigned i = 0; i < a_cat->entcnt; i++ )
		{
		if ( a_cat->ents[i].isdir || a_cat->ents[i].absent )
			continue;

		help$strcpy(a_bufsz, a_fspec, a_cat->ents[i].fspec);

		return	STS$K_SUCCESS;
		}

	return	STS$K_WARN;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Step through the topics of the catalogue, skipping those a library
**	earlier on the search path already answers for - so a caller
**	listing the topics sees each of them once, attributed to the
**	library that would actually be opened for it.
**
**  FORMAL PARAMETERS:
**
**	a_cat	The catalogue
**	a_from	Index to resume at, -1 - start from the beginning
**	a_index	Receives the index of the next topic
**
**  IMPLICIT INPUTS/OUTPUTS:
**	None.
**
**  RETURN VALUE:
**	STS$K_SUCCESS - a topic has been returned;
**	STS$K_WARN    - the catalogue is exhausted.
**--
*/
int	cat$nextkey	(
		HLB$CAT *	a_cat,
		int		a_from,
		int *		a_index
			)
{
int		l_dup;

	*a_index = -1;

	for ( unsigned i = (unsigned) (a_from + 1); i < a_cat->keycnt; i++ )
		{
		l_dup = 0;

		for ( unsigned j = 0; j < i; j++ )
			{
			if ( (a_cat->keys[j].keylen == a_cat->keys[i].keylen)
				&& !memcmp(a_cat->keys[j].key, a_cat->keys[i].key, a_cat->keys[i].keylen) )
				{
				l_dup = 1;
				break;
				}
			}

		if ( l_dup )
			continue;

		*a_index = (int) i;

		return	STS$K_SUCCESS;
		}

	return	STS$K_WARN;
}
