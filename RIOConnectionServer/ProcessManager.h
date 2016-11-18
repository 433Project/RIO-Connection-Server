#pragma once
#include "definitions.h"
#include <vector>

struct ExtendedRioBuf;


struct AddressInfo
{
	SRC_DEST_TYPE srcType;
	int srcCode;
	SRC_DEST_TYPE dstType;
	int dstCode;
};

class ProcessManager
{
public:
	std::vector<Instruction>*GetInstructions(ExtendedRioBuf* data);

	ProcessManager();
	~ProcessManager();

private:

	AddressInfo GetManagedInfo(AddressInfo receivedInfo, const ExtendedRioBuf& data);

private:
	int matchingServerCount;

};

