#pragma once
#include <unordered_map>
#include <deque>
#include <iostream>
#include <sstream>
#include "definitions.h"
#include "BufferManager.h"
#include "Ws2tcpip.h"
#include "cvmarkersobj.h"

using namespace Concurrency::diagnostic;

//#define PRINT_MESSAGES

#ifdef _DEBUG
#define PRINT_FOUR(a, b, c, d)		PrintMessageFormatter(a, b, c, d)
#define PRINT_THREE(e, f, g)		PrintMessageFormatter(e, f, g)
#define PRINT_WIN_ERROR(h, i, j)	PrintMessageFormatter(h, i, j);		\
									PrintWindowsErrorMessage()
#else
#define PRINT_FOUR(a, b, c, d)
#define PRINT_THREE(e, f, g)
#define PRINT_WIN_ERROR(h, i, j)
#endif // _DEBUG

typedef deque<ExtendedOverlapped> AcceptStructs;

typedef std::unordered_map<int, RQ_Handler> SocketList;

struct ConnectionServerService 
{
	//Main components
	int port;
	int maxClients;
	int serviceMaxAccepts;
	int serviceRQMaxReceives;
	int serviceRQMaxSends;
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
	volatile int roundRobinLocation;

	ConnectionServerService() 
	{

	}

	ConnectionServerService(int valuePort,
		int valueMaxClients,
		int valueServiceMaxAccepts,
		int valueServiceRQMaxReceives,
		int valueServiceRQMaxSends,
		CQ_Handler valueReceiveCQ,
		CQ_Handler valueSendCQ,
		bool valueIsUDPService,
		SOCKET valueListeningSocket,
		LPFN_ACCEPTEX valueAcceptExFunction,
		AcceptStructs valueAcceptStructs,
		CRITICAL_SECTION valueSocketListCriticialSection,
		SocketList* valueSocketList,
		CRITICAL_SECTION valueUdpCriticialSection,
		RIO_RQ valueUdpRQ,
		bool valueIsAddressRequired,
		CRITICAL_SECTION valueRoundRobinCriticalSection,
		int valueRoundRobinLocation) 
	{
		port = valuePort;
		maxClients = valueMaxClients;
		serviceMaxAccepts = valueServiceMaxAccepts;
		serviceRQMaxReceives = valueServiceRQMaxReceives;
		serviceRQMaxSends = valueServiceRQMaxSends;
		receiveCQ = valueReceiveCQ;
		sendCQ = valueSendCQ;
		isUDPService = valueIsUDPService;
		listeningSocket = valueListeningSocket;
		acceptExFunction = valueAcceptExFunction;
		acceptStructs = valueAcceptStructs;
		socketListCriticalSection = valueSocketListCriticialSection;
		socketList = valueSocketList;
		udpCriticalSection = valueUdpCriticialSection;
		udpRQ = valueUdpRQ;
		isAddressRequired = valueIsAddressRequired;
		roundRobinLocation = valueRoundRobinLocation;
	}
};

typedef std::unordered_map<DWORD, ConnectionServerService> ServiceList;

typedef deque<HANDLE> HandleList;

typedef deque<CQ_Handler> CQList;

class RIOManager
{
	RIO_EXTENSION_FUNCTION_TABLE rioFunctions; 
	BufferManager bufferManager;
	ExtendedOverlapped mainExtendedOverlapped;

	CRITICAL_SECTION consoleCriticalSection;
	CRITICAL_SECTION serviceListCriticalSection;
	SRWLOCK serviceListSRWLock;
	ServiceList serviceList;	//Keeps track of all generated services

	
	HandleList iocpList;		//Keep track of all IOCP queues for cleanup
	SOCKET socketRIO;			//A dedicated socket in order to load extension functions
	CQList rioCQList;			//Keep track of all RIO CQs for cleanup
	std::vector<CRITICAL_SECTION> criticalSectionDump;

	GUID rioFunctionTableID		= WSAID_MULTIPLE_RIO;
	GUID acceptExID				= WSAID_ACCEPTEX;
	DWORD dwBytes				= 0;
	int rioSpinCount			= 4000;	//Default spincount of 4000
	int dequeueCount			= 2000;	//Default dequeue count of 2000

	marker_series mySeries;
	


public:
	RIOManager();
	~RIOManager();

