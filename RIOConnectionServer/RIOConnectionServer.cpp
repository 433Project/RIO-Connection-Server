#include "stdafx.h"
#include <thread>
#include "RIOManager.h"
#include "ProcessManager.h"

//#define		TRACK_MESSAGES

inline void ReportError(
	const char *pFunction, bool willExit);

CRITICAL_SECTION consoleCriticalSection;

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

void MainProcess(BasicConnectionServerHandles* connectionServer, int threadID);

int _tmain(int argc, _TCHAR* argv[])
{
	RIOManager rioManager;
	std::vector<std::thread*> threadPool;
	std::vector<ServiceData> services;

	//***SETUP***
	BasicConnectionServerHandles connectionServer;


	InitializeCriticalSectionAndSpinCount(&consoleCriticalSection, 4000);//
	connectionServer.rioManager.AssignConsoleCriticalSection(consoleCriticalSection);//

	connectionServer.rioManager.InitializeRIO();
	connectionServer.iocp = connectionServer.rioManager.CreateIOCP();
	CQ_Handler cqHandler = connectionServer.rioManager.CreateCQ();

	connectionServer.cqHandler = cqHandler;




	//Load Services

	//enum DestinationType
	//{
	//	MATCHING_SERVER = 0,		8433
	//	MATCHING_CLIENT = 1,		10433 (TCP - packet generator okay)
	//	ROOM_MANAGER = 2,			9433
	//	PACKET_GENERATOR = 3,		5050 (UDP)
	//	MONITORING_SERVER = 4		11433
	//};

	services.push_back(*(new ServiceData(TCPListener, 0, 8433)));
	services.push_back(*(new ServiceData(TCPListener, 1, 10433)));
	services.push_back(*(new ServiceData(TCPListener, 2, 9433)));
	services.push_back(*(new ServiceData(UDPSocket, 3, 5050)));
	services.push_back(*(new ServiceData(TCPListener, 4, 11433)));

	//Create basic UDPSocket at Port 5050
	//connectionServer.rioManager.CreateRIOSocket(UDPSocket, 1, 5050);
	//connectionServer.rioManager.CreateRIOSocket(TCPListener, 2, 10433);
	for each (auto serviceData in services)
	{
		connectionServer.rioManager.CreateRIOSocket(serviceData.serviceType, serviceData.serviceCode, serviceData.servicePort);
	}

	//Start threads and keep track of them
	for (int i = 0; i < 8; i++)
	{
		std::thread* thread = new std::thread(MainProcess, &connectionServer, i);
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
					case VK_NUMPAD2:
						cout << "Numpad #2 Pressed. . . requesting thread counter information. . ." << endl;
						PostQueuedCompletionStatus(connectionServer.iocp, 0, CK_COUNTER, &ov);
						break;
					case VK_NUMPAD3:
						cout << "Numpad #3 Pressed. . . requesting buffer usage statistics. . ." << endl;
						PostQueuedCompletionStatus(connectionServer.iocp, 0, CK_BUFINFO, &ov);
						break;
					case VK_NUMPAD4:
						cout << "Numpad #4 Pressed. . . requesting critical section check. . ." << endl;
						PostQueuedCompletionStatus(connectionServer.iocp, 0, CK_CHECKCRITSEC, &ov);
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

void MainProcess(BasicConnectionServerHandles* connectionServer, int threadID)
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
	int sendCount = 0;
	int receiveCount = 0;
	int freeBufferCount = 0;

	while (true)
	{
		connectionServer->rioManager.RIONotifyIOCP(connectionServer->cqHandler.rio_CQ);

		/*EnterCriticalSection(&consoleCriticalSection);
		cout << "Waiting on Completions" << endl;
		LeaveCriticalSection(&consoleCriticalSection);*/

		if (!GetQueuedCompletionStatus(connectionServer->iocp, &bytes, &key, (OVERLAPPED**)&op, INFINITE)) {
			//ERROR
			EnterCriticalSection(&consoleCriticalSection);
			cout << "ERROR: Could not call GQCS. . ." << endl;
			LeaveCriticalSection(&consoleCriticalSection);
			break;
		}

		switch ((COMPLETION_KEY)key) {
		case CK_RIO:
			numResults = connectionServer->rioManager.GetCompletedResults(results, rioResults);
			//cout << "Received " << numResults << " packet(s) from RIO Completion Queue (CK_RIO)" << endl;
			if (numResults == 0) {
				EnterCriticalSection(&consoleCriticalSection);
				cout << "ERROR: RIO Completion Queue found empty. . ." << endl;
				LeaveCriticalSection(&consoleCriticalSection);
			}
			else if (numResults == RIO_CORRUPT_CQ) {
				EnterCriticalSection(&consoleCriticalSection);
				cout << "ERROR: RIO Completion Queue corrupted. . ." << endl;
				LeaveCriticalSection(&consoleCriticalSection);
			}

			for each(auto result in results)
			{


#ifdef TRACK_MESSAGES
				EnterCriticalSection(&consoleCriticalSection);
				cout << "\nMessage came from service #" << result->srcType << endl;
				cout << "Message came from RQ #" << result->socketContext << endl;
				cout << "Completion was type " << result->operationType << endl;
				if (result->operationType == OP_SEND) {
					cout << "OP_SEND" << endl;
				}
				else if (result->operationType == OP_RECEIVE) {
					cout << "OP_RECEIVE" << endl;
				}
				LeaveCriticalSection(&consoleCriticalSection);
#endif // TRACK_MESSAGES



				if (result->operationType == OP_RECEIVE) {
					receiveCount++;
				}
				else if (result->operationType == OP_SEND) {
					sendCount++;
				}

				instructionSet = processManager.GetInstructions(result);

				/*EnterCriticalSection(&consoleCriticalSection);
				cout << "Received " << instructionSet->size() << " instructions." << endl;
				LeaveCriticalSection(&consoleCriticalSection);*/

				for each (auto instruction in *instructionSet)
				{
					if (instruction.type == FREEBUFFER) {
						freeBufferCount++;
					}
					connectionServer->rioManager.ProcessInstruction(instruction);
				}
			}

			break;
		case CK_ACCEPT:
			//cout << "Received Accept Completion." << endl;
			//connectionServer->rioManager.ConfigureNewSocket(op);
			connectionServer->rioManager.CreateRIOSocket(TCPConnection, op->serviceType, op->relevantSocket);
			connectionServer->rioManager.ResetAcceptCall(op);
			break;
		case CK_QUIT:
			cout << "\nReceived Quit Command from Main Thread." << endl;
			cout << "\nPrinting Count on Thread #" << threadID << endl;
			cout << "\tReceives:\t" << receiveCount << endl;
			cout << "\tSends:\t\t" << sendCount << endl;
			cout << "\tBufferFrees:\t" << freeBufferCount << endl;
			quitTrigger = true;
			PostQueuedCompletionStatus(connectionServer->iocp, 0, CK_QUIT, op);
			break;
		case CK_GETINFO:
			//cout << "Received Info Request from Main Thread." << endl;
			connectionServer->rioManager.PrintServiceInformation();
			break;
		case CK_COUNTER:
			cout << "\nPrinting Count on Thread #" << threadID << endl;
			cout << "\tReceives:\t" << receiveCount << endl;
			cout << "\tSends:\t\t" << sendCount << endl;
			cout << "\tBufferFrees:\t" << freeBufferCount << endl;
			break;
		case CK_BUFINFO:
			connectionServer->rioManager.PrintBufferUsageStatistics();
			break;
		case CK_CHECKCRITSEC:
			connectionServer->rioManager.CheckCriticalSections();
			break;
		default:
			cout << "ERROR: Received erroneous message in IOCP queue" << endl;
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
