#include "stdafx.h"
#include "SwitchManager.h"
#include "RIOManager.h"


void SwitchManager::StartProcessing(ReceivedData* data)
{
	int length;
	int src;
	int dst;
	memcpy(&length, data->buffer, 4);
	memcpy(&src, data->buffer, 4);
	memcpy(&dst, data->buffer, 4);


}



SwitchManager::SwitchManager()
{
}


SwitchManager::~SwitchManager()
{
}
