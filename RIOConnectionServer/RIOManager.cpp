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
#ifdef PRINT_MESSAGES
	PrintMessageFormatter(0, "RIO MANAGER", "InitializeRIO", "1. Initializing WinSock. . .");
#endif // PRINT_MESSAGES

	if (0 != ::WSAStartup(0x202, &wsaData)) {
#ifdef PRINT_MESSAGES
		PrintMessageFormatter(1, "ERROR", "WinSock Initialization Failed.");
#endif // PRINT_MESSAGES
		return -1;
	}
#ifdef PRINT_MESSAGES
	PrintMessageFormatter(1, "SUCCESS", " ");
#endif // PRINT_MESSAGES

	// 2. Load RIO Extension Functions
#ifdef PRINT_MESSAGES
	PrintMessageFormatter(1, "InitializeRIO", "2. Loading RIO Extension Functions. . .");
#endif // PRINT_MESSAGES
	socketRIO = WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, NULL, 0, WSA_FLAG_REGISTERED_IO);
	if (socketRIO == INVALID_SOCKET) {
#ifdef PRINT_MESSAGES
		PrintMessageFormatter(1, "ERROR", "WSASocket failed to generate socket.");
		PrintWindowsErrorMessage();
#endif // PRINT_MESSAGES
		return -2;
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
#ifdef PRINT_MESSAGES
		PrintMessageFormatter(1, "ERROR", "WSAIoctl failed to retrieve RIO extension functions.");
#endif // PRINT_MESSAGES
		return -3;
	}

#ifdef PRINT_MESSAGES
	PrintMessageFormatter(1, "SUCCESS", " ");
#endif // PRINT_MESSAGES

	// 3. **Initialize Buffer Manager**//
#ifdef PRINT_MESSAGES
	PrintMessageFormatter(1, "InitializeRIO", "3. . . .");
#endif // PRINT_MESSAGES

	//**Initialize Buffer Manager**//

#ifdef PRINT_MESSAGES
	PrintMessageFormatter(1, "COMPLETE", " ");
#endif // PRINT_MESSAGES
	return 0;
}

///This function creates a new IOCP queue for the RIOManager instance and registers the IOCP queue with the RIOManager.
///The first IOCP queue that is registered is registered as the "main" or "default" IOCP queue.
HANDLE RIOManager::CreateIOCP() {
	HANDLE hIOCP = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);
	if (NULL == hIOCP) {
		return NULL;
		//ReportError("CreateIoComplectionPort - Create", true);
	}

#ifdef PRINT_MESSAGES
	PrintMessageFormatter(0, "RIO MANAGER", "CreateIOCP", "Creating IOCP Handle. . .");
#endif // PRINT_MESSAGES

	//**Register New IOCP with RIO Manager**//
	iocpList.push_back(hIOCP);

#ifdef PRINT_MESSAGES
	int length = iocpList.size();
	PrintMessageFormatter(1, "SUCCESS", "Created and Added IOCP #" + to_string(length));
#endif // PRINT_MESSAGES

	return hIOCP;
}

///This function creates a new RIO Completion Queue with IOCP Queue and Completion Key specified (For Multi-CQ systems with multi-IOCP)
CQ_Handler RIOManager::CreateCQ(HANDLE hIOCP, COMPLETION_KEY completionKey) {
	CQ_Handler cqHandler;
	OVERLAPPED overlapped;
	RIO_NOTIFICATION_COMPLETION rioNotificationCompletion;

#ifdef PRINT_MESSAGES
	PrintMessageFormatter(0, "RIO MANAGER", "CreateCQ", "Creating RIO Completion Queue. . .");
#endif // PRINT_MESSAGES

	rioNotificationCompletion.Type = RIO_IOCP_COMPLETION;
	rioNotificationCompletion.Iocp.IocpHandle = hIOCP;
	rioNotificationCompletion.Iocp.CompletionKey = (void*)completionKey;
	rioNotificationCompletion.Iocp.Overlapped = &overlapped;

	cqHandler.rio_CQ = rioFunctions.RIOCreateCompletionQueue(
		1000,	//MAX_PENDING_RECEIVES + MAX_PENDING_SENDS
		&rioNotificationCompletion);
	if (cqHandler.rio_CQ == RIO_INVALID_CQ) {
#ifdef PRINT_MESSAGES
		PrintMessageFormatter(1, "ERROR", "CreateCQ failed to create an RIO Completion Queue.");
#endif // PRINT_MESSAGES
		return cqHandler;
	}

	InitializeCriticalSectionAndSpinCount(&cqHandler.criticalSection, 4000); //Add Spin Count Parameter here

	//*** Need to Store this RIO_CQ handle/Critical Section into the RIO Manager Instance ***
	rioCQList.push_back(cqHandler);

#ifdef PRINT_MESSAGES
	int length = rioCQList.size();
	PrintMessageFormatter(1, "SUCCESS", "Created and Added RIO CQ #" + to_string(length));
#endif // PRINT_MESSAGES

	return cqHandler;
}

