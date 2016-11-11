#pragma once
#include "definitions.h"
#include <vector>
#include <iostream>
#include <fstream>
#include <map>
#include <string>

typedef std::map <string, string> Data;

class ConfigurationManager
{
	Data rawData;
	RIOMainConfig rioMainConfig;
	std::vector<ServiceData> services;

public:
	ConfigurationManager();
	~ConfigurationManager();

	int LoadConfiguration(string filename);
	RIOMainConfig GetRIOConfiguration();
	std::vector<ServiceData> GetServiceConfiguration();

};

