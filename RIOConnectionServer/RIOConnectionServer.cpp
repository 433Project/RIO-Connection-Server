// RIOConnectionServer.cpp : 콘솔 응용 프로그램에 대한 진입점을 정의합니다.
//

#include "stdafx.h"
#include "RIOManager.h"


inline void ReportError(
	const char *pFunction, bool willExit);

int _tmain(int argc, _TCHAR* argv[])
{
	
	

    return 0;
}

void MainProcess()
{
	while (true)
	{
		GetQueuedCompletionStatus(RIOManager.GEt)
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

