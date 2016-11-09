#include "stdafx.h"
#include "BufferManager.h"

BufferManager::BufferManager()
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

	DWORD totalBufferSize = 0;
	DWORD totalBufferCount = 0;
	char* newMemoryArea = AllocateBufferSpace(bufferCount, bufferSize, totalBufferSize, totalBufferCount);

	this->bufferSize = bufferSize;
	this->maxBufferCount = totalBufferCount;

	RIO_BUFFERID id = rioFuntionsTable.RIORegisterBuffer(newMemoryArea, totalBufferSize);

	rioBuffers.emplace_back(id, newMemoryArea);

	for (int i = 0; i < totalBufferCount; i++)
	{
		EXTENDED_RIO_BUF* buffer = new EXTENDED_RIO_BUF();
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

EXTENDED_RIO_BUF* BufferManager::GetBuffer()
{

	if (freeBufferIndex.size() <= 0)
	{
// 		char* newMemoryArea = AllocateBufferSpace();
// 		rioBuffers.emplace_back(newMemoryArea);
// 		RIO_BUFFERID id = rioFuntionsTable.RIORegisterBuffer(newMemoryArea, bufferSize * bufferSize);
// 		for (int i = 0; i < bufferCount; i++)
// 		{
// 			EXTENDED_RIO_BUF* buffer = new EXTENDED_RIO_BUF();
// 			buffer->BufferId = id;
// 			bufferPool.push_back(buffer);
// 		}
		return nullptr;
	}
	int index = freeBufferIndex.front();
	freeBufferIndex.pop();

	return bufferPool[index];
}

void BufferManager::FreeBuffer(EXTENDED_RIO_BUF* buffer)
{
	buffer->messageLength = 0;
	buffer->socketContext = 0;

	int bufferIndex = buffer->Offset / bufferSize;
	freeBufferIndex.push(bufferIndex);
}

void BufferManager::ShutdownCleanup(RIO_EXTENSION_FUNCTION_TABLE& rioFuntionsTable)
{
	rioFuntionsTable.RIODeregisterBuffer(rioBuffers[0].first);
}

void BufferManager::PrintBufferState()
{
	std::cout << "========================= Buffer State ============================" << std::endl;
	std::cout << "Buffer Usage: " << freeBufferIndex.size() / bufferPool.size() * 100 << "%" << std::endl;
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
