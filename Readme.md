glbindify
=========

glbindify is a command line tool that generates C bindings for OpenGL, WGL, and GLX. The generated bindings can then be included in your projects, eliminating the need to link to a seperate loader library. It supports generating bindings for compatibility contexts up to GL version 3.1 and core profile contexts for versions 3.2+. The bindings are generated using XML API specifications mainained by khronos so only these XML files need to be updated to support new GL versions or extensions.

Why use glbindify?
------------------

Starting with OpenGL 3.x each new OpenGL version depricates some features. If an application uses a so called "core profile" context then these depricated commands may be removed from the API. By generating bindings for a specific GL version and list of extensions you can insure at compile-time that your application only depends on that feature set. The GL header and binding code will also be considerably smaller. A GL header containing all OpenGL functions specified so far is more than 18k lines but a glbindify header containing only OpenGL 3.3 core functions is only 1.5k lines.

I created glbindify because none of the binding generators I evaluated suited me. They either didn't manage extensions, had run-time performance implications, weren't maintained, or had undesirable build time dependencies.

Command line usage
------------------

To generate bindings you must specify the API name, required version, and an optional list of extensions. The tool will generate a source file and header file for the API in the current directory. The filenames generated will reflect the API and version number chosen.

Example: Generate C bindings for OpenGL 3.3 with support for `EXT_texture_filter_anisotropic` and `EXT_direct_state_access` extensions

`glbindify -a gl -v 3.3 -e EXT_texture_filter_anisotropic -e EXT_direct_state_access`

Example: Generate C bindings for GLX version 1.4 with support for `ARB_create_context` and `ARB_create_context_profile` extensions

`glbindify -a glx -v 1.4 -e ARB_create_context -e ARB_create_context_profile`

Example: Generate C bindings for WGL 

`glbindify -a wgl -v 1.0`

Using the bindings
------------------

Call `init_<api>()` after you have created an OpenGL context, where `<api>` is one of `gl`, `wgl`, or `glx`. If all functions (not including extention functions) were found `init_<api>()` will return `true`. Note that since glbindify mangles the GL function names with '#define' macros you must avoid including system OpenGL headers in files that also include the bindings.

Example:

	#include "gl_3_3.h"
	...
	if (!init_gl())
		exit(-1);
	...
	glDrawArrays(...);

Extensions
----------

After initializing the bindings you may determine if an extension was successully loaded by checking its corresponding support flag. The support flag is the full name of the extension. An extension support flag will be set to true if it's name was found in the driver's extension string *and* all functions for the extension were located. Note that `glbindify` will only attempt to load extensions specified on the command line.

Example: Checking for the `GL_ARB_texture_storage` extension

	if (GL_ARB_texture_storage) {
		glTexStorage3D(...);
	} else {
		...
	}

Dependencies
------------

The only dependencies of glbindify are C++ compiler and a unix-like build environment. The tool and it's generated bindings are known to build with Linux, Cygwin, and MSYS2 using gcc 4.8.2 and higher. Other configurations may work but have not been tested.
