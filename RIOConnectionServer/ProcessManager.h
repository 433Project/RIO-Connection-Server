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
	void StartProcessing(ReceivedData* data);

	ProcessManager();
	~ProcessManager();

private:

	AddressInfo GetManagedInfo(AddressInfo receivedInfo, const ReceivedData& data);

private:
	int matchingServerCount;

};

