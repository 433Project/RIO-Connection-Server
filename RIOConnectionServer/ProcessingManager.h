#pragma once

struct EXTENDED_RIO_BUF;

class ProcessingManager
{
public:
	void StartProcessing(EXTENDED_RIO_BUF* buf);

	ProcessingManager();
	~ProcessingManager();
};

