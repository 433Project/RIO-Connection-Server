#include "stdafx.h"
#include "RIOManager.h"


RIOManager::RIOManager()
{

}

int RIOManager::SetConfiguration(_TCHAR* config[]) {

	return 0;
}

///This function
int RIOManager::InitializeRIO()
{
	// 1. Initialize WinSock
	WSADATA wsaData;

	if (0 != ::WSAStartup(0x202, &wsaData)) {
		return -1;
		//ReportError("WSAStartup", true);
	}

	// 2. Load RIO Extension Functions
	socketRIO = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_UDP, NULL, 0, WSA_FLAG_REGISTERED_IO);
	if (socketRIO == INVALID_SOCKET) {
		return -2;
		//ReportError("WSASocket - UDP Socket", true);
	}

	if (NULL != WSAIoctl(
		socketRIO,
		SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER,
		&rioFunctionTableID,
		sizeof(GUID),
		(void**)&rioFunctions,
		sizeof(rioFunctions),
		&dwBytes,
		NULL,
		NULL))
	{
		return -3;
		//ReportError("WSAIoctl - TCP Server - RIO", true);
	}

	// 3. **Initialize Buffer Manager**//

	//**Initialize Buffer Manager**//

	return 0;
}

///This function creates a new IOCP for the RIOManager instance and registers the IOCP with the RIOManager
HANDLE RIOManager::CreateIOCP() {
	HANDLE hIOCP = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);
	if (NULL == hIOCP) {
		return NULL;
		//ReportError("CreateIoComplectionPort - Create", true);
	}

	//**Register New IOCP with RIO Manager**//

	//**Register New IOCP with RIO Manager**//

	return hIOCP;
}

///This function creates a new RIO Completion Queue with IOCP Queue and Completion Key specified (For Multi-CQ systems with multi-IOCP)
RIO_CQ RIOManager::CreateCQ(HANDLE hIOCP, COMPLETION_KEY completionKey) {
	OVERLAPPED overlapped;
	RIO_NOTIFICATION_COMPLETION rioNotificationCompletion;
	RIO_CQ rioCompletionQueue;
	CRITICAL_SECTION criticalSection;

	rioNotificationCompletion.Type = RIO_IOCP_COMPLETION;
	rioNotificationCompletion.Iocp.IocpHandle = hIOCP;
	rioNotificationCompletion.Iocp.CompletionKey = (void*)completionKey;
	rioNotificationCompletion.Iocp.Overlapped = &overlapped;

	rioCompletionQueue = rioFunctions.RIOCreateCompletionQueue(
		1000,	//MAX_PENDING_RECEIVES + MAX_PENDING_SENDS
		&rioNotificationCompletion);
	if (rioCompletionQueue == RIO_INVALID_CQ) {
		return RIO_INVALID_CQ;
		//ReportError("RIOCreateCompletionQueue", true);
	}

	InitializeCriticalSectionAndSpinCount(&criticalSection, 4000); //Add Spin Count Parameter here

	//*** Need to Store this RIO_CQ handle into the RIO Manager Instance ***

	return rioCompletionQueue;
}

///This function creates a new RIO Completion Queue with default IOCP Queue but custom Completion Key (For creating Multi-CQ system with one IOCP)
RIO_CQ RIOManager::CreateCQ(COMPLETION_KEY completionKey) {
	return CreateCQ(GetMainIOCP(), completionKey);
}

///This function creates a new RIO Completion Queue with IOCP Queue specified (For creating main-CQ in Multi-IOCP system)
RIO_CQ RIOManager::CreateCQ(HANDLE hIOCP) {
	return CreateCQ(hIOCP, CK_RIO);
}

///This function creates a new RIO Completion Queue with default values (For creating main-CQ for main-IOCP queue)
RIO_CQ RIOManager::CreateCQ() {
	return CreateCQ(GetMainIOCP());
}

