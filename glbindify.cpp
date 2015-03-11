/*

Copyright (c) 2014 Neils Nesse

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

*/

#include <stdint.h>
#include <getopt.h>

#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <set>
#include <memory>
#include <fstream>
#include <stdio.h>
#include <math.h>

#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>

#include "tinyxml2.h"

using namespace tinyxml2;

struct api *g_api = NULL;
std::string g_indent_string;

void increase_indent()
{
	g_indent_string.push_back('\t');
}

void decrease_indent()
{
	g_indent_string.pop_back();
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

std::set<const char *, cstring_compare> g_common_gl_typedefs = {
	"GLenum",
	"GLboolean",
	"GLbitfield",
	"GLbyte",
	"GLshort",
	"GLint",
	"GLubyte",
	"GLushort",
	"GLuint"
	"GLsizei",
	"GLfloat",
	"GLdouble",
	"GLintptr",
	"GLsizeiptr",
};

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
			for(int i = 1; i < params.size(); i++) {
				fprintf(out, ", %s", params[i].decl.c_str());
			}
		}
		fprintf(out, ");\n");
	}
	void print_initialize(FILE *out, const char *command_prefix) {
		indent_fprintf(out, "%s (*%s%s)(", type_decl.c_str(), command_prefix, name);
		if (params.size()) {
			fprintf(out, "%s", params[0].decl.c_str());
			for(int i = 1; i < params.size(); i++) {
				fprintf(out, ", %s", params[i].decl.c_str());
			}
		}
		fprintf(out, ") = NULL;\n");
	}

	void print_load(FILE *out, const char *command_prefix) {
		indent_fprintf(out, "%s%s = (%s (*)(", command_prefix, name, type_decl.c_str());
		if (params.size()) {
			fprintf(out, "%s", params[0].decl.c_str());
			for(int i = 1; i < params.size(); i++) {
				fprintf(out, ", %s", params[i].decl.c_str());
			}
		}
		fprintf(out, ") ) LoadProcAddress(\"%s%s\");\n", command_prefix, name);
	}

	command() : name(NULL), type(NULL) {}
};

struct interface {
	std::set<const char *, cstring_compare> enums;
	std::map<const char *, command *, cstring_compare> commands;
	std::set<const char *, cstring_compare> removed_enums;
	std::map<const char *, command *, cstring_compare> removed_commands;
	std::map<const char *, std::string, cstring_compare> types;
	void append(const interface &other);
	void include_type(const char *type);
	void resolve_types();
	void print_definition(FILE *header_file);
	void print_declaration(FILE *header_file);
	void print_load_check(FILE *source_file);
};

struct api {
	//Api description
	const char *m_name;
	const char *m_command_prefix;
	const char *m_mangle_prefix;
	const char *m_enumeration_prefix;

	std::set<const char *, cstring_compare> m_extensions;

	//List of all enums and commands
	std::map<const char *, unsigned int, cstring_compare> m_enum_map;
	std::vector<enumeration *> m_enumerations;
	std::map<const char *, command *, cstring_compare> m_commands;
	std::map<const char *, std::string, cstring_compare> m_types;

	std::map<int, interface *> m_feature_interfaces;
	std::map<const char *, interface *, cstring_compare> m_extension_interfaces;

	bool is_command_in_namespace(const char **name) {
		if (strstr(*name, m_command_prefix)) {
			*name = *name + strlen(m_command_prefix);
			return true;
		} else {
			return false;
		}
	}
	bool is_enum_in_namespace(const char **name) {
		if (strstr(*name, m_enumeration_prefix)) {
			*name = *name + strlen(m_enumeration_prefix);
			return true;
		} else {
			return false;
		}
	}

	const char *name() {
		return m_name;
	}

