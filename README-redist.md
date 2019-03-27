# libacfutils DLL redistribution version

This a compact redistribution package for libacfutils for Windows 7 or
later (x64 architecture only). Includes the header and library files. For
the macOS and Linux versions, ask the developer, he'll be more than happy
to start uploading those as well.

## Compiler setup

To make the library functions visible in your compiler, simply add the
following subfolders to the include path of your IDE of choice:

* `include`
* `include/cairo`
* `include/freetype`

Then you can use the following include header to grab all the provided
functionality:

* To get `libacfutils` functionality, simply do (obviously replace
`FILE.H` with the appropriate header file):
```
#include <acfutils/FILE.H>
```

* To get Cairo functionality, do:
```
#include <cairo.h>
#include <cairo-ft.h>
```

* To get FreeType functionality, do:
```
#include <ft2build.h>
#include FT_FREETYPE_H
```

* To get GLEW functionality, do (also don't forget to do `glewInit()` in
your loading routine):
```
#include <acfutils/glew.h>
```

## Linker setup

* On Windows, add libacfutils-redist/win64/lib to your library search path.
  The exact list of libraries is automatically picked up from the header
  files. Also be sure to add libacfutils-redist/win64/src/lacf_msvc_compat.cpp
  to your project. This is needed to pull in certain symbol defines and
  basic scaffolding for GLEW integration.

* On Mac and Linux, add libacfutils-redist/{mac,lin}64/lib to your
  library search and link all static libraries in the respective directories.
  On these platforms you don't need to add any source files to your project.