///This function creates a new RIO Socket of various types
int RIOManager::CreateRIOSocket(SocketType socketType, int port, RIO_CQ receiveCQ, RIO_CQ sendCQ, HANDLE hIOCP) {
	SOCKET newSocket;
		sockaddr_in socketAddress;
	socketAddress.sin_family = AF_INET;
	socketAddress.sin_port = htons(port);
	socketAddress.sin_addr.s_addr = INADDR_ANY;
	IPPROTO ipProto;
	DWORD controlCode;
	bool isListener;
	RIO_EXTENSION_FUNCTION_TABLE rioFunctions;
	LPFN_ACCEPTEX acceptExFunction;
	RIO_RQ rio_RQ;

	switch (socketType) {
	case UDPSocket:
		ipProto = IPPROTO_UDP;
		controlCode = SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER;
		isListener = false;
		break;
	case TCPListener:
		ipProto = IPPROTO_TCP;
		controlCode = SIO_GET_EXTENSION_FUNCTION_POINTER;
		isListener = true;
		break;
	case TCPClient:
		ipProto = IPPROTO_TCP;
		controlCode = SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER;
		isListener = false;
		break;
	case TCPServer:
		ipProto = IPPROTO_TCP;
		controlCode = SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER;
		isListener = false;
		break;
	default:
		return -1; //Incorrect socket Type
	}
	
	newSocket = WSASocket(AF_INET, SOCK_DGRAM, ipProto, NULL, 0, WSA_FLAG_REGISTERED_IO);

	if (newSocket == INVALID_SOCKET) {
		return -2;
		//ReportError("WSASocket - TCP Server", true);
	}

	if (SOCKET_ERROR == ::bind(newSocket, reinterpret_cast<struct sockaddr *>(&socketAddress), sizeof(socketAddress))) {
		return -3;
		//ReportError("bind - TCP Server", true);
	}

	if (isListener) {
		if (NULL != WSAIoctl(
			newSocket,
			SIO_GET_EXTENSION_FUNCTION_POINTER,
			&acceptExID,
			sizeof(GUID),
			(void**)&acceptExFunction,
			sizeof(acceptExFunction),
			&dwBytes,
			NULL,
			NULL))
		{
			return -4;
			//ReportError("WSAIoctl - TCP Server - AcceptEx", true);
		}

		if (SOCKET_ERROR == listen(newSocket, 100)) {	//MAX_LISTEN_BACKLOG_SERVER
			return -5;
			//ReportError("listen - TCP Server", true);
		}

		hIOCP = ::CreateIoCompletionPort(
			(HANDLE)newSocket,
			hIOCP,
			(ULONG_PTR)CK_ACCEPT_SERVER,			//////////
			4);								//MAX_CONCURRENT_THREADS
		if (NULL == hIOCP) {
			return -6;
			//ReportError("CreateIoComplectionPort - TCP Server", true);
		}
	}
	else {
		if (NULL != WSAIoctl(
			newSocket,
			controlCode,
			&rioFunctionTableID,
			sizeof(GUID),
			(void**)&rioFunctions,
			sizeof(rioFunctions),
			&dwBytes,
			NULL,
			NULL))
		{
			return -4;
			//ReportError("WSAIoctl - UDP Socket", true);
		}

		rio_RQ = rioFunctions.RIOCreateRequestQueue(
			newSocket, 1000, 1,		//MAX_PENDING_RECEIVES_UDP, MAX_PENDING_SENDS_UDP
			1000, 1, receiveCQ,
			sendCQ, NULL);			//Need to define socket context!!!
		if (rio_RQ == RIO_INVALID_RQ) {
			return -7;
			//ReportError("RIOCreateRequestQueue - UDP Socket", true);
		}
	}

	return 0;
}

int RIOManager::CreateRIOSocket(SocketType socketType, RIO_CQ receiveCQ, RIO_CQ sendCQ) {
	return CreateRIOSocket(socketType, 0, receiveCQ, sendCQ, GetMainIOCP());
}

