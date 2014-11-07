glbindify
=========

glbindify is a command line tool that generates C++ bindings for OpenGL, WGL, and GLX. The generated bindings can then be included in your projects, eliminating the need to link to a seperate loader library. It supports generating bindings for compatibility contexts up to GL version 3.2 and core profile contexts for versions 3.3 to 4.5. The bindings are generated using XML API specifications mainained by khronos so only these XML files need to be updated to support new GL versions or extensions.

Command line usage
------------------

To generate bindings you must specify the API name, required version, and an optional list of extensions.
The tool will generate a C++ source file and header for the API in the current directory.

Example: Generate GL bindings for OpenGL 3.3 with support for `EXT_texture_filter_anisotropic` and `EXT_direct_state_access` extensions

`glbindify -a gl -v 3.3 -e EXT_texture_filter_anisotropic -e EXT_direct_state_access`

Example: Generate bindings for GLX version 1.4

`glbindify -a glx -v 1.4 -e ARB_create_context -e ARB_create_context_profile`

Example: Generate bindings for WGL 

`glbindify -a wgl -v 1.0`

Using the bindings
------------------

Call `gbindify::<api>::init()` after you have created an OpenGL context, where `<api>` is one of `gl`, `wgl`, or `glx`. If all functions (not including extention functions) were found `glbindify::<api>::init()` will return `true`.

Example:   

	#include "glbindify_gl.hpp"
	using namespace glbindify;
	...
	if (!gl::init())
		exit(-1);


The tool places typedefs such as `GLenum`, `GLint`, etc in the `glbindify` namespace and places functions and enums in the `glbindify` namespace and as well as in the API specific namespaces `glbindify::gl`, `glbindify::wgl`, and `glbindify::glx`. For brevity and clarity functions and enum's placed in API specific namespaces are not prefixed with the API name. For example a C GL API call such as:

`glBindTexture(GL_TEXTURE_2D, tex)`

could be made as:

`glbindify::gl::BindTexture(glbindify::gl::TEXTURE_2D, tex)`

or

`glbindify::glBindTexture(glbindify::GL_TEXTURE_2D, tex)`

By encapsuating the API's in namespaces `glbindify` avoids colliding with system headers and libraries without resorting to using macros to mangle function names as some loaders do. To use `glbindify` with existing GL code include a `using namespace glbindify` statement in your sources.

Extensions
----------

After running `<api>::init()` you may determine if an extension was successully loaded by checking its corresponding support flag in the `glbindify::<api>` namespace. An extension's support flag has the same name as the extension excluding 'GL*_*', or 'GLX*_*' prefixes. An extension flag will be set to true if it's name was found in the driver's extension string *and* all functions for the extension were located. Note that `glbindify` will only attempt to load extensions specified on the command line.

Example:   

	using namespace glbindify;
	...
	if (gl::ARB_texture_storage) {
		gl::TexStorage3D(...);
	} else {
		...
	}

Dependencies
------------

A C++11 compiler is required to build the command line tool and but the generated bindings do not require C++11. The command line tool and it's generated bindings are known to build in Linux, Cygwin, and MSYS2 using gcc 4.8.2 and higher. Other configurations may work but have not been tested.
