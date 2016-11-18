#pragma once
#include "definitions.h"
#include <queue>

class BufferManager
{
	CRITICAL_SECTION bufferCriticalSection;

public:
	BufferManager();
	~BufferManager();

	void Initialize(RIO_EXTENSION_FUNCTION_TABLE& rioFuntionsTable, DWORD bufferCount, DWORD bufferSize);
	ExtendedRioBuf* GetBuffer();
	void FreeBuffer(ExtendedRioBuf* buffer);
	void ShutdownCleanup(RIO_EXTENSION_FUNCTION_TABLE& rioFuntionsTable);

	void PrintBufferState();

private:
	char* AllocateBufferSpace(const DWORD bufCount, const DWORD bufSize, DWORD& totalBufferSize, DWORD& totalBufferCount);

private:
	std::vector<ExtendedRioBuf*> bufferPool;					//buffer segments pool
	std::queue<int> freeBufferIndex;							//indexes of free buffer segments
	std::vector<std::pair<RIO_BUFFERID, char*>> rioBuffers;		//rio buffer pointers. used vector to make it scalable.
	int maxBufferCount;
	int bufferSize;
};

