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

	if (NULL != WSAIoctl(
		socketRIO,
		SIO_GET_EXTENSION_FUNCTION_POINTER,
		&acceptExID,
		sizeof(GUID),
		(void**)&acceptExFunctionMain,
		sizeof(acceptExFunctionMain),
		&dwBytes,
		NULL,
		NULL))
	{
#ifdef PRINT_MESSAGES
		PrintMessageFormatter(1, "ERROR", "WSAIoctl failed to retrieve Accept EX.");
#endif // PRINT_MESSAGES
		return -4;
	}

#ifdef PRINT_MESSAGES
	PrintMessageFormatter(1, "SUCCESS", " ");
#endif // PRINT_MESSAGES

	// 3. **Initialize Buffer Manager**//
#ifdef PRINT_MESSAGES
	PrintMessageFormatter(1, "InitializeRIO", "3. Initializing Buffer Mananger. . .");
#endif // PRINT_MESSAGES

	//**Initialize Buffer Manager**//
	bufferManager.Initialize(rioFunctions, 10000, 1024);

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

#ifdef PRINT_MESSAGES
	PrintMessageFormatter(1, "COMPLETE", " ");
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

#ifdef PRINT_MESSAGES
	PrintMessageFormatter(1, "COMPLETE", " ");
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
int RIOManager::CreateRIOSocket(SocketType socketType, int serviceType, int port, SOCKET newSocket, CQ_Handler receiveCQ, CQ_Handler sendCQ, HANDLE hIOCP) {
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

#ifdef PRINT_MESSAGES
	PrintMessageFormatter(0, "RIO MANAGER", "CreateRIOSocket", "Creating new RIO Socket. . .");
#endif // PRINT_MESSAGES

	switch (socketType) {
		//Non-accepted Socket Cases
	case UDPSocket:
#ifdef PRINT_MESSAGES
		PrintMessageFormatter(1, "TYPE", "UDP Listening Socket at port #" + to_string(port));
#endif // PRINT_MESSAGES
		type = SOCK_DGRAM;
		ipProto = IPPROTO_UDP;
		controlCode = SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER;
		isListener = false;
		requiresBind = true;
		newSocket = WSASocket(AF_INET, type, ipProto, NULL, 0, WSA_FLAG_REGISTERED_IO);
		break;
	case TCPListener:
#ifdef PRINT_MESSAGES
		PrintMessageFormatter(1, "TYPE", "TCP Listening Socket at port #" + to_string(port));
#endif // PRINT_MESSAGES
		type = SOCK_STREAM;
		ipProto = IPPROTO_TCP;
		controlCode = SIO_GET_EXTENSION_FUNCTION_POINTER;
		isListener = true;
		requiresBind = true;
		newSocket = WSASocket(AF_INET, type, ipProto, NULL, 0, WSA_FLAG_OVERLAPPED);
		break;
		//Accepted Socket Cases
	case TCPConnection:
#ifdef PRINT_MESSAGES
		PrintMessageFormatter(1, "TYPE", "New TCP Connection for service #" + to_string(serviceType));
#endif // PRINT_MESSAGES
		controlCode = SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER;
		isListener = false;
		requiresBind = false;
		break;
	default:
#ifdef PRINT_MESSAGES
		PrintMessageFormatter(1, "ERROR", "Invalid Socket Type.");
#endif // PRINT_MESSAGES
		return -1; //Incorrect socket Type
	}

	if (newSocket == INVALID_SOCKET) {
#ifdef PRINT_MESSAGES
		PrintMessageFormatter(1, "ERROR", "WSASocket failed to generate socket.");
		PrintWindowsErrorMessage();
#endif // PRINT_MESSAGES
		return -2;
	}

	if (requiresBind) {
#ifdef PRINT_MESSAGES
		PrintMessageFormatter(2, "BIND", "Binding to port #" + to_string(port));
#endif // PRINT_MESSAGES
		if (SOCKET_ERROR == ::bind(newSocket, reinterpret_cast<struct sockaddr *>(&socketAddress), sizeof(socketAddress))) {
#ifdef PRINT_MESSAGES
			PrintMessageFormatter(1, "ERROR", "Bind failed.");
			PrintWindowsErrorMessage();
#endif // PRINT_MESSAGES
			return -3;
		}
	}

	if (isListener) {			//******Listeners******
#ifdef PRINT_MESSAGES
		PrintMessageFormatter(2, "WSAIoctl", "Loading AcceptEx function Pointer. . .");
#endif // PRINT_MESSAGES
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
#ifdef PRINT_MESSAGES
			PrintMessageFormatter(1, "ERROR", "WSAIoctl failed to load extensions.");
			PrintWindowsErrorMessage();
#endif // PRINT_MESSAGES
			return -4;
		}

#ifdef PRINT_MESSAGES
		PrintMessageFormatter(2, "LISTEN", "Initiate listening. . .");
#endif // PRINT_MESSAGES

		if (SOCKET_ERROR == listen(newSocket, 100)) {	//MAX_LISTEN_BACKLOG_SERVER
#ifdef PRINT_MESSAGES
			PrintMessageFormatter(1, "ERROR", "Listen failed.");
			PrintWindowsErrorMessage();
#endif // PRINT_MESSAGES
			return -5;
		}

#ifdef PRINT_MESSAGES
		PrintMessageFormatter(2, "IOCP Queue", "Connecting accept completions to specified IOCP queue. . .");
#endif // PRINT_MESSAGES

		hIOCP = ::CreateIoCompletionPort(
			(HANDLE)newSocket,
			hIOCP,
			(ULONG_PTR)CK_ACCEPT,			//////////
			0);								//MAX_CONCURRENT_THREADS
		if (NULL == hIOCP) {
#ifdef PRINT_MESSAGES
			PrintMessageFormatter(1, "ERROR", "New socket could not be added to IOCP queue.");
			PrintWindowsErrorMessage();
#endif // PRINT_MESSAGES
			return -6;
		}

#ifdef PRINT_MESSAGES
		PrintMessageFormatter(2, "CreateNewService", "Registering the TCP service #" + to_string(serviceType));
#endif // PRINT_MESSAGES
		//Create a new service to represent this new listening socket
		if (CreateNewService(serviceType, port, newSocket, acceptExFunction) < 0) {
#ifdef PRINT_MESSAGES
			PrintMessageFormatter(1, "ERROR", "Could not register new service.");
#endif // PRINT_MESSAGES
			return -8;
		}

#ifdef PRINT_MESSAGES
		PrintMessageFormatter(2, "BeginAcceptEx", "Posting an Accept to Service #" + to_string(serviceType));
#endif // PRINT_MESSAGES
		//Post-Initial accepts???
		FillAcceptStructures(serviceType, 10);

	}

	else {		//********Non-Listeners******
#ifdef PRINT_MESSAGES
		PrintMessageFormatter(2, "WSAIoctl", "Loading RIO extension function table. . .");
#endif // PRINT_MESSAGES
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
#ifdef PRINT_MESSAGES
			PrintMessageFormatter(1, "ERROR", "WSAIoctl failed to load extensions.");
			PrintWindowsErrorMessage();
#endif // PRINT_MESSAGES
			return -4;
		}

		//Create Critical Section
		CRITICAL_SECTION criticalSection;
		InitializeCriticalSectionAndSpinCount(&criticalSection, 4000); //Add Spin Count Parameter here

		int socketContext = (int)newSocket;

