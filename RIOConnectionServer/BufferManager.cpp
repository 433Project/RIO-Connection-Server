#include "stdafx.h"
#include "BufferManager.h"

BufferManager::BufferManager(): isQueueInUse(false)
{
}


BufferManager::~BufferManager()
{
	for each(auto buffer in bufferPool)
	{
		delete buffer;
	}

	bufferPool.clear();

	for each(auto pair in rioBuffers)
	{
		VirtualFree(pair.second, 0, MEM_RELEASE);
	}

	rioBuffers.clear();
}


void BufferManager::Initialize(RIO_EXTENSION_FUNCTION_TABLE& rioFuntionsTable, DWORD bufferCount, DWORD bufferSize)
{
	//InitializeCriticalSection(&bufferCriticalSection);
	DWORD totalBufferSize = 0;
	DWORD totalBufferCount = 0;
	char* newMemoryArea = AllocateBufferSpace(bufferCount, bufferSize, totalBufferSize, totalBufferCount);

	this->bufferSize = bufferSize;
	this->maxBufferCount = totalBufferCount;

	RIO_BUFFERID id = rioFuntionsTable.RIORegisterBuffer(newMemoryArea, totalBufferSize);

	rioBuffers.emplace_back(id, newMemoryArea);

	for (int i = 0; i < totalBufferCount; i++)
	{
		ExtendedRioBuf* buffer = new ExtendedRioBuf();
		buffer->BufferId = id;
		buffer->buffer = newMemoryArea;
		buffer->Offset = bufferSize * i;
		buffer->Length = bufferSize;
		buffer->messageLength = 0;
		buffer->socketContext = 0;

		bufferPool.push_back(buffer);
		freeBufferIndex.push(i);
	}
}

ExtendedRioBuf* BufferManager::GetBuffer()
{
	while (true)
	{
		bool testVal = false;
		bool newVal = true;
		if (isQueueInUse.compare_exchange_strong(testVal, newVal))
		{
			if (freeBufferIndex.size() <= 0)
			{
				isQueueInUse = false;
				return nullptr;
			}
			int index = freeBufferIndex.front();
			freeBufferIndex.pop();

			isQueueInUse = false;
			return bufferPool[index];
		}
	}
	
}

void BufferManager::FreeBuffer(ExtendedRioBuf* buffer)
{

	if (buffer == nullptr)
	{
		cout << "buffer is nullptr" << endl;
	}
	buffer->messageLength = 0;
	buffer->socketContext = 0;
	int bufferIndex = buffer->Offset / bufferSize;
	//EnterCriticalSection(&bufferCriticalSection);
	//LeaveCriticalSection(&bufferCriticalSection);

	while (true)
	{
		bool testVal = false;
		bool newVal = true;
		if (isQueueInUse.compare_exchange_strong(testVal, newVal))
		{
			freeBufferIndex.push(bufferIndex);
			isQueueInUse = false;
			return;
		}
	}

}

void BufferManager::ShutdownCleanup(RIO_EXTENSION_FUNCTION_TABLE& rioFuntionsTable)
{
	rioFuntionsTable.RIODeregisterBuffer(rioBuffers[0].first);
}

void BufferManager::PrintBufferState()
{
	std::cout << "========================= Buffer State ============================" << std::endl;
	std::cout << "Buffer Usage: " << (bufferPool.size() - freeBufferIndex.size()) * 100 / bufferPool.size() << "%" << std::endl;
	std::cout << "Buffer On Use: " << (bufferPool.size() - freeBufferIndex.size()) << std::endl;
	std::cout << "Free Buffer: " << freeBufferIndex.size() << std::endl;
}

char* BufferManager::AllocateBufferSpace(const DWORD bufCount, const DWORD bufSize, DWORD& totalBufferSize, DWORD& totalBufferCount)
{
	SYSTEM_INFO systemInfo;

	GetSystemInfo(&systemInfo);

	const unsigned __int64 granularity = systemInfo.dwAllocationGranularity;
	const unsigned __int64 desiredSize = bufSize *  bufCount;

	unsigned __int64 actualSize;
	if (desiredSize < desiredSize / granularity * granularity)
		actualSize = bufSize * (desiredSize / granularity + 1);
	else
		actualSize = desiredSize;

	if (actualSize > UINT_MAX)
		actualSize = (UINT_MAX / granularity) * granularity;

	totalBufferCount = min(bufCount, static_cast<DWORD>(actualSize));

	totalBufferSize = static_cast<DWORD>(actualSize);

	char* buffer = reinterpret_cast<char*>(VirtualAllocEx(GetCurrentProcess(), 0, totalBufferSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));

	if (buffer == nullptr)
	{
		printf("VirtualAllocEx Error: %d\n", GetLastError());
		exit(0);
	}

	return buffer;
}
