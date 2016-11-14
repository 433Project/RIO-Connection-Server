#pragma once
#include "definitions.h"
#include <vector>
#include <iostream>
#include <fstream>
#include <map>
#include <unordered_map>
#include <string>

typedef std::map <string, string> Data;

enum CONFIGURATION_COMMANDS {
	BUFFER_SIZE = 0,
	DEQUEUE_COUNT,
	NUM_THREADS,
	SPINCOUNT,

	NEW_SERVICE = 10,
	SERVICE_TYPE,
	SERVICE_CODE,
	SERVICE_PORT,
	SERVICE_MAX_CLIENTS,
	SERVICE_MAX_ACCEPTS,
	SERVICE_RQ_MAX_RECEIVES,
	SERVICE_RQ_MAX_SENDS,
	SERVICE_ADDRESS_REQUIRED
};

typedef std::unordered_map <string, CONFIGURATION_COMMANDS> ConfigurationMapping;

class ConfigurationManager
{
	Data rawData;
	RIOMainConfig rioMainConfig;
	std::vector<ServiceData> services;
	ConfigurationMapping configMap;

public:
	ConfigurationManager();
	~ConfigurationManager();

	int LoadConfiguration(string filename);
	RIOMainConfig GetRIOConfiguration();
	std::vector<ServiceData> GetServiceConfiguration();

};

