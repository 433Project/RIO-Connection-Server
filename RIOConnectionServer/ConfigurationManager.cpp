#include "stdafx.h"
#include "ConfigurationManager.h"


ConfigurationManager::ConfigurationManager()
{
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

		//if (string::compare(key)

	}

	file.close();
	return 0;
}

RIOMainConfig ConfigurationManager::GetRIOConfiguration() {


	return rioMainConfig;
}

std::vector<ServiceData> ConfigurationManager::GetServiceConfiguration() {

	return services;
}