///This function creates a new RIO Completion Queue with default IOCP Queue but custom Completion Key (For creating Multi-CQ system with one IOCP)
CQ_Handler RIOManager::CreateCQ(COMPLETION_KEY completionKey) {
	return CreateCQ(GetMainIOCP(), completionKey);
}

///This function creates a new RIO Completion Queue with IOCP Queue specified (For creating main-CQ in Multi-IOCP system)
CQ_Handler RIOManager::CreateCQ(HANDLE hIOCP) {
	return CreateCQ(hIOCP, CK_RIO);
}

///This function creates a new RIO Completion Queue with default values (For creating main-CQ for main-IOCP queue)
CQ_Handler RIOManager::CreateCQ() {
	return CreateCQ(GetMainIOCP());
}

///This function creates a new RIO Socket of various types
int RIOManager::CreateRIOSocket(SocketType socketType, int serviceType, SOCKET newSocket, int port, RIO_CQ receiveCQ, RIO_CQ sendCQ, HANDLE hIOCP) {
	sockaddr_in socketAddress;
	socketAddress.sin_family = AF_INET;
	socketAddress.sin_port = htons(port);
	socketAddress.sin_addr.s_addr = INADDR_ANY;
	int type;
	IPPROTO ipProto;
	DWORD controlCode;
	bool isListener, requiresBind;
	RIO_EXTENSION_FUNCTION_TABLE rioFunctions;
	LPFN_ACCEPTEX acceptExFunction;
	RIO_RQ rio_RQ;

	switch (socketType) {
		//Non-accepted Socket Cases
	case UDPSocket:
		type = SOCK_DGRAM;
		ipProto = IPPROTO_UDP;
		controlCode = SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER;
		isListener = false;
		requiresBind = true;
		newSocket = WSASocket(AF_INET, type, ipProto, NULL, 0, WSA_FLAG_REGISTERED_IO);
		break;
	case TCPListener:
		type = SOCK_STREAM;
		ipProto = IPPROTO_TCP;
		controlCode = SIO_GET_EXTENSION_FUNCTION_POINTER;
		isListener = true;
		requiresBind = true;
		newSocket = WSASocket(AF_INET, type, ipProto, NULL, 0, WSA_FLAG_REGISTERED_IO);
		break;
		//Accepted Socket Cases
	case TCPConnection:
		controlCode = SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER;
		isListener = false;
		requiresBind = false;
		break;
	default:
		return -1; //Incorrect socket Type
	}

	if (newSocket == INVALID_SOCKET) {
		return -2;
		//ReportError("WSASocket - TCP Server", true);
	}

	if (requiresBind) {
		if (SOCKET_ERROR == ::bind(newSocket, reinterpret_cast<struct sockaddr *>(&socketAddress), sizeof(socketAddress))) {
			return -3;
			//ReportError("bind - TCP Server", true);
		}
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
			(ULONG_PTR)CK_ACCEPT,			//////////
			4);								//MAX_CONCURRENT_THREADS
		if (NULL == hIOCP) {
			return -6;
			//ReportError("CreateIoComplectionPort - TCP Server", true);
		}

		//Create a new service to represent this new listening socket
		if (CreateNewService(serviceType, port, newSocket) < 0) {
			return -8;
		}
	}
	else {		//Non-Listeners
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

		int socketContext = (int)newSocket;

		rio_RQ = rioFunctions.RIOCreateRequestQueue(
			newSocket, 1000, 1,				//MAX_PENDING_RECEIVES_UDP, MAX_PENDING_SENDS_UDP
			1000, 1, receiveCQ,
			sendCQ, &socketContext);		//Need to define socket context!!!
		if (rio_RQ == RIO_INVALID_RQ) {
			return -7;
			//ReportError("RIOCreateRequestQueue - UDP Socket", true);
		}

		//Add a socket to a service
		if (socketType == UDPSocket) {
			if (CreateNewService(serviceType, port, newSocket) < 0) {
				return -8;
			}
		}
		else {
			if (AddEntryToService(serviceType, socketContext, rio_RQ) < 0) {
				return -9;
			}
		}
	}

	return 0;
}

