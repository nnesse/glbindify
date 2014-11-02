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

struct enumeration {
	std::string name;
	std::map<std::string,unsigned int > enum_map;
};
typedef std::shared_ptr<enumeration> enumeration_ptr;

std::set<std::string> g_common_gl_typedefs = {
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
	std::string name;
	std::string type;
	std::string type_decl;
	struct param {
		std::string type;
		std::string name;
		std::string decl;
	};
	std::vector<param> params;
	void print_declare(std::ofstream &out, const char *command_prefix, std::string &indent_string) {
		out << indent_string << "extern " << type_decl << " (*" << command_prefix << name << ")(";
		if (params.size()) {
			out << params[0].decl;
			for(int i = 1; i < params.size(); i++) {
				out << ", " << params[i].decl;
			}
		}
		out << ");" << std::endl;
	}
	void print_initialize(std::ofstream &out, const char *command_prefix, std::string &indent_string) {
		out << indent_string << "" << type_decl << " (*" << command_prefix << name << ")(";
		if (params.size()) {
			out << params[0].decl;
			for(int i = 1; i < params.size(); i++) {
				out << ", " << params[i].decl;
			}
		}
		out << ") = NULL;" << std::endl;
	}

	void print_load(std::ofstream &out, const char *command_prefix, bool include_prefix, std::string &indent_string) {
		out << indent_string << (include_prefix ? command_prefix : "") << name << " = (" << type_decl << " (*)(";
		if (params.size()) {
			out << params[0].decl;
			for(int i = 1; i < params.size(); i++) {
				out << ", " << params[i].decl;
			}
		}
		out << ") ) LoadProcAddress(\"" << command_prefix << name << "\");" << std::endl;
	}
};
typedef std::shared_ptr<command> command_ptr;

class api {
	friend class enumeration_visitor;
	friend class feature_visitor;
	friend class extension_visitor;
	friend class type_visitor;
	friend class khronos_registry_visitor;

	//Api description
	std::string m_name;
	const char *m_command_prefix;
	const char *m_enumeration_prefix;

	bool m_use_api_namespaces;

	std::set<std::string> m_extensions;
	float m_version;

	//List of all enums and commands
	std::map<std::string, unsigned int> m_enum_map;
	std::vector<enumeration_ptr> m_enumerations;
	std::map<std::string, command_ptr> m_commands;
	std::map<std::string, std::string> m_types;

	//Types enums and commands needed
	std::map<std::string, std::string> m_target_types;
	std::map<std::string, unsigned int> m_target_enums;
	std::map<std::string, command_ptr> m_target_commands;
	std::map<std::string, std::map<std::string, command_ptr> > m_target_extension_commands;

	void include_type(std::string &type);
	void resolve_types();

	void types_declare(std::ofstream &header, std::string &indent_string);
	void namespace_declare(std::ofstream &header, std::string &indent_string);
	void namespace_define(std::ofstream &header, std::string &indent_string);
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

	const std::string &name() {
		return m_name;
	}

	float version() {
		return m_version;
	}

	api(std::string name, float version, bool use_api_namespace, std::set<std::string> &extensions) :
		m_name(name),
		m_version(version),
		m_extensions(extensions),
		m_use_api_namespaces(use_api_namespace)
	{
		if (m_name == "wgl") {
			m_command_prefix = "wgl";
			m_enumeration_prefix = "WGL_";
		} else if (m_name == "glx") {
			m_command_prefix = "glx";
			m_enumeration_prefix = "GLX_";
		} else if (m_name == "gl") {
			m_command_prefix = "gl";
			m_enumeration_prefix = "GL_";
		}
	}
	void bindify(XMLDocument &doc, std::string &header_name, std::ofstream &header_stream, std::ofstream &cpp_stream);
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
			if (elem.Attribute("api") && elem.Attribute("api") != m_api.m_name)
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
			return (m_api.m_name == elem.Attribute("api")) &&
				(elem.FloatAttribute("number") <= m_api.version());
		} else if (tag_stack_test(elem, "require", "feature") || tag_stack_test(elem, "remove", "feature")) {
			return (!elem.Attribute("profile")) || (elem.Attribute("profile") != "core");
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
			m_api.m_target_commands.erase(elem.Attribute("name"));
			return true;
		}
	}
public:
	feature_visitor(api &api) : m_api(api) {}
};

