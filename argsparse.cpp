#include "argsparse.h"

optionsParser& optionsParser::instance()
{
	static optionsParser s_instance;
	return s_instance;
}

void optionsParser::addOption(const char* longName, const char* description)
{
	m_argInfo.insert(std::make_tuple(std::string(longName), std::string(description)));
	m_validArgs.insert(longName);
}

void optionsParser::printHelp(FILE* fdOut) const
{
	char buf[4096];

	for(auto iter : m_argInfo)
	{
		sprintf(buf, "\t%s  %s\n", std::get<0>(iter).c_str(), std::get<1>(iter).c_str());
		fprintf(fdOut, "%s", buf);
	}
}

void optionsParser::parseArgs(int argc, char** argv)
{
	for(int i = 0; i < argc - 1; i++)
	{
		auto iter = m_validArgs.find(argv[i]);
		if(iter == m_validArgs.end())
		{
			continue;
		}
		m_seenArgs[argv[i]] = argv[i + 1];
	}
}

const char* optionsParser::getOption(const char* longName, const char* defaultOut) const
{
	auto iter = m_seenArgs.find(longName);
	if(iter == m_seenArgs.end())
	{
		return defaultOut;
	}

	return iter->second;
}