#ifdef PRINT_MESSAGES
		PrintMessageFormatter(2, "RIOCreateRequestQueue", "Creating RIO RQ and linking specified RIO CQ. . .");
#endif // PRINT_MESSAGES

		rio_RQ = rioFunctions.RIOCreateRequestQueue(
			newSocket, 100, 1,				//MAX_PENDING_RECEIVES_UDP, MAX_PENDING_SENDS_UDP
			100, 1, receiveCQ.rio_CQ,
			sendCQ.rio_CQ, &socketContext);		//Need to define socket context!!!
		if (rio_RQ == RIO_INVALID_RQ) {
#ifdef PRINT_MESSAGES
			PrintMessageFormatter(1, "ERROR", "Failed to generate RIO RQ.");
			PrintWindowsErrorMessage();
#endif // PRINT_MESSAGES
			return -7;
		}

		//Add a socket to a service and Post Initial Receives
		if (socketType == UDPSocket) {
#ifdef PRINT_MESSAGES
			PrintMessageFormatter(2, "CreateNewService", "Registering the UDP service #" + to_string(serviceType));
#endif // PRINT_MESSAGES
			if (CreateNewService(serviceType, port, newSocket, rio_RQ, criticalSection) < 0) {
#ifdef PRINT_MESSAGES
				PrintMessageFormatter(1, "ERROR", "Could not register new service.");
#endif // PRINT_MESSAGES
				return -8;
			}

#ifdef PRINT_MESSAGES
			PrintMessageFormatter(2, "PostUDPReceive", "Posting receives on new service #" + to_string(serviceType));
#endif // PRINT_MESSAGES
			for (int y = 0; y < 20; y++) {
				if (!PostReceiveOnUDPService(serviceType)) {
#ifdef PRINT_MESSAGES
					PrintMessageFormatter(2, "ERROR", "Failed to Post Receive.");
					PrintWindowsErrorMessage();
#endif // PRINT_MESSAGES
				}
			}
		}
		else {
#ifdef PRINT_MESSAGES
			PrintMessageFormatter(2, "AddEntryToService", "Adding the new connection to TCP service #" + to_string(serviceType));
#endif // PRINT_MESSAGES
			if (AddEntryToService(serviceType, socketContext, rio_RQ, newSocket, criticalSection) < 0) {
#ifdef PRINT_MESSAGES
				PrintMessageFormatter(1, "ERROR", "Could add entry to service.");
#endif // PRINT_MESSAGES
				return -9;
			}
		}

		///////////Post Initial Receives
		//PostRecv(serviceType)
	}