int RIOManager::CreateRIOSocket(SocketType socketType, int port, RIO_CQ receiveCQ, RIO_CQ sendCQ) {
	return CreateRIOSocket(socketType, port, receiveCQ, sendCQ, GetMainIOCP());
}

int RIOManager::CreateRIOSocket(SocketType socketType, int port, HANDLE hIOCP) {
	return CreateRIOSocket(socketType, port, GetMainRIOCQ(), GetMainRIOCQ(), hIOCP);
}

int RIOManager::CreateRIOSocket(SocketType socketType, int port) {
	return CreateRIOSocket(socketType, port, GetMainRIOCQ(), GetMainRIOCQ(), GetMainIOCP());
}

int RIOManager::CreateRIOSocket(SocketType socketType) {
	return CreateRIOSocket(socketType, 0, GetMainRIOCQ(), GetMainRIOCQ(), GetMainIOCP());
}


///This function gets the main IOCP



RIOManager::~RIOManager()
{
}

/*
//***Global Variables***
RIO_EXTENSION_FUNCTION_TABLE g_RIO_UDP, g_RIO_TCPClient, g_RIO_TCPServer;
LPFN_ACCEPTEX g_AcceptExClient, g_AcceptExServer;
SOCKET g_SocketUDP, g_SocketTCPClient, g_SocketTCPServer;
DWORD g_PortUDP, g_PortTCPClient, g_PortTCPServer;
HANDLE g_hIOCP = 0;
RIO_CQ g_RIOCQ = 0;
RIO_RQ g_RIORQ_UDP = 0;

CRITICAL_SECTION g_CriticalSection;
DWORD g_SpinCount;
DWORD MAX_CONCURRENT_THREADS = 4;
DWORD MAX_PENDING_RECEIVES = 10000;
DWORD MAX_PENDING_SENDS = 1000;
DWORD MAX_PENDING_RECEIVES_UDP = 10000;
DWORD MAX_PENDING_SENDS_UDP = 1000;
DWORD MAX_PENDING_RECEIVES_TCPServer = 10000;
DWORD MAX_PENDING_SENDS_TCPServer = 1000;
DWORD MAX_CLIENTS = 10000000;
DWORD MAX_SERVERS = 100;
DWORD MAX_LISTEN_BACKLOG_CLIENT = 10000;
DWORD MAX_LISTEN_BACKLOG_SERVER = 20;


//*********************************************************
//************************SETUP****************************
//*********************************************************

// Setup Part 0: Load Setup Variables from File, Initialize WinSock, and Get Extension Functions
// A. Load Setup Variables from File
g_PortUDP = 9433;
g_PortTCPClient = 10433;
g_PortTCPServer = 11433;



// Setup Part 1: Create RIO Buffers
// Buffer Manager Class

// Setup Part 2: Create IOCP Queue

g_hIOCP = ::CreateIoCompletionPort(
(HANDLE)g_SocketTCPServer,
g_hIOCP,
(ULONG_PTR)CK_ACCEPT_SERVER,
MAX_CONCURRENT_THREADS);
if (NULL == g_hIOCP) {
ReportError("CreateIoComplectionPort - TCP Server", true);
}
g_hIOCP = ::CreateIoCompletionPort(
(HANDLE)g_SocketTCPClient,
g_hIOCP,
(ULONG_PTR)CK_ACCEPT_CLIENT,
MAX_CONCURRENT_THREADS);
if (NULL == g_hIOCP) {
ReportError("CreateIoComplectionPort - TCP Client", true);
}


//*********************************************************
//**********************Initiation*************************
//*********************************************************

//Post Receives/Accepts
//Initiate Threads




//Cleanup
g_RIO_UDP.RIOCloseCompletionQueue(g_RIOCQ);
closesocket(g_SocketTCPServer);
closesocket(g_SocketTCPClient);
closesocket(g_SocketUDP);
CloseHandle(g_hIOCP);



*/
