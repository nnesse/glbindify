glbindify
=========

glbindify is a command line tool that generates C bindings for OpenGL, WGL, and GLX.  The generated bindings can then be included in your projects, eliminating the need to link to a seperate loader library. The bindings are generated using XML API specifications mainained by khronos so only these XML files need to be updated to support new GL versions or extensions.

It supports generating bindings for core profile contexts only which substantially reduces the size of the bindings. The generated header file only exposes functions and enums for API versions and extensions you select at compile time, ensuring that your application does not accidentally aquire unwanted dependencies.

Command line usage
------------------

To generate bindings just specify the API name to `glbindify` where the API name is one of `gl`, `wgl`, or `glx`. The tool will generate a source file and header file for the API in the current directory with the named `glb-<api>.c` and `glb-<api>.h`.

Example: Generate C bindings for OpenGL core profile contexts

`glbindify -a gl`

Using the bindings
------------------

After you have created your OpenGL context and made it current you must call

`bool glb_<api>_init(int major_version, int minor_version)`


where `<api>` is one of `glcore`, `wgl`, or `glx`. If all functions for the requested version, excluding extensions, were found `glb_<api>_init()` will return `true`. Since glbindify mangles the GL function names with '#define' macros you must avoid including system OpenGL headers in files that also include the bindings.

Example:

	#include "glb-glcore.h"
	...
	if (!glb_glcore_init(3, 3))
		exit(-1);
	...
	glDrawArrays(...);

Targeting specific versions
---------------------------

By default the generated header file will only expose functions and enums for the minimum version supported by `glbindify`. For OpenGL the minimum version is 3.2 core profile, for GLX it is 1.4, and for WGL it is 1.0. To access functionality for later versions you must define a macro `GLB_<API>_VERSION` as an integer `(major_version * 10) + minor_version` where `<API>` is the name of the API in all caps.

Extensions
----------

After initializing the bindings you may determine if an extension was successully loaded by checking its corresponding support flag. The support flags are named `GLB_<extension name>`. An extension support flag will be set to true if it's name was found in the driver's extension string *and* all functions for the extension were located. An extension's specific functions and enum values will only be exposed if the macro `GLB_ENABLE_<extension name>` is defined before `glb-glcore.h` is included.

Example: Checking for the `GL_ARB_texture_storage` extension

	#define GLB_GL_VERSION 33
	#define GLB_ENABLE_GL_ARB_texture_storage 1
	#include "glb-glcore.h"
	...
	if (GLB_GL_ARB_texture_storage) {
		glTexStorage3D(...);
	} else {
		...
	}

Binding namespace
-----------------

The naming of functions and macros in the bindings can be changed by passing a `-n` option to `glbindify`. When used, all instances of `glb` above
will be changed to the selected namespace and all instances of `GLB` will be replaced with the upper case version of the selected namespace.

Example: Generating C bindings for OpenGL with a `myapp` namespace

	`glbindify -a gl -n myapp`
	
Example: Using bindings with a `myapp` namespace

	#include "myapp-glcore.h"
	...
	if (!myapp_glcore_init(3, 3))
		exit(-1);
	...
	glDrawArrays(...);

Building
--------

`glbindify` requires only a C++98 compatible compiler to build. `glbindify` is known to build on GNU/Linux and Windows. On UNIX-like systems if `gperf` is available glbindify will generate a perfect hash map for extension checking at initialization time.

On UNIX-like systems `glbindify` can be built with its autotools build system:

	./autogen.sh
	./configure <options>
	make
	make install

On Windows `glbindify` can be built with the Visual Studio solution file in the `windows` folder. Note that the resulting executable must be run from the top level source directory.

The generated bindings will work on any supported platform regardless of the system they were built on. `glbindify` can also be built without the build system by compiling the sources with default options. For example:

	g++ glbindify.cpp tinyxml2.cpp -o glbindify