int RIOManager::CreateRIOSocket(SocketType socketType, int serviceType, SOCKET relevantSocket, RIO_CQ receiveCQ, RIO_CQ sendCQ) {
	return CreateRIOSocket(socketType, serviceType, relevantSocket, 0, receiveCQ, sendCQ, INVALID_HANDLE_VALUE);
}

///This function allows one to customize the receive/send CQ of a particular service.
int RIOManager::SetServiceCQs(int typeCode, RIO_CQ receiveCQ, RIO_CQ sendCQ) {
	//Find the service entry
	ServiceList::iterator iter = serviceList.find(typeCode);
	if (iter == serviceList.end()) {
		return -1;		//Service doesn't exist
	}

	//Get the service entry
	ConnectionServerService service;
	service = iter->second;

	service.receiveCQ = receiveCQ;
	service.sendCQ = sendCQ;

	return 0;
}

///This function processes an AcceptEx completion by creating a new RIOSocket with the appropriate settings.
int RIOManager::NewConnection(EXTENDED_OVERLAPPED* extendedOverlapped) {
	RIO_CQ serviceCQs[2];

	//Find the service entry
	ServiceList::iterator iter = serviceList.find((*extendedOverlapped).identifier);
	if (iter == serviceList.end()) {
		return NULL;		//Service doesn't exist
	}

	//Get the service entry
	ConnectionServerService service;
	service = iter->second;

	serviceCQs[0] = service.receiveCQ;
	serviceCQs[1] = service.sendCQ;

	RIO_CQ* pointer = serviceCQs;

	if (CreateRIOSocket(TCPConnection,
		(*extendedOverlapped).identifier,
		(*extendedOverlapped).relevantSocket,
		serviceCQs[0],
		serviceCQs[1]) < 0)
	{
		return -1;
	}

	BeginAcceptEx(extendedOverlapped);

	//Get Buffer/Post Receives

	return 0;
}

///This function closes all resources associated with the RIOManager.
void RIOManager::Shutdown() {
#ifdef PRINT_MESSAGES
	PrintMessageFormatter(0, "RIO MANAGER", "SHUTDOWN", "Initializing shutdown sequence. . .");
#endif // PRINT_MESSAGES
	CloseCQs();
	CloseIOCPHandles();

#ifdef PRINT_MESSAGES
	PrintMessageFormatter(1, "COMPLETE", " ");
#endif // PRINT_MESSAGES
}


//////PRIVATE FUNCTIONS

///This function gets the main IOCP



///This function creates a new service in the RIO Manager service list.
///Note that the receive and send CQs are set to the default value.
int RIOManager::CreateNewService(int typeCode, int portNumber, SOCKET listeningSocket) {
	if (serviceList.find(typeCode) != serviceList.end()) {
		return -1;		//Service already exists
	}

	ConnectionServerService service;
	service.port = portNumber;
	service.listeningSocket = listeningSocket;
	//RIO_CQ mainRIOCQ = GetMainRIOCQ();
	//service.receiveCQ = mainRIOCQ;
	//service.sendCQ = mainRIOCQ;
	service.socketList = new SocketList();

	serviceList.insert(std::pair<DWORD, ConnectionServerService>(typeCode, service));

	return 0;

}



int RIOManager::AddEntryToService(int typeCode, int socketContext, RIO_RQ rioRQ) {
	//Find the service entry
	ServiceList::iterator iter = serviceList.find(typeCode);
	if (iter == serviceList.end()) {
		return -1;		//Service doesn't exist
	}

//Get the service entry
ConnectionServerService service;
service = iter->second;

//Get a pointer to the service list in which we will add to
SocketList* socketList;
socketList = service.socketList;

if ((*socketList).find(socketContext) != (*socketList).end()) {
	return -2;		//Particular socket entry already exists
}

if (rioRQ == NULL || rioRQ == RIO_INVALID_RQ) {
	return -3;		//Invalid RIO_RQ
}

//Add the socket context/ RQ pair into the service
(*socketList).insert(std::pair<int, RIO_RQ>(socketContext, rioRQ));

return 0;
}


SOCKET RIOManager::GetListeningSocket(int typeCode) {
	//Find the service entry
	ServiceList::iterator iter = serviceList.find(typeCode);
	if (iter == serviceList.end()) {
		return INVALID_SOCKET;		//Service doesn't exist
	}

	//Get the service entry
	ConnectionServerService service;
	service = iter->second;

	if (service.listeningSocket == NULL) {
		return INVALID_SOCKET;		//Socket Not Assigned
	}

	return service.listeningSocket;
}