#ifdef PRINT_MESSAGES
	PrintMessageFormatter(1, "COMPLETE", " ");
#endif // PRINT_MESSAGES

	return 0;
}

int RIOManager::CreateRIOSocket(SocketType socketType, int serviceType, SOCKET relevantSocket, CQ_Handler receiveCQ, CQ_Handler sendCQ) {
	return CreateRIOSocket(socketType, serviceType, relevantSocket, 0, receiveCQ, sendCQ, GetMainIOCP());
}

int RIOManager::CreateRIOSocket(SocketType socketType, int serviceType, SOCKET newSocket) {
	return CreateRIOSocket(socketType, serviceType, 0, newSocket, GetMainRIOCQ(), GetMainRIOCQ(), GetMainIOCP());
}
int RIOManager::CreateRIOSocket(SocketType socketType, int serviceType, int port) {
	SOCKET socket = INVALID_SOCKET;
	return CreateRIOSocket(socketType, serviceType, port, socket, GetMainRIOCQ(), GetMainRIOCQ(), GetMainIOCP());
}

///This function allows one to customize the receive/send CQ of a particular service.
int RIOManager::SetServiceCQs(int typeCode, CQ_Handler receiveCQ, CQ_Handler sendCQ) {
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


///This function gets the RIO results from a particular RIO CQ.
int RIOManager::GetCompletedResults(vector<EXTENDED_RIO_BUF*>& results, RIORESULT* rioResults, CQ_Handler cqHandler) {
#ifdef PRINT_MESSAGES
	PrintMessageFormatter(0, "RIO MANAGER", "GetCompletedResults", "Dequeuing results from a RIO_CQ. . .");
#endif // PRINT_MESSAGES
	
#ifdef PRINT_MESSAGES
	PrintMessageFormatter(1, "Enter Critical", "Entering the CQ's critical section.");
#endif // PRINT_MESSAGES
	//Enter critical section of the CQ we are trying to access
	EnterCriticalSection(&(cqHandler.criticalSection));

#ifdef PRINT_MESSAGES
	PrintMessageFormatter(1, "RIODequeueCompletion", "Pulling results from the CQ.");
#endif // PRINT_MESSAGES
	int numResults = rioFunctions.RIODequeueCompletion(cqHandler.rio_CQ, rioResults, 1000); ////Maximum array size

	//Leave the critical section asap so another thread can access asap
#ifdef PRINT_MESSAGES
	PrintMessageFormatter(1, "Leave Critical", "Leaving the CQ's critical section.");
#endif // PRINT_MESSAGES
	LeaveCriticalSection(&(cqHandler.criticalSection));

	if (numResults == RIO_CORRUPT_CQ) {
#ifdef PRINT_MESSAGES
		PrintMessageFormatter(1, "ERROR", "RIO_CORRUPT_CQ upon RIODequeueCompletion.");
		PrintWindowsErrorMessage();
#endif // PRINT_MESSAGES
		return -1;
	}
	else if (numResults == 0) {
#ifdef PRINT_MESSAGES
		PrintMessageFormatter(1, "ERROR", "No RIORESULTs found during RIODequeueCompletion.");
#endif // PRINT_MESSAGES
		return numResults;
	}

	EXTENDED_RIO_BUF* tempRIOBuf;

	results.clear();

#ifdef PRINT_MESSAGES
	PrintMessageFormatter(1, "Results->Bufs", "Determining EXTENDED_RIO_BUF structures from RIORESULTS.");
#endif // PRINT_MESSAGES
	for (int i = 0; i < numResults; i++)
	{
		tempRIOBuf = reinterpret_cast<EXTENDED_RIO_BUF*>(rioResults[i].RequestContext);
		results.push_back(tempRIOBuf);
	}

#ifdef PRINT_MESSAGES
	PrintMessageFormatter(1, "COMPLETE", " ");
#endif // PRINT_MESSAGES

	return numResults;
}

int RIOManager::GetCompletedResults(vector<EXTENDED_RIO_BUF*>& results, RIORESULT* rioResults) {
	return GetCompletedResults(results, rioResults, GetMainRIOCQ());
}

///This function processes an AcceptEx completion by creating a new RIOSocket with the appropriate settings.
int RIOManager::NewConnection(EXTENDED_OVERLAPPED* extendedOverlapped) {
	CQ_Handler serviceCQs[2];

	//Find the service entry
	ServiceList::iterator iter = serviceList.find((*extendedOverlapped).serviceType);
	if (iter == serviceList.end()) {
		return NULL;		//Service doesn't exist
	}

	//Get the service entry
	ConnectionServerService service;
	service = iter->second;

	serviceCQs[0] = service.receiveCQ;
	serviceCQs[1] = service.sendCQ;

	CQ_Handler* pointer = serviceCQs;

	if (CreateRIOSocket(TCPConnection,
		(*extendedOverlapped).serviceType,
		(*extendedOverlapped).relevantSocket,
		serviceCQs[0],
		serviceCQs[1]) < 0)
	{
		return -1;
	}

	BeginAcceptEx(extendedOverlapped, service.acceptExFunction);

	//Get Buffer/Post Receives

	return 0;
}



RIO_BUFFERID RIOManager::RegBuf(char* buffer, DWORD length) {
	return rioFunctions.RIORegisterBuffer(buffer, length);
}

void RIOManager::DeRegBuf(RIO_BUFFERID riobuf) {
	rioFunctions.RIODeregisterBuffer(riobuf);
	return;
}


void RIOManager::PostRecv(int serviceType) {
	EXTENDED_RIO_BUF* rioBuf = bufferManager.GetBuffer();
	ServiceList::iterator iter = serviceList.find(serviceType);
	ConnectionServerService connServ = iter->second;
	bool x = rioFunctions.RIOReceive(connServ.udpRQ, rioBuf, 1, 0, 0);
}

int RIOManager::RIONotifyIOCP(RIO_CQ rioCQ) {
	return rioFunctions.RIONotify(rioCQ);
}




int RIOManager::ResetAcceptCall(EXTENDED_OVERLAPPED* extendedOverlapped) {
#ifdef PRINT_MESSAGES
	PrintMessageFormatter(3, "ResetAcceptCall", "Looking for Service #" + to_string(extendedOverlapped->serviceType));
#endif // PRINT_MESSAGES
	//Find the service entry
	ServiceList::iterator iter = serviceList.find(extendedOverlapped->serviceType);
	if (iter == serviceList.end()) {
#ifdef PRINT_MESSAGES
		PrintMessageFormatter(3, "Error", "Did not find Service #" + to_string(extendedOverlapped->serviceType));
#endif // PRINT_MESSAGES
		return INVALID_SOCKET;		//Service doesn't exist
	}

	//Get the service entry
	ConnectionServerService* service;
	service = &iter->second;

	extendedOverlapped->relevantSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_IP, NULL, 0, WSA_FLAG_OVERLAPPED | WSA_FLAG_REGISTERED_IO);
	if (extendedOverlapped->relevantSocket == INVALID_SOCKET) {
#ifdef PRINT_MESSAGES
		PrintMessageFormatter(3, "Error", "Could not make new socket");
#endif // PRINT_MESSAGES
		return -2;
	}

#ifdef PRINT_MESSAGES
	PrintMessageFormatter(3, "SUCCESS", " ");
#endif // PRINT_MESSAGES

	return BeginAcceptEx(extendedOverlapped, service->acceptExFunction);
}



