#pragma once

struct ReceivedData;

class ProcessingManager
{
public:
	void StartProcessing(ReceivedData* data);

	ProcessingManager();
	~ProcessingManager();
};

