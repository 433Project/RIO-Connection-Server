#include "stdafx.h"
#include "ProcessManager.h"
#include "RIOManager.h"


std::vector<Instruction>* ProcessManager::GetInstructions(EXTENDED_RIO_BUF* data)
{
	std::vector<Instruction>* instructions = new std::vector<Instruction>();
	switch (data->operationType)
	{
	case OP_RECEIVE:
		AddressInfo receivedInfo;

		memcpy(&receivedInfo.srcType, (byte*)data->buffer + data->Offset + 4, 4);
		memcpy(&receivedInfo.srcCode, (byte*)data->buffer + data->Offset + 8, 4);

		memcpy(&receivedInfo.dstType, (byte*)data->buffer + data->Offset + 12, 4);
		memcpy(&receivedInfo.dstCode, (byte*)data->buffer + data->Offset + 16, 4);

		//cout << "srcType " << receivedInfo.srcType << endl;
		//cout << "srcCode " << receivedInfo.srcCode << endl;
		//cout << "dstType " << receivedInfo.dstType << endl;
		//cout << "dstCode " << receivedInfo.dstCode << endl;

		AddressInfo changedInfo = GetManagedInfo(receivedInfo, *data);

		memcpy((byte*)data->buffer + data->Offset + 4, &changedInfo.srcType, 4);
		memcpy((byte*)data->buffer + data->Offset + 8, &changedInfo.srcCode, 4);

		memcpy((byte*)data->buffer + data->Offset + 12, &changedInfo.dstType, 4);
		memcpy((byte*)data->buffer + data->Offset + 16, &changedInfo.dstCode, 4);

		Instruction sendInstruction;
		sendInstruction.type = SEND;
		sendInstruction.buffer = data;
		sendInstruction.destinationType = changedInfo.dstType;
		sendInstruction.socketContext = changedInfo.dstCode;


		Instruction receiveInstruction;
		receiveInstruction.type = RECEIVE;
		receiveInstruction.buffer = nullptr;
		receiveInstruction.destinationType = data->srcType;
		receiveInstruction.socketContext = data->socketContext;

		instructions->push_back(receiveInstruction);
		instructions->push_back(sendInstruction);
		return instructions;


	case OP_SEND:

		Instruction freeInstruction;
		receiveInstruction.type = FREEBUFFER;
		receiveInstruction.buffer = nullptr;
		receiveInstruction.destinationType = data->srcType;
		receiveInstruction.socketContext = data->socketContext;

		instructions->push_back(freeInstruction);
		return instructions;

	default:
		return instructions;
	}

}



ProcessManager::ProcessManager()
{
}


ProcessManager::~ProcessManager()
{
}

AddressInfo ProcessManager::GetManagedInfo(AddressInfo receivedInfo, const EXTENDED_RIO_BUF& data)
{
	AddressInfo result;
	if(matchingServerCount > 3)
		matchingServerCount = 1;

	result = receivedInfo;
	result.srcCode = data.socketContext;

	switch (receivedInfo.srcType)
	{
	case MATCHING_SERVER:
		break;
	case MATCHING_CLIENT:
		break;
	case ROOM_SERVER:
		break;
	case PACKET_GENERATOR:
		break;
	case MONITORING_SERVER:
		break;
	}
	return result;
}
