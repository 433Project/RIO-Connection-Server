#include "stdafx.h"
#include <thread>
#include "RIOManager.h"
#include "ProcessManager.h"
#include "ConfigurationManager.h"

//#define		TRACK_MESSAGES

#ifdef _DEBUG
#define PRINT(x, y)		EnterCriticalSection(&consoleCriticalSection);	\
						cout << x << y << endl;							\
						LeaveCriticalSection(&consoleCriticalSection)
#else
#define PRINT(x, y)
#endif // _DEBUG


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

	if (InitializeCriticalSectionAndSpinCount(&consoleCriticalSection, 4000) == 0) {
		// Error Exit - can't make console critical section
		return 0;
	}
	//C:\\RIOConfig\\config.txt
	string configFileLocation = ".\\config.txt";

	PRINT("\nReading configuration data from: ", configFileLocation);

	std::vector<std::thread*> threadPool;
	std::vector<ServiceData> services;
	RIOMainConfig rioMainConfig;
	ConfigurationManager* configManager = new ConfigurationManager();
	BasicConnectionServerHandles connectionServer;


	connectionServer.rioManager.AssignConsoleCriticalSection(consoleCriticalSection);


	//Load configuration from file
	if (configManager->LoadConfiguration(configFileLocation, &rioMainConfig, &services) != 0) {
		// Error exit - could not load configuration file
		return 0;
	}
	if (services.empty()) {
		// Error Exit - no services specified
		return 0;
	}
	if (configManager != nullptr) {
		delete configManager;
		configManager = nullptr;
	}


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
	PRINT("\nRequired number of buffers: \t", numberBuffersRequired);
	PRINT("Maximum CQ size: \t\t", maximumCQSize);


	//Initialize the RIO Manager and create our IOCP and CQ
	if (connectionServer.rioManager.InitializeRIO(rioMainConfig.bufferSize,
		numberBuffersRequired,
		rioMainConfig.spinCount,
		rioMainConfig.dequeueCount) != 0) {
		// Error exit - can't initialize RIO
		return 0;
	}
	connectionServer.iocp = connectionServer.rioManager.CreateIOCP();
	if (connectionServer.iocp == INVALID_HANDLE_VALUE) {
		// Error exit - couldn't make IOCP
		return 0;
	}
	CQ_Handler cqHandler = connectionServer.rioManager.CreateCQ(maximumCQSize);
	if (cqHandler.rio_CQ == RIO_INVALID_CQ) {
		// Error exit - couldn't make CQ
		return 0;
	}
	connectionServer.cqHandler = cqHandler;
	

	//Create each service
	for each (auto serviceData in services)
	{	
		if (connectionServer.rioManager.CreateRIOSocket(
			serviceData.serviceType, 
			serviceData.serviceCode, 
			serviceData.servicePort,
			serviceData.serviceMaxClients,		//
			serviceData.serviceMaxAccepts,
			serviceData.serviceRQMaxReceives,
			serviceData.serviceRQMaxSends,
			serviceData.isAddressRequired) != 0) {
			// Error exit - error creating service
			return 0;
		}
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

					default:
						cout << "Some other key pressed" << endl;
						break;

					}
				}
			}
		}

		SetConsoleMode(consoleHandle, mode);
	}

	//##########################################
	//		Clean up Threads and RIO Manager
	//##########################################

	//Wait for threads to exit
	for each(auto thread in threadPool)
	{
		if (thread != nullptr) {
			thread->join();
			delete thread;
		}
	}
	threadPool.clear();

	//Clean-up RIO Manager
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
	//std::vector<Instruction>* instructionSet;
	EXTENDED_OVERLAPPED* extendedOverlapped = 0;
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

		//##########################################
		//		Get Queued Completion Status
		//##########################################

		if (!GetQueuedCompletionStatus(connectionServer->iocp, &bytes, &key, (OVERLAPPED**)&extendedOverlapped, INFINITE)) {
			PRINT("ERROR: Could not call GQCS. . .", " ");
			break;
		}

		//##########################################
		//		Completion Key Demultiplexing
		//##########################################

		switch ((COMPLETION_KEY)key) {

		case CK_RIO:
			// Get RIO results
			numResults = connectionServer->rioManager.GetCompletedResults(results, rioResults);
			if (numResults == 0) {
				PRINT("ERROR: RIO Completion Queue found empty. . .", " ");
			}
			else if (numResults == RIO_CORRUPT_CQ) {
				PRINT("ERROR: RIO Completion Queue corrupted. . .", " ");
			}

			// RIO Result processing
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

				std::vector<Instruction>* instructionSet = processManager.GetInstructions(result);

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
			connectionServer->rioManager.CreateRIOSocket(TCPConnection, extendedOverlapped->serviceType, extendedOverlapped->relevantSocket);
			connectionServer->rioManager.ResetAcceptCall(extendedOverlapped);
			break;


		case CK_QUIT:
			cout << "\nReceived Quit Command from Main Thread." << endl;
			cout << "\nPrinting Count on Thread #" << threadID << endl;
			cout << "\tReceives:\t" << receiveCount << endl;
			cout << "\tSends:\t\t" << sendCount << endl;
			cout << "\tBufferFrees:\t" << freeBufferCount << endl;
			quitTrigger = true;
			PostQueuedCompletionStatus(connectionServer->iocp, 0, CK_QUIT, extendedOverlapped);
			break;


		case CK_GETINFO:
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


		default:
			PRINT("ERROR: Received erroneous message in IOCP queue", " ");
			break;

		} // switch((COMPLETION_KEY)key)
		
		if (quitTrigger) {
			break;
		}
	} // while (true)
} // Main Process (thread function)