///This function goes through the service list and prints relevant information
void RIOManager::PrintServiceInformation() {
	ConnectionServerService* connectionServerService;
	SocketList* sockList;
	RQ_Handler* rqHandler;
	SOCKET* socket;

	PrintMessageFormatter(1, "PrintServiceInformation", "Printing all service information. . .");
	int i = 1;
	int j = 1;

	//iterate through all registered services
	for (auto it = serviceList.begin(); it != serviceList.end(); ++it) {
		//Close the service's listening socket
		connectionServerService = &it->second;

		PrintMessageFormatter(2, "LOOP #" + to_string(i), "Service Information at port #" + to_string((*connectionServerService).port));
		

		PrintMessageFormatter(3, "# Pend Accepts", to_string((connectionServerService->acceptStructs.size())));

		PrintMessageFormatter(3, "# Connections", to_string((connectionServerService->socketList->size())));
		
		sockList = connectionServerService->socketList;
		RQ_Handler rqHandler;
		//iterate through all sockets associated with the service
		for (auto it = (*sockList).begin(); it != (*sockList).end(); ++it) {
			PrintMessageFormatter(4, "Connect #" + to_string(j), to_string(it->first));
			j++;
		}
		i++;
	}
}

///This function closes all resources associated with the RIOManager.
void RIOManager::Shutdown() {
#ifdef PRINT_MESSAGES
	PrintMessageFormatter(0, "RIO MANAGER", "SHUTDOWN", "Initializing shutdown sequence. . .");
#endif // PRINT_MESSAGES

	CloseAllSockets();
	CloseCQs();
	CloseIOCPHandles();

	bufferManager.ShutdownCleanup(rioFunctions);
	
#ifdef PRINT_MESSAGES
	PrintMessageFormatter(1, "COMPLETE", " ");
#endif // PRINT_MESSAGES
}


