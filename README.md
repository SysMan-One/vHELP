# HELP

An OpenVMS-style help subsystem for Linux: a librarian that translates
Markdown into a help library, and a browser that navigates it.

    $ vhelptran doc/helplib.md /OUTPUT=helplib.hlb /IDENT=X-01
    %HELP-I-CREATED, helplib.hlb created, 21 topics, 5432 bytes of text

    $ vhelp submit /after

      SUBMIT

        /AFTER

          Requests that the job be held until the given time.

    $ vhelptran /EXTRACT helplib.hlb /KEY="VHELPTRAN SOURCE FORMAT"

Extraction is the reverse of translation.  Its first pass is lossy --
the descriptive tail of a heading and the emphasis markers live in the
source only -- but from there the round trip settles: extract,
translate and extract again yields the identical text.

Keys may carry blanks and may be Cyrillic.  A key is named either as a
run of words or quoted, and the lookup folds the case of both alphabets:

    $ vhelp help vhelptran source format
    $ vhelp help vhelptran "source format" directives
    $ vhelp /library=helplib_ru.hlb help vhelptran "формат исходного текста"

## Finding a library

The images are `vhelp` and `vhelptran`, not `help` and `helptran`.
`help` is a builtin of bash and of ksh, and a shell looks a builtin up
*before* it searches `PATH`, so no ordering of the path can bring an
image of that name to the front -- the shell answers first, about its
own builtins:

    $ PATH=/usr/local/bin:$PATH bash -c 'help cd'
    cd: cd [-L|[-P [-e]] [-@]] [dir]        <- the shell, not us

Shells with no such builtin -- dash, zsh, fish -- find the image
through `PATH` like any other command.

Both names are settings, `HELP_HELPNAME` and `HELP_TRANNAME`, and
the sources take them from there rather than spelling them out.

`-DHELP_ALIAS=ON` additionally installs `bin/help` as a link to the
image, and `share/help/help.sh` to source from `~/.bashrc`:

    if [ -n "${BASH_VERSION-}" ]; then enable -n help 2>/dev/null; fi

That turns the builtin off, which lets `PATH` decide -- and costs the
builtin: `help while` will no longer describe the shell's syntax.
Hence the option is off by default.

`vhelp` looks along a search path rather than at one file:

```
$HELP_LIBRARY           a library, a directory, or a list of either
$HOME/.help             the private libraries of the user
<prefix>/share/help/<lang>  the translation, when the locale asks
<prefix>/share/help     the libraries installed with the product
<image>/../share/help   the same, relative to the running program
```

`LANG=ru_RU.UTF-8 vhelp vhelptran` answers in Russian: a translation
defines the same topics as its original, so the two never share a
place on the path -- one would merely hide the other.  An element
ending in `...` is scanned to the depth of its tree, VMS fashion:
`/LIBRARY=/opt/pkg/help/...`.

The last of these makes the installation relocatable: a tree moved
elsewhere, or staged through `DESTDIR`, still finds its libraries.

A directory on the path is scanned for `.hlb` files; a topic defined
earlier on the path hides the same topic defined later, as a command
earlier on `PATH` does.  Because the catalogue merges the level 1
topics of every library, a library should carry exactly one -- the
name of its facility -- which `vhelptran /FACILITY=name` arranges.
Otherwise one product's verbs collide with another's in a namespace
they share.  `/CATALOG` shows the path, the libraries and
what hides what.  `/LIBRARY=spec` names either one library, pinning
the run to it, or a search path, which is then the whole of it.

Opening every library on every run would be waste, so which library
answers for which topic is cached under `$XDG_CACHE_HOME/help/`,
`$HOME/.cache/help/`, or `/tmp/help-<uid>/`.  The cache is validated
against the modification time and size of every path element, so a
library added, changed or removed is noticed without opening
anything; a cache that disagrees is rebuilt.  `/NOCACHE` bypasses it.
Deleting the cache by hand is never necessary and never harmful.

