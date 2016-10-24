
#include "stdafx.h"
#include <thread>
#include "RIOManager.h"
#include "SwitchManager.h"

inline void ReportError(
	const char *pFunction, bool willExit);

void MainProcess(RIOManager& rioManager);

int _tmain(int argc, _TCHAR* argv[])
{
	
	RIOManager rioManager;
	std::vector<std::thread*> threadPool;

	for (int i = 0; i < 8; i++)
	{
		std::thread* thread = new std::thread(MainProcess, rioManager);
		threadPool.emplace_back(thread);
	}

	for each(auto thread in threadPool)
	{
		thread->join();
	}

    return 0;
}

void MainProcess(RIOManager& rioManager)
{
	SwitchManager swichManager;
	while (true)
	{
		GetQueuedCompletionStatus();
		std::vector<ReceivedData*> results;
		rioManager.GetCompletedResults(results);
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