//////PRIVATE FUNCTIONS

///This function gets the main IOCP



///This function creates a new service in the RIO Manager service list.
///Note that the receive and send CQs are set to the default value.
int RIOManager::CreateNewService(int typeCode, int portNumber, SOCKET listeningSocket, RIO_RQ udpRQ, CRITICAL_SECTION udpCriticalSection, LPFN_ACCEPTEX acceptExFunction) {
	if (serviceList.find(typeCode) != serviceList.end()) {
		return -1;		//Service already exists
	}

	ConnectionServerService service;
	service.port = portNumber;
	service.listeningSocket = listeningSocket;
	CQ_Handler mainRIOCQ = GetMainRIOCQ();
	service.receiveCQ = mainRIOCQ;
	service.sendCQ = mainRIOCQ;
	service.socketList = new SocketList();
	service.udpRQ = udpRQ;
	service.udpCriticalSection = udpCriticalSection;
	service.acceptExFunction = acceptExFunction;

	serviceList.insert(std::pair<DWORD, ConnectionServerService>(typeCode, service));

	return 0;

}

int RIOManager::CreateNewService(int typeCode, int portNumber, SOCKET listeningSocket, RIO_RQ udpRQ, CRITICAL_SECTION udpCriticalSection) {
	return CreateNewService(typeCode, portNumber, listeningSocket, udpRQ, udpCriticalSection, nullptr);
}

int RIOManager::CreateNewService(int typeCode, int portNumber, SOCKET listeningSocket, LPFN_ACCEPTEX acceptExFunction) {
	CRITICAL_SECTION emptyCriticalSection;
	InitializeCriticalSectionAndSpinCount(&emptyCriticalSection, 4000);
	return CreateNewService(typeCode, portNumber, listeningSocket, RIO_INVALID_RQ, emptyCriticalSection, acceptExFunction);
}


