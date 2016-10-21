#pragma once

struct ReceivedData;

class SwitchManager
{
public:
	void StartProcessing(ReceivedData* data);

	SwitchManager();
	~SwitchManager();
};

