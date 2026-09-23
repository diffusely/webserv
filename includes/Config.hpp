#pragma once

#include <string>
#include <vector>

class Config
{
public:
	Config(const std::string &path);

	int getPort() const;
	const std::string &getRoot() const;
	const std::string &getIndex() const;

private:
	int _port;
	std::string _root;
	std::string _index;

	std::vector<std::string> _tokens;
	size_t _pos;

	void tokenize(const std::string &content);
	void parseServerBlock();
	void parseDirective(const std::string &name);
	void skipBlock();

	const std::string &next();
	void expect(const std::string &token);
};
