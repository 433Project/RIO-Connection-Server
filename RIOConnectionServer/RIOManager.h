#pragma once
#include <unordered_map>
#include <deque>
#include <iostream>
#include <sstream>
#include "definitions.h"
#include "BufferManager.h"
#include "Ws2tcpip.h"

//#define PRINT_MESSAGES

typedef deque<EXTENDED_OVERLAPPED> AcceptStructs;

typedef std::unordered_map<int, RQ_Handler> SocketList;

struct ConnectionServerService {
	//Main components
	int port;
	CQ_Handler receiveCQ;
	CQ_Handler sendCQ;
	bool isUDPService;

	//Listening and connection accepts
	SOCKET listeningSocket;
	LPFN_ACCEPTEX acceptExFunction;
	AcceptStructs acceptStructs;

	//Connections
	CRITICAL_SECTION socketListCriticalSection;			//Critical Section for modifying the socket list hash
	SocketList* socketList;

	//UDP Specific
	CRITICAL_SECTION udpCriticalSection;				//Critical Section for using the UDP RQ
	RIO_RQ udpRQ;

	//Round-Robin Service
	bool isAddressRequired;
	CRITICAL_SECTION roundRobinCriticalSection;			//Critical Section for modifying roundRobinLocation
	int roundRobinLocation;
};

typedef std::unordered_map<DWORD, ConnectionServerService> ServiceList;

typedef deque<HANDLE> HandleList;

typedef deque<CQ_Handler> CQList;

class RIOManager
{
	RIO_EXTENSION_FUNCTION_TABLE rioFunctions; 
	LPFN_ACCEPTEX acceptExFunctionMain;
	BufferManager bufferManager;
	EXTENDED_OVERLAPPED mainExtendedOverlapped;

	CRITICAL_SECTION consoleCriticalSection;
	CRITICAL_SECTION serviceListCriticalSection;
	std::vector<CRITICAL_SECTION> criticalSectionDump;
	
	HandleList iocpList;		//Keep track of all IOCP queues for cleanup
	SOCKET socketRIO;			//A dedicated socket in order to load extension functions
	CQList rioCQList;			//Keep track of all RIO CQs for cleanup
	GUID rioFunctionTableID = WSAID_MULTIPLE_RIO;
	GUID acceptExID = WSAID_ACCEPTEX;
	DWORD dwBytes = 0;

	//Data Structures for Managing Servers/Clients
	ServiceList serviceList;	//Keeps track of all generated services

public:
	RIOManager();
	~RIOManager();
	int SetConfiguration(_TCHAR* config[]);		//Assigns configuration variables for the RIO_M instance
	int InitializeRIO();						//Starts WinSock, Creates Buffer Manger and Registers Buffers
	HANDLE CreateIOCP();						//Creates a new IOCP for the RIO_M instance

	//Overloaded Series of Functions to Create a New RIO Completion Queue
	CQ_Handler CreateCQ(HANDLE hIOCP, COMPLETION_KEY completionKey);	//IOCP Queue and Completion Key specified (For Multi-CQ system with Multi-IOCP)
	CQ_Handler CreateCQ(COMPLETION_KEY completionKey);					//Create CQ with default IOCP Queue but custom Completion Key (For creating Multi-CQ system with one IOCP)
	CQ_Handler CreateCQ(HANDLE hIOCP);									//Create CQ with IOCP Queue specified (For creating main-CQ in Multi-IOCP system)
	CQ_Handler CreateCQ();												//Default (For creating main-CQ for main-IOCP queue)

	//Overloaded Series of Functions to Create a new RIO Socket of various types
	int CreateRIOSocket(SocketType socketType, int serviceType, int port, SOCKET newSocket, CQ_Handler receiveCQ, CQ_Handler sendCQ, HANDLE hIOCP);	//Any Type of Socket 
	int CreateRIOSocket(SocketType socketType, int serviceType, SOCKET newSocket, CQ_Handler receiveCQ, CQ_Handler sendCQ);							//TCP Client or Server with CQs specified
	int CreateRIOSocket(SocketType socketType, int serviceType, SOCKET newSocket);							//TCP Client or Server with CQs specified
	int CreateRIOSocket(SocketType socketType, int serviceType, int port); //UDP Service with defaults or TCP listener with defaults

	int SetServiceCQs(int typeCode, CQ_Handler receiveCQ, CQ_Handler sendCQ);
	int SetServiceAddressSpecificity(int serviceType, bool isAddressRequired);

	int GetCompletedResults(vector<EXTENDED_RIO_BUF*>& results, RIORESULT* rioResults, CQ_Handler cqHandler);
	int GetCompletedResults(vector<EXTENDED_RIO_BUF*>& results, RIORESULT* rioResults);
	int ProcessInstruction(Instruction instruction);

	int ConfigureNewSocket(EXTENDED_OVERLAPPED* extendedOverlapped);
	int ResetAcceptCall(EXTENDED_OVERLAPPED* extendedOverlapped);

	int NewConnection(EXTENDED_OVERLAPPED* extendedOverlapped);

	//TEMPS
	int RIONotifyIOCP(RIO_CQ  rioCQ);
	void AssignConsoleCriticalSection(CRITICAL_SECTION critSec);
	//

	void CheckCriticalSections();
	void PrintServiceInformation();
	void PrintBufferUsageStatistics();

	void Shutdown();

private:
	int CreateNewService(int typeCode, int portNumber, bool isUDPService, SOCKET listeningSocket, RIO_RQ udpRQ, CRITICAL_SECTION udpCriticalSection, LPFN_ACCEPTEX acceptExFunction);
	int CreateNewService(int typeCode, int portNumber, bool isUDPService, SOCKET listeningSocket, RIO_RQ udpRQ, CRITICAL_SECTION udpCriticalSection);
	int CreateNewService(int typeCode, int portNumber, bool isUDPService, SOCKET listeningSocket, LPFN_ACCEPTEX acceptExFunction);
	int CreateNewService(int typeCode, int portNumber, bool isUDPService, SOCKET listeningSocket);
	int AddEntryToService(int typeCode, int socketContext, RIO_RQ rioRQ, SOCKET socket, CRITICAL_SECTION criticalSection);
	SOCKET GetListeningSocket(int typeCode);
	int BeginAcceptEx(EXTENDED_OVERLAPPED* extendedOverlapped, LPFN_ACCEPTEX acceptExFunction);
	HANDLE GetMainIOCP();
	CQ_Handler GetMainRIOCQ();
	bool PostReceiveOnUDPService(int serviceType);
	bool PostReceiveOnTCPService(int serviceType, int destinationCode);
	int FillAcceptStructures(int typeCode, int numStruct);
	int CloseServiceEntry(int typeCode, int socketContext);
	void CloseAllSockets();
	void CloseIOCPHandles();
	void CloseCQs();
	void PrintMessageFormatter(int level, string type, string subtype, string message);
	void PrintMessageFormatter(int level, string type, string message);
	void PrintWindowsErrorMessage();
	void PrintBox(string word);
};

