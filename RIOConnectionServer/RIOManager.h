#pragma once

enum SocketType {
	UDPSocket,
	TCPListener,
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
	CK_QUIT,
	CK_RIO,
	CK_ACCEPT_CLIENT,
	CK_ACCEPT_SERVER
};

struct EXTENDED_RIO_BUF : public RIO_BUF {
	OperationType operationType;
};

class RIOManager
{
	RIO_EXTENSION_FUNCTION_TABLE rioFunctions;
	SOCKET socketRIO;
	GUID rioFunctionTableID = WSAID_MULTIPLE_RIO;
	GUID acceptExID = WSAID_ACCEPTEX;
	DWORD dwBytes = 0;

public:
	RIOManager();
	~RIOManager();
	int SetConfiguration(_TCHAR* config[]);		//Assigns configuration variables for the RIO_M instance
	int InitializeRIO();						//Starts WinSock, Creates Buffer Manger and Registers Buffers
	HANDLE CreateIOCP();						//Creates a new IOCP for the RIO_M instance

	//Overloaded Series of Functions to Create a New RIO Completion Queue
	RIO_CQ CreateCQ(HANDLE hIOCP, COMPLETION_KEY completionKey);	//IOCP Queue and Completion Key specified (For Multi-CQ system with Multi-IOCP)
	RIO_CQ CreateCQ(COMPLETION_KEY completionKey);					//Create CQ with default IOCP Queue but custom Completion Key (For creating Multi-CQ system with one IOCP)
	RIO_CQ CreateCQ(HANDLE hIOCP);									//Create CQ with IOCP Queue specified (For creating main-CQ in Multi-IOCP system)
	RIO_CQ CreateCQ();												//Default (For creating main-CQ for main-IOCP queue)

	//Overloaded Series of Functions to Create a new RIO Socket of various types
	int CreateRIOSocket(SocketType socketType, RIO_CQ receiveCQ, RIO_CQ sendCQ);			//TCP Client or Server with CQs specified
	int CreateRIOSocket(SocketType socketType, int port, RIO_CQ receiveCQ, RIO_CQ sendCQ);	//UDP Socket with CQs specified
	int CreateRIOSocket(SocketType socketType, int port, HANDLE hIOCP);						//TCP Listener with IOCP queue specified
	int CreateRIOSocket(SocketType socketType, int port);									//UDP Socket OR TCP Listener with default handles
	int CreateRIOSocket(SocketType socketType);												//Any Type with default values

	int GetCompletedResults(EXTENDED_RIO_BUF* results[]);
	int ProcessInstruction(InstructionType instructionType);
	void Shutdown();
private:
	int CloseSocket();
	int RegisterIOCP();
	int RegisterRIOCQ();
	int RegisterRIORQ();
	HANDLE GetMainIOCP();
};

