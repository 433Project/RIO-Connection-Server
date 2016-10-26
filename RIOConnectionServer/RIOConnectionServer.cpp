
#include "stdafx.h"
#include <thread>
#include "RIOManager.h"
#include "ProcessManager.h"

#define MIK_TEST_SPACE

inline void ReportError(
	const char *pFunction, bool willExit);

void MainProcess(RIOManager& rioManager);

int _tmain(int argc, _TCHAR* argv[])
{
	RIOManager rioManager;
	std::vector<std::thread*> threadPool;

#ifdef MIK_TEST_SPACE
	//Testing IOCP Functions - START
	HANDLE iocp;
	iocp = rioManager.CreateIOCP();
	iocp = rioManager.CreateIOCP();
	iocp = rioManager.CreateIOCP();
	iocp = rioManager.CreateIOCP();
	iocp = rioManager.CreateIOCP();

	rioManager.Shutdown();
	std::cin.get();
	return 1;
	//Testing IOCP Functions - END
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
		std::vector<ReceivedData*> results;
		//rioManager.GetCompletedResults(results);
		for each(auto result in results)
		{
			
			swichManager.StartProcessing(result);
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

