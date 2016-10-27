#include "stdafx.h"
#include "ProcessManager.h"
#include "RIOManager.h"


Instruction ProcessManager::GetInstructions(ReceivedData* data)
{
	AddressInfo receivedInfo;
	
	memcpy(&receivedInfo.srcType, (byte*)data->buffer + data->offset + 4, 4);
	memcpy(&receivedInfo.srcCode, (byte*)data->buffer + data->offset + 8, 4);

	memcpy(&receivedInfo.dstType, (byte*)data->buffer + data->offset + 12, 4);
	memcpy(&receivedInfo.dstCode, (byte*)data->buffer + data->offset + 16, 4);

	AddressInfo changedInfo = GetManagedInfo(receivedInfo, *data);

	memcpy((byte*)data->buffer + data->offset + 4, &receivedInfo.srcType, 4);
	memcpy((byte*)data->buffer + data->offset + 8, &receivedInfo.srcCode, 4);

	memcpy((byte*)data->buffer + data->offset + 12, &receivedInfo.dstType, 4);
	memcpy((byte*)data->buffer + data->offset + 16, &receivedInfo.dstCode, 4);

	Instruction instruction;
	instruction.buffer = data->buffer;
	instruction.destinationType = changedInfo.dstType;
	instruction.length = data->length;
	instruction.socketContext = data->socketContext;
	instruction.type = SEND;
}



ProcessManager::ProcessManager()
{
}


ProcessManager::~ProcessManager()
{
}

AddressInfo ProcessManager::GetManagedInfo(AddressInfo receivedInfo, const ReceivedData& data)
{
	AddressInfo result;
	if(matchingServerCount > 3)
		matchingServerCount = 1;

	result.srcCode = data.socketContext;
	switch (receivedInfo.srcType)
	{
	case MATCHING_SERVER:
		break;
	case MATCHING_CLIENT:
		break;
	case ROOM_MANAGER:
		break;
	case PACKET_GENERATOR:
		break;
	case MONITORING_SERVER:
		break;
	}
	return result;
}