`vhelp /SYSCACHE` builds the catalogue shared by every user, which the
installation does for the libraries it installs.  It is a store of
libraries already opened rather than a cache of one search path, so
anybody's first run takes the system's libraries from it and pays
only for its own -- 8.6 ms against 19.0 with eighteen megabytes of
libraries installed.  Each entry is validated as it is taken, and a
shared catalogue anyone may write is refused.

Both caches are reached through a descriptor of the directory holding
them, opened once with `O_DIRECTORY|O_NOFOLLOW` and checked by
`fstat`; the files are then `openat`/`renameat`/`unlinkat` relative to
it.  Checking a name and using that name again would leave the
interval between the two to whoever can rename in the parent -- which
in `/tmp` is everybody.  A descriptor cannot be redirected once
opened, so the sticky bit of the parent does not come into it.

## The reading API

The reading side is a library, so a product may answer for its own help
instead of spawning the HELP image:

```cmake
find_package(HELP REQUIRED)
target_link_libraries(myprog PRIVATE HELP::help)
```

Drawn in as a subproject it carries the same name, so a consumer writes
one thing either way:

```cmake
add_subdirectory(external/help)
target_link_libraries(myprog PRIVATE HELP::help)
```

One header declares the whole of it:

```c
#include "helpdef.h"

HLB$LIB *   lib;
const char *txt;
unsigned    len;
int         parent = -1, index, used, pos = 0;

    hlb$open("/usr/local/share/help/myprog.hlb", &lib);

    /*  A key may carry blanks, so LOOKUP_RUN takes the longest run of
     *  words that matches and reports how many it consumed.  One call
     *  descends one level; a path of several is a loop.  */
    while ( pos < argc )
        {
        if ( $STS_ERROR(hlb$lookup_run(lib, parent, argc - pos, argv + pos, &used, &index)) )
            break;

        parent = index;
        pos   += used;
        }

    if ( 1 & hlb$gettext(lib, parent, &len, &txt) )
        write(1, txt, len);
```

`HLB$OPEN` reads the file whole, so navigation costs no further I/O:
`HLB$LOOKUP` finds a key by name, `HLB$LOOKUP_RUN` does the same for a
key that carries blanks, `HLB$FIRSTCHILD` walks the tree, `HLB$SEARCH`
matches a wildcard, `HLB$PATH` renders the path of a topic and
`HLB$GETTEXT` yields its text -- a run inside the file image, not a
string, so it comes with its length and is not terminated.  Output, paging and the width
of a terminal are `HELP$OUT_INIT`, `HELP$PUT_TEXT` and `HELP$FLUSH`.
The writing side - `HLB$BLD_CREATE` and what follows it - is in the same
library, for a product that would rather build a library of its own than
call HELPTRAN.

`HELPDEF.H` draws in the headers of UTILITY$ROUTINES, so the installed
config asks for `StarLet` with `find_dependency`.  That package is not
carried along: one copy of it in a system is what keeps two revisions
out of one image, its structures being laid out in its headers and its
consumers embedding them.

A product of the line that carries its own documentation translates it
at build time with the helper the package installs:

    find_package(HELP REQUIRED)
    help_add_library(NAME myprod_help SOURCE doc/myprod.md ALL)

which builds the library, names it after the source, stamps it with
`PROJECT_VERSION`, roots it at `PROJECT_NAME` and installs it where
the browser looks.  Several sources make one library, which is how a
product with a manual per command keeps them under a single key:

    help_add_library(NAME batch_help
                     SOURCE doc/submit.md doc/queue.md
                     OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/batch.hlb
                     ALL)

`FACILITY` names the root when it is not the project, and
`NOFACILITY` declines it for a library meant to carry several keys at
level 1.

A cross build -- the product built for another machine, Android with
the NDK, say -- cannot run the translator the package carries: that
one is of the target.  The library is the same on every architecture,
so it is translated on the host instead, by the first of:

* `-DHELP_HOST_TRANSLATOR=<path>` -- a `vhelptran` of the host, named;
* `vhelptran` found on the host -- in `PATH` and `/usr/local/bin`,
  `/usr/bin`, `/bin`, never in the prefixes of the target;
* the target translator under `CMAKE_CROSSCOMPILING_EMULATOR`
  (qemu-user and the like).