///This function creates a new service in the RIO Manager service list.
///Note that the receive and send CQs are set to the default value.
int RIOManager::CreateNewService(int typeCode, int portNumber, SOCKET listeningSocket) {
	CRITICAL_SECTION emptyCriticalSection;
	InitializeCriticalSectionAndSpinCount(&emptyCriticalSection, 4000);
	return CreateNewService(typeCode, portNumber, listeningSocket, RIO_INVALID_RQ, emptyCriticalSection, nullptr);
}



int RIOManager::AddEntryToService(int typeCode, int socketContext, RIO_RQ rioRQ, SOCKET socket, CRITICAL_SECTION criticalSection) {
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
RQ_Handler rqHandler;
rqHandler.rio_RQ = rioRQ;
rqHandler.socket = socket;
rqHandler.criticalSection = criticalSection;
(*socketList).insert(std::pair<int, RQ_Handler>(socketContext, rqHandler));

return 0;
}


SOCKET RIOManager::GetListeningSocket(int typeCode) {
#ifdef PRINT_MESSAGES
	PrintMessageFormatter(3, "GetListeningSocket", "Looking for Service #" + to_string(typeCode));
#endif // PRINT_MESSAGES
	//Find the service entry
	ServiceList::iterator iter = serviceList.find(typeCode);
	if (iter == serviceList.end()) {
#ifdef PRINT_MESSAGES
		PrintMessageFormatter(3, "Error", "Did not find Service #" + to_string(typeCode));
#endif // PRINT_MESSAGES
		return INVALID_SOCKET;		//Service doesn't exist
	}

	//Get the service entry
	ConnectionServerService service;
	service = iter->second;

	if (service.listeningSocket == NULL) {
#ifdef PRINT_MESSAGES
		PrintMessageFormatter(3, "Error", "Invalid Socket with Service #" + to_string(typeCode));
#endif // PRINT_MESSAGES
		return INVALID_SOCKET;		//Socket Not Assigned
	}
#ifdef PRINT_MESSAGES
	PrintMessageFormatter(3, "SUCCESS", " ");
#endif // PRINT_MESSAGES

	return service.listeningSocket;
}


int RIOManager::FillAcceptStructures(int typeCode, int numStruct) {
#ifdef PRINT_MESSAGES
	PrintMessageFormatter(3, "FillAcceptStructures", "Looking for Service #" + to_string(typeCode));
#endif // PRINT_MESSAGES
	//Find the service entry
	ServiceList::iterator iter = serviceList.find(typeCode);
	if (iter == serviceList.end()) {
#ifdef PRINT_MESSAGES
		PrintMessageFormatter(3, "Error", "Did not find Service #" + to_string(typeCode));
#endif // PRINT_MESSAGES
		return INVALID_SOCKET;		//Service doesn't exist
	}

	//Get the service entry
	ConnectionServerService* service;
	service = &iter->second;

	EXTENDED_OVERLAPPED* exOver;

	for (int i = 0; i < numStruct; i++) {
		exOver = new EXTENDED_OVERLAPPED();
		exOver->serviceType = typeCode;
		exOver->relevantSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_IP, NULL, 0, WSA_FLAG_OVERLAPPED | WSA_FLAG_REGISTERED_IO);
		if (exOver->relevantSocket == INVALID_SOCKET) {
#ifdef PRINT_MESSAGES
			PrintMessageFormatter(3, "Error", "Number of accepts posted = " + to_string(i));
#endif // PRINT_MESSAGES
			return i;
		}
		service->acceptStructs.push_back(*exOver);

		if (service->acceptStructs.size() == 0) {
			cout << "Empty" << endl;
		}
		else {
			cout << "Entry Added" << endl;
		}
		if (service->acceptStructs.empty()) {
			cout << "Empty" << endl;
		}


		int test = BeginAcceptEx(exOver, service->acceptExFunction);
		cout << test << endl;
	}
#ifdef PRINT_MESSAGES
	PrintMessageFormatter(3, "FillAcceptStructures", "Completely filled accepts = " + to_string(numStruct));
	PrintServiceInformation();
#endif // PRINT_MESSAGES

	return numStruct;
}