	//Setup and configuration functions
	int InitializeRIO(int bufferSize, DWORD bufferCount, int spinCount, int rioDequeueCount);	//Starts WinSock, Creates Buffer Manger and Registers Buffers
	HANDLE CreateIOCP();													//Creates a new IOCP for the RIO_M instance
	//Overloaded Series of Functions to Create a New RIO Completion Queue
	CQ_Handler CreateCQ(int size, HANDLE hIOCP, COMPLETION_KEY completionKey);		//IOCP Queue and Completion Key specified (For Multi-CQ system with Multi-IOCP)
	CQ_Handler CreateCQ(int size, COMPLETION_KEY completionKey);					//Create CQ with default IOCP Queue but custom Completion Key (For creating Multi-CQ system with one IOCP)
	CQ_Handler CreateCQ(int size, HANDLE hIOCP);									//Create CQ with IOCP Queue specified (For creating main-CQ in Multi-IOCP system)
	CQ_Handler CreateCQ(int size);													//Default (For creating main-CQ for main-IOCP queue)
	//Overloaded Series of Functions to Create a new RIO Socket of various types
	int CreateRIOSocket(SOCKET_TYPE socketType, int serviceType, int port, SOCKET newSocket, CQ_Handler receiveCQ, CQ_Handler sendCQ, HANDLE hIOCP,
						int serviceMaxClients, int serviceMaxAccepts, int serviceRQMaxReceives, int serviceRQMaxSends, bool isAddressRequired);	//Any Type of Socket
	//int CreateRIOSocket(SocketType socketType, int serviceType, int port, SOCKET newSocket, CQ_Handler receiveCQ, CQ_Handler sendCQ, HANDLE hIOCP);	//Any Type of Socket 
	int CreateRIOSocket(SOCKET_TYPE socketType, int serviceType, SOCKET newSocket, CQ_Handler receiveCQ, CQ_Handler sendCQ);							//TCP Client or Server with CQs specified
	int CreateRIOSocket(SOCKET_TYPE socketType, int serviceType, int port, 
		int serviceMaxClients, int serviceMaxAccepts, int serviceRQMaxReceives, int serviceRQMaxSends, bool isAddressRequired);
	int CreateRIOSocket(SOCKET_TYPE socketType, int serviceType, SOCKET newSocket);							//TCP Client or Server with CQs specified
	int CreateRIOSocket(SOCKET_TYPE socketType, int serviceType, int port); //UDP Service with defaults or TCP listener with defaults

	int SetServiceCQs(int typeCode, CQ_Handler receiveCQ, CQ_Handler sendCQ);

	int GetCompletedResults(vector<ExtendedRioBuf*>& results, RIORESULT* rioResults, CQ_Handler cqHandler);
	int GetCompletedResults(vector<ExtendedRioBuf*>& results, RIORESULT* rioResults);
	int ProcessInstruction(Instruction instruction);

	int ConfigureNewSocket(ExtendedOverlapped* extendedOverlapped);
	int ResetAcceptCall(ExtendedOverlapped* extendedOverlapped);

	int NewConnection(ExtendedOverlapped* extendedOverlapped);

	int RIONotifyIOCP(RIO_CQ  rioCQ);
	void AssignConsoleCriticalSection(CRITICAL_SECTION critSec);

	void PrintServiceInformation();
	void PrintBufferUsageStatistics();

	void Shutdown();

private:
	int CreateNewService(int typeCode, int portNumber, int maxClients, int serviceMaxAccepts, int serviceRQMaxReceives, int serviceRQMaxSends, bool isAddressRequired, bool isUDPService, SOCKET listeningSocket, RIO_RQ udpRQ, CRITICAL_SECTION udpCriticalSection, LPFN_ACCEPTEX acceptExFunction);
	int CreateNewService(int typeCode, int portNumber, int maxClients, int serviceMaxAccepts, int serviceRQMaxReceives, int serviceRQMaxSends, bool isAddressRequired, bool isUDPService, SOCKET listeningSocket, RIO_RQ udpRQ, CRITICAL_SECTION udpCriticalSection);
	int CreateNewService(int typeCode, int portNumber, int maxClients, int serviceMaxAccepts, int serviceRQMaxReceives, int serviceRQMaxSends, bool isAddressRequired, bool isUDPService, SOCKET listeningSocket, LPFN_ACCEPTEX acceptExFunction);
	int CreateNewService(int typeCode, int portNumber, int maxClients, int serviceMaxAccepts, int serviceRQMaxReceives, int serviceRQMaxSends, bool isAddressRequired, bool isUDPService, SOCKET listeningSocket);
	int AddEntryToService(int typeCode, int socketContext, RIO_RQ rioRQ, SOCKET socket, CRITICAL_SECTION criticalSection);
	SOCKET GetListeningSocket(int typeCode);
	HANDLE GetMainIOCP();
	CQ_Handler GetMainRIOCQ();

	//Initiator functions
	int FillAcceptStructures(int typeCode, int numStruct);
	bool PostReceiveOnUDPService(int serviceType, DWORD flags);
	int BeginAcceptEx(ExtendedOverlapped* extendedOverlapped, LPFN_ACCEPTEX acceptExFunction);
	bool PostReceiveOnUDPService(int serviceType);
	bool PostReceiveOnTCPService(int serviceType, int destinationCode, DWORD flags);
	bool PostReceiveOnTCPService(int serviceType, int destinationCode);

	//Clean-up functions
	int CloseServiceEntry(int typeCode, int socketContext);
	void CloseAllSockets();
	void CloseIOCPHandles();
	void CloseCQs();

	//Printing functions
	void PrintMessageFormatter(int level, const string &type, const string &subtype, string message);
	void PrintMessageFormatter(int level, const string &type, string message);
	void PrintWindowsErrorMessage();
	void PrintBox(string word);
};

