
# VHELP -- display information from a help library

Invokes the help facility to display information about a topic
stored in a help library.

Format:

```
VHELP [topic [subtopic ...]] [/qualifiers]
```

At the \`Topic?\` prompt type the name of a topic, an empty line to
move one level up, or EXIT to leave the facility.

A key that carries a blank is named either as a run of words or in
quotes -- the facility takes the longest key that matches:

```
$ vhelp help vhelptran source format
$ vhelp help vhelptran "source format" directives
```

A topic may carry a wildcard, and then every subtopic whose name
matches it is displayed in turn: * stands for any run of characters,
% and ? alike for any single one.

```
$ vhelp help vhelptran /*
$ vhelp help vhelptran /?DENT
```

A qualifier is spelled with a slash or with a doubled dash --
/WIDTH=80 and --WIDTH=80 are the same thing. A single dash is not a
qualifier prefix, so -LIST is a topic; the sole exception is the
request for the command syntax, which is answered in every usual
spelling.

A key may be spelled exactly like a qualifier of the utility --
/LIST is both -- and the qualifier wins. A lone \`--\` ends the
qualifiers, so that such a key can still be asked for:

```
$ vhelp -- help vhelp /list
```

## Parameters

### topic

The name of the topic the information is wanted about. An
unambiguous leading abbreviation of a key is accepted in place of
the whole key; an ambiguous one is treated as if an asterisk had
been appended to it.

### subtopic

The name of a subtopic of the topic named before it. Any number of
levels may be named on one command, up to the nesting depth of the
library.

A subtopic whose key carries a blank may be named as several words;
they are gathered back into one key before the lookup.

## /LIBRARY -- select the help library or the search path

```
/LIBRARY=filespec
/LIBRARY=element[:element ...]
```

A value naming a single library pins the run to it: that library
answers, or nothing does. A name that does not exist is reported
rather than quietly answered out of another library.

A value naming a directory, or a list of elements separated by a
colon, is a search path -- and it is the whole of it. A qualifier is
an instruction for this one run and is obeyed literally, so nothing
else is consulted, the installed libraries included.

When the qualifier is absent the search path is composed of

```
$HELP_LIBRARY           a library, a directory, or a list of either
$HOME/.help             the private libraries of the user
<install>/<language>    the translation of the manual, when the
                        locale names a language and the directory
                        is there
the install directory   the libraries the product was installed with
beside the image        the same, found relative to the running
                        program, so a tree that has been moved or
                        staged elsewhere still answers
```

The language is the first component of LC_MESSAGES, LC_ALL or LANG,
whichever is set first -- ru out of ru_RU.UTF-8. A translation
defines the same topics as its original, so the two cannot share a
place on the path: one would merely hide the other, and which is
wanted is a question only the user can answer.

The environment variable is a standing preference rather than an
instruction, so it leads the default path instead of replacing it.

in that order, which is the order of precedence: a topic defined by
a library earlier on the path hides the one defined later, as a
command found earlier on PATH does. /CATALOG shows what hides what.

An element of the path that is a directory is scanned for files of
the type .hlb, in the collating order of their names. An element
ending in an ellipsis is scanned to the depth of its tree, after the
manner of the [dir...] of a VMS file specification:

```
$ vhelp /LIBRARY=/opt/pkg/help/... topic
```

A link to a directory is not descended into; that is how a tree
comes to contain itself.

## /WIDTH -- set the page width

```
/WIDTH=n
```

Sets the width of the page in characters, overriding the width
reported by the terminal driver. The value is clamped to the range
32 to 512.

## /PAGE -- stop at the end of a page

The qualifier is negatable: /NOPAGE suppresses the pause at the end
of every page, /PAGE asks for it explicitly.

Paging is in effect only when the output is a terminal, so a
redirected run never waits for a keystroke whichever form is given.

At the end of a page HELP asks you what to do:

- press RETURN to see the next page;
- type Q and press RETURN to skip the rest of the topic. HELP lists
  the subtopics again and asks for one at the same level;
- press Ctrl-D to leave HELP at once.

## /PROMPT -- enter the dialogue

The qualifier is negatable: /NOPROMPT displays the topic named on
the command and exits, /PROMPT asks for the dialogue explicitly.

The dialogue is entered only when the output is a terminal.

## /LIST -- list the topic tree

Lists every key of the library, one per line, indented by its
nesting level, preceded by a header giving the identification, the
creation time and a note of what produced the library out of what.

## /CATALOG -- list the search path and its libraries

Lists the search path, the catalogue cache in use, every library
found, and the topics each of them answers for. A topic that a
library earlier on the path already answers for is marked hidden.

## /CACHE -- use the catalogue cache

The qualifier is negatable and /CACHE is the default. The catalogue
of the search path -- which library answers for which topic -- is
kept in a cache so that the libraries need not all be opened on
every run. /NOCACHE builds the catalogue afresh and writes nothing.

The cache lives in the first of these that can be had:

```
$HELP_CACHE             named outright
$XDG_CACHE_HOME/help/   the usual place
$HOME/.cache/help/      when XDG_CACHE_HOME is unset
/tmp/help-<uid>/        when there is no home at all
```

It is validated against the modification time and the size of every
element of the search path, directories included, so a library
added, changed or removed is noticed without opening anything. A
cache that disagrees, that cannot be read, or that is not a private
file of this user is dropped and rebuilt rather than repaired --
deleting it by hand is never necessary and never harmful.

The last of the directories above is one anyone may write to, so
the cache is treated as hostile there. The directory must belong to
this user and be closed to everyone else; it is inspected once, and
everything afterwards is done relative to a descriptor of it rather
than to its name. A name would be resolved anew on every call, and
between two resolutions it can be made to mean something else -- a
descriptor cannot. Whether the shared directory carries the sticky
bit therefore does not matter; nothing is trusted to it.

## /SYSCACHE -- build the catalogue shared by every user

Harvests the search path and writes the catalogue that every user of
the system reads, in the directory the product was configured with.

```
$ vhelp /SYSCACHE /LIBRARY=/usr/local/share/help
%HELP-I-SYSCACHE, /var/cache/help/catalog.idx written, 93 topics out of 31 files
```

The directory is the one the product was configured with, unless
HELP_SYSCACHE names another; the same variable is read by an
ordinary run, so a catalogue written elsewhere is found there.

It is an administrative action: it needs the right to write that
directory, which an ordinary run neither has nor asks for. The
installation of the product performs it on its own libraries, and it
may be repeated whenever libraries are added to a system directory.

The shared catalogue is a store of libraries already opened, not a
cache of any one search path, so a user whose path differs still
takes from it whatever libraries it holds. Each is validated against
the file system as it is taken, exactly as a private cache is, so a
stale entry is passed over rather than believed.

A shared catalogue that anyone may write is refused outright -- it
would be a way of steering every user of the system to a library of
one's choosing.

## /HELP -- display the command syntax

Displays a summary of the command syntax and exits, without reading
a library at all. Use it when the library itself cannot be reached.

The request is recognised in every usual spelling -- /HELP, /H,
-help, --help, -h -- but only in the dashed or slashed form. A bare
HELP is the name of a topic, and this library defines one.

## Examples

```
$ vhelp submit
$ vhelp submit /after
$ vhelp sub*
$ vhelp submit /nopage /noprompt
$ vhelp /library=mylib.hlb topic subtopic
$ vhelp /list /nopage | wc -l
$ vhelp /library=helplib_ru.hlb help vhelptran "формат исходного текста"
```

# VHELPTRAN -- create a help library

Translates one or several Markdown files into a help library.
VHELPTRAN is to this facility what LIBRARY/HELP/CREATE is to
OpenVMS.

Format:

```
VHELPTRAN file.md [file.md ...] [/qualifiers]
VHELPTRAN /EXTRACT file.hlb [/KEY=path] [/OUTPUT=file.md]
```

Every input file contributes its topics to the one library named by
/OUTPUT. The second form runs the other way and recovers a Markdown
source out of a library.

## Source format

A source is an ordinary Markdown file. Its headings become the keys
of the library and everything between them becomes the text of a
topic; nothing else in the file carries any structural meaning.

```
# SUBMIT                ->      1 SUBMIT
## /AFTER               ->      2 /AFTER
### Examples            ->      3 EXAMPLES
```

Several sources may be named on one command; each contributes its
topics to the one library.

### Headings -- how a key is made

A heading is a run of one to nine number signs followed by white
space. The run gives the nesting level of the topic; a closing run
of number signs, if any, is discarded.

The key is the text of the heading with the markup removed,
truncated at the first descriptive separator, trimmed, its internal
runs of white space collapsed to one blank, and upper-cased. The
separators are

```
" - "     " -- "     " : "     an em dash     an en dash
```

so the heading

```
## /AFTER -- hold the job until a given time
```

yields the key /AFTER while the whole line stays legible in the
source. When no separator is present the whole heading becomes the
key, blanks and all: the heading

```
## Source format
```

yields the key SOURCE FORMAT, which is asked for either as a run of
words or in quotes.

The case is folded for the Latin and for the Cyrillic alphabet
alike, in UTF-8, so a Russian key is found whatever case it was
typed in.

When the key that the rules yield is not the one wanted, state it:

```
<!-- key: /AFTER -->
## Anything at all as a heading
```

### Text -- what happens to a body

The body of a topic is rendered into plain text. The table says
what becomes of each construct; anything not named here is copied
through untouched.

```
fenced block, ``` or ~~~    copied verbatim, shifted right by two
                            positions; the fence lines themselves
                            disappear, and nothing inside a fence
                            is interpreted
**strong**  __strong__      the markers are removed
`code`                      the back-quotes are removed, the text
                            between them is taken as it stands
                            (\x is \x there)
[text](url)                 becomes: text
![alt](url)                 becomes: alt
- item   * item   + item    the bullet becomes the letter o
> quoted                    the marker is removed
\x                          becomes: x
| a | b |                   copied as it stands
```

The leading indent of a line is preserved, so nested lists and
indented examples keep their shape. Trailing white space is
dropped. A line that renders to nothing at all -- a horizontal rule
does -- becomes a blank line.

Blank lines inside a topic are kept; those leading and trailing it
are dropped, so a display never opens or closes with white space.

**The text is never re-wrapped.** It is shown exactly as written,
indented by two positions per nesting level plus two. A topic at
level three on an eighty-column terminal therefore has sixty-two
positions left, so a source is best kept to some sixty-five columns.

Text that stands before the first heading belongs to no topic and
is dropped, with a diagnostic.

### Directives

Three directives are recognised, each written as an HTML comment
occupying a line of its own:

```
<!-- key: /AFTER -->      the key of the topic that follows
<!-- ident: X-01 -->      identification of the library
<!-- ignore -->           opens a region left out of the library
<!-- resume -->           closes it
```

Inside an ignored region nothing is interpreted -- headings and all
-- so a document that serves both as a manual and as a source of
help can keep its front matter out of the library.

An identification given by /IDENT outranks the one written here: a
qualifier is an instruction for the run at hand, a directive is a
property of a file that anybody may translate for any purpose.

Inside a fenced block a directive is text, which is how the four
above are shown here.

### Limits

```
nesting level           9
key                     64 bytes, i.e. 32 Cyrillic characters
source line             1024 bytes after rendering
```

A heading deeper than nine is clamped, a longer key is truncated, a
longer line loses its tail -- each with a diagnostic of its own, see
DIAGNOSTICS.

One limit has no diagnostic because it cannot have one: a fenced
block cannot contain a line that begins with a fence. The two
spellings are one and the same marker, so the first line inside a
block whose first printable characters are \`\`\` or ~~~ closes it. A
marker anywhere else in the line is ordinary text, which is how the
table above shows both spellings.

### Example

A complete source of two topics:

```
<!-- ident: MYLIB-01 -->

# COPY -- copy a file

Copies one file to another. The input file is left alone.

Format:

    COPY input output

## /LOG -- report each file copied

Reports each file as it is copied. The default is /NOLOG.

## EXAMPLES

    $ copy a.txt b.txt
    $ copy /log a.txt b.txt
```

Translated and consulted:

```
$ vhelptran mylib.md /OUTPUT=mylib.hlb /IDENT=MYLIB-01
%HELP-I-CREATED, mylib.hlb created, 3 topics, 214 bytes of text

$ vhelp /LIBRARY=mylib.hlb copy /log
```

## /OUTPUT -- name the library

```
/OUTPUT=filespec
```

Names the library to be created. By default it is the first input
file with its type replaced by .hlb.

The library is written to a temporary file in the same directory
and renamed over the target only after the last byte has reached
the file system, so a failed run never leaves a half-written
library behind.

## /IDENT -- stamp an identification

```
/IDENT=string
```

Stamps an identification of up to fifteen characters into the
header of the library. It is displayed by VHELP/LIST.

## /FACILITY -- root the library at one key

```
/FACILITY=name
```

Creates the single key of level 1 that names the facility and puts
everything the sources hold one level below it.

The catalogue of a search path merges the level 1 topics of every
library it finds, so what a library puts there is what the whole
system sees. A library that exports its verbs -- SUBMIT, DELETE,
PURGE -- puts them in a namespace it shares with every other
library, where they collide with the verbs of the next product and
hide, or are hidden by, them. One key per library, naming the
facility, is what keeps that namespace legible:

```
  Information available:

  BATCH         DCL           HELP
```

A root costs one of the nine nesting levels, so a source that fills
all nine on its own does not fit under one; the topic that overflows
is reported with %HELP-E-TOODEEP and flattened onto its parent.

Text that precedes the first heading has nowhere to go in a library
that is not rooted, and is dropped with %HELP-W-NOTOP. Under a root
it has: it becomes the text of the facility topic itself.

## /SORT -- sort the subtopics

The qualifier is negatable and /SORT is the default: the subtopics
of every key are ordered by the key, which is what the "Additional
information available" block of OpenVMS HELP shows. /NOSORT keeps
them in the order they appear in the source.

## /EXTRACT -- recover the source of a library

Reads a library and writes its Markdown source, the way
LIBRARY/EXTRACT recovers a module on OpenVMS.

```
$ vhelptran /EXTRACT helplib.hlb /OUTPUT=helplib.md
$ vhelptran /EXTRACT helplib.hlb /KEY="HELP VHELPTRAN SOURCE FORMAT"
```

Without /OUTPUT the source goes to the standard output.

The first pass of a recovery is lossy: the descriptive tail of a
heading and the emphasis markers live in the source only, not in
the library. From there on the round trip settles -- extract,
translate and extract again yields the identical text -- and the
product is tested against exactly that.

/EXTRACT cannot be given together with the qualifiers that describe
how a library is to be built; the attempt is refused with
%HELP-E-CONFQUAL.

## /KEY -- what exactly to extract

```
/KEY=path
```

Extracts the named topic together with its subtopics only. The path
is the keys from level one downwards separated by blanks; a key
that itself carries a blank is recognised whole. The root of the
extracted subtree is lifted to level one, so the result is a source
in its own right rather than a fragment.

## /LIST -- list the created library

Reads the library back after it has been written and lists its
topic tree. Doubles as a proof that the library can be opened and
that its checksum agrees.

## /HELP -- display the command syntax

Displays a summary of both forms of the command and exits. The
request is recognised as /HELP, /H, -help, --help or -h.

## Diagnostics

The translator reports, but does not reject, the following:

```
%HELP-W-SKIPLVL   a heading level was skipped, the intermediate
                  levels have been assumed
%HELP-W-DUPKEY    two subtopics of one key carry the same name
%HELP-W-LONGKEY   a key longer than 64 bytes was truncated
%HELP-W-LONGLINE  a line longer than 1024 bytes lost its tail
%HELP-E-NOKEY     a heading carries no printable text
%HELP-W-NOTOP     text appears before the first heading
```

# MESSAGES -- the diagnostics of the facility

Every diagnostic is written in the OpenVMS format

```
%HELP-<severity>-<identification>, <text>
```

and goes to the standard error, never to the standard output, which
carries the text of a topic or the source of an /EXTRACT.

The severity is S for success, I for information, W for a warning, E
for an error and F for a fatal condition. A warning means the work
was done with something left out; an error, that it was not done.

## Reading a library

```
%HELP-E-NOTFOUND   no topic of that name, the whole path is named
%HELP-E-NOTHLB     the file is not a help library
%HELP-E-BADVERS    the library is of another format version
%HELP-E-BADCRC     the checksum of the library disagrees
%HELP-E-TRUNCLIB   the library is shorter than its header says
%HELP-E-OPENIN     the file could not be opened
%HELP-E-READERR    the file could not be read
```

A library that fails any of these is not used at all. Nothing is
repaired in place: a library is a build artefact, and the answer to
a damaged one is to translate it again.

## The search path and the catalogue

```
%HELP-E-NOLIBS     nothing on the search path holds a library
%HELP-W-TOOMANY    more elements on the path than the catalogue takes
%HELP-W-TOOMANYT   more topics than the catalogue takes
%HELP-W-CACHERR    the cache directory is unusable, caching is off
%HELP-I-SYSCACHE   the shared catalogue has been written
```

TOOMANY and TOOMANYT mean the catalogue is incomplete, so a topic
that exists may not be found. CACHERR is not a failure of the run:
the catalogue is simply built afresh every time.

## Translating a source

```
%HELP-I-CREATED    the library has been created
%HELP-I-DIAGS      how many diagnostics the translation issued
%HELP-E-EMPTYLIB   the source holds no topic, nothing was created
%HELP-W-NOTOP      text stands before the first heading, dropped
%HELP-E-NOKEY      a heading carries no printable text, topic dropped
%HELP-W-SKIPLVL    a heading level was skipped, levels assumed
%HELP-W-DUPKEY     two subtopics of one key carry the same name
%HELP-W-LONGKEY    a key longer than 64 bytes was truncated
%HELP-W-LONGLINE   a line longer than 1024 bytes lost its tail
%HELP-E-TOODEEP    a heading deeper than nine levels, clamped
%HELP-E-OPENOUT    the output file could not be created
%HELP-E-WRITERR    the output file could not be written
```

A warning does not stop the translation; the library is created and
the count of what was reported ends the run as %HELP-I-DIAGS.

## Extraction

```
%HELP-I-EXTRACTED  the source has been written
%HELP-E-CONFQUAL   /EXTRACT was given with a qualifier of the other form
```

## The command line

```
%HELP-E-IVQUAL     the value of a qualifier is not acceptable
%HELP-W-TOOMANYIN  more input files than the command takes
%HELP-W-TOOMANYQ   more qualifiers than the command takes
%HELP-W-TOOMANYW   more words in the topic than the command takes
```

Each of the three counts is reported once, however many arguments
were dropped. They matter: a command whose tail went missing would
otherwise answer about a topic above the one that was asked for, and
report success doing it.

## Exhaustion

```
%HELP-F-NOMEM      memory could not be allocated
```

## Internal

These name a fault in the facility itself rather than in what it was
given, and none of them should ever be seen:

```
%HELP-E-QUEFAIL    a key could not be queued while a library was built
%HELP-E-BADRUN     a key names text lying outside the heap of its library
```

BADRUN follows a library whose checksum agreed, so it means the index
and the heap disagree with each other -- translate the source again.
Report either of them.

# LIBRARY -- the format of a help library

A help library is a flat, self-contained, read-only file of three
sections: a fixed header, an index of fixed-size key records and a
heap holding the text of every topic.

The key records are stored in the pre-order of the topic tree, so
the whole subtree of a key occupies a contiguous run of the index.
That is what makes the wildcard scan and the listing a plain
sequential pass over the array.

## Header

The header carries a magic number, the format version, the offsets
and the sizes of the two other sections, a CRC-32 over both of
them, the creation time, the identification given by /IDENT and a
note of who produced the library and out of what.

The magic number is stored in the native byte order, so a library
carried to a host of the opposite endianness is refused rather than
misread. A library is a build artefact, not an exchange format.

## Checksum

The checksum covers the index and the heap together. It is verified
by VHELP every time a library is opened; a mismatch is reported as

```
%HELP-E-BADCRC, checksum error in ...
```

and the library is not used.
