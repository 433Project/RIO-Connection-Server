#include "stdafx.h"
#include <thread>
#include "RIOManager.h"
#include "ProcessManager.h"

//#define MIK_TEST_SPACE

inline void ReportError(
	const char *pFunction, bool willExit);

struct BasicConnectionServerHandles {
	RIOManager rioManager;
	HANDLE iocp;
	CQ_Handler cqHandler;
};

void MainProcess(BasicConnectionServerHandles* connectionServer);

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
	DWORD totalBufMade = 100;

	//char* buffer = TEMPORARYAllocateBufferSpace(bufSize, bufCount, totalBufSize, totalBufMade);

	EXTENDED_RIO_BUF* rioBufs = new EXTENDED_RIO_BUF[totalBufMade];

	for (int x = 0; x < totalBufMade; x++) {
		rioManager.PostRecv(1);
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
			if (i == 17) {
				break;
			}


		}
		else {
			cout << "Received erroneous message in IOCP queue" << endl;
		}
	}

	//rioManager.DeRegBuf(riobufID);
	rioManager.Shutdown();
	std::cin.get();
	return 1;
#endif // MIK_TEST_SPACE


	//***SETUP***
	BasicConnectionServerHandles connectionServer;
	connectionServer.rioManager.InitializeRIO();
	connectionServer.iocp = connectionServer.rioManager.CreateIOCP();
	CQ_Handler cqHandler = connectionServer.rioManager.CreateCQ();


	connectionServer.cqHandler = cqHandler;
	//Create basic UDPSocket at Port 5050
	connectionServer.rioManager.CreateRIOSocket(UDPSocket, 1, 5050);

	//Start threads and keep track of them
	for (int i = 0; i < 1; i++)
	{
		std::thread* thread = new std::thread(MainProcess, &connectionServer);
		threadPool.emplace_back(thread);
	}

	//Loop and wait for a command to be sent to the main IOCP
	bool isRunning = true;
	DWORD mode, inputCount;
	INPUT_RECORD inputEvent;
	OVERLAPPED ov;
	HANDLE consoleHandle = GetStdHandle(STD_INPUT_HANDLE);
	GetConsoleMode(consoleHandle, &mode);
	SetConsoleMode(consoleHandle, 0);

	while (isRunning) {
		if (WaitForSingleObject(consoleHandle, INFINITE) == WAIT_OBJECT_0) {
			ReadConsoleInput(consoleHandle, &inputEvent, 1, &inputCount);

			if ((inputEvent.EventType == KEY_EVENT) && !inputEvent.Event.KeyEvent.bKeyDown) {
				switch (inputEvent.Event.KeyEvent.wVirtualKeyCode)
				{
				case VK_ESCAPE:
					cout << "Escape Key Pressed" << endl;
					PostQueuedCompletionStatus(connectionServer.iocp, 0, CK_QUIT, &ov);
					isRunning = false;
					break;
				default:
					cout << "Some other key pressed" << endl;
					break;
				}
			}
		}
	}

	SetConsoleMode(consoleHandle, mode);

	for each(auto thread in threadPool)
	{
		thread->join();
		delete thread;
	}


	threadPool.clear();

	//Clean-up program
	connectionServer.rioManager.Shutdown();
	std::cin.get();
	return 0;
}

void MainProcess(BasicConnectionServerHandles* connectionServer)
{
	//Initiate thread's variables and objects
	ProcessManager processManager;
	RIORESULT rioResults[1000];					//Maximum rio result load off per RIODequeueCompletion call
	std::vector<EXTENDED_RIO_BUF*> results;		//Vector of Extended_RIO_BUF structs to give process manager
	OVERLAPPED* op = 0;
	DWORD bytes = 0;
	ULONG_PTR key = 0;
	BOOL quitTrigger = false;

	int i = 0;

	while (true)
	{
		connectionServer->rioManager.RIONotifyIOCP(connectionServer->cqHandler.rio_CQ);
		cout << "Waiting on Completions" << endl;
		if (!GetQueuedCompletionStatus(connectionServer->iocp, &bytes, &key, &op, INFINITE)) {
			//ERROR
		}

		switch ((COMPLETION_KEY)key) {
		case CK_RIO:
			cout << "Received packet from RIO Completion Queue (CK_RIO)" << endl;
			i++;
			break;
		case CK_ACCEPT:
			break;
		case CK_QUIT:
			cout << "Received Quit Command from Main Thread." << endl;
			quitTrigger = true;
			break;
		default:
			cout << "Received erroneous message in IOCP queue" << endl;
			break;
		}
		
		if (quitTrigger) {
			break;
		}

		for each(auto result in results)
		{
			
			processManager.GetInstructions(result);
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