With none of them the configure step stops and says so.  Usually
nothing has to be given: install HELP on the host as well.  The host
translator should be of the same delivery as the package the target
links, or later.  The build of this product itself translates its own
library the same way.

Installing removes `lib/cmake/HELP` before writing it.  CMake names
one file of the export set per build configuration and includes every
one it finds by a wildcard, so an install of one configuration over
another would otherwise leave the entry of a configuration nobody
rebuilt -- and a consumer would import it in silence.  It works
the same whether the package was found with `find_package` or embedded
with `add_subdirectory`.

## Build

The product builds against the StarLet package (UTILITY$ROUTINES),
which is found where it was installed rather than carried inside this
tree.  Build and install it first:

    cmake -S <utility_routines> -B sl -DCMAKE_BUILD_TYPE=Release
    cmake --build sl
    cmake --install sl --prefix /usr/local

then this product:

    cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/usr/local
    cmake --build build
    ctest --test-dir build
    cmake --install build

When StarLet lives somewhere CMake does not search by default, add
`-DCMAKE_PREFIX_PATH=<prefix>`.  StarLet X.00-11 or later is
required: from that revision the CLI service intercepts only the
dashed and slashed spellings of a syntax request, so a utility named
`help` can be parsed at all.  UTILITY$ROUTINES V.01-17 is required
as well, for `__util$setlogfd()`; both travel in the one package, and
that package must be **1.1.0 or later** -- the first delivery whose
number moves with the modules inside it, and therefore the first that
can be asked for.

Settings, all of them cache variables of the build:

| | |
|---|---|
| `HELP_IDENT` | identification of the delivery, `Xaa-bb[ECOc]` |
| `HELP_HELPNAME`, `HELP_TRANNAME` | names of the images, `vhelp` and `vhelptran` |
| `HELP_FACILITY` | the level 1 key of the help library, `HELP` |
| `HELP_LIBDIR` | where the libraries are installed, `share/help` |
| `HELP_DEFLIB` | the library named when nothing else is |
| `HELP_LOGNAME` | the environment variable of the search path |
| `HELP_SYSCACHE` | directory of the shared catalogue, `/var/cache/help` |
| `HELP_BUILD_SYSCACHE` | build that catalogue when installing, ON |
| `HELP_ALIAS` | install `bin/help` as well, OFF |
| `HELP_HOST_TRANSLATOR` | `vhelptran` of the host for a cross build, empty: look for one |
| `HELP_STRICT` | `-Werror`, OFF |
| `HELP_CONTACT` | the maintainer named in the packages |
| `HELP_TESTS`, `BUILD_TESTING` | build the checks, ON |
(default ON), `HELP_TESTS` (default ON, turns this product's tests
off on their own), `HELP_IDENT`, `HELP_LIBDIR`, `HELP_DEFLIB`,
`HELP_LOGNAME`.

`ctest` runs fifty-five functional checks, a check of what an
installation contains and of what its exported package imposes on a
consumer, and a check of what a package contains.  The regression tests of StarLet belong to
StarLet and are run from its own build.

Within that package, STARLET X.01-09 or later is what the output
depends on -- the FAO engine takes a quadword and a time parameter by
value from that revision on -- and CLI_ROUTINES X.00-11 is what the
command line depends on, for two reasons.

From X.00-07 an argument beginning with a slash that matches no
qualifier is refused outright, and both utilities receive such
arguments as a matter of course: a key named `/AFTER`, an input file
named by an absolute path.  The command is therefore split before it
is parsed, and the parser is shown the utility's own qualifiers and
nothing else.

From X.00-09 the service answers a syntax request out of its own
tables.  X.00-11 narrowed that to the dashed and slashed spellings,
which is what lets the browser be a verb named `HELP` at all; the
request itself is still caught by the utility first, so it is answered
in the utility's own terms wherever it stands in the command.

The standard output of both utilities carries data -- a paged topic,
the Markdown of an `/EXTRACT` -- so the logging of the package is
pointed at the standard error at startup with `__util$setlogfd()`.
UTILITY$ROUTINES V.01-17 or later is required for it: the diversion
hook used before was withdrawn in V.01-16, and `__util$deflog()`, the
only other way, names a file rather than a descriptor, seizes both
standard streams and announces itself on every call.

