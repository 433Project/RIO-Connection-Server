#include "stdafx.h"
#include "ProcessingManager.h"
#include "RIOManager.h"


void ProcessingManager::StartProcessing(ReceivedData* data)
{
	int length;
	int src;
	int dst;
	memcpy(&length, data->buffer, 4);
	memcpy(&src, data->buffer, 4);
	memcpy(&dst, data->buffer, 4);


}



ProcessingManager::ProcessingManager()
{
}


ProcessingManager::~ProcessingManager()
{
}