	api(const char *name,
			std::set<const char *, cstring_compare> &extensions) :
		m_name(name),
		m_extensions(extensions)
	{
		if (!strcmp(m_name,"wgl")) {
			m_command_prefix = "wgl";
			m_mangle_prefix = "_glb_wgl";
			m_enumeration_prefix = "WGL_";
		} else if (!strcmp(m_name,"glx")) {
			m_command_prefix = "glX";
			m_mangle_prefix = "_glb_glX";
			m_enumeration_prefix = "GLX_";
		} else if (!strcmp(m_name,"gl")) {
			m_command_prefix = "gl";
			m_mangle_prefix = "_glb_gl";
			m_enumeration_prefix = "GL_";
		}
	}
	void bindify(const char *header_name, int min_version, FILE *header_file, FILE *source_file);
};

template <class T>
class data_builder_visitor : public XMLVisitor
{
protected:
	T *m_data;
private:
	const XMLElement &m_root;

	virtual bool visit(const XMLText &text) { return true; }
	bool Visit(const XMLText &text) { visit(text); }

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
	api &m_api;
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
			if (m_api.is_command_in_namespace(&command_name)) {
				m_data->name = command_name;
				return true;
			} else {
				delete m_data;
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
	command_visitor(const XMLElement &tag, api &api) :
		data_builder_visitor<command>(tag),
		m_api(api)
	{
	}
};

class enumeration_visitor : public data_builder_visitor<enumeration>
{
	api &m_api;

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
			if (elem.Attribute("api") && strcmp(elem.Attribute("api"), m_api.m_name))
				return false;
			unsigned int val = 0xffffffff;
			int ret = sscanf(elem.Attribute("value"), "0x%x", &val);
			if (ret != 1)
				ret = sscanf(elem.Attribute("value"), "%d", &val);
			const char *enumeration_name = elem.Attribute("name");
			if (ret == 1) {
				if (m_api.is_enum_in_namespace(&enumeration_name)) {
					m_data->enum_map[enumeration_name] = val;
					m_api.m_enum_map[enumeration_name] = val;
				}
			} else {
				printf("warning: can't parse value of enum %s: \"%s\"\n", elem.Attribute("name"), elem.Attribute("value"));
			}
		}
		return false;
	}
public:
	enumeration_visitor(const XMLElement &tag, api &api) :
		data_builder_visitor<enumeration>(tag),
		m_api(api)
	{
	}
};

class interface_visitor : public XMLVisitor
{
	api &m_api;
	const XMLElement &m_root;
	interface *m_interface;
	bool VisitEnter(const XMLElement &elem, const XMLAttribute *attrib)
	{
		if (&elem == &m_root) {
			return true;
		} else if (tag_test(elem, "require") && elem.Parent() == &m_root) {
			return !elem.Attribute("profile") || !strcmp(elem.Attribute("profile"), "core");
		} else if (tag_test(elem, "remove") && elem.Parent() == &m_root) {
			return !elem.Attribute("profile") || !strcmp(elem.Attribute("profile"), "core");
		} else if (tag_stack_test(elem, "enum", "require")) {
			const char *enumeration_name = elem.Attribute("name");
			if (m_api.is_enum_in_namespace(&enumeration_name)) {
				m_interface->enums.insert(enumeration_name);
				return true;
			} else {
				return false;
			}
		} else if (tag_stack_test(elem, "enum", "remove")) {
			const char *enumeration_name = elem.Attribute("name");
			if (m_api.is_enum_in_namespace(&enumeration_name)) {
				m_interface->removed_enums.insert(enumeration_name);
				return true;
			} else {
				return false;
			}
		} else if (tag_stack_test(elem, "command", "require")) {
			const char *command_name = elem.Attribute("name");
			if (m_api.is_command_in_namespace(&command_name)) {
				m_interface->commands[command_name] = m_api.m_commands[command_name];
				return true;
			} else {
				return false;
			}
		} else if (tag_stack_test(elem, "command", "remove")) {
			const char *command_name = elem.Attribute("name");
			if (m_api.is_command_in_namespace(&command_name)) {
				m_interface->removed_commands[command_name] = m_api.m_commands[command_name];
				return true;
			} else {
				return false;
			}
		}
		return false;
	}
public:
	interface_visitor(api &api, const XMLElement &root, interface *interface) :
		m_api(api), m_root(root), m_interface(interface) {}
};

