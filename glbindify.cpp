#include <stdint.h>

#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <set>
#include <fstream>
#include <stdio.h>
#include <math.h>

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
#endif

#if HAVE_GPERF
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#endif

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

struct interface {
	typedef std::map<const char *, command *, cstring_compare> commands_type;
	typedef std::set<const char *, cstring_compare> enums_type;

	enums_type enums;
	commands_type commands;
	enums_type removed_enums;
	commands_type removed_commands;

	typedef std::map<const char *, std::string, cstring_compare> types_type;
	types_type types;

	void append(const interface &other);
	void include_type(const char *type);
	void resolve_types();
	void print_definition(FILE *header_file);
	void print_declaration(FILE *header_file);
	void print_load_check(FILE *source_file);
};

	//Api description
const char *g_api_name;
const char *g_variant_name;
const char *g_command_prefix;
const char *g_enumeration_prefix;

//List of all enums and commands
typedef std::map<const char *, unsigned int, cstring_compare> enum_map_type;
enum_map_type g_enum_map;

std::vector<enumeration *> g_enumerations;

typedef std::map<const char *, command *, cstring_compare> commands_type;
commands_type g_commands;

typedef std::map<const char *, std::string, cstring_compare> types_type;
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
	if (strstr(*name, g_enumeration_prefix)) {
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
			int ret = sscanf(elem.Attribute("value"), "0x%x", &val);
			if (ret != 1)
				ret = sscanf(elem.Attribute("value"), "%d", &val);
			const char *enumeration_name = elem.Attribute("name");
			if (ret == 1) {
				if (is_enum_in_namespace(&enumeration_name)) {
					m_data->enum_map[enumeration_name] = val;
					g_enum_map[enumeration_name] = val;
				}
			} else {
				printf("warning: can't parse value of enum %s: \"%s\"\n", elem.Attribute("name"), elem.Attribute("value"));
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
			return !elem.Attribute("profile") || !strcmp(elem.Attribute("profile"), "core");
		} else if (tag_test(elem, "remove") && elem.Parent() == &m_root) {
			return !elem.Attribute("profile") || !strcmp(elem.Attribute("profile"), "core");
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
		if (tag_test(elem, "type") && m_type_name != NULL) {
			g_types[m_type_name] = m_type_decl;
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
			const char *name = elem.Attribute("name") + strlen(g_api_name) + 1;

			//We can't support SGI extensions due to missing types
			if (!strcmp(g_api_name, "glx") && (strstr(name, "SGI") == name) && !strstr(name,"swap_control")) {
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

void interface::print_declaration(FILE *header_file)
{
	const char *enumeration_prefix = g_enumeration_prefix;

	FOREACH(val, types, types_type) {
		char *temp = strdup(val->first);
		char *cur;
		for (cur = temp; *cur; cur++)
			if (*cur == ' ')
				*cur = '_';
		indent_fprintf(header_file, "#ifndef %s_TYPE_%s\n", g_macro_prefix, temp);
		indent_fprintf(header_file, "#define %s_TYPE_%s\n", g_macro_prefix, temp);
		indent_fprintf(header_file, "%s\n", val->second.c_str());
		indent_fprintf(header_file, "#endif\n", enumeration_prefix, val->first);
		free(temp);
	}

	indent_fprintf(header_file, "\n");
	FOREACH (val, removed_enums, enums_type)
		fprintf(header_file, "#undef %s%s\n", enumeration_prefix, *val);

	FOREACH (val, enums, enums_type)
		fprintf(header_file, "#define %s%s 0x%x\n",
				enumeration_prefix, *val,
				g_enum_map[*val]);

	indent_fprintf(header_file, "\n");
	FOREACH (iter, removed_commands, commands_type) {
		command *command = iter->second;
		fprintf(header_file, "#undef %s%s\n",
				g_command_prefix, command->name);
	}
	FOREACH (iter, commands, commands_type) {
		command *command = iter->second;
		fprintf(header_file, "#define %s%s _%s_%s%s\n",
				g_command_prefix, command->name,
				g_prefix, g_command_prefix, command->name);
		command->print_declare(header_file, g_command_prefix);
	}
}

void interface::print_definition(FILE *source_file)
{
	indent_fprintf(source_file, "\n");
	FOREACH (iter, commands, commands_type)
		iter->second->print_initialize(source_file, g_command_prefix);
}

void interface::include_type(const char *type)
{
	if (type != NULL && !g_common_gl_typedefs.count(type) && !types.count(type)) {
		types[type] = g_types[type];
	}
}

void interface::resolve_types()
{
	FOREACH (iter, commands, commands_type) {
		command *command = iter->second;
		include_type(command->type);
		for (std::vector<command::param>::iterator iter = command->params.begin(); iter != command->params.end(); iter++) {
			include_type(iter->type);
		}
	}
}

void interface::append(const interface &other)
{
	enums.insert(other.enums.begin(), other.enums.end());
	FOREACH_CONST (e, other.removed_enums, enums_type)
		enums.erase(*e);
	commands.insert(other.commands.begin(), other.commands.end());
	FOREACH_CONST (iter, other.removed_commands, commands_type)
		commands.erase(iter->first);
}

void interface::print_load_check(FILE *source_file)
{
	if (!commands.size()) {
		fprintf(source_file, "true");
	} else {
		const char *command_prefix = g_command_prefix;
		int i = 0;
		FOREACH (iter,  commands, commands_type) {
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
	interface core_3_2;
	int max_version = min_version;

	bool is_gl_api = !strcmp(g_api_name, "gl");
	FOREACH (iter, g_feature_interfaces, feature_interfaces_type) {
		if (iter->first > min_version)
			iter->second->resolve_types();
		else
			core_3_2.append(*(iter->second));
		max_version = iter->first > max_version ? iter->first : max_version;
		full_interface.append(*(iter->second));
	}
	FOREACH (iter, g_extension_interfaces, extension_interfaces_type) {
		iter->second->resolve_types();
		full_interface.append(*(iter->second));
	}
	core_3_2.resolve_types();

	fprintf(header_file, "#ifndef GL_BINDIFY_%s_H\n", g_api_name);
	fprintf(header_file, "#define GL_BINDIFY_%s_H\n", g_api_name);

	fprintf(header_file, "#ifdef __cplusplus\n");
	fprintf(header_file, "extern \"C\" {\n");
	fprintf(header_file, "#endif\n");

	if (!strcmp(g_api_name, "glx")) {
		fprintf(header_file, "#include <X11/Xlib.h>\n");
		fprintf(header_file, "#include <X11/Xutil.h>\n");
	} else if (!strcmp(g_api_name, "wgl")) {
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
	indent_fprintf(header_file, "#ifndef %s_%sVERSION\n", g_macro_prefix, g_enumeration_prefix);
	indent_fprintf(header_file, "#define %s_%sVERSION %d\n", g_macro_prefix, g_enumeration_prefix, min_version);
	indent_fprintf(header_file, "#endif\n");

	core_3_2.print_declaration(header_file);
	FOREACH (iter, g_feature_interfaces, feature_interfaces_type) {
		if (iter->first > min_version) {
			indent_fprintf(header_file, "\n");
			indent_fprintf(header_file, "#if defined(%s_%sVERSION) && %s_%sVERSION >= %d\n",
					g_macro_prefix,
					g_enumeration_prefix,
					g_macro_prefix,
					g_enumeration_prefix,
					iter->first);
			indent_fprintf(header_file, "\n");
			iter->second->print_declaration(header_file);
			indent_fprintf(header_file, "#endif\n");
		}
	}

	indent_fprintf(header_file, "\n");
	FOREACH (iter, g_extension_interfaces, extension_interfaces_type) {
		indent_fprintf(header_file, "\n");
		indent_fprintf(header_file, "#if defined(%s_ENABLE_%s%s)\n", g_macro_prefix, g_enumeration_prefix, iter->first);
		indent_fprintf(header_file, "extern bool %s_%s%s;\n", g_macro_prefix, g_enumeration_prefix, iter->first);
		iter->second->print_declaration(header_file);
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
	fprintf(source_file, "extern void (*glXGetProcAddress(const unsigned char *))(void);\n");
	fprintf(source_file, "static inline void *LoadProcAddress(const char *name) { return glXGetProcAddress((const unsigned char *)name); }\n");
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
		indent_fprintf(source_file, "#define %s_ENABLE_%s%s\n", g_macro_prefix, g_enumeration_prefix, iter->first);
	}

	fprintf(source_file, "#include \"%s\"\n", header_name);

	full_interface.print_definition(source_file);

	indent_fprintf(source_file, "\n");
	FOREACH (iter, g_extension_interfaces, extension_interfaces_type) {
		indent_fprintf(source_file, "bool %s_%s%s = %s;\n",
				g_macro_prefix,
				g_enumeration_prefix,
				iter->first,
				is_gl_api ? "false" : "true");
	}

#if HAVE_GPERF
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
			close(fdpair[0]);
			FILE *gperf_in = fdopen(fdpair[1], "w");
			fprintf(gperf_in, "struct extension_match { const char *name; bool *support_flag; };\n");
			fprintf(gperf_in, "\%\%\%\%\n");
			FOREACH (iter, g_extension_interfaces, extension_interfaces_type) {
				fprintf(gperf_in, "%s%s, &%s_%s%s\n", g_enumeration_prefix, iter->first,
					g_macro_prefix, g_enumeration_prefix, iter->first);
			}
			fflush(gperf_in);
			close(fdpair[1]);
			waitpid(child_pid, NULL, 0);
		} else {
			close(fdpair[1]);
			dup2(fdpair[0], STDIN_FILENO);
			dup2(fileno(source_file), STDOUT_FILENO);
			execlp("gperf", "gperf", "-D","-t", "-F", ",NULL", 0);
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

	FOREACH (iter, full_interface.commands, interface::commands_type)
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
#if HAVE_GPERF
		indent_fprintf(source_file, "\tstruct extension_match *match = in_word_set(extname, strlen(extname));\n");
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
			iter->second->print_load_check(source_file);
			decrease_indent();
			fprintf(source_file, ";\n");
		}
	}

	indent_fprintf(source_file, "\n");
	indent_fprintf(source_file, "return ");
	core_3_2.print_load_check(source_file);

	FOREACH(iter, g_feature_interfaces, feature_interfaces_type) {
		if (iter->first <= min_version || !iter->second->commands.size())
			continue;
		fprintf(source_file, "\n");
		indent_fprintf(source_file, " && ((req_version < %d) ||\n", iter->first);
		increase_indent();
		indent_fprintf(source_file, "(");
		iter->second->print_load_check(source_file);
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
	       "  -a,--api <api>                     Generate bindings for API <api>. Must be one\n"
	       "                                     of 'gl', 'wgl', or 'glx'. Default is 'gl'\n"
	       "  -n,--namespace <Namespace>         Namespace for generated bindings. This is the first\n"
	       "                                     part of the name of every function and macro.\n"
	       "  -s,--srcdir <dir>                  Directory to find XML sources\n"
	       "  -v,--version                       Print version information\n");
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
		{"version"    , 1, 0, 'v' },
		{"namespace" , 1, 0, 'n' },
		{"help"      , 0, 0, 'h' }
	};

	g_api_name = "gl";
	g_variant_name = "glcore";
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
			if (!strcmp(g_api_name, "gl")) {
				g_variant_name = "glcore";
			} else {
				g_variant_name = g_api_name;
			}
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

	if (!strcmp(g_api_name,"wgl")) {
		g_command_prefix = "wgl";
		g_enumeration_prefix = "WGL_";
	} else if (!strcmp(g_api_name,"glx")) {
		g_command_prefix = "glX";
		g_enumeration_prefix = "GLX_";
	} else if (!strcmp(g_api_name,"gl")) {
		g_command_prefix = "gl";
		g_enumeration_prefix = "GL_";
	}

	char in_filename[200];
#ifdef PKGDATADIR
	if (!srcdir) {
		srcdir = PKGDATADIR;
	}
	snprintf(in_filename, sizeof(in_filename), PKGDATADIR "/%s.xml", g_api_name);
#else
	if (!srcdir) {
		srcdir = ".";
	}
	snprintf(in_filename, sizeof(in_filename), "%s/%s.xml", srcdir, g_api_name);
#endif
	err = doc.LoadFile(in_filename);
	if (err != XML_NO_ERROR) {
		printf("Error loading khronos registry file %s\n", in_filename);
		exit(-1);
	}

	char header_name[100];
	char cpp_name[100];
	snprintf(header_name, sizeof(header_name), "%s-%s%s",
		g_prefix,
		g_variant_name,
		".h");
	snprintf(cpp_name, sizeof(header_name), "%s-%s%s",
		g_prefix,
		g_variant_name,
		".c");

	FILE *header_file = fopen(header_name, "w");
	FILE *source_file = fopen(cpp_name, "w");

	printf("Writing bindings to %s and %s\n", cpp_name, header_name);

	khronos_registry_visitor registry_visitor(doc);
	doc.Accept(&registry_visitor);

	if (!strcmp(g_api_name, "gl")) {
		bindify(header_name, 32, header_file, source_file);
	} else if (!strcmp(g_api_name, "glx")) {
		bindify(header_name, 14, header_file, source_file);
	} else if (!strcmp(g_api_name, "wgl")) {
		bindify(header_name, 10, header_file, source_file);
	}

	return 0;
}