Anything a user is meant to read goes through this facility's own
message catalogue rather than through `$LOG` regardless -- `$LOG` is
the developer's channel, and the two are not interchangeable.

## Packaging

CPack makes the packages out of the same install rules, so a package
holds exactly what an installation holds:

    cpack --config build/CPackConfig.cmake
    cpack --config build/CPackSourceConfig.cmake

The first makes `HELP-X01-36ECO2.tar.gz`, a tarball with `bin/`, `lib/`
and `share/` at its top -- unpack it anywhere, the images find their
libraries beside themselves -- and `help_1.36.2_amd64.deb` where
`dpkg-deb` is found, an RPM where `rpmbuild` is.  The second makes
`HELP-X01-36ECO2-src.tar.gz`, the sources alone.  A `.sha256` comes with
each.

The version of a package follows from `HELP_IDENT`: `X01-36` is
`1.36.0`, `X01-36ECO2` is `1.36.2`.  The archives are named
after the product and the ident, the DEB and the RPM as `dpkg` and
`rpm` name theirs, so that a repository takes them as they come.

A DEB or an RPM is written into the prefix the build was configured
for, since that is what the images carry in `HELP_DEFLIB`; a package
meant for a distribution is configured with
`-DCMAKE_INSTALL_PREFIX=/usr`.  The maintainer named in the package
is `HELP_CONTACT`.

The shared catalogue is not a file of the package: it is built by the
package's `postinst` on the system the package lands in, and removed
by its `postrm`.  It cannot be built when the package is made -- the
files lie in a staging tree at that time, and the paths recorded would
be those of the tree -- which is also why an install into a prefix
other than the configured one, `cmake --install --prefix` included,
leaves the catalogue to `vhelp /SYSCACHE`.

## Conventions

The identification of a module is `Xaa-bb`, with `ECOc` for a
correction too small to be a revision of its own; `__REV__` follows
from it as `aa.bb.c` and is never written by hand.  Both are checked
when the build is configured, not merely by the test suite.

Diagnostics go through the message catalogue of the facility, in the
form `%HELP-S-MNEMONIC, text`, and always to the standard error --
the standard output carries data.  `$TRACE` is the one exception and
is compiled away by `NDEBUG`.

Sources are indented in the Whitesmiths manner with tabs of eight and
lines up to 132 columns.

Macros and condition values carry the `$` of the house style;
identifiers inside the CMake files do not.  A name with a `$` has to
be escaped on the command line of `cmake`, inside `install(CODE)` and
in every message that mentions it, which is a standing invitation to
get one of them wrong -- and `configure_file` will not substitute it
at all.

## Layout

| Module | What it holds |
|---|---|
| `include/helpdef.h` | the .HLB format, the condition values, the prototypes |
| `include/helpvers.h.in` | identification and build-time configuration |
| `cmake/HELPHelpers.cmake` | `help_add_library()` for a consuming product |
| `src/helputl.c` | FAO output, the pager, the message catalogue |
| `src/hlb.c` | the library service: reader and builder |
| `src/mdparse.c` | Markdown to topic tree |
| `src/mdgen.c` | topic tree back to Markdown, for /EXTRACT |
| `src/hlbcat.c` | the search path, the catalogue and its cache |
| `src/helptran.c` | the librarian, installed as `vhelptran` |
| `src/help.c` | the browser, installed as `vhelp` |
| `doc/helplib.md` | the manual of the product, English |
| `doc/helplib_ru.md` | the same in Russian, installed under `share/help/ru` |
| `test/smoke.sh` | fifty-five functional checks |
| `test/install.sh` | what an installation contains |
| `test/package.sh` | what a package contains |
| `packaging/postinst.in`, `postrm.in` | the scripts of a DEB or RPM: the shared catalogue |
| `packaging/CPackProject.cmake.in` | the prefix of each package, told to CPack at packaging time |
| `LICENSE` | Apache License 2.0 |

## License

Apache License 2.0, see `LICENSE`.

Copyright 2026 StarLet Squad and Ruslan R. Laishev.
