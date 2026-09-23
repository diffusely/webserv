#include "Config.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>

Config::Config(const std::string &path)
	: _port(-1), _index("index.html"), _pos(0)
{
	std::ifstream file(path.c_str());
	if (!file.is_open())
		throw std::runtime_error("cannot open config file: " + path);

	std::ostringstream content;
	content << file.rdbuf();
	tokenize(content.str());

	expect("server");
	parseServerBlock();

	if (_pos != _tokens.size())
		throw std::runtime_error("config: unexpected '" + _tokens[_pos] + "' after server block");
	if (_port == -1)
		throw std::runtime_error("config: missing 'listen'");
	if (_root.empty())
		throw std::runtime_error("config: missing 'root'");
}

void Config::tokenize(const std::string &content)
{
	std::string current;

	for (size_t i = 0; i < content.size(); i++) {
		char c = content[i];

		if (c == '#') {
			while (i < content.size() && content[i] != '\n')
				i++;
			c = '\n';
		}

		if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '{' || c == '}' || c == ';') {
			if (!current.empty()) {
				_tokens.push_back(current);
				current.clear();
			}
			if (c == '{' || c == '}' || c == ';')
				_tokens.push_back(std::string(1, c));
		} else {
			current += c;
		}
	}

	if (!current.empty())
		_tokens.push_back(current);
}

void Config::parseServerBlock()
{
	expect("{");

	while (true) {
		const std::string &token = next();

		if (token == "}")
			return;
		if (token == "location")
			skipBlock();
		else
			parseDirective(token);
	}
}

void Config::parseDirective(const std::string &name)
{
	std::vector<std::string> args;

	while (true) {
		const std::string &token = next();

		if (token == ";")
			break;
		if (token == "{" || token == "}")
			throw std::runtime_error("config: missing ';' after '" + name + "'");
		args.push_back(token);
	}

	if (name == "server_name")
		return;

	if (args.size() != 1)
		throw std::runtime_error("config: '" + name + "' takes exactly one value");

	if (name == "listen") {
		const std::string &value = args[0];
		long port = 0;

		for (size_t i = 0; i < value.size(); i++) {
			if (value[i] < '0' || value[i] > '9' || port > 65535)
				throw std::runtime_error("config: invalid port '" + value + "'");
			port = port * 10 + (value[i] - '0');
		}
		if (value.empty() || port < 1 || port > 65535)
			throw std::runtime_error("config: invalid port '" + value + "'");
		_port = port;
	} else if (name == "root") {
		_root = args[0];
	} else if (name == "index") {
		_index = args[0];
	} else {
		throw std::runtime_error("config: unknown directive '" + name + "'");
	}
}

void Config::skipBlock()
{
	while (next() != "{")
		;

	int depth = 1;
	while (depth > 0) {
		const std::string &token = next();

		if (token == "{")
			depth++;
		else if (token == "}")
			depth--;
	}
}

const std::string &Config::next()
{
	if (_pos >= _tokens.size())
		throw std::runtime_error("config: unexpected end of file");
	return _tokens[_pos++];
}

void Config::expect(const std::string &token)
{
	const std::string &got = next();

	if (got != token)
		throw std::runtime_error("config: expected '" + token + "', got '" + got + "'");
}

int Config::getPort() const
{
	return _port;
}

const std::string &Config::getRoot() const
{
	return _root;
}

const std::string &Config::getIndex() const
{
	return _index;
}
