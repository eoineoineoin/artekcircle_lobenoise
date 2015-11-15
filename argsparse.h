#pragma once
#include <set>
#include <map>
#include <string>

struct optionsParser
{
	static optionsParser& instance();
	void addOption(const char* name, const char* description);
	void printHelp(FILE* fdOut) const;
	void parseArgs(int argc, char** argv);
	const char* getOption(const char* longName, const char* defaultOut) const;

	std::set<std::tuple<std::string,std::string>> m_argInfo;
	std::set<std::string> m_validArgs;
	std::map<std::string, const char*> m_seenArgs;
};