class extension_visitor :  public XMLVisitor
{
	api &m_api;
	std::string m_name;
	bool VisitEnter(const XMLElement &elem, const XMLAttribute *attrib)
	{
		if (tag_stack_test(elem, "extension", "extensions")) {
			m_name = std::string(elem.Attribute("name") + m_api.name().size() + 1);
			if (m_api.m_extensions.count(m_name)) {
				const char *supported = elem.Attribute("supported");
				char *supported_copy = strdup(supported);
				char *saveptr;
				char *token = strtok_r(supported_copy, "|", &saveptr);
				while (token) {
					if (!strcmp(token, m_api.name().c_str())) {
						break;
					}
					token = strtok_r(NULL, "|", &saveptr);
				}
				return (token != NULL);
			}
			return false;
		} else if (tag_stack_test(elem, "require", "extension") || tag_stack_test(elem, "remove", "extension")) {
			return !elem.Attribute("profile") || (elem.Attribute("profile") == "core");
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
	std::string m_type_name;
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
		if (tag_test(elem, "type") && !m_type_name.empty()) {
			m_api.m_types[m_type_name] = m_type_decl;
		}
		return true;
	}
public:
	type_visitor(api &api) : m_api(api) {}
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

void api::types_declare(std::ofstream &header_stream, std::string &indent_string)
{
	for (auto val : m_target_types) {
		header_stream << indent_string << val.second << std::endl;
	}
}

void api::namespace_declare(std::ofstream &header_stream, std::string &indent_string)
{
	if (m_use_api_namespaces) {
		header_stream << indent_string << "namespace " << m_name << " {" << std::endl;
		indent_string.push_back('\t');
	}

	header_stream << indent_string << "enum { " << std::endl;
	header_stream << std::hex;

	for (auto val : m_target_enums) {
		if (m_use_api_namespaces) {
			header_stream << "#pragma push_macro(\"" <<  val.first << "\")" << std::endl;
			header_stream << "#undef " << val.first << std::endl;
			header_stream << indent_string << "" << val.first << " = 0x" << val.second << "," << std::endl;
			header_stream << "#pragma pop_macro(\"" << val.first << "\")" << std::endl;
		} else {
			header_stream << indent_string << "" << m_enumeration_prefix << val.first << " = 0x" << val.second << "," << std::endl;
		}
	}
	header_stream << indent_string << "}; " << std::endl;

	for (auto val : m_target_commands)
		val.second->print_declare(header_stream, m_use_api_namespaces ? "" : m_command_prefix, indent_string);
	for (auto iter : m_target_extension_commands)
		for (auto val : iter.second)
			val.second->print_declare(header_stream, m_use_api_namespaces ? "" : m_command_prefix, indent_string);

	const char *extension_prefix = m_use_api_namespaces ? "" : m_enumeration_prefix;

	for (auto extension : m_extensions)
		header_stream << indent_string << "extern bool " << extension_prefix << extension << ";" << std::endl;

	if (m_use_api_namespaces) {
		header_stream
			<< indent_string << "bool init();" << std::endl;
	} else {
		header_stream
			<< indent_string << "bool init_" << m_name << "();" << std::endl;
	}
	if (m_use_api_namespaces) {
		indent_string.pop_back();
		header_stream << indent_string << "}" << std::endl;
	}
}

void api::namespace_define(std::ofstream &cpp_stream, std::string &indent_string)
{
	if (m_use_api_namespaces) {
		cpp_stream << indent_string << "namespace " << m_name << " {" << std::endl;
		indent_string.push_back('\t');
	}

	const char *command_prefix = m_use_api_namespaces ? "" : m_command_prefix;
	const char *extension_prefix = m_use_api_namespaces ? "" : m_enumeration_prefix;

	for (auto val : m_target_commands)
		val.second->print_initialize(cpp_stream, command_prefix, indent_string);

	for (auto iter : m_target_extension_commands)
		for (auto val : iter.second)
			val.second->print_initialize(cpp_stream, command_prefix, indent_string);

	cpp_stream
		<< indent_string << "static std::set<std::string> supported_extensions;" << std::endl;
	for (auto extension : m_extensions)
		cpp_stream
			<< indent_string << "bool " << extension_prefix << extension << " = false;" << std::endl;

	if (m_use_api_namespaces) {
		cpp_stream
			<< indent_string << "bool init()" << std::endl;
	} else {
		cpp_stream
			<< indent_string << "bool init_" << m_name << "()" << std::endl;
	}
	cpp_stream
		<< indent_string << "{ " << std::endl;

	indent_string.push_back('\t');

	for (auto val : m_target_commands)
		val.second->print_load(cpp_stream, m_command_prefix, !m_use_api_namespaces, indent_string);
	for (auto iter : m_target_extension_commands)
		for (auto val : iter.second)
			val.second->print_load(cpp_stream, m_command_prefix, !m_use_api_namespaces, indent_string);

	//
	// Identify supported gl extensions
	//
	if (m_name == "gl") {
		cpp_stream
			<< indent_string << "GLint extension_count;" << std::endl;
		if (m_use_api_namespaces) {
			cpp_stream
				<< indent_string << "GetIntegerv(NUM_EXTENSIONS, &extension_count);" << std::endl;
		} else {
			cpp_stream
				<< indent_string << "glGetIntegerv(GL_NUM_EXTENSIONS, &extension_count);" << std::endl;
		}
		cpp_stream
			<< indent_string << "for (int i = 0; i < extension_count; i++) {" << std::endl;
		indent_string.push_back('\t');

		if (m_use_api_namespaces) {
			cpp_stream
				<< indent_string << "supported_extensions.insert(std::string((const char *)GetStringi(EXTENSIONS, i) + 3));" << std::endl;
		} else {
			cpp_stream
				<< indent_string << "supported_extensions.insert(std::string((const char *)glGetStringi(GL_EXTENSIONS, i) + 3));" << std::endl;
		}
		indent_string.pop_back();

		cpp_stream
			<< indent_string << "}" << std::endl;
		for (auto extension : m_extensions) {
			//
			// Check if the extension is in the supported extensions list
			//
			cpp_stream
				<< indent_string << ""
				<< extension_prefix << extension
				<< " = (supported_extensions.count(\"" << extension << "\") == 1)";
			//
			// Check if the extension's functions have been found.
			//
			// Note: EXT_direct_state_access extends compatibility profile functions since there is no easy way to determine
			// which functions in this extension are in core or compatibility we will just rely on the extension string to determine
			// support for it
			//
			indent_string.push_back('\t');
			int i = 0;
			if (extension != "EXT_direct_state_access" ) {
				for (auto iter : m_target_extension_commands[extension]) {
					if ((i % 4) == 0) {
						cpp_stream << std::endl << indent_string << "";
					}
					i++;
					cpp_stream << " && " << command_prefix << iter.first;
				}
			}
			cpp_stream << ";" << std::endl;
			indent_string.pop_back();
		}
	} else {
		int i = 0;
		for (auto extension : m_extensions) {
			cpp_stream << indent_string << "" << extension_prefix << extension << " = true";
			indent_string.push_back('\t');
			int i = 0;
			for (auto iter : m_target_extension_commands[extension]) {
				i++;
				if ((i % 4) == 0) {
					cpp_stream << std::endl << indent_string;
				}
				cpp_stream << " && " << command_prefix << iter.first;
			}
			cpp_stream << ";" << std::endl;
			indent_string.pop_back();
		}
	}

	cpp_stream
		<< indent_string << "return true";

	indent_string.push_back('\t');
	int i = 0;
	for (auto val : m_target_commands) {
		i++;
		if ((i % 4) == 0) {
			cpp_stream << std::endl << indent_string << "";
		}
		cpp_stream << " && " << command_prefix << val.first;
	}

	cpp_stream
		<< ";" << std::endl;
	indent_string.pop_back();

	indent_string.pop_back();
	cpp_stream
		<< indent_string << "}" << std::endl;  //init()
	if (m_use_api_namespaces) {
		indent_string.pop_back();
		cpp_stream
			<< indent_string << "}" << std::endl;  //namespace m_name {
	}
}

void api::include_type(std::string &type)
{
	if (!type.empty() && !g_common_gl_typedefs.count(type) && !m_target_types.count(type)) {
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

void api::bindify(XMLDocument &doc, std::string &header_name, std::ofstream &header_stream, std::ofstream &cpp_stream)
{
	khronos_registry_visitor registry_visitor(doc, *this);
	doc.Accept(&registry_visitor);
	std::string indent_string;

	resolve_types();

	header_stream
		<< "#ifndef GL_BINDIFY_" << m_name  << "_H"  << std::endl
		<< "#define GL_BINDIFY_" << m_name  << "_H" << std::endl;

	if (m_name == "glx") {
		header_stream
			<< "#include <X11/Xlib.h>" << std::endl
			<< "#include <X11/Xutil.h>" << std::endl;
	} else if (m_name == "wgl") {
		header_stream
			<< "#include <windows.h>" << std::endl;
	}
	header_stream
		<< "#include <stdint.h>" << std::endl
		<< "#include <stddef.h>" << std::endl;

	header_stream
		<< "namespace glbindify {" << std::endl;
	indent_string.push_back('\t');

	//
	//We need to include these typedefs even for glx and wgl since they are referenced there without being defined
	//
	header_stream
		<< "#ifndef GLBINDIFY_COMMON_GL_TYPEDEFS" << std::endl
		<< "#define GLBINDIFY_COMMON_GL_TYPEDEFS" << std::endl
		<< indent_string << "typedef unsigned int GLenum;" << std::endl
		<< indent_string << "typedef unsigned char GLboolean;" << std::endl
		<< indent_string << "typedef unsigned int GLbitfield;" << std::endl
		<< indent_string << "typedef signed char GLbyte;" << std::endl
		<< indent_string << "typedef short GLshort;" << std::endl
		<< indent_string << "typedef int GLint;" << std::endl
		<< indent_string << "typedef unsigned char GLubyte;" << std::endl
		<< indent_string << "typedef unsigned short GLushort;" << std::endl
		<< indent_string << "typedef unsigned int GLuint;" << std::endl
		<< indent_string << "typedef int GLsizei;" << std::endl
		<< indent_string << "typedef float GLfloat;" << std::endl
		<< indent_string << "typedef double GLdouble;" << std::endl
		<< indent_string << "typedef ptrdiff_t GLintptr;" << std::endl
		<< indent_string << "typedef ptrdiff_t GLsizeiptr;" << std::endl
		<< "#endif" << std::endl;

	types_declare(header_stream, indent_string);

	namespace_declare(header_stream, indent_string);

	indent_string.pop_back();

	header_stream << "}" << std::endl; //namespace glbindify {

	header_stream << "#endif" << std::endl;

	indent_string.clear();

	cpp_stream
		<< "#include \"" << header_name << "\"" << std::endl
		<< "#include <set>" << std::endl
		<< "#include <string>" << std::endl
		<< "#ifndef _WIN32" << std::endl
		<< "extern \"C\" {" << std::endl
		<< "\textern void (*glXGetProcAddress(const unsigned char *))(void);" << std::endl
		<< "}" << std::endl
		<< "static inline void (*LoadProcAddress(const char *name))(void) {" << std::endl
		<< "\treturn (*glXGetProcAddress)((const unsigned char *)name);" << std::endl
		<< "}" << std::endl
		<< "#include <stdio.h>" << std::endl
		<< "#else" << std::endl
		<< "#include <windows.h>" << std::endl
		<< "#include <wingdi.h>" << std::endl
		<< "#include <stdio.h>" << std::endl
		<< "static PROC LoadProcAddress(const char *name) {" << std::endl
		<< "\tPROC addr = wglGetProcAddress((LPCSTR)name);" << std::endl
		<< "\tif (addr) return addr;" << std::endl
		<< "\telse return (PROC)GetProcAddress(GetModuleHandleA(\"OpenGL32.dll\"), (LPCSTR)name);" << std::endl
		<< "}" << std::endl
		<< "#endif" << std::endl;

	cpp_stream << "namespace glbindify {" << std::endl;

	indent_string.push_back('\t');

	namespace_define(cpp_stream, indent_string);

	indent_string.pop_back();
	cpp_stream << "}" << std::endl;
}

static void usage(const char *program_name)
{
	std::cout << "Usage: " << program_name << " [-a api_name] [-n] [-v api_version] [-e extension] [-e extension] ..." << std::endl;
}

int main(int argc, char **argv)
{
	XMLDocument doc;
	XMLError err;

	static struct option options [] = {
		{"api"       , 1, 0, 'a' },
		{"extension" , 1, 0, 'e' },
		{"version"   , 1, 0, 'v' },
		{"api-namespaces"   , 1, 0, 'n' },
		{"help"      , 1, 0, 'h' }
	};

	const char *api_name = "gl";
	float api_version = 3.3;
	bool use_api_namespace = false;
	std::set<std::string> extensions;

	while (1) {
		int option_index;
		int c = getopt_long(argc, argv, "a:e:v:n", options, &option_index);
		if (c == -1)
			break;
		switch (c) {
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
			api_version = atof(optarg);
			break;
		case 'h':
			usage(argv[0]);
			exit(0);
			break;
		}
	}

	std::cout << "Generating bindings for " << api_name << " version " << api_version << std::endl;
	api api(api_name, api_version, use_api_namespace, extensions);

	std::string in_filename = std::string(PKGDATADIR) + "/" + api.name() + ".xml";
	err = doc.LoadFile(in_filename.c_str());
	if (err != XML_NO_ERROR) {
		std::cout << "Error loading khronos registry file " << in_filename << std::endl;
		exit(-1);
	}

	std::ofstream header_stream;
	std::ofstream cpp_stream;
	std::string header_name = "glbindify_" + api.name() + ".hpp";
	std::string cpp_name = "glbindify_" + api.name() + ".cpp";
	header_stream.open(header_name);
	cpp_stream.open(cpp_name);

	std::cout << "Writing bindings to " << cpp_name << " and " << header_name << std::endl;
	api.bindify(doc, header_name, header_stream, cpp_stream);

	return 0;
}