class type_visitor :  public XMLVisitor
{
	std::string m_type_decl;
	const char *m_type_name;
	api &m_api;

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
			return !elem.Attribute("api") || (elem.Attribute("api") == m_api.name());
		} else if (tag_stack_test(elem, "name", "type")) {
			return true;
		} else {
			return false;
		}
	}

	bool VisitExit(const XMLElement &elem)
	{
		if (tag_test(elem, "type") && m_type_name != NULL) {
			m_api.m_types[m_type_name] = m_type_decl;
		}
		return true;
	}
public:
	type_visitor(api &api) : m_api(api), m_type_name(NULL) {}
};

class khronos_registry_visitor : public XMLVisitor
{
	XMLDocument &m_doc;
	api &m_api;

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
			enumeration_visitor e(elem, m_api);
			enumeration *enumeration = e.build();
			if (enumeration) {
				m_api.m_enumerations.push_back(enumeration);
			}
			return false;
		} else if (tag_stack_test(elem, "feature", "registry")) {
			const char *supported = elem.Attribute("api");
			if (!strcmp(supported, m_api.m_name)) {
				interface *feature = new interface();
				float version = elem.FloatAttribute("number");
				m_api.m_feature_interfaces[(int)roundf(version*10)] = feature;
				interface_visitor i_visitor(m_api, elem, feature);
				elem.Accept(&i_visitor);
			}
			return false;
		} else if (tag_stack_test(elem, "extension", "extensions")) {
			const char *api_name = m_api.name();
			if (!strcmp(m_api.m_name, "gl")) {
				api_name = "glcore";
			}

			const char *supported = elem.Attribute("supported");
			char *supported_copy = strdup(supported);
			char *saveptr;
			char *token = strtok_r(supported_copy, "|", &saveptr);
			const char *name = elem.Attribute("name") + strlen(m_api.name()) + 1;

			//We can't support SGI extensions due to missing types
			if (!strcmp(m_api.m_name, "glx") && (strstr(name, "SGI") == name)) {
				return false;
			}

			//Check if this extension is supported by the target API
			while (token) {
				if (!strcmp(token, api_name)) {
					break;
				}
				token = strtok_r(NULL, "|", &saveptr);
			}
			if (token != NULL) {
				interface *feature = new interface();
				m_api.m_extension_interfaces[name] = feature;
				interface_visitor i_visitor(m_api, elem, feature);
				elem.Accept(&i_visitor);
			}
			return false;
		} else if (tag_stack_test(elem, "command", "commands")) {
			command_visitor c(elem, m_api);
			command * command = c.build();
			if (command) {
				while(command->type_decl.back() == ' ')
					command->type_decl.pop_back();
				m_api.m_commands[command->name] = command;
			}
			return false;
		} else if (tag_stack_test(elem, "type", "types")) {
			type_visitor t(m_api);
			elem.Accept(&t);
			return false;
		} else {
			return false;
		}
	}
public:
	khronos_registry_visitor(XMLDocument &doc, api &api) : m_doc(doc), m_api(api) { }
};

void interface::print_declaration(FILE *header_file)
{
	const char *enumeration_prefix = g_api->m_enumeration_prefix;

	for (auto val : types) {
		char *temp = strdup(val.first);
		char *cur;
		for (cur = temp; *cur; cur++)
			if (*cur == ' ')
				*cur = '_';
		indent_fprintf(header_file, "#ifndef GLB_TYPE_%s\n", temp);
		indent_fprintf(header_file, "#define GLB_TYPE_%s\n", temp);
		indent_fprintf(header_file, "%s\n", val.second.c_str());
		indent_fprintf(header_file, "#endif\n", enumeration_prefix, val.first);
		free(temp);
	}

	indent_fprintf(header_file, "\n");
	for (auto val : removed_enums) {
		fprintf(header_file, "#undef %s%s\n", enumeration_prefix, val);
	}
	for (auto val : enums) {
		fprintf(header_file, "#define %s%s 0x%x\n",
				enumeration_prefix, val,
				g_api->m_enum_map[val]);
	}
	indent_fprintf(header_file, "\n");
	for (auto iter : removed_commands) {
		auto command = iter.second;
		fprintf(header_file, "#undef %s%s\n",
				g_api->m_command_prefix, command->name);
	}
	for (auto iter : commands) {
		auto command = iter.second;
		fprintf(header_file, "#define %s%s %s%s\n",
				g_api->m_command_prefix, command->name,
				g_api->m_mangle_prefix, command->name);
		command->print_declare(header_file, g_api->m_command_prefix);
	}
}

