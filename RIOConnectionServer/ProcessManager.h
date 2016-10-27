#pragma once

struct ReceivedData;

struct AddressInfo
{
	DestinationType srcType;
	int srcCode;
	DestinationType dstType;
	int dstCode;
};

class ProcessManager
{
public:
	Instruction GetInstructions(ReceivedData* data);

	ProcessManager();
	~ProcessManager();

private:

	AddressInfo GetManagedInfo(AddressInfo receivedInfo, const ReceivedData& data);

private:
	int matchingServerCount;

};

