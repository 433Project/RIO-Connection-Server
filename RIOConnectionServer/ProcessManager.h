#pragma once
#include "definitions.h"
#include <vector>

struct EXTENDED_RIO_BUF;


struct AddressInfo
{
	SrcDstType srcType;
	int srcCode;
	SrcDstType dstType;
	int dstCode;
};

class ProcessManager
{
public:
	std::vector<Instruction>*GetInstructions(EXTENDED_RIO_BUF* data);

	ProcessManager();
	~ProcessManager();

private:

	AddressInfo GetManagedInfo(AddressInfo receivedInfo, const EXTENDED_RIO_BUF& data);

private:
	int matchingServerCount;

};