void interface::print_definition(FILE *source_file)
{
	indent_fprintf(source_file, "\n");
	for (auto iter : commands)
		iter.second->print_initialize(source_file, g_api->m_command_prefix);
}

void interface::include_type(const char *type)
{
	if (type != NULL && !g_common_gl_typedefs.count(type) && !types.count(type)) {
		types[type] = g_api->m_types[type];
	}
}

void interface::resolve_types()
{
	for (auto iter: commands) {
		auto command = iter.second;
		include_type(command->type);
		for (auto param : command->params) {
			include_type(param.type);
		}
	}
}

void interface::append(const interface &other)
{
	enums.insert(other.enums.begin(), other.enums.end());
	for (auto e : other.removed_enums)
		enums.erase(e);
	commands.insert(other.commands.begin(), other.commands.end());
	for (auto iter : other.removed_commands)
		commands.erase(iter.first);
}

void interface::print_load_check(FILE *source_file)
{
	if (!commands.size()) {
		fprintf(source_file, "true");
	} else {
		const char *command_prefix = g_api->m_command_prefix;
		int i = 0;
		for (auto iter : commands) {
			if ((i % 3) == 2) {
				fprintf(source_file, "\n");
				indent_fprintf(source_file, "");
			}
			if (i)
				fprintf(source_file, " && ");
			fprintf(source_file, "%s%s", g_api->m_command_prefix, iter.first);
			i++;
		}
	}
}

