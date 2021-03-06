#include <stdint.h>

#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <set>
#include <fstream>
#include <stdio.h>
#include <math.h>
#include <errno.h>

#include <stdio.h>

#include <errno.h>

#if defined(_WIN32)
#define strdup _strdup
#include "getopt.h"
#else
#include <getopt.h>
#endif

#include "tinyxml2.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#define PACKAGE_VERSION "<unknown>"
#define PACKAGE_STRING "<unknown>"
#endif

#define USE_GPERF HAVE_GPERF && !defined(_WIN32)

#if USE_GPERF
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#endif

enum API {
	API_GL,
	API_GLES2,
	API_EGL,
	API_GLX,
	API_WGL
};

enum API g_api;

using namespace tinyxml2;

std::string g_indent_string;

const char *g_prefix;
const char *g_macro_prefix;

#define FOREACH(var, cont, type) \
	for (type::iterator var = cont.begin(); var != cont.end(); var++)

#define FOREACH_CONST(var, cont, type) \
	for (type::const_ ## iterator var = cont.begin(); var != cont.end(); var++)

void increase_indent()
{
	g_indent_string.push_back('\t');
}

void decrease_indent()
{
	if (g_indent_string.size() > 0)
		g_indent_string.resize(g_indent_string.size() -1);
}

void reset_indent()
{
	g_indent_string.clear();
}

int indent_fprintf(FILE *file, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	fputs(g_indent_string.c_str(), file);
	return vfprintf(file, format, args);
}

static inline bool tag_test(const XMLNode &elem, const char *value)
{
	return !strcmp(elem.Value(), value);
}

static inline bool parent_tag_test(const XMLNode &elem, const char *value)
{
	return elem.Parent() && tag_test(*elem.Parent(), value);
}

static inline bool tag_stack_test(const XMLNode &elem, const char *value, const char *parent_value)
{
	return tag_test(elem, value) && elem.Parent() && tag_test(*elem.Parent(), parent_value);
}

static inline bool parent_tag_stack_test(const XMLNode &elem, const char *value, const char *parent_value)
{
	return elem.Parent() && tag_stack_test(*elem.Parent(), value, parent_value);
}

struct cstring_compare {
	bool operator()(const char *a, const char *b) const {
		return strcmp(a, b) < 0;
	}
};

struct enumeration {
	const char *name;
	std::map<const char *, unsigned int, cstring_compare> enum_map;
};

std::set<const char *, cstring_compare> g_common_gl_typedefs;

struct command {
	const char *name;
	const char *type;
	std::string type_decl;
	struct param {
		const char *type;
		const char *name;
		std::string decl;
	};
	std::vector<param> params;
	void print_declare(FILE *out, const char *command_prefix) {
		indent_fprintf(out, "extern %s (*%s%s)(", type_decl.c_str(), command_prefix, name);
		if (params.size()) {
			fprintf(out, "%s", params[0].decl.c_str());
			for(unsigned int i = 1; i < params.size(); i++) {
				fprintf(out, ", %s", params[i].decl.c_str());
			}
		}
		fprintf(out, ");\n");
	}
	void print_initialize(FILE *out, const char *command_prefix) {
		indent_fprintf(out, "%s (*%s%s)(", type_decl.c_str(), command_prefix, name);
		if (params.size()) {
			fprintf(out, "%s", params[0].decl.c_str());
			for(unsigned int i = 1; i < params.size(); i++) {
				fprintf(out, ", %s", params[i].decl.c_str());
			}
		}
		fprintf(out, ") = NULL;\n");
	}

	void print_load(FILE *out, const char *command_prefix) {
		indent_fprintf(out, "%s%s = (%s (*)(", command_prefix, name, type_decl.c_str());
		if (params.size()) {
			fprintf(out, "%s", params[0].decl.c_str());
			for(unsigned int i = 1; i < params.size(); i++) {
				fprintf(out, ", %s", params[i].decl.c_str());
			}
		}
		fprintf(out, ") ) LoadProcAddress(\"%s%s\");\n", command_prefix, name);
	}

	command() : name(NULL), type(NULL) {}
};

typedef std::map<const char *, command *, cstring_compare> commands_type;
typedef std::set<const char *, cstring_compare> enums_type;

struct interface {
	enums_type enums;
	commands_type commands;
	enums_type removed_enums;
	commands_type removed_commands;
};

//Api description
const char *g_api_name;
const char *g_variant_name;
const char *g_command_prefix;
const char *g_enumeration_prefix;
const char *g_api_print_name;;

//List of all enums and commands
typedef std::map<const char *, unsigned int, cstring_compare> enum_map_type;
typedef std::map<const char *, const char *, cstring_compare> enum_str_map_type;
enum_map_type g_enum_map;
enum_str_map_type g_enum_str_map;

std::vector<enumeration *> g_enumerations;

typedef std::map<const char *, command *, cstring_compare> commands_type;
commands_type g_commands;

typedef std::vector<std::string> types_type;
types_type g_types;

typedef std::map<int, interface *> feature_interfaces_type;
feature_interfaces_type g_feature_interfaces;

typedef std::map<const char *, interface *, cstring_compare> extension_interfaces_type;
extension_interfaces_type g_extension_interfaces;

bool is_command_in_namespace(const char **name)
{
	if (strstr(*name, g_command_prefix)) {
		*name = *name + strlen(g_command_prefix);
		return true;
	} else {
		return false;
	}
}

bool is_enum_in_namespace(const char **name)
{
	if (strstr(*name, g_enumeration_prefix) == *name) {
		*name = *name + strlen(g_enumeration_prefix);
		return true;
	} else {
		return false;
	}
}

void bindify(const char *header_name, int min_version, FILE *header_file, FILE *source_file);

template <class T>
class data_builder_visitor : public XMLVisitor
{
protected:
	T *m_data;
private:
	const XMLElement &m_root;

	virtual bool visit(const XMLText &text) { return true; }
	bool Visit(const XMLText &text) { return visit(text); }

	virtual bool visit_begin(const XMLElement &elem, const XMLAttribute *attrib) { return true; }
	virtual bool visit_enter(const XMLElement &elem, const XMLAttribute *attrib) { return true; }

	bool VisitEnter(const XMLElement &elem, const XMLAttribute *attrib)
	{
		if (&elem == &m_root) {
			return visit_begin(elem, attrib);
		} else {
			return visit_enter(elem, attrib);
		}
	}

	virtual bool visit_end(const XMLElement &elem, const XMLAttribute *attrib) { return true; }
	virtual bool visit_exit(const XMLElement &elem, const XMLAttribute *attrib) { return true; }

	bool VisitExit(const XMLElement &elem, const XMLAttribute *attrib)
	{
		if (&elem == &m_root) {
			return visit_end(elem, attrib);
		} else {
			return visit_exit(elem, attrib);
		}
	}
public:
	data_builder_visitor(const XMLElement &root) :
		m_data(new T()),
		m_root(root)
	{
	}

	T *build()
	{
		m_root.Accept(this);
		return m_data;
	}
};

class command_visitor : public data_builder_visitor<command>
{
	virtual bool visit(const XMLText &text)
	{
		if (!m_data)
			return false;

		if (parent_tag_test(text, "param")) {
			m_data->params.back().decl += text.Value();
		} else if (parent_tag_stack_test(text, "name", "param")) {
			m_data->params.back().name = text.Value();
		} else if (parent_tag_stack_test(text, "ptype", "param")) {
			m_data->params.back().type = text.Value();
			m_data->params.back().decl += text.Value();
		} else if (parent_tag_test(text, "proto")) {
			m_data->type_decl += text.Value();
		} else if (parent_tag_stack_test(text, "name", "proto")) {
			const char *command_name = text.Value();
			if (is_command_in_namespace(&command_name)) {
				m_data->name = command_name;
				return true;
			} else {
				delete m_data;
				m_data = NULL;
				return false;
			}
		} else if (parent_tag_stack_test(text, "ptype", "proto")) {
			m_data->type = text.Value();
			m_data->type_decl += text.Value();
		}
		return true;
	}

	virtual bool visit_enter(const XMLElement &elem, const XMLAttribute *attrib)
	{
		if (!m_data)
			return false;

		if (tag_test(elem, "proto")) {
			return true;
		} else if (tag_stack_test(elem, "ptype", "proto")) {
			return true;
		} else if (tag_stack_test(elem, "name", "proto")) {
			return true;
		} else if (tag_test(elem, "param")) {
			m_data->params.push_back(command::param());
			return true;
		} else if (tag_stack_test(elem, "name", "param")) {
			return true;
		} else if (tag_stack_test(elem, "ptype", "param")) {
			return true;
		} else {
			return false;
		}
	}
public:
	command_visitor(const XMLElement &tag) :
		data_builder_visitor<command>(tag)
	{
	}
};

class enumeration_visitor : public data_builder_visitor<enumeration>
{
	virtual bool visit_begin(const XMLElement &elem, const XMLAttribute *attrib)
	{
		const char *group_c = elem.Attribute("group");
		if (group_c)
			m_data->name = group_c;
		return true;
	}

	virtual bool visit_enter(const XMLElement &elem, const XMLAttribute *attrib)
	{
		if (tag_test(elem, "enum")) {
			if (elem.Attribute("api") && strcmp(elem.Attribute("api"), g_api_name))
				return false;
			unsigned int val = 0xffffffff;
			const char *enumeration_name = elem.Attribute("name");
			if (is_enum_in_namespace(&enumeration_name)) {
				int ret = sscanf(elem.Attribute("value"), "0x%x", &val);
				if (ret != 1)
					ret = sscanf(elem.Attribute("value"), "%d", &val);
				if (ret == 1) {
					m_data->enum_map[enumeration_name] = val;
					g_enum_map[enumeration_name] = val;
				} else {
					g_enum_str_map[enumeration_name] = strdup(elem.Attribute("value"));
				}
			}
		}
		return false;
	}
public:
	enumeration_visitor(const XMLElement &tag) :
		data_builder_visitor<enumeration>(tag)
	{
	}
};

class interface_visitor : public XMLVisitor
{
	const XMLElement &m_root;
	interface *m_interface;
	bool VisitEnter(const XMLElement &elem, const XMLAttribute *attrib)
	{
		if (&elem == &m_root) {
			return true;
		} else if (tag_test(elem, "require") && elem.Parent() == &m_root) {
			return g_api != API_GL || !elem.Attribute("profile") || !strcmp(elem.Attribute("profile"), "core");
		} else if (tag_test(elem, "remove") && elem.Parent() == &m_root) {
			return g_api != API_GL || !elem.Attribute("profile") || !strcmp(elem.Attribute("profile"), "core");
		} else if (tag_stack_test(elem, "enum", "require")) {
			const char *enumeration_name = elem.Attribute("name");
			if (is_enum_in_namespace(&enumeration_name)) {
				m_interface->enums.insert(enumeration_name);
				return true;
			} else {
				return false;
			}
		} else if (tag_stack_test(elem, "enum", "remove")) {
			const char *enumeration_name = elem.Attribute("name");
			if (is_enum_in_namespace(&enumeration_name)) {
				m_interface->removed_enums.insert(enumeration_name);
				return true;
			} else {
				return false;
			}
		} else if (tag_stack_test(elem, "command", "require")) {
			const char *command_name = elem.Attribute("name");
			if (is_command_in_namespace(&command_name)) {
				m_interface->commands[command_name] = g_commands[command_name];
				return true;
			} else {
				return false;
			}
		} else if (tag_stack_test(elem, "command", "remove")) {
			const char *command_name = elem.Attribute("name");
			if (is_command_in_namespace(&command_name)) {
				m_interface->removed_commands[command_name] = g_commands[command_name];
				return true;
			} else {
				return false;
			}
		}
		return false;
	}
public:
	interface_visitor(const XMLElement &root, interface *interface) :
		m_root(root), m_interface(interface) {}
};

class type_visitor :  public XMLVisitor
{
	std::string m_type_decl;
	const char *m_type_name;

	bool Visit(const XMLText &text)
	{
		if (parent_tag_test(text, "type")) {
			m_type_decl += text.Value();
		} else if (parent_tag_stack_test(text, "name", "type")) {
			m_type_decl += text.Value();
			m_type_name = text.Value();
		}
		return true;
	}

	bool VisitEnter(const XMLElement &elem, const XMLAttribute *attrib)
	{
		if (tag_stack_test(elem, "type", "types")) {
			return !elem.Attribute("api") || (elem.Attribute("api") == g_api_name);
		} else if (tag_stack_test(elem, "name", "type")) {
			return true;
		} else {
			return false;
		}
	}

	bool VisitExit(const XMLElement &elem)
	{
		if (m_type_name != NULL) {
			if (tag_test(elem, "type")) {
				if (!g_common_gl_typedefs.count(m_type_name)) {
					g_common_gl_typedefs.insert(m_type_name);
					g_types.push_back(m_type_decl);
				}
			}
		}
		return true;
	}
public:
	type_visitor() : m_type_name(NULL) {}
};

class khronos_registry_visitor : public XMLVisitor
{
	XMLDocument &m_doc;

	bool VisitEnter(const XMLElement &elem, const XMLAttribute *attrib)
	{
		if (tag_test(elem, "registry") && elem.Parent() == &m_doc) {
			return true;
		} else if (tag_stack_test(elem, "commands", "registry")) {
			return true;
		} else if (tag_stack_test(elem, "extensions", "registry")) {
			return true;
		} else if (tag_stack_test(elem, "types", "registry")) {
			return true;
		} else if (tag_stack_test(elem, "enums", "registry")) {
			enumeration_visitor e(elem);
			enumeration *enumeration = e.build();
			if (enumeration) {
				g_enumerations.push_back(enumeration);
			}
			return false;
		} else if (tag_stack_test(elem, "feature", "registry")) {
			const char *supported = elem.Attribute("api");
			if (!strcmp(supported, g_api_name)) {
				interface *feature = new interface();
				float version = elem.FloatAttribute("number");
				g_feature_interfaces[(int)roundf(version*10)] = feature;
				interface_visitor i_visitor(elem, feature);
				elem.Accept(&i_visitor);
			}
			return false;
		} else if (tag_stack_test(elem, "extension", "extensions")) {
			const char *api_variant_name = g_variant_name;
			const char *supported = elem.Attribute("supported");
			char *supported_copy = strdup(supported);
			char *token = strtok(supported_copy, "|");
			const char *name = elem.Attribute("name") + strlen(g_enumeration_prefix);

			//We can't support many SGI extensions due to missing types
			if (g_api == API_GLX && (strstr(name, "SGI") == name) && !strstr(name,"swap_control")) {
				return false;
			}

			//No need to support android and it breaks due to missing types
			if (g_api == API_EGL && strstr(name, "ANDROID")) {
				return false;
			}

			//Check if this extension is supported by the target API
			while (token) {
				if (!strcmp(token, g_variant_name)) {
					break;
				}
				token = strtok(NULL, "|");
			}
			if (token != NULL) {
				interface *feature = new interface();
				g_extension_interfaces[name] = feature;
				interface_visitor i_visitor(elem, feature);
				elem.Accept(&i_visitor);
			}
			return false;
		} else if (tag_stack_test(elem, "command", "commands")) {
			command_visitor c(elem);
			command * command = c.build();
			if (command) {
				while(command->type_decl.size() > 0 && command->type_decl[command->type_decl.size() - 1] == ' ')
					command->type_decl.resize(command->type_decl.size() - 1);
				g_commands[command->name] = command;
			}
			return false;
		} else if (tag_stack_test(elem, "type", "types")) {
			type_visitor t;
			elem.Accept(&t);
			return false;
		} else {
			return false;
		}
	}
public:
	khronos_registry_visitor(XMLDocument &doc) : m_doc(doc) { }
};

void print_interface_declaration(struct interface *iface, FILE *header_file)
{
	const char *enumeration_prefix = g_enumeration_prefix;

	FOREACH (val, iface->removed_enums, enums_type)
		fprintf(header_file, "#undef %s%s\n", enumeration_prefix, *val);

	FOREACH (val, iface->enums, enums_type) {
		enum_map_type::iterator iter = g_enum_map.find(*val);
		fprintf(header_file, "#undef %s%s\n", enumeration_prefix, *val);
		if (iter != g_enum_map.end()) {
			fprintf(header_file, "#define %s%s 0x%x\n",
					enumeration_prefix, *val,
					iter->second);
		} else {
			fprintf(header_file, "#define %s%s %s\n",
					enumeration_prefix, *val,
					g_enum_str_map[*val]);
		}
	}

	if (iface->enums.size())
		indent_fprintf(header_file, "\n");
	FOREACH (iter, iface->removed_commands, commands_type) {
		command *command = iter->second;
		fprintf(header_file, "#undef %s%s\n",
				g_command_prefix, command->name);
	}
	FOREACH (iter, iface->commands, commands_type) {
		command *command = iter->second;
		fprintf(header_file, "#undef %s%s\n", g_command_prefix, command->name);
		fprintf(header_file, "#define %s%s _%s_%s%s\n",
				g_command_prefix, command->name,
				g_prefix, g_command_prefix, command->name);
		command->print_declare(header_file, g_command_prefix);
	}
}

void print_interface_definition(struct interface *iface, FILE *source_file)
{
	indent_fprintf(source_file, "\n");
	FOREACH (iter, iface->commands, commands_type)
		iter->second->print_initialize(source_file, g_command_prefix);
}

void interface_append(struct interface *iface, const interface &other)
{
	iface->enums.insert(other.enums.begin(), other.enums.end());
	FOREACH_CONST (e, other.removed_enums, enums_type)
		iface->enums.erase(*e);
	iface->commands.insert(other.commands.begin(), other.commands.end());
	FOREACH_CONST (iter, other.removed_commands, commands_type)
		iface->commands.erase(iter->first);
}

void print_interface_load_check(struct interface *iface, FILE *source_file)
{
	if (!iface->commands.size()) {
		fprintf(source_file, "true");
	} else {
		const char *command_prefix = g_command_prefix;
		int i = 0;
		FOREACH (iter,  iface->commands, commands_type) {
			if ((i % 3) == 2) {
				fprintf(source_file, "\n");
				indent_fprintf(source_file, "");
			}
			if (i)
				fprintf(source_file, " && ");
			fprintf(source_file, "%s%s", command_prefix, iter->first);
			i++;
		}
	}
}

void bindify(const char *header_name, int min_version, FILE *header_file , FILE *source_file)
{
	interface full_interface;
	interface base_interface;
	int max_version = min_version;

	bool is_gl_api = g_api == API_GL;
	FOREACH (iter, g_feature_interfaces, feature_interfaces_type) {
		if (iter->first <= min_version)
			interface_append(&base_interface, *(iter->second));
		max_version = iter->first > max_version ? iter->first : max_version;
		interface_append(&full_interface,*(iter->second));
	}
	FOREACH (iter, g_extension_interfaces, extension_interfaces_type) {
		interface_append(&full_interface, *(iter->second));
	}

	fprintf(header_file, "#ifndef GL_BINDIFY_%s_H\n", g_api_name);
	fprintf(header_file, "#define GL_BINDIFY_%s_H\n", g_api_name);

	fprintf(header_file, "#ifdef __cplusplus\n");
	fprintf(header_file, "extern \"C\" {\n");
	fprintf(header_file, "#endif\n");

	switch (g_api) {
	case API_GLX:
		fprintf(header_file, "#include <X11/Xlib.h>\n");
		fprintf(header_file, "#include <X11/Xutil.h>\n");
		break;
	case API_WGL:
		fprintf(header_file, "#include <windows.h>\n");
		break;
	}
	fprintf(header_file, "#include <stdint.h>\n");
	fprintf(header_file, "#include <stddef.h>\n");
	fprintf(header_file, "#include <string.h>\n");
	fprintf(header_file, "#include <stdbool.h>\n");

	//
	//We need to include these typedefs even for glx and wgl since they are referenced there without being defined
	//
	indent_fprintf(header_file, "#ifndef GLBINDIFY_COMMON_GL_TYPEDEFS\n");
	indent_fprintf(header_file, "#define GLBINDIFY_COMMON_GL_TYPEDEFS\n");
	indent_fprintf(header_file, "typedef unsigned int GLenum;\n");
	indent_fprintf(header_file, "typedef unsigned char GLboolean;\n");
	indent_fprintf(header_file, "typedef unsigned int GLbitfield;\n");
	indent_fprintf(header_file, "typedef signed char GLbyte;\n");
	indent_fprintf(header_file, "typedef short GLshort;\n");
	indent_fprintf(header_file, "typedef int GLint;\n");
	indent_fprintf(header_file, "typedef unsigned char GLubyte;\n");
	indent_fprintf(header_file, "typedef unsigned short GLushort;\n");
	indent_fprintf(header_file, "typedef unsigned int GLuint;\n");
	indent_fprintf(header_file, "typedef int GLsizei;\n");
	indent_fprintf(header_file, "typedef float GLfloat;\n");
	indent_fprintf(header_file, "typedef double GLdouble;\n");
	indent_fprintf(header_file, "typedef ptrdiff_t GLintptr;\n");
	indent_fprintf(header_file, "typedef ptrdiff_t GLsizeiptr;\n");

	indent_fprintf(header_file, "#endif\n");
	indent_fprintf(header_file, "#ifndef %s_%sVERSION\n", g_macro_prefix, g_enumeration_prefix);
	indent_fprintf(header_file, "#define %s_%sVERSION %d\n", g_macro_prefix, g_enumeration_prefix, min_version);
	indent_fprintf(header_file, "#endif\n");

	if (g_api == API_EGL) {
		indent_fprintf(header_file, "#include <eglplatform.h>\n");
		indent_fprintf(header_file, "#include <khrplatform.h>\n");
	}

	FOREACH(val, g_types, types_type)
		indent_fprintf(header_file, "%s\n", val->c_str());

	print_interface_declaration(&base_interface, header_file);
	FOREACH (iter, g_feature_interfaces, feature_interfaces_type) {
		if (iter->first > min_version) {
			indent_fprintf(header_file, "\n");
			indent_fprintf(header_file, "#if defined(%s_%sVERSION) && %s_%sVERSION >= %d\n",
					g_macro_prefix,
					g_enumeration_prefix,
					g_macro_prefix,
					g_enumeration_prefix,
					iter->first);
			print_interface_declaration(iter->second, header_file);
			indent_fprintf(header_file, "#endif\n");
		}
	}

	indent_fprintf(header_file, "\n");
	FOREACH (iter, g_extension_interfaces, extension_interfaces_type) {
		indent_fprintf(header_file, "\n");
		indent_fprintf(header_file, "#if defined(%s_ENABLE_%s%s)\n", g_macro_prefix, g_enumeration_prefix, iter->first);
		indent_fprintf(header_file, "extern bool %s_%s%s;\n", g_macro_prefix, g_enumeration_prefix, iter->first);
		print_interface_declaration(iter->second, header_file);
		indent_fprintf(header_file, "#endif\n");
	}

	indent_fprintf(header_file, "\n");
	indent_fprintf(header_file, "bool %s_%s_init(int maj, int min);\n",  g_prefix, g_variant_name);

	indent_fprintf(header_file, "\n");
	fprintf(header_file, "#ifdef __cplusplus\n");
	fprintf(header_file, "}\n"); //extern "C" {
	fprintf(header_file, "#endif\n");

	fprintf(header_file, "#endif\n");

	reset_indent();

	fprintf(source_file, "#ifndef _WIN32\n");

	if (g_api != API_EGL && g_api != API_GLX)
		fprintf(source_file, "#ifdef %s_USE_EGL\n", g_macro_prefix);
	if (g_api != API_GLX) {
		fprintf(source_file, "extern void (*eglGetProcAddress(const unsigned char *))(void);\n");
		fprintf(source_file, "static inline void *LoadProcAddress(const char *name) { return eglGetProcAddress((const unsigned char *)name); }\n");
	}
	if (g_api != API_EGL && g_api != API_GLX)
		fprintf(source_file, "#else\n");
	if (g_api != API_EGL) {
		fprintf(source_file, "extern void (*glXGetProcAddress(const unsigned char *))(void);\n");
		fprintf(source_file, "static inline void *LoadProcAddress(const char *name) { return glXGetProcAddress((const unsigned char *)name); }\n");
	}
	if (g_api != API_EGL && g_api != API_GLX)
		fprintf(source_file, "#endif\n");
	fprintf(source_file, "#include <stdio.h>\n");
	fprintf(source_file, "#else\n");
	fprintf(source_file, "#include <windows.h>\n");
	fprintf(source_file, "#include <wingdi.h>\n");
	fprintf(source_file, "#include <stdio.h>\n");
	fprintf(source_file, "static PROC LoadProcAddress(const char *name) {\n");
	fprintf(source_file, "\tPROC addr = wglGetProcAddress((LPCSTR)name);\n");
	fprintf(source_file, "\tif (addr) return addr;\n");
	fprintf(source_file, "\telse return (PROC)GetProcAddress(GetModuleHandleA(\"OpenGL32.dll\"), (LPCSTR)name);\n");
	fprintf(source_file, "}\n");
	fprintf(source_file, "#endif\n");
	fprintf(source_file, "#define %s_%sVERSION %d\n", g_macro_prefix, g_enumeration_prefix, max_version);

	FOREACH (iter, g_extension_interfaces, extension_interfaces_type) {
		indent_fprintf(source_file, "#undef %s_ENABLE_%s%s\n", g_macro_prefix, g_enumeration_prefix, iter->first);
		indent_fprintf(source_file, "#define %s_ENABLE_%s%s\n", g_macro_prefix, g_enumeration_prefix, iter->first);
	}

	fprintf(source_file, "#include \"%s\"\n", header_name);

	print_interface_definition(&full_interface, source_file);

	indent_fprintf(source_file, "\n");
	FOREACH (iter, g_extension_interfaces, extension_interfaces_type) {
		indent_fprintf(source_file, "bool %s_%s%s = %s;\n",
				g_macro_prefix,
				g_enumeration_prefix,
				iter->first,
				is_gl_api ? "false" : "true");
	}

#if USE_GPERF
	if (is_gl_api) {
		//
		// Have gperf make a hash table for extension names. We can write the output
		// directly to source file
		//
		fflush(source_file);
		int fdpair[2];
		int rc = pipe(fdpair);
		if (rc) {
			fprintf(stderr, "pipe() failed: %s. Aborting...\n", strerror(errno));
		}
		pid_t child_pid = fork();
		if (child_pid) {
			int status;
			pid_t pid;
			close(fdpair[0]);
			FILE *gperf_in = fdopen(fdpair[1], "w");
			fprintf(gperf_in, "%%struct-type\n");
			fprintf(gperf_in, "%%define lookup-function-name %s_find_extension\n", g_prefix);
			fprintf(gperf_in, "%%define initializer-suffix ,NULL\n");
			fprintf(gperf_in, "struct extension_match { const char *name; bool *support_flag; };\n");
			fprintf(gperf_in, "%%%%\n");
			FOREACH (iter, g_extension_interfaces, extension_interfaces_type) {
				fprintf(gperf_in, "%s%s, &%s_%s%s\n", g_enumeration_prefix, iter->first,
					g_macro_prefix, g_enumeration_prefix, iter->first);
			}
			fflush(gperf_in);
			close(fdpair[1]);
			pid = waitpid(child_pid, &status, 0);
			if (pid != child_pid || WEXITSTATUS(status)) {
				fprintf(stderr, "Error encountered while running gperf\n");
				exit(-1);
			}
		} else {
			close(fdpair[1]);
			dup2(fdpair[0], STDIN_FILENO);
			dup2(fileno(source_file), STDOUT_FILENO);
			execlp("gperf", "gperf", 0);
		}
	}
#endif

	indent_fprintf(source_file, "\n");
	indent_fprintf(source_file, "bool %s_%s_init(int maj, int min)\n", g_prefix, g_variant_name);
	indent_fprintf(source_file, "{\n");
	increase_indent();
	indent_fprintf(source_file, "int req_version = maj * 10 + min;\n");
	if (is_gl_api) {
		indent_fprintf(source_file, "int actual_maj, actual_min, actual_version, i;\n");
		indent_fprintf(source_file, "int num_extensions;\n");
	}
	indent_fprintf(source_file, "if (req_version < %d) return false;\n", min_version);
	indent_fprintf(source_file, "if (req_version > %d) return false;\n", max_version);

	FOREACH (iter, full_interface.commands, commands_type)
		(iter->second)->print_load(source_file, g_command_prefix);

	if (is_gl_api) {
		indent_fprintf(source_file, "\n");
		indent_fprintf(source_file, "if (!glGetIntegerv || !glGetStringi) return false;\n");
		indent_fprintf(source_file, "glGetIntegerv(GL_NUM_EXTENSIONS, &num_extensions);\n");
		indent_fprintf(source_file, "glGetIntegerv(GL_MAJOR_VERSION, &actual_maj);\n");
		indent_fprintf(source_file, "glGetIntegerv(GL_MINOR_VERSION, &actual_min);\n");
		indent_fprintf(source_file, "actual_version = actual_maj * 10 + actual_min;\n");
		indent_fprintf(source_file, "if (actual_version < req_version) return false;\n");
		indent_fprintf(source_file, "for (i = 0; i < num_extensions; i++) {\n");
		indent_fprintf(source_file, "\tconst char *extname = (const char *)glGetStringi(GL_EXTENSIONS, i);\n");
#if USE_GPERF
		indent_fprintf(source_file, "\tstruct extension_match *match = %s_find_extension(extname, strlen(extname));\n", g_prefix);
		indent_fprintf(source_file, "\tif (match)\n");
		indent_fprintf(source_file, "\t\t*match->support_flag = true;\n");
#else
		FOREACH (iter, g_extension_interfaces, extension_interfaces_type) {
			indent_fprintf(source_file, "\tif (!strcmp(extname, \"%s%s\")) {\n", g_enumeration_prefix, iter->first);
			indent_fprintf(source_file, "\t\t%s_%s%s = true;\n", g_macro_prefix, g_enumeration_prefix, iter->first);
			indent_fprintf(source_file, "\t\tcontinue;\n");
			indent_fprintf(source_file, "\t}\n");
		}
#endif
		indent_fprintf(source_file, "}\n");
	}

	FOREACH (iter, g_extension_interfaces, extension_interfaces_type) {
		if (iter->second->commands.size()) {
			indent_fprintf(source_file, "\n");
			indent_fprintf(source_file, "%s_%s%s = %s_%s%s && ",
					g_macro_prefix, g_enumeration_prefix, iter->first,
					g_macro_prefix, g_enumeration_prefix, iter->first);
			increase_indent();
			print_interface_load_check(iter->second, source_file);
			decrease_indent();
			fprintf(source_file, ";\n");
		}
	}

	indent_fprintf(source_file, "\n");
	indent_fprintf(source_file, "return ");
	print_interface_load_check(&base_interface, source_file);

	FOREACH(iter, g_feature_interfaces, feature_interfaces_type) {
		if (iter->first <= min_version || !iter->second->commands.size())
			continue;
		fprintf(source_file, "\n");
		indent_fprintf(source_file, " && ((req_version < %d) ||\n", iter->first);
		increase_indent();
		indent_fprintf(source_file, "(");
		print_interface_load_check(iter->second, source_file);
		fprintf(source_file, "))");
		decrease_indent();
	}
	fprintf(source_file, ";\n");
	decrease_indent();
	indent_fprintf(source_file, "}\n"); //init()
}

static void print_help(const char *program_name)
{
	printf("Usage: %s [OPTION]...\n", program_name);
	printf("\n"
	       "Options:\n"
	       "  -a,--api <api>                Generate bindings for API <api>. Must be one\n"
	       "                                of 'gl', 'wgl', 'egl', 'gles2', or 'glx'. Default is 'gl'\n"
	       "  -n,--namespace <Namespace>    Namespace for generated bindings. This is the first\n"
	       "                                part of the name of every function and macro.\n"
	       "  -s,--srcdir <dir>             Directory to find XML sources\n"
	       "  -v,--version                  Print version information\n"
	       "  -h,--help                     Display this page\n");
}

int main(int argc, char **argv)
{
	XMLDocument doc;
	XMLError err;

	g_common_gl_typedefs.insert("GLenum");
	g_common_gl_typedefs.insert("GLboolean");
	g_common_gl_typedefs.insert("GLbitfield");
	g_common_gl_typedefs.insert("GLbyte");
	g_common_gl_typedefs.insert("GLshort");
	g_common_gl_typedefs.insert("GLint");
	g_common_gl_typedefs.insert("GLubyte");
	g_common_gl_typedefs.insert("GLushort");
	g_common_gl_typedefs.insert("GLuint");
	g_common_gl_typedefs.insert("GLsizei");
	g_common_gl_typedefs.insert("GLfloat");
	g_common_gl_typedefs.insert("GLdouble");
	g_common_gl_typedefs.insert("GLintptr");
	g_common_gl_typedefs.insert("GLsizeiptr");

	static struct option options [] = {
		{"api"       , 1, 0, 'a' },
		{"srcdir"    , 1, 0, 's' },
		{"version"   , 1, 0, 'v' },
		{"namespace" , 0, 0, 'n' },
		{"help"      , 0, 0, 'h' }
	};

	g_api_name = "gl";
	const char *srcdir = NULL;

	const char *prefix = "glb";
	char *macro_prefix;

	while (1) {
		int option_index;
		int c = getopt_long(argc, argv, "a:s:n:v", options, &option_index);
		if (c == -1) {
			break;
		}
		switch (c) {
		case '?':
		case ':':
			print_help(argv[0]);
			exit(-1);
			break;
		case 'v':
			printf("glbindify version %s\n", PACKAGE_VERSION);
			exit(0);
			break;
		case 'a':
			g_api_name = optarg;
			break;
		case 's':
			srcdir = optarg;
			break;
		case 'n':
			prefix = optarg;
			break;
		case 'h':
			print_help(argv[0]);
			exit(0);
			break;
		}
	}

	macro_prefix = strdup(prefix);
	int i;
	for (i = 0; macro_prefix[i]; i++) {
		macro_prefix[i] = toupper(macro_prefix[i]);
	}
	g_prefix = prefix;
	g_macro_prefix = macro_prefix;

	printf("Generating bindings for %s with namespace '%s'\n", g_api_name, g_prefix);

	const char *xml_name = NULL;

	if (!strcmp(g_api_name, "wgl")) {
		g_api = API_WGL;
		g_command_prefix = "wgl";
		g_enumeration_prefix = "WGL_";
		g_api_print_name = "WGL";
		g_variant_name = g_api_name;
		xml_name = "wgl.xml";
	} else if (!strcmp(g_api_name, "glx")) {
		g_api = API_GLX;
		g_command_prefix = "glX";
		g_enumeration_prefix = "GLX_";
		g_variant_name = g_api_name;
		xml_name = "glx.xml";
		g_api_print_name = "glX";
	} else if (!strcmp(g_api_name, "gl")) {
		g_api = API_GL;
		g_command_prefix = "gl";
		g_enumeration_prefix = "GL_";
		g_api_print_name = "OpenGL";
		g_variant_name = "glcore";
		xml_name = "gl.xml";
	} else if (!strcmp(g_api_name, "egl")) {
		g_api = API_EGL;
		g_command_prefix = "egl";
		g_enumeration_prefix = "EGL_";
		g_api_print_name = "EGL";
		g_variant_name = g_api_name;
		xml_name = "egl.xml";
	} else if (!strcmp(g_api_name, "gles2")) {
		g_api = API_GLES2;
		g_command_prefix = "gl";
		g_enumeration_prefix = "GL_";
		g_api_print_name = "GLES2";
		g_variant_name = g_api_name;
		xml_name = "gl.xml";
	} else {
		fprintf(stderr, "Unrecognized API '%s'\n", g_api_name);
		print_help(argv[0]);
		exit(-1);
	}

	char in_filename[200];
#ifdef PKGDATADIR
	if (!srcdir) {
		srcdir = PKGDATADIR;
	}
	snprintf(in_filename, sizeof(in_filename), "%s/%s", srcdir, xml_name);
#else
	if (!srcdir) {
		srcdir = ".";
	}
	snprintf(in_filename, sizeof(in_filename), "%s/%s", srcdir, xml_name);
#endif
	err = doc.LoadFile(in_filename);
	if (err != XML_NO_ERROR) {
		fprintf(stderr, "Error loading khronos registry file %s\n", in_filename);
		exit(-1);
	}

	char header_name[100];
	char c_name[100];
	snprintf(header_name, sizeof(header_name), "%s.h", g_variant_name);
	snprintf(c_name, sizeof(c_name), "%s.c", g_variant_name);

	FILE *header_file = fopen(header_name, "w+");
	if (!header_file) {
		fprintf(stderr, "Error creating header file '%s': %s\n", header_name, strerror(errno));
		exit(-1);
	}

	FILE *source_file = fopen(c_name, "w+");
	if (!source_file) {
		fprintf(stderr, "Error creating source file '%s': %s\n", c_name, strerror(errno));
		exit(-1);
	}

	printf("Writing bindings to %s and %s\n", c_name, header_name);

	khronos_registry_visitor registry_visitor(doc);
	doc.Accept(&registry_visitor);

	fprintf(source_file, "/* C %s bindings generated by %s */\n", g_api_print_name, PACKAGE_STRING);
	fprintf(header_file, "/* C %s bindings generated by %s */\n", g_api_print_name, PACKAGE_STRING);
	fprintf(source_file, "/* Command line: ");
	fprintf(header_file, "/* Command line: ");

	for (i = 0; i < argc; i++) {
		fprintf(source_file, "%s ", argv[i]);
		fprintf(header_file, "%s ", argv[i]);
	}
	fprintf(source_file, "*/\n\n");
	fprintf(header_file, "*/\n\n");

	int min_ver = 10;
	switch (g_api) {
	case API_GL:
		min_ver = 32;
		break;
	case API_GLX:
		min_ver = 14;
		break;
	case API_GLES2:
		min_ver = 20;
		break;
	}
	bindify(header_name, min_ver, header_file, source_file);

	fclose(source_file);
	fclose(header_file);

	return 0;
}
