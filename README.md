# QSEFS

_A name derived from a very distorted (CUEs + FUSE)-FS_

A FUSE3 filesystem that provides individual access to tracks declared by CUE
sheets as read-only virtual WAV files. For players that don't support CUE
sheets on their own.

Inspired by [trackfs](https://github.com/andresch/trackfs), although I've never
used it directly. Unlike [trackfs](https://github.com/andresch/trackfs),
supports CUE sheets referencing multiple files and overall doesn't care about
the file name other than the extension, but does not support embedded CUE sheets
(yet?)

WAV is chosen as the "emulated track" format due to its simplicity for simulated
read operations. No full files are actuall cached in RAM, which gives (or is
supposed to give, at least) little memory overhead. Runs in single threaded
mode, for now at least.

__Project status:__ good enough for me to run at home for the time being.
Very much unfinished  on many, many levels, but just as much functional.

## Dependencies

### Compiler and tools

* _At least_ __GCC 16__ or any other compiler that supports C++26 __with
  reflection__. At the time of writing, Clang does not support that, sadly
* __CMake__ and __Ninja__, supporting C++ modules and `import std`

_Some may find the usage of reflection in `metadata.xx` laughably small to
justify imposing such restrictions. I find it a very convenient solution to
this little problem, and hope that more support will arrive sooner rather
than later._

### System libraries (discovered by CMake via `PkgConfig`)

* __FUSE3__, to run the filesystem
* __libav__ from __ffmpeg__, for audio decoding

### Other libraries

* my [dot-xx](https://github.com/gregthemadmonk/dot-xx) via
  [CPM](https://github.com/cpm-cmake/cpm.cmake), linked statically, for utility

## Building & running

Are you sure?

If yes, consult the __PKGBUILD__ in `pkg/arch` for building, but overall it's
the regular __CMake__+__Ninja__+C++ modules setup. Don't forget to do a
recursive clone of this repo!

## Cusomization

None, pretty much, at the moment. You can supply FUSE with additional arguments
like `-o allow_other` or `-f`. If packaged right, can be used from `fstab` -
as I do on my home server.

## TODOs

I won't make these into issues, but ideally this project should:

* have a configuration file/proper command-line arguments, at least for things
  like `FUSE::ignore_dotfiles`, `FUSE::max_cached_files` and
  `FUSE::show_base_files`
* have proper UID/GID setup
* have proper testing, especially of bit-correctness of the output WAVs. They
  seemed correct when I compared a track in Tenacity with the source FLAC,
  but actual testing is very much a good thing to do here
* have proper error reporting in all read operations instead of some places
  where "it just works" now
* solve all in-code TODOs/FIXMEs
* support more media tags, ID3 tags and inline CUE sheets
* support in-line cover art
* have proper stability testing and better error reporting when something
  goes wrong (now it mostly just exits/crashes on exceptions)
* it can use some refactoring, but what project couldnt?

Overall I'm happy with how it turned out so far, but I probably won't be working
on it _a lot_ since it is entered the state of being kind of practical already.

## LLM disclosure

LLMs were and are used as a part of research when writing this project and to
help catch some bugs. I try to lean more on local ones, but Google's overview
is also quite useful.

LLMs were not and are not used to generate the actual source code for this
project.
