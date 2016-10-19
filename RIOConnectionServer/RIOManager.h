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
public:
	RIOManager();
	~RIOManager();
	int SetConfiguration(_TCHAR* config[]);		//Assigns configuration variables for the RIO_M instance
	int InitializeRIO();						//Starts WinSock, Creates Buffer Manger and Registers Buffers
	int CreateIOCP(HANDLE* hIOCP);				//Creates a new IOCP for the RIO_M instance
	int CreateCQ(); //Figure out CQ identifier

	//Overloaded Series of Functions to Create a new RIO Socket of various types
	int CreateRIOSocket(enum SocketType, RIO_CQ receiveCQ, RIO_CQ sendCQ);				//TCP Client or Server with CQs specified
	int CreateRIOSocket(enum SocketType, int port, RIO_CQ receiveCQ, RIO_CQ sendCQ);	//UDP Socket with CQs specified
	int CreateRIOSocket(enum SocketType, int port, HANDLE hIOCP);						//TCP Listener with IOCP queue specified
	int CreateRIOSocket(enum SocketType, int port);										//UDP Socket OR TCP Listener with default handles
	int CreateRIOSocket(enum SocketType);												//Any Type with default values

	int GetCompletedResults(EXTENDED_RIO_BUF* results[]);
	int ProcessInstruction(enum InstructionType);
	void Shutdown();
private:
	int CloseSocket();
};

