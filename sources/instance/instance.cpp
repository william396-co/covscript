/*
* Covariant Script Instance
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
* Copyright (C) 2018 Michael Lee(李登淳)
* Email: mikecovlee@163.com
* Github: https://github.com/mikecovlee
*/
#include <covscript/instance.hpp>
#include <covscript/statement.hpp>
#include <covscript/codegen.hpp>

namespace cs {
	const std::string &statement_base::get_file_path() const noexcept
	{
		return context->file_path;
	}

	const std::string &statement_base::get_package_name() const noexcept
	{
		return context->package_name;
	}

	const std::string &statement_base::get_raw_code() const noexcept
	{
		return context->file_buff.at(line_num - 1);
	}

	extension_t instance_type::import(const std::string &path, const std::string &name)
	{
		std::vector<std::string> collection;
		{
			std::string tmp;
			for (auto &ch:path) {
				if (ch == cs::path_delimiter) {
					collection.push_back(tmp);
					tmp.clear();
				}
				else
					tmp.push_back(ch);
			}
			collection.push_back(tmp);
		}
		for (auto &it:collection) {
			std::string package_path = it + path_separator + name;
			if (std::ifstream(package_path + ".csp")) {
				refers.emplace_front();
				instance_type &instance = refers.front();
				instance.compile(package_path + ".csp");
				instance.interpret();
				context_t rt = instance.context;
				if (rt->package_name.empty())
					throw runtime_error("Target file is not a package.");
				if (rt->package_name != name)
					throw runtime_error("Package name is different from file name.");
				return std::make_shared<extension_holder>(rt->instance->storage.get_global());
			}
			else if (std::ifstream(package_path + ".cse"))
				return std::make_shared<extension_holder>(package_path + ".cse");
		}
		throw fatal_error("No such file or directory.");
	}

	void instance_type::compile(const translator_type& translator, const std::string &path)
	{
		context->file_path = path;
		// Read from file
		std::deque<char> buff;
		std::ifstream in(path);
		if (!in.is_open())
			throw fatal_error(path + ": No such file or directory");
		for (int ch = in.get(); ch != EOF; ch = in.get())
			buff.push_back(ch);
		std::deque<std::deque<token_base *>> ast;
		// Compile
		context->compiler->build_ast(buff, ast);
		context->translator->translate(ast, statements, true);
		// Mark Constants
		context->compiler->mark_constant();
	}

	void instance_type::interpret()
	{
		// Run the instruction
		for (auto &ptr:statements) {
			try {
				ptr->run();
			}
			catch (const lang_error &le) {
				throw fatal_error(std::string("Uncaught exception: ") + le.what());
			}
			catch (const cs::exception &e) {
				throw e;
			}
			catch (const std::exception &e) {
				throw exception(ptr->get_line_num(), ptr->get_file_path(), ptr->get_raw_code(), e.what());
			}
		}
	}

	void instance_type::dump_ast(std::ostream &stream)
	{
		stream << "< Covariant Script AST Dump >\n< BeginMetaData >\n< Version: " << version << " >\n< STD Version: "
		       << std_version
		       << " >\n< Output Precision: " << *output_precision_ref << " >\n< Import Path: \"" << import_path
		       << "\" >\n";
#ifdef COVSCRIPT_PLATFORM_WIN32
		stream << "< Platform: Win32 >\n";
#else
		stream<<"< Platform: Unix >\n";
#endif
		stream << "< EndMetaData >\n";
		for (auto &ptr:statements)
			ptr->dump(stream);
		stream << std::flush;
	}

	void repl::run(const string &code)
	{
		if (code.empty())
			return;
		std::deque<char> buff;
		for (auto &ch:code)
			buff.push_back(ch);
		statement_base *statement = nullptr;
		try {
			std::deque<token_base *> line;
			context->instance->compiler->build_line(buff, line);
			method_base *m = context->instance->translator.match(line);
			switch (m->get_type()) {
			case method_types::null:
				throw runtime_error("Null type of grammar.");
				break;
			case method_types::single: {
				if (level > 0) {
					if (m->get_target_type() == statement_types::end_) {
						context->instance->storage.remove_set();
						context->instance->storage.remove_domain();
						--level;
					}
					if (level == 0) {
						statement = method->translate(tmp);
						tmp.clear();
						method = nullptr;
					}
					else {
						m->preprocess({line});
						tmp.push_back(line);
					}
				}
				else {
					if (m->get_target_type() == statement_types::end_)
						throw runtime_error("Hanging end statement.");
					else {
						m->preprocess({line});
						statement = m->translate({line});
					}
				}
			}
			break;
			case method_types::block: {
				if (level == 0)
					method = m;
				++level;
				context->instance->storage.add_domain();
				context->instance->storage.add_set();
				m->preprocess({line});
				tmp.push_back(line);
			}
			break;
			case method_types::jit_command:
				m->translate({line});
				break;
			}
			if (statement != nullptr)
				statement->repl_run();
		}
		catch (const lang_error &le) {
			reset_status();
			throw fatal_error(std::string("Uncaught exception: ") + le.what());
		}
		catch (const cs::exception &e) {
			reset_status();
			throw e;
		}
		catch (const std::exception &e) {
			reset_status();
			throw exception(line_num, context->file_path, code, e.what());
		}
		context->instance->compiler.mark_constant();
	}

	void repl::exec(const string &code)
	{
		// Preprocess
		++line_num;
		int mode = 0;
		for (auto &ch:code) {
			if (mode == 0) {
				if (!std::isspace(ch)) {
					switch (ch) {
					case '#':
						context->file_buff.emplace_back();
						return;
					case '@':
						mode = 1;
						break;
					default:
						mode = -1;
					}
				}
			}
			else if (mode == 1) {
				if (!std::isspace(ch))
					cmd_buff.push_back(ch);
			}
			else
				break;
		}
		switch (mode) {
		default:
			break;
		case 0:
			return;
		case 1:
			if (cmd_buff == "begin" && !multi_line) {
				multi_line = true;
				context->file_buff.emplace_back();
			}
			else if (cmd_buff == "end" && multi_line) {
				multi_line = false;
				this->run(line_buff);
				line_buff.clear();
			}
			else
				throw exception(line_num, context->file_path, cmd_buff, "Wrong grammar for preprocessor command.");
			cmd_buff.clear();
			return;
		}
		if (multi_line) {
			context->file_buff.emplace_back();
			line_buff.append(code);
		}
		else {
			context->file_buff.push_back(code);
			this->run(code);
		}
	}
}