void api::bindify(const char *header_name, int min_version, FILE *header_file , FILE *source_file)
{
	interface full_interface;
	interface core_3_2;
	int max_version = min_version;

	bool is_gl_api = !strcmp(m_name, "gl");

	for (auto iter : m_feature_interfaces) {
		if (iter.first > min_version)
			iter.second->resolve_types();
		else
			core_3_2.append(*(iter.second));
		max_version = iter.first > max_version ? iter.first : max_version;
		full_interface.append(*(iter.second));
	}
	for (auto iter : m_extension_interfaces) {
		iter.second->resolve_types();
		full_interface.append(*(iter.second));
	}
	core_3_2.resolve_types();

	fprintf(header_file, "#ifndef GL_BINDIFY_%s_H\n", m_name);
	fprintf(header_file, "#define GL_BINDIFY_%s_H\n", m_name);

	fprintf(header_file, "#ifdef __cplusplus\n");
	fprintf(header_file, "extern \"C\" {\n");
	fprintf(header_file, "#endif\n");

	if (!strcmp(m_name, "glx")) {
		fprintf(header_file, "#include <X11/Xlib.h>\n");
		fprintf(header_file, "#include <X11/Xutil.h>\n");
	} else if (!strcmp(m_name, "wgl")) {
		fprintf(header_file, "#include <windows.h>\n");
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
	indent_fprintf(header_file, "#ifndef GLB_%sVERSION\n", m_enumeration_prefix);
	indent_fprintf(header_file, "#define GLB_%sVERSION %d\n", m_enumeration_prefix, min_version);
	indent_fprintf(header_file, "#endif\n");

	core_3_2.print_declaration(header_file);
	for (auto iter : m_feature_interfaces) {
		if (iter.first > min_version) {
			indent_fprintf(header_file, "\n");
			indent_fprintf(header_file, "#if defined(GLB_%sVERSION) && GLB_%sVERSION >= %d\n",
					m_enumeration_prefix,
					m_enumeration_prefix,
					iter.first);
			indent_fprintf(header_file, "\n");
			iter.second->print_declaration(header_file);
			indent_fprintf(header_file, "#endif\n");
		}
	}

	indent_fprintf(header_file, "\n");
	for (auto iter : m_extension_interfaces) {
		indent_fprintf(header_file, "\n");
		indent_fprintf(header_file, "#if defined(GLB_ENABLE_%s%s)\n", m_enumeration_prefix, iter.first);
		indent_fprintf(header_file, "extern bool GLB_%s%s;\n", m_enumeration_prefix, iter.first);
		iter.second->print_declaration(header_file);
		indent_fprintf(header_file, "#endif\n");
	}

	indent_fprintf(header_file, "\n");
	indent_fprintf(header_file, "bool init_%s(int maj, int min);\n", m_name);

	indent_fprintf(header_file, "\n");
	fprintf(header_file, "#ifdef __cplusplus\n");
	fprintf(header_file, "}\n"); //extern "C" {
	fprintf(header_file, "#endif\n");

	fprintf(header_file, "#endif\n");

	reset_indent();

	fprintf(source_file, "#ifndef _WIN32\n");
	fprintf(source_file, "extern void (*glXGetProcAddress(const unsigned char *))(void);\n");
	fprintf(source_file, "static inline void (*LoadProcAddress(const char *name))(void) {\n");
	fprintf(source_file, "\treturn (*glXGetProcAddress)((const unsigned char *)name);\n");
	fprintf(source_file, "}\n");
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
	fprintf(source_file, "#define GLB_%sVERSION %d\n", m_enumeration_prefix, max_version);

	for (auto iter : m_extension_interfaces) {
		indent_fprintf(source_file, "#define GLB_ENABLE_%s%s\n", m_enumeration_prefix, iter.first);
	}

	fprintf(source_file, "#include \"%s\"\n", header_name);

	full_interface.print_definition(source_file);

	indent_fprintf(source_file, "\n");
	for (auto iter : m_extension_interfaces) {
		indent_fprintf(source_file, "bool GLB_%s%s = false;\n", m_enumeration_prefix, iter.first);
	}

	if (is_gl_api) {
		//
		// Have gperf make a hash table for extension names. We can write the output
		// directly to source file
		//
		fflush(source_file);
		int fdpair[2];
		pipe(fdpair);
		pid_t child_pid = fork();
		if(child_pid) {
			close(fdpair[0]);
			FILE *gperf_in = fdopen(fdpair[1], "w");
			fprintf(gperf_in, "struct extension_match { const char *name; bool *support_flag; };\n");
			fprintf(gperf_in, "\%\%\%\%\n");
			for (auto iter : m_extension_interfaces) {
				fprintf(gperf_in, "%s%s, &GLB_%s%s\n", m_enumeration_prefix, iter.first,
					m_enumeration_prefix, iter.first);
			}
			fflush(gperf_in);
			close(fdpair[1]);
			waitpid(child_pid, NULL, 0);
		} else {
			close(fdpair[1]);
			dup2(fdpair[0], STDIN_FILENO);
			dup2(fileno(source_file), STDOUT_FILENO);
			execlp("gperf", "gperf", "-t", 0);
		}
	}

	indent_fprintf(source_file, "\n");
	indent_fprintf(source_file, "bool init_%s(int maj, int min)\n", m_name);
	indent_fprintf(source_file, "{\n");
	increase_indent();
	indent_fprintf(source_file, "int req_version = maj * 10 + min;\n");
	if (is_gl_api) {
		indent_fprintf(source_file, "int actual_maj, actual_min, actual_version, i;\n");
		indent_fprintf(source_file, "int num_extensions;\n");
	}
	indent_fprintf(source_file, "if (req_version < %d) return false;\n", min_version);
	indent_fprintf(source_file, "if (req_version > %d) return false;\n", max_version);

	for (auto iter : full_interface.commands)
		(iter.second)->print_load(source_file, m_command_prefix);

	if (is_gl_api) {
		indent_fprintf(source_file, "\n");
		indent_fprintf(source_file, "if (!glGetIntegerv || !glGetStringi) return false;\n");
		indent_fprintf(source_file, "glGetIntegerv(GL_NUM_EXTENSIONS, &num_extensions);\n");
		indent_fprintf(source_file, "glGetIntegerv(GL_MAJOR_VERSION, &actual_maj);\n");
		indent_fprintf(source_file, "glGetIntegerv(GL_MINOR_VERSION, &actual_min);\n");
		indent_fprintf(source_file, "actual_version = actual_maj * 10 + actual_min;\n");
		indent_fprintf(source_file, "if (actual_version < req_version) return false;\n");
		indent_fprintf(source_file, "for (i = 0; i < num_extensions; i++) {\n");
		indent_fprintf(source_file, "\tconst char *extname = glGetStringi(GL_EXTENSIONS, i);\n");
		indent_fprintf(source_file, "\tstruct extension_match *match = in_word_set(extname, strlen(extname));\n");
		indent_fprintf(source_file, "\tif (match)\n");
		indent_fprintf(source_file, "\t\t*match->support_flag = true;\n");
		indent_fprintf(source_file, "}\n");
	}

	for (auto iter : m_extension_interfaces) {
		if (iter.second->commands.size()) {
			indent_fprintf(source_file, "\n");
			indent_fprintf(source_file, "GLB_%s%s = GLB_%s%s && ",
					m_enumeration_prefix, iter.first,
					m_enumeration_prefix, iter.first);
			increase_indent();
			iter.second->print_load_check(source_file);
			decrease_indent();
			fprintf(source_file, ";\n");
		}
	}

	indent_fprintf(source_file, "\n");
	indent_fprintf(source_file, "return ");
	core_3_2.print_load_check(source_file);

	for (auto iter : m_feature_interfaces) {
		if (iter.first <= min_version || !iter.second->commands.size())
			continue;
		fprintf(source_file, "\n");
		indent_fprintf(source_file, " && ((req_version < %d) ||\n", iter.first);
		increase_indent();
		indent_fprintf(source_file, "");
		iter.second->print_load_check(source_file);
		fprintf(source_file, ")");
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
	       "  -a,--api <api>                     Generate bindings for API <api>. Must be one\n"
	       "                                     of 'gl', 'wgl', or 'glx'. Default is 'gl'\n");
}

int main(int argc, char **argv)
{
	XMLDocument doc;
	XMLError err;

	static struct option options [] = {
		{"api"       , 1, 0, 'a' },
		{"help"      , 0, 0, 'h' }
	};

	const char *api_name = "gl";
	std::set<const char *, cstring_compare> extensions;

	while (1) {
		int option_index;
		int ret;
		int c = getopt_long(argc, argv, "a:e:v:l:", options, &option_index);
		if (c == -1) {
			break;
		}
		switch (c) {
		case '?':
		case ':':
			print_help(argv[0]);
			exit(-1);
			break;
		case 'a':
			api_name = optarg;
			break;
		case 'e':
			extensions.insert(optarg);
			break;
		case 'h':
			print_help(argv[0]);
			exit(0);
			break;
		}
	}

	printf("Generating bindings for %s\n", api_name);
	api api(api_name, extensions);

	g_api = &api;

	char in_filename[200];
	snprintf(in_filename, sizeof(in_filename),PKGDATADIR "/%s.xml", api.name());
	err = doc.LoadFile(in_filename);
	if (err != XML_NO_ERROR) {
		printf("Error loading khronos registry file %s\n", in_filename);
		exit(-1);
	}

	char header_name[100];
	char cpp_name[100];
	snprintf(header_name, sizeof(header_name), "glb_%s%s",
		api.name(),
		".h");
	snprintf(cpp_name, sizeof(header_name), "glb_%s%s",
		api.name(),
		".c");

	FILE *header_file = fopen(header_name, "w");
	FILE *source_file = fopen(cpp_name, "w");

	printf("Writing bindings to %s and %s\n", cpp_name, header_name);

	khronos_registry_visitor registry_visitor(doc, api);
	doc.Accept(&registry_visitor);

	if (!strcmp(api_name, "gl")) {
		api.bindify(header_name, 32, header_file, source_file);
	} else if (!strcmp(api_name, "glx")) {
		api.bindify(header_name, 14, header_file, source_file);
	} else if (!strcmp(api_name, "wgl")) {
		api.bindify(header_name, 10, header_file, source_file);
	}

	return 0;
}
