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

struct ServiceData {
	SocketType serviceType;
	int serviceCode;
	int servicePort;

	ServiceData(SocketType servType, int code, int port) {
		serviceType = servType;
		serviceCode = code;
		servicePort = port;
	}
};

void MainProcess(BasicConnectionServerHandles* connectionServer);

int _tmain(int argc, _TCHAR* argv[])
{
	RIOManager rioManager;
	std::vector<std::thread*> threadPool;
	std::vector<ServiceData> services;

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

	//Load Services

	//enum DestinationType
	//{
	//	MATCHING_SERVER = 0,
	//	MATCHING_CLIENT = 1,
	//	ROOM_MANAGER = 2,
	//	PACKET_GENERATOR = 3,
	//	MONITORING_SERVER = 4
	//};

	services.push_back(*(new ServiceData(TCPListener, 0, 8433)));
	services.push_back(*(new ServiceData(TCPListener, 1, 10433)));
	services.push_back(*(new ServiceData(TCPListener, 2, 9433)));
	services.push_back(*(new ServiceData(UDPSocket, 3, 5050)));
	services.push_back(*(new ServiceData(TCPListener, 4, 11433)));

	//Create basic UDPSocket at Port 5050
	connectionServer.rioManager.CreateRIOSocket(UDPSocket, 1, 5050);
	connectionServer.rioManager.CreateRIOSocket(TCPListener, 2, 10433);

	//Start threads and keep track of them
	for (int i = 0; i < 8; i++)
	{
		std::thread* thread = new std::thread(MainProcess, &connectionServer);
		threadPool.emplace_back(thread);
	}

	//Loop and wait for a command to be sent to the main IOCP
	{
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
						cout << "Escape Key Pressed. . . closing all threads. . ." << endl;
						PostQueuedCompletionStatus(connectionServer.iocp, 0, CK_QUIT, &ov);
						isRunning = false;
						break;
					case VK_NUMPAD1:
						cout << "Numpad #1 Pressed. . . requesting service information. . ." << endl;
						PostQueuedCompletionStatus(connectionServer.iocp, 0, CK_GETINFO, &ov);
						break;
					default:
						cout << "Some other key pressed" << endl;
						break;
					}
				}
			}
		}

		SetConsoleMode(consoleHandle, mode);
	}
	//Command Input Region End

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
	std::vector<Instruction>* instructionSet;
	EXTENDED_OVERLAPPED* op = 0;
	DWORD bytes = 0;
	ULONG_PTR key = 0;
	BOOL quitTrigger = false;
	ULONG numResults;

	while (true)
	{
		connectionServer->rioManager.RIONotifyIOCP(connectionServer->cqHandler.rio_CQ);
		cout << "Waiting on Completions" << endl;
		if (!GetQueuedCompletionStatus(connectionServer->iocp, &bytes, &key, (OVERLAPPED**)&op, INFINITE)) {
			//ERROR
		}
		cout << "IOCP Event Triggered" << endl;
		switch ((COMPLETION_KEY)key) {
		case CK_RIO:
			numResults = connectionServer->rioManager.GetCompletedResults(results, rioResults);
			cout << "Received " << numResults << " packet(s) from RIO Completion Queue (CK_RIO)" << endl;
			if (numResults == 0) {
				cout << "RIO Completion Queue found empty. . ." << endl;
			}
			else if (numResults == RIO_CORRUPT_CQ) {
				cout << "RIO Completion Queue corrupted. . ." << endl;
			}

			for each(auto result in results)
			{
				instructionSet = processManager.GetInstructions(result);
				for each (auto instruction in *instructionSet)
				{
					connectionServer->rioManager.ProcessInstruction(instruction);
				}
			}


			//Repost receives
			for (int i = 0; i < numResults; i++) {
				connectionServer->rioManager.PostRecv(1);
			}

			break;
		case CK_ACCEPT:
			cout << "Received Accept Completion." << endl;
			//connectionServer->rioManager.ConfigureNewSocket(op);
			connectionServer->rioManager.CreateRIOSocket(TCPConnection, op->serviceType, op->relevantSocket);
			connectionServer->rioManager.ResetAcceptCall(op);
			break;
		case CK_QUIT:
			cout << "Received Quit Command from Main Thread." << endl;
			quitTrigger = true;
			PostQueuedCompletionStatus(connectionServer->iocp, 0, CK_QUIT, op);
			break;
		case CK_GETINFO:
			cout << "Received Info Request from Main Thread." << endl;
			connectionServer->rioManager.PrintServiceInformation();
			break;
		default:
			cout << "Received erroneous message in IOCP queue" << endl;
			break;
		}
		
		if (quitTrigger) {
			break;
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
