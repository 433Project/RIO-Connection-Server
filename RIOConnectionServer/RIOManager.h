#pragma once
#include <unordered_map>
#include <deque>
#include <iostream>
#include <sstream>

#define PRINT_MESSAGES

using namespace std;

enum SocketType {
	UDPSocket,
	TCPListener,
	TCPConnection,
	TCPClient,
	TCPServer
};

enum InstructionType {
	SEND,
	RECEIVE,
	CLOSESOCKET,
	FREEBUFFER
};

enum OperationType {
	OP_SEND,
	OP_RECEIVE
};

enum COMPLETION_KEY {
	CK_QUIT = 0,
	CK_RIO = 1,
	CK_ACCEPT =2
};

enum SubjectType {
	MATCHING_SERVER,


};

struct ReceivedData {
	OperationType operationType;
	void* buffer;
	int offset;
	int length;
};

struct EXTENDED_OVERLAPPED : OVERLAPPED {
	DWORD identifier;
	SOCKET relevantSocket;
};

struct CQ_Handler {
	RIO_CQ rio_CQ;
	CRITICAL_SECTION criticalSection;
};

struct Instruction {
	InstructionType type;
	int socketContext;		//Destination Code
	int destinationType;
	char* buffer;
	int length;
};

typedef std::unordered_map<int, RIO_RQ> SocketList;

struct ConnectionServerService {
	int port;
	SOCKET listeningSocket;
	RIO_CQ receiveCQ;
	RIO_CQ sendCQ;
	SocketList* socketList;
};

typedef std::unordered_map<DWORD, ConnectionServerService> ServiceList;

typedef deque<HANDLE> HandleList;

typedef deque<CQ_Handler> CQList;

class RIOManager
{
	RIO_EXTENSION_FUNCTION_TABLE rioFunctions; 
	LPFN_ACCEPTEX acceptExFunction;
	
	HandleList iocpList;
	SOCKET socketRIO;
	CQList rioCQList;
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
	int CreateRIOSocket(SocketType socketType, int serviceType, SOCKET newSocket, int port, RIO_CQ receiveCQ, RIO_CQ sendCQ, HANDLE hIOCP);	//Any Type of Socket 
	int CreateRIOSocket(SocketType socketType, int serviceType, SOCKET newSocket, RIO_CQ receiveCQ, RIO_CQ sendCQ);							//TCP Client or Server with CQs specified
	//int CreateRIOSocket(SocketType socketType, DWORD serviceType, int port, RIO_CQ receiveCQ, RIO_CQ sendCQ);				//UDP Socket with CQs specified
	//int CreateRIOSocket(SocketType socketType, int port, HANDLE hIOCP);														//TCP Listener with IOCP queue specified
	//int CreateRIOSocket(SocketType socketType, int port);																	//UDP Socket OR TCP Listener with default handles
	//int CreateRIOSocket(SocketType socketType, DWORD serviceType);																				//Any Type with default values
	//int CreateRIOSocket(SocketType socketType);																				//Any Type with default values

	int SetServiceCQs(int typeCode, RIO_CQ receiveCQ, RIO_CQ sendCQ);


	//int GetCompletedResults(vector<ReceivedData*>& results);
	//int ProcessInstruction(InstructionType instructionType);

	int NewConnection(EXTENDED_OVERLAPPED* extendedOverlapped);

	void Shutdown();
private:
	int CreateNewService(int typeCode, int portNumber, SOCKET listeningSocket);
	//SocketList* GetService(DWORD typeCode);
	int AddEntryToService(int typeCode, int socketContext, RIO_RQ rioRQ);
	SOCKET GetListeningSocket(int typeCode);
	/*int CloseSocket();
	int RegisterRIOCQ();
	int RegisterRIORQ();*/
	int BeginAcceptEx(EXTENDED_OVERLAPPED* extendedOverlapped);
	HANDLE GetMainIOCP();
	//RIO_CQ GetMainRIOCQ();
	void CloseIOCPHandles();
	void CloseCQs();
	void PrintMessageFormatter(int level, string type, string subtype, string message);
	void PrintMessageFormatter(int level, string type, string message);
	void PrintWindowsErrorMessage();
	void PrintBox(string word);
};

