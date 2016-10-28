#include "stdafx.h"
#include <thread>
#include "RIOManager.h"
#include "ProcessManager.h"

#define MIK_TEST_SPACE

char* TEMPORARYAllocateBufferSpace(const DWORD bufSize, const DWORD bufCount, DWORD& totalBufferSize, DWORD& totalBufferCount);

inline void ReportError(
	const char *pFunction, bool willExit);

void MainProcess(RIOManager& rioManager);

int _tmain(int argc, _TCHAR* argv[])
{
	RIOManager rioManager;
	std::vector<std::thread*> threadPool;

#ifdef MIK_TEST_SPACE
	rioManager.InitializeRIO();
	HANDLE iocp;
	iocp = rioManager.CreateIOCP();
	CQ_Handler rio_CQ = rioManager.CreateCQ();
	rioManager.CreateRIOSocket(UDPSocket, 1, 5050);

	//Temporary Buffer Creation for testing
	DWORD offset = 0;
	DWORD bufSize = 1024;
	DWORD bufCount = 10;
	DWORD totalBufSize = 0;
	DWORD totalBufMade = 0;
	char* buffer = TEMPORARYAllocateBufferSpace(bufSize, bufCount, totalBufSize, totalBufMade);
	EXTENDED_RIO_BUF* rioBufs = new EXTENDED_RIO_BUF[totalBufMade];
	RIO_BUFFERID riobufID = rioManager.RegBuf((char*)buffer, totalBufSize);
	for (DWORD i = 0; i < totalBufMade; ++i)
	{
		/// split g_sendRioBufs to SEND_BUFFER_SIZE for each RIO operation
		EXTENDED_RIO_BUF* pBuffer = rioBufs + i;

		pBuffer->operationType = OP_RECEIVE;
		pBuffer->BufferId = riobufID;
		pBuffer->Offset = offset;
		pBuffer->Length = bufSize;

		rioManager.PostRecv(1, pBuffer);

		offset += bufSize;

	}
	//

	OVERLAPPED* op = 0;
	DWORD bytes = 0;
	ULONG_PTR key = 0;
	int i = 0;

	while (true) {
		rioManager.RIONotifyIOCP(rio_CQ.rio_CQ);
		cout << "Waiting on Completions" << endl;
		if (!GetQueuedCompletionStatus(iocp, &bytes, &key, &op, INFINITE)) {
			//ERROR
		}
		
		if ((COMPLETION_KEY)key == CK_RIO) {
			cout << "Received packet from RIO Completion Queue (CK_RIO)" << endl;
			i++;
			if (i == 9) {
				break;
			}


		}
		else {
			cout << "Received erroneous message in IOCP queue" << endl;
		}
	}

	rioManager.DeRegBuf(riobufID);
	rioManager.Shutdown();
	std::cin.get();
	return 1;
#endif // MIK_TEST_SPACE



	for (int i = 0; i < 8; i++)
	{
		std::thread* thread = new std::thread(MainProcess, rioManager);
		threadPool.emplace_back(thread);
	}

	for each(auto thread in threadPool)
	{
		thread->join();
		delete thread;
	}

	threadPool.clear();
	

    return 0;
}

void MainProcess(RIOManager& rioManager)
{
	ProcessManager swichManager;
	while (true)
	{
		//GetQueuedCompletionStatus();
		std::vector<EXTENDED_RIO_BUF*> results;
		//rioManager.GetCompletedResults(results);
		for each(auto result in results)
		{
			
			swichManager.GetInstructions(result);
		}
	}
}

///The ReportError function prints an error message and may shutdown the program if flagged to do so.
inline void ReportError(
	const char *pFunction, bool willExit)
{
	const DWORD lastError = ::GetLastError();

	cout << "\tError!!! Function - " << pFunction << " failed - " << lastError << endl;

	if (willExit) {
		exit(0);
	}
}



//TEMPORARY FUNCTIONS FOR TESTING WHILE BUFFER MANAGER IS IN DEVELOPMENT

template <typename TV, typename TM>
inline TV RoundDown(TV Value, TM Multiple)
{
	return((Value / Multiple) * Multiple);
}

template <typename TV, typename TM>
inline TV RoundUp(TV Value, TM Multiple)
{
	return(RoundDown(Value, Multiple) + (((Value % Multiple) > 0) ? Multiple : 0));
}

char* TEMPORARYAllocateBufferSpace(const DWORD bufSize, const DWORD bufCount, DWORD& totalBufferSize, DWORD& totalBufferCount)
{
	SYSTEM_INFO systemInfo;

	::GetSystemInfo(&systemInfo);

	const unsigned __int64 granularity = systemInfo.dwAllocationGranularity;

	const unsigned __int64 desiredSize = bufSize * bufCount;

	unsigned __int64 actualSize = RoundUp(desiredSize, granularity);

	if (actualSize > UINT_MAX)
	{
		actualSize = (UINT_MAX / granularity) * granularity;
	}

	//totalBufferCount = min(bufCount, static_cast<DWORD>(actualSize / bufSize));
	totalBufferCount = bufCount;

	totalBufferSize = static_cast<DWORD>(actualSize);

	char* pBuffer = reinterpret_cast<char*>(VirtualAllocEx(GetCurrentProcess(), 0, totalBufferSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));

	if (pBuffer == 0)
	{
		printf_s("VirtualAllocEx Error: %d\n", GetLastError());
		exit(0);
	}

	return pBuffer;
}

