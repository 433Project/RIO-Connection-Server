#include "stdafx.h"
#include <thread>
#include "RIOManager.h"
#include "ProcessManager.h"
#include "ConfigurationManager.h"

//#define		TRACK_MESSAGES

inline void ReportError(
	const char *pFunction, bool willExit);

CRITICAL_SECTION consoleCriticalSection;

struct BasicConnectionServerHandles {
	RIOManager rioManager;
	HANDLE iocp;
	CQ_Handler cqHandler;
};



void MainProcess(BasicConnectionServerHandles* connectionServer, int threadID);

int _tmain(int argc, _TCHAR* argv[])
{
	//##########################################
	//				Setup/Config
	//##########################################
	cout << "* * * * * * * * * * * * * * * * * * * * * * * * * * * *" << endl;
	cout << "* * * * * 433 Project - RIO-Connection-Server * * * * *" << endl;
	cout << "* * * * * * * * * * * * * * * * * * * * * * * * * * * *" << endl;

	string configFileLocation = "C:\\RIOConfig\\config.txt";

	cout << "\nReading configuration data from: " << configFileLocation << endl;

	std::vector<std::thread*> threadPool;
	std::vector<ServiceData> services;
	RIOMainConfig rioMainConfig;
	ConfigurationManager* configManager = new ConfigurationManager();

	//Load configuration from file
	configManager->LoadConfiguration(configFileLocation);
	rioMainConfig = configManager->GetRIOConfiguration();
	services = configManager->GetServiceConfiguration();

	BasicConnectionServerHandles connectionServer;

	//For error message console printing, we are creating a critical section to prevent multi-threads
	//from printing at the same time
	InitializeCriticalSectionAndSpinCount(&consoleCriticalSection, rioMainConfig.spinCount);
	connectionServer.rioManager.AssignConsoleCriticalSection(consoleCriticalSection);

	//Calculate the number of buffers required to handle all services and the maximum CQ size
	DWORD numberBuffersRequired = 0;
	int maximumCQSize = 0;
	for each (auto serviceData in services)
	{
		numberBuffersRequired = numberBuffersRequired +
				(serviceData.serviceMaxClients * 
				(serviceData.serviceRQMaxReceives + serviceData.serviceRQMaxSends));
	}
	maximumCQSize = (int)min(numberBuffersRequired, (DWORD)RIO_MAX_CQ_SIZE);
	cout << "\nRequired number of buffers: \t" << numberBuffersRequired << endl;
	cout << "Maximum CQ size: \t\t" << maximumCQSize << endl;

	//Initialize the RIO Manager and create our IOCP and CQ
	connectionServer.rioManager.InitializeRIO(rioMainConfig.bufferSize, 
				numberBuffersRequired, 
				rioMainConfig.spinCount);
	connectionServer.iocp = connectionServer.rioManager.CreateIOCP();
	CQ_Handler cqHandler = connectionServer.rioManager.CreateCQ(maximumCQSize);
	connectionServer.cqHandler = cqHandler;

	//Initialize all our services
	//enum DestinationType
	//{
	//	MATCHING_SERVER = 0,		8433
	//	MATCHING_CLIENT = 1,		10433 (TCP - packet generator okay)
	//	ROOM_MANAGER = 2,			9433
	//	PACKET_GENERATOR = 3,		5050 (UDP)
	//	MONITORING_SERVER = 4		11433
	//};
	for each (auto serviceData in services)
	{	
		connectionServer.rioManager.CreateRIOSocket(
			serviceData.serviceType, 
			serviceData.serviceCode, 
			serviceData.servicePort,
			serviceData.serviceMaxClients,		//
			serviceData.serviceMaxAccepts,
			serviceData.serviceRQMaxReceives,
			serviceData.serviceRQMaxSends,
			serviceData.isAddressRequired);
	}

	//##########################################
	//			Create IOCP Threads
	//##########################################
	for (int i = 0; i < rioMainConfig.numThreads; i++)
	{
		std::thread* thread = new std::thread(MainProcess, &connectionServer, i);
		threadPool.emplace_back(thread);
	}

	//##########################################
	//		Wait for Commands from User
	//##########################################
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
