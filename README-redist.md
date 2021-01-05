# libacfutils redistribution version

This a compact redistribution package for libacfutils for Linux (Ubuntu
16.04+), Mac OS 10.12+ and Windows 7+ (x86-64 architectures only).
Includes the header and library files.

The package has several subdirectories, organized by environment type:

- win64 - a dynamic build that lets you link the library against your
plugin. Use this when building on Windows and you're unsure which version
you need.

- mac64 & lin64 - a static build for MacOS and Linux respectively. This
lets you link the library into your plugin.

- mingw64 - a static build for Windows which, however, requires a
MinGW-compatible compiler setup (DO NOT use this if you're using Visual
Studio).

## Compiler setup

To make the library functions visible in your compiler, simply add the
following subfolders to the include path of your IDE of choice:

* `libacfutils-redist/include`
* `libacfutils-redist/${PLATFORM}/include`

Where ``${PLATFORM}`` is one of ``win64``, ``mac64``, ``lin64`` or
``mingw64``. Then you can use the following include statements to grab
all the required functionality:

* To get `libacfutils` functionality, simply do (obviously replace
`FILE.H` with the appropriate header file):
```c
#include <acfutils/FILE.H>
```

* To get Cairo functionality, do:
```c
#include <cairo.h>
#include <cairo-ft.h>
```

* To get FreeType functionality, do:
```c
#include <ft2build.h>
#include FT_FREETYPE_H
```

* To get OpenGL/GLEW functionality, do (also don't forget to call
`glewInit()` in ``XPluginStart``):
```c
#include <acfutils/glew.h>

/* goes in XPluginStart */
if (glewInit() != GLEW_OK) {
	/* Couldn't initialize OpenGL interface, report error & return */
	return (0);
}
```

* For static builds of the library on MinGW, you must also include the
following code blurb somewhere in your plugin to get proper GLEW-MX
multi-threading initialization working. This isn't necessary when using
the dynamic (DLL) version under MSVC.
```c
#if	IBM
BOOL WINAPI
DllMain(HINSTANCE hinst, DWORD reason, LPVOID resvd)
{
	UNUSED(hinst);
	UNUSED(resvd);
	lacf_glew_dllmain_hook(reason);
	return (TRUE);
}
#endif	/* IBM */
```

## Linker setup

* On Windows when using the dynamic build (DLL), add
  ``libacfutils-redist/win64/lib/libacfutils${VERSION}.a`` to your linker
  inputs. Here ``${VERSION}`` is the version number of the library (there
  should only be a single ``.a`` file, so you'll know which one to pick).
  Also don't forget to include the DLL file together with your plugin in
  the final distribution package (just place it next to the XPL).

* On Mac, Linux and MinGW, add libacfutils-redist/{mac64,lin64,mingw64}/lib
  to your library search and link all static libraries in the respective
  directories. Since the build is static, it will get linked into your XPL
  and no other files are needed for distribution.