int RIOManager::BeginAcceptEx(EXTENDED_OVERLAPPED* extendedOverlapped) {
	//Needed for AcceptEx
	DWORD bytes;
	char* buffer = NULL;

	if (!(acceptExFunction(			////////////
		GetListeningSocket((*extendedOverlapped).identifier),
		(*extendedOverlapped).relevantSocket,
		buffer,
		0,							//No read
		sizeof(sockaddr_in) + 16,
		sizeof(sockaddr_in) + 16,	//MSDN specifies that dwRemoteAddressLength "Cannot be zero."
		&bytes,
		extendedOverlapped
	)))
	{
		return -1;
	}

	return 0;
}

///Gets the first IOCP handle in the list of IOCPs
HANDLE RIOManager::GetMainIOCP() {
	if (iocpList.empty()) {
		return INVALID_HANDLE_VALUE;
	}
	return iocpList.front();
}

///This function goes through the list of IOCP handles and closes them all for proper cleanup
void RIOManager::CloseIOCPHandles() {
	HANDLE goodbyeIOCP;

#ifdef PRINT_MESSAGES
	PrintMessageFormatter(1, "CloseIOCPHandles", "Closing all IOCP Handles. . .");
	int i = 1;
#endif // PRINT_MESSAGES

	while (!iocpList.empty()) {
		goodbyeIOCP = iocpList.front();
#ifdef PRINT_MESSAGES
		PrintMessageFormatter(1, "LOOP", "Closing IOCP Handle #" + to_string(i));
		i++;
#endif // PRINT_MESSAGES
		CloseHandle(goodbyeIOCP);
		iocpList.pop_front();
	}

	return;
}

///This function 
void RIOManager::CloseCQs() {
	CQ_Handler goodbyeRIOCQ;

#ifdef PRINT_MESSAGES
	PrintMessageFormatter(1, "CloseCQs", "Closing all RIO CQs and critical sections. . .");
	int i = 1;
#endif // PRINT_MESSAGES

	while (!rioCQList.empty()) {
		goodbyeRIOCQ = rioCQList.front();
		rioCQList.pop_front();
#ifdef PRINT_MESSAGES
		PrintMessageFormatter(1, "LOOP", "Closing RIO_CQ #" + to_string(i));
#endif // PRINT_MESSAGES
		rioFunctions.RIOCloseCompletionQueue(goodbyeRIOCQ.rio_CQ);
#ifdef PRINT_MESSAGES
		PrintMessageFormatter(1, "LOOP", "Closing Critical Section #" + to_string(i));
		i++;
#endif // PRINT_MESSAGES
		DeleteCriticalSection(&goodbyeRIOCQ.criticalSection);
	}
	return;
}

///This function prints a message to console with a specified format (two boxes).
void RIOManager::PrintMessageFormatter(int level, string type, string subtype, string message) {
	//Initial Spacing based on "level"
	for (int i = 0; i < level; i++) {
		printf_s("               ");
	}

	//Print Boxes
	PrintBox(type);
	PrintBox(subtype);

	//Print Message
	printf_s("%s\n", message.c_str());

	return;
}

///This function prints a message to console with a specified format (one box).
void RIOManager::PrintMessageFormatter(int level, string type, string message) {
	//Initial Spacing based on "level"
	for (int i = 0; i < level; i++) {
		printf_s("               ");
	}

	//Print Boxes
	PrintBox(type);

	//Print Message
	printf_s("%s\n", message.c_str());

	return;
}

///This function prints a box with a word inside.
void RIOManager::PrintBox(string word) {
	int length;
	printf_s("[");

	if (word.empty()) {
		printf_s("STRING ERROR]\n");
		return;
	}

	length = word.length();
	if (length < 12) {
		printf_s("%s", word.c_str());
		for (int x = 0; x < (12 - length); x++) {
			printf_s(" ");
		}
	}
	else {
		printf_s("%s", word.substr(0, 12).c_str());
	}
	printf_s("] ");
}

///This function gets the last windows error and prints it using our message formatter.
void RIOManager::PrintWindowsErrorMessage() {
	wchar_t buf[256];
	FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(),
		MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), buf, 256, NULL);
	wstring ws(buf);
	PrintMessageFormatter(1, "MESSAGE", string(ws.begin(), ws.end()));
	return;
}


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
