#include "stdafx.h"
#include "ConfigurationManager.h"

//enum CONFIGURATION_COMMANDS {
//	BUFFER_SIZE = 0,
//	DEQUEUE_COUNT,
//	NUM_THREADS,
//	SPINCOUNT,
//
//	NEW_SERVICE = 10,
//	SERVICE_TYPE,
//	SERVICE_CODE,
//	SERVICE_PORT,
//	SERVICE_MAX_CLIENTS,
//	SERVICE_MAX_ACCEPTS,
//	SERVICE_RQ_MAX_RECEIVES,
//	SERVICE_RQ_MAX_SENDS
//	SERVICE_ADDRESS_REQUIRED
//};


ConfigurationManager::ConfigurationManager()
{
	configMap.insert(std::pair<string, CONFIGURATION_COMMANDS>("BUFFER_SIZE", BUFFER_SIZE));
	configMap.insert(std::pair<string, CONFIGURATION_COMMANDS>("DEQUEUE_COUNT", DEQUEUE_COUNT));
	configMap.insert(std::pair<string, CONFIGURATION_COMMANDS>("NUM_THREADS", NUM_THREADS));
	configMap.insert(std::pair<string, CONFIGURATION_COMMANDS>("SPINCOUNT", SPINCOUNT));
	configMap.insert(std::pair<string, CONFIGURATION_COMMANDS>("NEW_SERVICE", NEW_SERVICE));
	configMap.insert(std::pair<string, CONFIGURATION_COMMANDS>("SERVICE_TYPE", SERVICE_TYPE));
	configMap.insert(std::pair<string, CONFIGURATION_COMMANDS>("SERVICE_CODE", SERVICE_CODE));
	configMap.insert(std::pair<string, CONFIGURATION_COMMANDS>("SERVICE_PORT", SERVICE_PORT));
	configMap.insert(std::pair<string, CONFIGURATION_COMMANDS>("SERVICE_MAX_CLIENTS", SERVICE_MAX_CLIENTS));
	configMap.insert(std::pair<string, CONFIGURATION_COMMANDS>("SERVICE_MAX_ACCEPTS", SERVICE_MAX_ACCEPTS));
	configMap.insert(std::pair<string, CONFIGURATION_COMMANDS>("SERVICE_RQ_MAX_RECEIVES", SERVICE_RQ_MAX_RECEIVES));
	configMap.insert(std::pair<string, CONFIGURATION_COMMANDS>("SERVICE_RQ_MAX_SENDS", SERVICE_RQ_MAX_SENDS));
	configMap.insert(std::pair<string, CONFIGURATION_COMMANDS>("SERVICE_ADDRESS_REQUIRED", SERVICE_ADDRESS_REQUIRED));
}


ConfigurationManager::~ConfigurationManager()
{
}

/*

Sample Config file style:

#Comment
#Comment

value = 1



*/


int ConfigurationManager::LoadConfiguration(string filename) {
	ifstream file (filename);	//Open the specified file

	if (!file.is_open()) {
		cout << "ERROR - Unable to open specified configuration file." << endl;
		return -1;
	}

	string s, key, value;
	int valueConverted;
	int serviceCount = 0;
	ServiceData* serviceData = new ServiceData();

	//Read through each line of the file and extract information
	while (getline(file, s)) {

		// ####################################
		// LOGIC FOR EXTRACTING KEY/VALUE PAIRS
		// ####################################
		string::size_type begin = s.find_first_not_of(" \f\t\v");

		if (begin == string::npos) {	//Skip line if blank
			continue;
		}

		if (string("#;").find(s[begin]) != string::npos) {	//Skip comment lines
			continue;
		}

		// KEY
		string::size_type end = s.find('=', begin);
		key = s.substr(begin, end - begin);

		key.erase(key.find_last_not_of(" \f\t\v") + 1);

		if (key.empty()) {
			continue;
		}

		// VALUE
		begin = s.find_first_not_of(" \f\n\r\t\v", end + 1);
		end = s.find_last_not_of(" \f\n\r\t\v") + 1;

		value = s.substr(begin, end - begin);

		valueConverted = atoi(value.c_str());

		// ######################################
		// LOGIC FOR INTERPRETING KEY/VALUE PAIRS
		// ######################################

		if (configMap.find(key) == configMap.end()) {
			continue;
		}

		switch (configMap.find(key)->second) {

		case BUFFER_SIZE:
			rioMainConfig.bufferSize = valueConverted;
			break;

		case DEQUEUE_COUNT:
			rioMainConfig.dequeueCount = valueConverted;
			break;

		case NUM_THREADS:
			rioMainConfig.numThreads = valueConverted;
			break;

		case SPINCOUNT:
			rioMainConfig.spinCount = valueConverted;
			break;

		case NEW_SERVICE:
			if (serviceCount > 0) {
				services.push_back(*serviceData);
			}
			serviceCount++;
			serviceData = new ServiceData();
			break;

		case SERVICE_TYPE:
			serviceData->serviceType = (SocketType)valueConverted;
			break;

		case SERVICE_CODE:
			serviceData->serviceCode = valueConverted;
			break;

		case SERVICE_PORT:
			serviceData->servicePort = valueConverted;
			break;

		case SERVICE_MAX_CLIENTS:
			serviceData->serviceMaxClients = valueConverted;
			break;

		case SERVICE_MAX_ACCEPTS:
			serviceData->serviceMaxAccepts = valueConverted;
			break;

		case SERVICE_RQ_MAX_RECEIVES:
			serviceData->serviceRQMaxReceives = valueConverted;
			break;

		case SERVICE_RQ_MAX_SENDS:
			serviceData->serviceRQMaxSends = valueConverted;
			break;

		case SERVICE_ADDRESS_REQUIRED:
			if (valueConverted == 1) {
				serviceData->isAddressRequired = true;
			}
			else {
				serviceData->isAddressRequired = false;
			}
			break;
		}
	}

	services.push_back(*serviceData);

	file.close();
	return 0;
}

RIOMainConfig ConfigurationManager::GetRIOConfiguration() {


	return rioMainConfig;
}

std::vector<ServiceData> ConfigurationManager::GetServiceConfiguration() {

	return services;
}