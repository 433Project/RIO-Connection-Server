// RIOConnectionServer.cpp : 콘솔 응용 프로그램에 대한 진입점을 정의합니다.
//

#include "stdafx.h"
#include "RIOManager.h"

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




inline void ReportError(
	const char *pFunction, bool willExit);

int _tmain(int argc, _TCHAR* argv[])
{
	//*********************************************************
	//************************SETUP****************************
	//*********************************************************

	// Setup Part 0: Load Setup Variables from File, Initialize WinSock, and Get Extension Functions
	// A. Load Setup Variables from File
	g_PortUDP = 9433;
	g_PortTCPClient = 10433;
	g_PortTCPServer = 11433;

	// B. Initialize Critical Section
	InitializeCriticalSectionAndSpinCount(&g_CriticalSection, g_SpinCount);

	// C. Initialize WinSock and Sockets
	WSADATA wsaData;
	
	if (0 != ::WSAStartup(0x202, &wsaData)) {
		ReportError("WSAStartup", true);
	}

	g_SocketTCPServer = WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, NULL, 0, WSA_FLAG_REGISTERED_IO);

	if (g_SocketUDP == INVALID_SOCKET) {
		ReportError("WSASocket - TCP Server", true);
	}

	g_SocketTCPClient = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_REGISTERED_IO);

	if (g_SocketUDP == INVALID_SOCKET) {
		ReportError("WSASocket - TCP Client", true);
	}

	g_SocketUDP = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_REGISTERED_IO);

	if (g_SocketUDP == INVALID_SOCKET) {
		ReportError("WSASocket - UDP Socket", true);
	}

	sockaddr_in socketAddress;
	socketAddress.sin_family = AF_INET;
	socketAddress.sin_port = htons(g_PortTCPServer);
	socketAddress.sin_addr.s_addr = INADDR_ANY;

	if (SOCKET_ERROR == ::bind(g_SocketTCPServer, reinterpret_cast<struct sockaddr *>(&socketAddress), sizeof(socketAddress))) {
		ReportError("bind - TCP Server", true);
	}

	socketAddress.sin_port = htons(g_PortTCPClient);

	if (SOCKET_ERROR == ::bind(g_SocketTCPClient, reinterpret_cast<struct sockaddr *>(&socketAddress), sizeof(socketAddress))) {
		ReportError("bind - TCP Client", true);
	}

	socketAddress.sin_port = htons(g_PortUDP);

	if (SOCKET_ERROR == ::bind(g_SocketUDP, reinterpret_cast<struct sockaddr *>(&socketAddress), sizeof(socketAddress))) {
		ReportError("bind - UDP Socket", true);
	}

	// D. Get Extension Functions (RIO and AcceptEx)
	GUID rioFunctionTableID = WSAID_MULTIPLE_RIO;
	GUID acceptExID = WSAID_ACCEPTEX;
	DWORD dwBytes = 0;

	if (NULL != WSAIoctl(
		g_SocketTCPServer,
		SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER,
		&rioFunctionTableID,
		sizeof(GUID),
		(void**)&g_RIO_TCPServer,
		sizeof(g_RIO_TCPServer),
		&dwBytes,
		NULL,
		NULL))
	{
		ReportError("WSAIoctl - TCP Server - RIO", true);
	}

	if (NULL != WSAIoctl(
		g_SocketTCPClient,
		SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER,
		&rioFunctionTableID,
		sizeof(GUID),
		(void**)&g_RIO_TCPClient,
		sizeof(g_RIO_TCPClient),
		&dwBytes,
		NULL,
		NULL))
	{
		ReportError("WSAIoctl - TCP Client - RIO", true);
	}

	if (NULL != WSAIoctl(
		g_SocketUDP,
		SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER,
		&rioFunctionTableID,
		sizeof(GUID),
		(void**)&g_RIO_UDP,
		sizeof(g_RIO_UDP),
		&dwBytes,
		NULL,
		NULL))
	{
		ReportError("WSAIoctl - UDP Socket", true);
	}

	if (NULL != WSAIoctl(
		g_SocketTCPServer,
		SIO_GET_EXTENSION_FUNCTION_POINTER,
		&acceptExID,
		sizeof(GUID),
		(void**)&g_AcceptExServer,
		sizeof(g_AcceptExServer),
		&dwBytes,
		NULL,
		NULL))
	{
		ReportError("WSAIoctl - TCP Server - AcceptEx", true);
	}

	if (NULL != WSAIoctl(
		g_SocketTCPClient,
		SIO_GET_EXTENSION_FUNCTION_POINTER,
		&acceptExID,
		sizeof(GUID),
		(void**)&g_AcceptExClient,
		sizeof(g_AcceptExClient),
		&dwBytes,
		NULL,
		NULL))
	{
		ReportError("WSAIoctl - TCP Client - AcceptEx", true);
	}

	// Setup Part 1: Create RIO Buffers
	// Buffer Manager Class

	// Setup Part 2: Create IOCP Queue
	g_hIOCP = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);
	if (NULL == g_hIOCP) {
		ReportError("CreateIoComplectionPort - Create", true);
	}
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

	// Setup Part 3: Create RIO Completion Queue
	OVERLAPPED overlapped;
	RIO_NOTIFICATION_COMPLETION rioNotificationCompletion;

	rioNotificationCompletion.Type = RIO_IOCP_COMPLETION;
	rioNotificationCompletion.Iocp.IocpHandle = g_hIOCP;
	rioNotificationCompletion.Iocp.CompletionKey = (void*)CK_RIO;
	rioNotificationCompletion.Iocp.Overlapped = &overlapped;

	g_RIOCQ = g_RIO_TCPServer.RIOCreateCompletionQueue(
		MAX_PENDING_RECEIVES + MAX_PENDING_SENDS,
		&rioNotificationCompletion);
	if (g_RIOCQ == RIO_INVALID_CQ) {
		ReportError("RIOCreateCompletionQueue", true);
	}

	// Setup Part 4: Initialize UDP Socket
	g_RIORQ_UDP = g_RIO_UDP.RIOCreateRequestQueue(
		g_SocketUDP, MAX_PENDING_RECEIVES_UDP, 1,
		MAX_PENDING_SENDS_UDP, 1, g_RIOCQ,
		g_RIOCQ, NULL);
	if (g_RIORQ_UDP == RIO_INVALID_RQ) {
		ReportError("RIOCreateRequestQueue - UDP Socket", true);
	}

	// Setup Part 5 Start TCP Listening
	if (SOCKET_ERROR == listen(g_SocketTCPServer, MAX_LISTEN_BACKLOG_SERVER)) {
		ReportError("listen - TCP Server", true);
	}
	if (SOCKET_ERROR == listen(g_SocketTCPClient, MAX_LISTEN_BACKLOG_CLIENT)) {
		ReportError("listen - TCP Client", true);
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
	

    return 0;
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

