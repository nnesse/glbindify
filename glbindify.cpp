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

#include "tinyxml2.h"

using namespace tinyxml2;

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
typedef std::shared_ptr<enumeration> enumeration_ptr;

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
	void print_declare(FILE *out, const char *command_prefix, std::string &indent_string) {
		fprintf(out, "%sextern %s (*%s%s)(", indent_string.c_str(), type_decl.c_str(), command_prefix, name);
		if (params.size()) {
			fprintf(out, "%s", params[0].decl.c_str());
			for(int i = 1; i < params.size(); i++) {
				fprintf(out, ", %s", params[i].decl.c_str());
			}
		}
		fprintf(out, ");\n");
	}
	void print_initialize(FILE *out, const char *command_prefix, std::string &indent_string) {
		fprintf(out, "%s%s (*%s%s)(", indent_string.c_str(), type_decl.c_str(), command_prefix, name);
		if (params.size()) {
			fprintf(out, "%s", params[0].decl.c_str());
			for(int i = 1; i < params.size(); i++) {
				fprintf(out, ", %s", params[i].decl.c_str());
			}
		}
		fprintf(out, ") = NULL;\n");
	}

	void print_load(FILE *out, const char *command_prefix, bool include_prefix, std::string &indent_string) {
		fprintf(out, "%s%s%s = (%s (*)(", indent_string.c_str(), (include_prefix ? command_prefix : ""), name, type_decl.c_str());
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
typedef std::shared_ptr<command> command_ptr;

class api {
	friend class enumeration_visitor;
	friend class feature_visitor;
	friend class extension_visitor;
	friend class type_visitor;
	friend class khronos_registry_visitor;

	//Api description
	const char *m_name;
	const char *m_command_prefix;
	const char *m_enumeration_prefix;

	bool m_use_api_namespaces;

	std::set<const char *, cstring_compare> m_extensions;
	float m_version;

	//List of all enums and commands
	std::map<const char *, unsigned int, cstring_compare> m_enum_map;
	std::vector<enumeration_ptr> m_enumerations;
	std::map<const char *, command_ptr, cstring_compare> m_commands;
	std::map<const char *, std::string, cstring_compare> m_types;

	//Types enums and commands needed
	std::map<const char *, std::string, cstring_compare> m_target_types;
	std::map<const char *, unsigned int, cstring_compare> m_target_enums;
	std::map<const char *, command_ptr, cstring_compare> m_target_commands;
	std::map<const char *, std::map<std::string, command_ptr>, cstring_compare > m_target_extension_commands;

	void include_type(const char *type);
	void resolve_types();

	void types_declare(FILE *header_filE, std::string &indent_string);
	void namespace_declare(FILE *header_file, std::string &indent_string);
	void namespace_define(FILE *header_file, std::string &indent_string);
public:
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

	float version() {
		return m_version;
	}

	api(const char *name, int major_version, int minor_version, bool use_api_namespace, std::set<const char *, cstring_compare> &extensions) :
		m_name(name),
		m_version(major_version + (minor_version * 0.1f)),
		m_extensions(extensions),
		m_use_api_namespaces(use_api_namespace)
	{
		if (!strcmp(m_name,"wgl")) {
			m_command_prefix = "wgl";
			m_enumeration_prefix = "WGL_";
		} else if (!strcmp(m_name,"glx")) {
			m_command_prefix = "glX";
			m_enumeration_prefix = "GLX_";
		} else if (!strcmp(m_name,"gl")) {
			m_command_prefix = "gl";
			m_enumeration_prefix = "GL_";
		}
	}
	void bindify(XMLDocument &doc, const char *header_name, FILE *header_file, FILE *cpp_file);
};

template <class T>
class data_builder_visitor : public XMLVisitor
{
protected:
	std::shared_ptr<T> m_data;
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

	std::shared_ptr<T> build()
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
				m_data.reset();
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
			unsigned int val;
			int ret = sscanf(elem.Attribute("value"), "0x%x", &val);
			if (!ret)
				ret = sscanf(elem.Attribute("value"), "%d", &val);
			const char *enumeration_name = elem.Attribute("name");

			if (ret) {
				if (m_api.is_enum_in_namespace(&enumeration_name)) {
					m_data->enum_map[enumeration_name] = val;
					m_api.m_enum_map[enumeration_name] = val;
				}
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

class feature_visitor :  public XMLVisitor
{
	api &m_api;
	bool VisitEnter(const XMLElement &elem, const XMLAttribute *attrib)
	{
		if (tag_stack_test(elem, "feature", "registry")) {
			return !strcmp(m_api.m_name, elem.Attribute("api")) &&
				(elem.FloatAttribute("number") <= m_api.version());
		} else if (tag_stack_test(elem, "require", "feature")) {
			return !elem.Attribute("profile") || !strcmp(elem.Attribute("profile"), "core");
		} else if (tag_stack_test(elem, "remove", "feature")) {
			return !elem.Attribute("profile") || !strcmp(elem.Attribute("profile"), "core");
		} else if (tag_stack_test(elem, "enum", "require")) {
			const char *enumeration_name = elem.Attribute("name");
			if (m_api.is_enum_in_namespace(&enumeration_name)) {
				m_api.m_target_enums[enumeration_name] = m_api.m_enum_map[enumeration_name];
				return true;
			} else {
				return false;
			}
		} else if (tag_stack_test(elem, "enum", "remove")) {
			const char *enumeration_name = elem.Attribute("name");
			if (m_api.is_enum_in_namespace(&enumeration_name)) {
				m_api.m_target_enums.erase(enumeration_name);
				return true;
			} else {
				return false;
			}
		} else if (tag_stack_test(elem, "command", "require")) {
			const char *command_name = elem.Attribute("name");
			if (m_api.is_command_in_namespace(&command_name)) {
				m_api.m_target_commands[command_name] = m_api.m_commands[command_name];
				return true;
			} else {
				return false;
			}
		} else if (tag_stack_test(elem, "command", "remove")) {
			const char *command_name = elem.Attribute("name");
			if (m_api.is_command_in_namespace(&command_name)) {
				m_api.m_target_commands.erase(command_name);
			}
			return true;
		}
	}
public:
	feature_visitor(api &api) : m_api(api) {}
};

class extension_visitor :  public XMLVisitor
{
	api &m_api;
	const char *m_name;
	bool VisitEnter(const XMLElement &elem, const XMLAttribute *attrib)
	{
		if (tag_stack_test(elem, "extension", "extensions")) {
			m_name = elem.Attribute("name") + strlen(m_api.name()) + 1;
			if (m_api.m_extensions.count(m_name)) {
				const char *supported = elem.Attribute("supported");
				char *supported_copy = strdup(supported);
				char *saveptr;
				char *token = strtok_r(supported_copy, "|", &saveptr);
				while (token) {
					if (!strcmp(token, m_api.name())) {
						break;
					}
					token = strtok_r(NULL, "|", &saveptr);
				}
				return (token != NULL);
			}
			return false;
		} else if (tag_stack_test(elem, "require", "extension") || tag_stack_test(elem, "remove", "extension")) {
			return !elem.Attribute("profile") || (!strcmp(elem.Attribute("profile"),"core"));
		} else if (tag_stack_test(elem, "enum", "require")) {
			const char *enumeration_name = elem.Attribute("name");
			if (m_api.is_enum_in_namespace(&enumeration_name)) {
				m_api.m_target_enums[enumeration_name] = m_api.m_enum_map[elem.Attribute("name")];
				return true;
			} else {
				return false;
			}
		} else if (tag_stack_test(elem, "command", "require")) {
			const char *command_name = elem.Attribute("name");
			if (m_api.is_command_in_namespace(&command_name)) {
				m_api.m_target_extension_commands[m_name][command_name] = m_api.m_commands[command_name];
				return true;
			} else {
				return false;
			}
			return true;
		}
	}
public:
	extension_visitor(api &api) : m_api(api) {}
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
			enumeration_ptr enumeration = e.build();
			if (enumeration) {
				m_api.m_enumerations.push_back(enumeration);
			}
			return false;
		} else if (tag_stack_test(elem, "feature", "registry")) {
			feature_visitor f(m_api);
			elem.Accept(&f);
			return false;
		} else if (tag_stack_test(elem, "extension", "extensions")) {
			extension_visitor e(m_api);
			elem.Accept(&e);
			return false;
		} else if (tag_stack_test(elem, "command", "commands")) {
			command_visitor c(elem, m_api);
			command_ptr command = c.build();
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

void api::types_declare(FILE *header_file, std::string &indent_string)
{
	for (auto val : m_target_types) {
		fprintf(header_file, "%s%s\n", indent_string.c_str(), val.second.c_str());
	}
}

void api::namespace_declare(FILE *header_file, std::string &indent_string)
{
	const char *enumeration_prefix = m_use_api_namespaces ? "" : m_enumeration_prefix;

	if (m_use_api_namespaces) {
		fprintf(header_file, "%snamespace %s\n {\n", indent_string.c_str(), m_name);
		indent_string.push_back('\t');
	}

	fprintf(header_file, "%senum {\n", indent_string.c_str());

	for (auto val : m_target_enums) {
		if (m_use_api_namespaces) {
			fprintf(header_file, "#pragma push_macro(\"%s\")\n", val.first);
			fprintf(header_file, "#undef %s\n", val.first);
			fprintf(header_file, "%s%s%s = 0x%x,\n", indent_string.c_str(), enumeration_prefix, val.first, val.second);
			fprintf(header_file, "#pragma pop_macro(\"%s\")\n", val.first);
		} else {
			fprintf(header_file, "%s%s%s = 0x%x,\n", indent_string.c_str(), enumeration_prefix, val.first, val.second);
		}
	}
	fprintf(header_file, "%s};\n", indent_string.c_str());

	for (auto val : m_target_commands)
		val.second->print_declare(header_file, m_use_api_namespaces ? "" : m_command_prefix, indent_string);
	for (auto iter : m_target_extension_commands)
		for (auto val : iter.second)
			val.second->print_declare(header_file, m_use_api_namespaces ? "" : m_command_prefix, indent_string);

	const char *extension_prefix = m_use_api_namespaces ? "" : m_enumeration_prefix;

	for (auto extension : m_extensions)
		fprintf(header_file, "%sextern bool %s%s;", indent_string.c_str(), extension_prefix, extension);

	if (m_use_api_namespaces) {
		fprintf(header_file, "%sbool init();\n", indent_string.c_str());
	} else {
		fprintf(header_file, "%sbool init_%s();\n", indent_string.c_str(), m_name);
	}
	if (m_use_api_namespaces) {
		indent_string.pop_back();
		fprintf(header_file, "%s}\n", indent_string.c_str());
	}
}

void api::namespace_define(FILE *cpp_file, std::string &indent_string)
{
	if (m_use_api_namespaces) {
		fprintf(cpp_file,  "%snamespace %s {\n", indent_string.c_str(), m_name);
		indent_string.push_back('\t');
	}

	const char *command_prefix = m_use_api_namespaces ? "" : m_command_prefix;
	const char *extension_prefix = m_use_api_namespaces ? "" : m_enumeration_prefix;

	for (auto val : m_target_commands)
		val.second->print_initialize(cpp_file, command_prefix, indent_string);

	for (auto iter : m_target_extension_commands)
		for (auto val : iter.second)
			val.second->print_initialize(cpp_file, command_prefix, indent_string);

	fprintf(cpp_file, "%sstatic std::set<std::string> supported_extensions;\n", indent_string.c_str());
	for (auto extension : m_extensions)
		fprintf(cpp_file, "%sbool %s = false;\n", indent_string.c_str(), extension);

	if (m_use_api_namespaces) {
		fprintf(cpp_file, "%sbool init()\n", indent_string.c_str());
	} else {
		fprintf(cpp_file, "%sbool init_%s()\n", indent_string.c_str(), m_name);
	}
	fprintf(cpp_file, "%s{\n", indent_string.c_str());

	indent_string.push_back('\t');

	for (auto val : m_target_commands)
		val.second->print_load(cpp_file, m_command_prefix, !m_use_api_namespaces, indent_string);
	for (auto iter : m_target_extension_commands)
		for (auto val : iter.second)
			val.second->print_load(cpp_file, m_command_prefix, !m_use_api_namespaces, indent_string);

	//
	// Identify supported gl extensions
	//
	if (!strcmp(m_name,"gl")) {
		fprintf(cpp_file, "%sGLint extension_count;\n", indent_string.c_str());
		if (m_use_api_namespaces) {
			fprintf(cpp_file, "%sGetIntegerv(NUM_EXTENSIONS, &extension_count);\n", indent_string.c_str());
		} else {
			fprintf(cpp_file, "%sglGetIntegerv(GL_NUM_EXTENSIONS, &extension_count);\n", indent_string.c_str());
		}
		fprintf(cpp_file, "%sfor (int i = 0; i < extension_count; i++) {\n", indent_string.c_str());
		indent_string.push_back('\t');

		if (m_use_api_namespaces) {
			fprintf(cpp_file, "%ssupported_extensions.insert(std::string((const char *)GetStringi(EXTENSIONS, i) + 3));\n", indent_string.c_str());
		} else {
			fprintf(cpp_file, "%ssupported_extensions.insert(std::string((const char *)glGetStringi(GL_EXTENSIONS, i) + 3));\n", indent_string.c_str());
		}
		indent_string.pop_back();

		fprintf(cpp_file, "%s}\n", indent_string.c_str());
		for (auto extension : m_extensions) {
			//
			// Check if the extension is in the supported extensions list
			//
			fprintf(cpp_file, "%s%s%s = (supported_extensions.count(\"%s\") == 1)", indent_string.c_str(),
					extension_prefix, extension, extension);
			//
			// Check if the extension's functions have been found.
			//
			// Note: EXT_direct_state_access extends compatibility profile functions since there is no easy way to determine
			// which functions in this extension are in core or compatibility we will just rely on the extension string to determine
			// support for it
			//
			indent_string.push_back('\t');
			int i = 0;
			if (strcmp(extension, "EXT_direct_state_access")) {
				for (auto iter : m_target_extension_commands[extension]) {
					if ((i % 4) == 0) {
						fprintf(cpp_file, "\n%s", indent_string.c_str());
					}
					i++;
					fprintf(cpp_file, " && %s%s", command_prefix, iter.first.c_str());
				}
			}
			fprintf(cpp_file, ";\n");
			indent_string.pop_back();
		}
	} else {
		int i = 0;
		for (auto extension : m_extensions) {
			fprintf(cpp_file, "%s%s%s = true", indent_string.c_str(), extension_prefix, extension);
			indent_string.push_back('\t');
			int i = 0;
			for (auto iter : m_target_extension_commands[extension]) {
				i++;
				if ((i % 4) == 0) {
					fprintf(cpp_file, "\n%s", indent_string.c_str());
				}
				fprintf(cpp_file, " && %s%s", command_prefix, iter.first.c_str());
			}
			fprintf(cpp_file, ";\n");
			indent_string.pop_back();
		}
	}

	fprintf(cpp_file, "%sreturn true", indent_string.c_str());

	indent_string.push_back('\t');
	int i = 0;
	for (auto iter : m_target_commands) {
		i++;
		if ((i % 4) == 0) {
			fprintf(cpp_file, "\n%s", indent_string.c_str());
		}
		fprintf(cpp_file, " && %s%s", command_prefix, iter.first);
	}

	fprintf(cpp_file, ";\n");
	indent_string.pop_back();

	indent_string.pop_back();
	fprintf(cpp_file, "%s}\n", indent_string.c_str());
	if (m_use_api_namespaces) {
		indent_string.pop_back();
		fprintf(cpp_file, "%s}\n", indent_string.c_str());
	}
}

void api::include_type(const char *type)
{
	if (type != NULL && !g_common_gl_typedefs.count(type) && !m_target_types.count(type)) {
		m_target_types[type] = m_types[type];
	}
}

void api::resolve_types()
{
	for (auto val : m_target_commands) {
		command_ptr command = val.second;
		include_type(command->type);
		for (auto param : command->params) {
			include_type(param.type);
		}
	}
}

void api::bindify(XMLDocument &doc, const char *header_name, FILE *header_file , FILE *cpp_file)
{
	khronos_registry_visitor registry_visitor(doc, *this);
	doc.Accept(&registry_visitor);
	std::string indent_string;

	resolve_types();

	fprintf(header_file, "#ifndef GL_BINDIFY_%s_H\n", m_name);
	fprintf(header_file, "#define GL_BINDIFY_%s_H\n", m_name);

	if (!strcmp(m_name, "glx")) {
		fprintf(header_file, "#include <X11/Xlib.h>\n");
		fprintf(header_file, "#include <X11/Xutil.h>\n");
	} else if (!strcmp(m_name, "wgl")) {
		fprintf(header_file, "#include <windows.h>\n");
	}
	fprintf(header_file, "#include <stdint.h>\n");
	fprintf(header_file, "#include <stddef.h>\n");
	fprintf(header_file, "namespace glbindify {\n");
	indent_string.push_back('\t');

	//
	//We need to include these typedefs even for glx and wgl since they are referenced there without being defined
	//
	fprintf(header_file, "#ifndef GLBINDIFY_COMMON_GL_TYPEDEFS\n");
	fprintf(header_file, "#define GLBINDIFY_COMMON_GL_TYPEDEFS\n");
	fprintf(header_file, "%stypedef unsigned int GLenum;\n", indent_string.c_str());
	fprintf(header_file, "%stypedef unsigned char GLboolean;\n", indent_string.c_str());
	fprintf(header_file, "%stypedef unsigned int GLbitfield;\n", indent_string.c_str());
	fprintf(header_file, "%stypedef signed char GLbyte;\n", indent_string.c_str());
	fprintf(header_file, "%stypedef short GLshort;\n", indent_string.c_str());
	fprintf(header_file, "%stypedef int GLint;\n", indent_string.c_str());
	fprintf(header_file, "%stypedef unsigned char GLubyte;\n", indent_string.c_str());
	fprintf(header_file, "%stypedef unsigned short GLushort;\n", indent_string.c_str());
	fprintf(header_file, "%stypedef unsigned int GLuint;\n", indent_string.c_str());
	fprintf(header_file, "%stypedef int GLsizei;\n", indent_string.c_str());
	fprintf(header_file, "%stypedef float GLfloat;\n", indent_string.c_str());
	fprintf(header_file, "%stypedef double GLdouble;\n", indent_string.c_str());
	fprintf(header_file, "%stypedef ptrdiff_t GLintptr;\n", indent_string.c_str());
	fprintf(header_file, "%stypedef ptrdiff_t GLsizeiptr;\n", indent_string.c_str());
	fprintf(header_file, "#endif\n");

	types_declare(header_file, indent_string);

	namespace_declare(header_file, indent_string);

	indent_string.pop_back();

	fprintf(header_file, "}\n"); //namespace glbindify {
	fprintf(header_file, "#endif\n");

	indent_string.clear();

	fprintf(cpp_file, "#include \"%s\"\n", header_name);
	fprintf(cpp_file, "#include <set>\n");
	fprintf(cpp_file, "#include <string>\n");
	fprintf(cpp_file, "#ifndef _WIN32\n");
	fprintf(cpp_file, "extern \"C\" {\n");
	fprintf(cpp_file, "\textern void (*glXGetProcAddress(const unsigned char *))(void);\n");
	fprintf(cpp_file, "}\n");
	fprintf(cpp_file, "static inline void (*LoadProcAddress(const char *name))(void) {\n");
	fprintf(cpp_file, "\treturn (*glXGetProcAddress)((const unsigned char *)name);\n");
	fprintf(cpp_file, "}\n");
	fprintf(cpp_file, "#include <stdio.h>\n");
	fprintf(cpp_file, "#else\n");
	fprintf(cpp_file, "#include <windows.h>\n");
	fprintf(cpp_file, "#include <wingdi.h>\n");
	fprintf(cpp_file, "#include <stdio.h>\n");
	fprintf(cpp_file, "static PROC LoadProcAddress(const char *name) {\n");
	fprintf(cpp_file, "\tPROC addr = wglGetProcAddress((LPCSTR)name);\n");
	fprintf(cpp_file, "\tif (addr) return addr;\n");
	fprintf(cpp_file, "\telse return (PROC)GetProcAddress(GetModuleHandleA(\"OpenGL32.dll\"), (LPCSTR)name);\n");
	fprintf(cpp_file, "}\n");
	fprintf(cpp_file, "#endif\n");
	fprintf(cpp_file, "namespace glbindify {\n");

	indent_string.push_back('\t');

	namespace_define(cpp_file, indent_string);

	indent_string.pop_back();
	fprintf(cpp_file, "}\n");
}

static void print_help(const char *program_name)
{
	printf("Usage: %s [OPTION]...\n", program_name);
	printf("\n"
	       "Options:\n"
	       "  -a,--api <api>                     Generate bindings for API <api> Must be one\n"
	       "                                     of 'gl', 'wgl', or 'glx'. Default is 'gl'\n"
	       "  -v, --version <major>.<minor>      Version number of <api> to generate\n"
	       "  -e, --extension <exstension-name>  Generate bindings for extension <extension-name>\n"
	       "  -n, --api-namespaces               Put bindings in API specific namespaces\n");
}

int main(int argc, char **argv)
{
	XMLDocument doc;
	XMLError err;

	static struct option options [] = {
		{"api"       , 1, 0, 'a' },
		{"extension" , 1, 0, 'e' },
		{"version"   , 1, 0, 'v' },
		{"api-namespaces"   , 0, 0, 'n' },
		{"help"      , 0, 0, 'h' }
	};

	const char *api_name = "gl";
	int api_major_version = 3;
	int api_minor_version = 3;
	bool use_api_namespace = false;
	std::set<const char *, cstring_compare> extensions;

	while (1) {
		int option_index;
		int ret;
		int c = getopt_long(argc, argv, "a:e:v:n", options, &option_index);
		if (c == -1) {
			break;
		}
		switch (c) {
		case '?':
		case ':':
			print_help(argv[0]);
			exit(-1);
			break;
			print_help(argv[0]);
			exit(-1);
			break;
		case 'a':
			api_name = optarg;
			break;
		case 'n':
			use_api_namespace = true;
			break;
		case 'e':
			extensions.insert(optarg);
			break;
		case 'v':
			api_minor_version = 0;
			ret = sscanf(optarg, "%d.%d", &api_major_version, &api_minor_version);
			if (ret == 0) {
				printf("Invalid version number '%s'\n\n", optarg);
				print_help(argv[0]);
				exit(-1);
			}
			break;
		case 'h':
			print_help(argv[0]);
			exit(0);
			break;
		}
	}

	printf("Generating bindings for %s version %d.%df\n", api_name, api_major_version, api_minor_version);
	api api(api_name, api_major_version, api_minor_version, use_api_namespace, extensions);

	char in_filename[200];
	snprintf(in_filename, sizeof(in_filename),PKGDATADIR "/%s.xml", api.name());
	err = doc.LoadFile(in_filename);
	if (err != XML_NO_ERROR) {
		printf("Error loading khronos registry file %s\n", in_filename);
		exit(-1);
	}

	char header_name[100];
	char cpp_name[100];
	snprintf(header_name, sizeof(header_name), "%s_%d_%d.hpp", api.name(), api_major_version, api_minor_version);
	snprintf(cpp_name, sizeof(header_name), "%s_%d_%d.cpp", api.name(), api_major_version, api_minor_version);

	FILE *header_file = fopen(header_name, "w");
	FILE *cpp_file = fopen(cpp_name, "w");

	printf("Writing bindings to %s and %s\n", cpp_name, header_name);
	api.bindify(doc, header_name, header_file, cpp_file);

	return 0;
}
