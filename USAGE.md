# Using libacfutils

* Use CMake, please. This library ships with all the needed scaffolding to use it from any other CMake project.
```cmake
find_package(libacfutils CONFIG REQUIRED)
target_link_libraries([TGT] PRIVATE libacfutils::acfutils)
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
