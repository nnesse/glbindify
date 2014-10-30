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
	void print_declare(std::ofstream &out, const std::string &api_name) {
		std::string trunc_name = name.substr(api_name.size());
		out << "\t\textern " << type_decl << " (*" << trunc_name << ")(";
		if (params.size()) {
			out << params[0].decl;
			for(int i = 1; i < params.size(); i++) {
				out << ", " << params[i].decl;
			}
		}
		out << ");" << std::endl;
	}
	void print_initialize(std::ofstream &out, const std::string &api_name) {
		std::string trunc_name = name.substr(api_name.size());
		out << "\t\t" << type_decl << " (*" << trunc_name << ")(";
		if (params.size()) {
			out << params[0].decl;
			for(int i = 1; i < params.size(); i++) {
				out << ", " << params[i].decl;
			}
		}
		out << ") = NULL;" << std::endl;
	}

	void print_load(std::ofstream &out, const std::string &api_name) {
		std::string trunc_name = name.substr(api_name.size());
		out << "\t\t\t" << trunc_name << " = (" << type_decl << " (*)(";
		if (params.size()) {
			out << params[0].decl;
			for(int i = 1; i < params.size(); i++) {
				out << ", " << params[i].decl;
			}
		}
		out << ") ) LoadProcAddress(\"" << name << "\");" << std::endl;
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

	void types_declare(std::ofstream &header);
	void namespace_declare(std::ofstream &header);
	void namespace_define(std::ofstream &header);

	bool is_in_namespace(const std::string &str) {
		return (m_name == "wgl" && str.substr(0, 3) == "wgl") ||
			(m_name == "glx" && str.substr(0, 3) == "glX") ||
			(m_name == "gl" && str.substr(0, 2) == "gl");
	}
public:

	const std::string &name() {
		return m_name;
	}

	float version() {
		return m_version;
	}

	api(std::string name, float version, std::set<std::string> &extensions) :
		m_name(name),
		m_version(version),
		m_extensions(extensions)
	{
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
			m_data->name = text.Value();
		} else if (parent_tag_stack_test(text, "ptype", "proto")) {
			m_data->type = text.Value();
			m_data->type_decl += text.Value();
		}
		return true;
	}

	virtual bool visit_enter(const XMLElement &elem, const XMLAttribute *attrib)
	{
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
			if(elem.Attribute("api") && elem.Attribute("api") != m_api.m_name)
				return false;
			unsigned int val;
			int ret = sscanf(elem.Attribute("value"), "0x%x", &val);
			if (!ret)
				ret = sscanf(elem.Attribute("value"), "%d", &val);
			const char *name_c = elem.Attribute("name");
			if (ret) {
				m_data->enum_map[name_c] = val;
				m_api.m_enum_map[name_c] = val;
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
			m_api.m_target_enums[elem.Attribute("name")] = m_api.m_enum_map[elem.Attribute("name")];
			return true;
		} else if (tag_stack_test(elem, "enum", "remove")) {
			m_api.m_target_enums.erase(elem.Attribute("name"));
			return true;
		} else if (tag_stack_test(elem, "command", "require")) {
			m_api.m_target_commands[elem.Attribute("name")] = m_api.m_commands[elem.Attribute("name")];
			return true;
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
			m_api.m_target_enums[elem.Attribute("name")] = m_api.m_enum_map[elem.Attribute("name")];
			return true;
		} else if (tag_stack_test(elem, "command", "require")) {
			m_api.m_target_extension_commands[m_name][elem.Attribute("name")] = m_api.m_commands[elem.Attribute("name")];
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

void api::types_declare(std::ofstream &header_stream)
{
	for (auto val : m_target_types) {
		header_stream << "\t" << val.second << std::endl;
	}
}

void api::namespace_declare(std::ofstream &header_stream)
{
	header_stream << "\tnamespace " << m_name << " {" << std::endl;
	header_stream << "\t\tenum { " << std::endl;
	header_stream << std::hex;
	for (auto val : m_target_enums) {
		std::string trunc_name = val.first.substr(m_name.size() + 1);
		header_stream << "#pragma push_macro(\"" <<  trunc_name << "\")" << std::endl;
		header_stream << "#undef " << trunc_name << std::endl;
		header_stream << "\t\t\t" << trunc_name << "= 0x" << val.second << "," << std::endl;
		header_stream << "#pragma pop_macro(\"" << trunc_name << "\")" << std::endl;
	}
	header_stream << "\t\t}; " << std::endl;

	for (auto val : m_target_commands)
		if (is_in_namespace(val.first))
			val.second->print_declare(header_stream, m_name);
	for (auto iter : m_target_extension_commands)
		for (auto val : iter.second)
			if (is_in_namespace(val.first))
				val.second->print_declare(header_stream, m_name);

	for (auto extension : m_extensions)
		header_stream << "\t\textern bool " << extension << ";" << std::endl;

	header_stream << "\t\tbool init();" << std::endl;
	header_stream << "\t}" << std::endl;
}

void api::namespace_define(std::ofstream &cpp_stream)
{
	cpp_stream << "\tnamespace " << m_name << " {" << std::endl;

	for (auto val : m_target_commands)
		if (is_in_namespace(val.first))
			val.second->print_initialize(cpp_stream, m_name);

	for (auto iter : m_target_extension_commands)
		for (auto val : iter.second)
			if (is_in_namespace(val.first))
				val.second->print_initialize(cpp_stream, m_name);

	cpp_stream
		<< "\t\tstatic std::set<std::string> supported_extensions;" << std::endl;
	for (auto extension : m_extensions)
		cpp_stream
			<< "\t\tbool " << extension << " = false;" << std::endl;

	cpp_stream <<
		"\t\tbool init()" << std::endl <<
		"\t\t{ " << std::endl;

	for (auto val : m_target_commands)
		if (is_in_namespace(val.first))
			val.second->print_load(cpp_stream, m_name);
	for (auto iter : m_target_extension_commands)
		for (auto val : iter.second)
			if (is_in_namespace(val.first))
				val.second->print_load(cpp_stream, m_name);

	//
	// Identify supported gl extensions
	//
	if (m_name == "gl") {
		cpp_stream
			<< "\t\t\tGLint extension_count;" << std::endl
			<< "\t\t\tGetIntegerv(NUM_EXTENSIONS, &extension_count);" << std::endl
			<< "\t\t\tfor (int i = 0; i < extension_count; i++) {" << std::endl
			<< "\t\t\t\tsupported_extensions.insert(std::string((const char *)GetStringi(EXTENSIONS, i) + 3));" << std::endl
			<< "\t\t\t}" << std::endl;
		for (auto extension : m_extensions) {
			//
			// Check if the extension is in the supported extensions list
			//
			cpp_stream
				<< "\t\t\t"
				<< extension
				<< " = (supported_extensions.count(\"" << extension << "\") == 1)";
			//
			// Check if the extension's functions have been found.
			//
			// Note: EXT_direct_state_access extends compatibility profile functions since there is no easy way to determine
			// which functions in this extension are in core or compatibility we will just rely on the extension string to determine
			// support for it
			//
			int i = 0;
			if (extension != "EXT_direct_state_access" ) {
				for (auto iter : m_target_extension_commands[extension]) {
					if ((i % 4) == 0) {
						cpp_stream << std::endl << "\t\t\t\t";
					}
					i++;
					cpp_stream << " && " << iter.first.substr(m_name.size());
				}
			}
			cpp_stream << ";" << std::endl;
		}
	} else {
		int i = 0;
		for (auto extension : m_extensions) {
			cpp_stream << "\t\t\t" << extension << " = true";
			int i = 0;
			for (auto iter : m_target_extension_commands[extension]) {
				i++;
				if ((i % 4) == 0) {
					cpp_stream << std::endl << "\t\t\t\t";
				}
				cpp_stream << " && " << iter.first.substr(m_name.size());
			}
			cpp_stream << ";" << std::endl;
		}
	}

	cpp_stream
		<< "\t\t\treturn true";

	int i = 0;
	for (auto val : m_target_commands) {
		if (is_in_namespace(val.first)) {
			cpp_stream << " && " << val.first.substr(m_name.size());
			i++;
			if ((i % 4) == 0) {
				cpp_stream << std::endl << "\t\t\t\t";
			}
		}
	}

	cpp_stream
		<< ";" << std::endl
		<< "\t\t}" << std::endl  //init()
		<< "\t}" << std::endl;  //namespace m_name {
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

	//
	//We need to include these typedefs even for glx and wgl since they are referenced there without being defined
	//
	header_stream
		<< "#ifndef GLBINDIFY_COMMON_GL_TYPEDEFS" << std::endl
		<< "#define GLBINDIFY_COMMON_GL_TYPEDEFS" << std::endl
		<< "\ttypedef unsigned int GLenum;" << std::endl
		<< "\ttypedef unsigned char GLboolean;" << std::endl
		<< "\ttypedef unsigned int GLbitfield;" << std::endl
		<< "\ttypedef signed char GLbyte;" << std::endl
		<< "\ttypedef short GLshort;" << std::endl
		<< "\ttypedef int GLint;" << std::endl
		<< "\ttypedef unsigned char GLubyte;" << std::endl
		<< "\ttypedef unsigned short GLushort;" << std::endl
		<< "\ttypedef unsigned int GLuint;" << std::endl
		<< "\ttypedef int GLsizei;" << std::endl
		<< "\ttypedef float GLfloat;" << std::endl
		<< "\ttypedef double GLdouble;" << std::endl
		<< "\ttypedef ptrdiff_t GLintptr;" << std::endl
		<< "\ttypedef ptrdiff_t GLsizeiptr;" << std::endl
		<< "#endif" << std::endl;

	types_declare(header_stream);

	namespace_declare(header_stream);

	header_stream << "}" << std::endl; //namespace glbindify {

	header_stream << "#endif" << std::endl;

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

	namespace_define(cpp_stream);

	cpp_stream << "}" << std::endl;
}

static void usage(const char *program_name)
{
	std::cout << "Usage: " << program_name << " [-a api_name] [-v api_version] [-e extension] [-e extension] ..." << std::endl;
}

int main(int argc, char **argv)
{
	XMLDocument doc;
	XMLError err;

	static struct option options [] = {
		{"api"       , 1, 0, 'a' },
		{"extension" , 1, 0, 'e' },
		{"version"   , 1, 0, 'v' },
		{"help"      , 1, 0, 'h' }
	};

	const char *api_name = "gl";
	float api_version = 3.3;
	std::set<std::string> extensions;

	while (1) {
		int option_index;
		int c = getopt_long(argc, argv, "a:e:v:", options, &option_index);
		if (c == -1)
			break;
		switch (c) {
		case 'a':
			api_name = optarg;
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
	api api(api_name, api_version, extensions);

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