int RIOManager::BeginAcceptEx(EXTENDED_OVERLAPPED* extendedOverlapped, LPFN_ACCEPTEX acceptExFunction) {
	//Needed for AcceptEx
	DWORD bytes;

	if (!(acceptExFunction(			////////////
		GetListeningSocket((*extendedOverlapped).serviceType),
		(*extendedOverlapped).relevantSocket,
		extendedOverlapped->buffer,
		0,							//No read
		sizeof(sockaddr_in) + 16,
		sizeof(sockaddr_in) + 16,	//MSDN specifies that dwRemoteAddressLength "Cannot be zero."
		&bytes,
		extendedOverlapped
	)))
	{
#ifdef PRINT_MESSAGES
		PrintMessageFormatter(1, "ERROR", "AcceptEx call failed.");
		PrintWindowsErrorMessage();
#endif // PRINT_MESSAGES
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

///This function gets the first RIO CQ from the list
CQ_Handler RIOManager::GetMainRIOCQ() {
	if (rioCQList.empty()) {
		return CQ_Handler();
	}
	return rioCQList.front();
}



bool RIOManager::PostReceiveOnUDPService(int serviceType) {
#ifdef PRINT_MESSAGES
	PrintMessageFormatter(2, "PostReceive", "Posting Receive on Service #" + to_string(serviceType));
#endif // PRINT_MESSAGES
	EXTENDED_RIO_BUF* rioBuf = bufferManager.GetBuffer();
	ServiceList::iterator iter = serviceList.find(serviceType);
	ConnectionServerService connServ = iter->second;
	return rioFunctions.RIOReceive(connServ.udpRQ, rioBuf, 1, 0, rioBuf);
}


//********CLOSING/CLEANUP FUNCTIONS********

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
		PrintMessageFormatter(2, "LOOP", "Closing IOCP Handle #" + to_string(i));
		i++;
#endif // PRINT_MESSAGES
		CloseHandle(goodbyeIOCP);
		iocpList.pop_front();
	}

	return;
}

///This function closes all RIO Completion Queues and their corresponding critical sections.
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
		PrintMessageFormatter(2, "LOOP", "Closing RIO_CQ #" + to_string(i));
#endif // PRINT_MESSAGES
		rioFunctions.RIOCloseCompletionQueue(goodbyeRIOCQ.rio_CQ);
#ifdef PRINT_MESSAGES
		PrintMessageFormatter(2, "LOOP", "Closing Critical Section #" + to_string(i));
		i++;
#endif // PRINT_MESSAGES
		DeleteCriticalSection(&goodbyeRIOCQ.criticalSection);
	}
	return;
}

///This function closes all sockets stored within the service list.
void RIOManager::CloseAllSockets() {
	ConnectionServerService* connectionServerService;
	SocketList* sockList;
	RQ_Handler* rqHandler;
	SOCKET* socket;

#ifdef PRINT_MESSAGES
	PrintMessageFormatter(1, "CloseAllSockets", "Closing all sockets (and RIO RQs). . .");
	int i = 1;
	int j = 1;
#endif // PRINT_MESSAGES

	//iterate through all registered services
	for (auto it = serviceList.begin(); it != serviceList.end(); ++it) {
		//Close the service's listening socket
		connectionServerService = &it->second;

#ifdef PRINT_MESSAGES
		PrintMessageFormatter(2, "LOOP #" + to_string(i), "Closing Service at port #" + to_string((*connectionServerService).port));
#endif // PRINT_MESSAGES

		closesocket((*connectionServerService).listeningSocket);

		sockList = (*connectionServerService).socketList;
		//iterate through all sockets associated with the service
		for (auto it = (*sockList).begin(); it != (*sockList).end(); ++it) {

#ifdef PRINT_MESSAGES
			PrintMessageFormatter(3, "LOOP #" + to_string(i), "Closing Service's Socket #" + to_string(j));
			j++;
#endif // PRINT_MESSAGES

			//Close each socket
			rqHandler = &it->second;
			closesocket((*rqHandler).socket);
		}

#ifdef PRINT_MESSAGES
		i++;
#endif // PRINT_MESSAGES
	}
}

///This function prints a message to console with a specified format (two boxes).
void RIOManager::PrintMessageFormatter(int level, string type, string subtype, string message) {
	if (level == 0) {
		printf_s("\n");
	}

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
	if (level == 0) {
		printf_s("\n");
	}

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
	//delete &bufferManager;
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
