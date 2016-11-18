#include "stdafx.h"
#include "RIOManager.h"


RIOManager::RIOManager()
{
	socketRIO = INVALID_SOCKET;
	span s(mySeries, _T("Thread Blocking Finder"));
}

///This function loads WinSock and initiates the RIOManager basic needs such as registered buffers.
int RIOManager::InitializeRIO(int bufferSize, DWORD bufferCount, int spinCount, int rioDequeueCount)
{
	rioSpinCount = spinCount;
	dequeueCount = rioDequeueCount;
	InitializeCriticalSectionAndSpinCount(&serviceListCriticalSection, spinCount);

	// 1. Initialize WinSock
	WSADATA wsaData;

	PRINT_FOUR(0, "RIO MANAGER", "InitializeRIO", "1. Initializing WinSock. . .");

	if (0 != ::WSAStartup(0x202, &wsaData)) 
	{
		PRINT_THREE(1, "ERROR", "WinSock Initialization Failed.");
		return -1;
	}

	PRINT_THREE(1, "SUCCESS", " ");


	// 2. Load RIO Extension Functions

	PRINT_THREE(1, "InitializeRIO", "2. Loading RIO Extension Functions. . .");

	socketRIO = WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, NULL, 0, WSA_FLAG_REGISTERED_IO);
	if (socketRIO == INVALID_SOCKET) 
	{
		PRINT_WIN_ERROR(1, "ERROR", "WSASocket failed to generate socket.");
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
		PRINT_WIN_ERROR(1, "ERROR", "WSAIoctl failed to retrieve RIO extension functions.");
		return -3;
	}

	PRINT_THREE(1, "SUCCESS", " ");

	// 3. **Initialize Buffer Manager**//
	PRINT_THREE(1, "InitializeRIO", "3. Initializing Buffer Mananger. . .");

	//**Initialize Buffer Manager**//
	bufferManager.Initialize(rioFunctions, bufferCount, bufferSize);

	PRINT_THREE(1, "COMPLETE", " ");

	return 0;
}

///This function creates a new IOCP queue for the RIOManager instance and registers the IOCP queue with the RIOManager.
///The first IOCP queue that is registered is registered as the "main" or "default" IOCP queue.
HANDLE RIOManager::CreateIOCP() 
{
	HANDLE hIOCP = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);
	if (NULL == hIOCP) 
	{
		return NULL;
		//ReportError("CreateIoComplectionPort - Create", true);
	}

	PRINT_FOUR(0, "RIO MANAGER", "CreateIOCP", "Creating IOCP Handle. . .");

	//**Register New IOCP with RIO Manager**//
	iocpList.push_back(hIOCP);


#ifdef _DEBUG
	int length = iocpList.size();
#endif // _DEBUG
	PRINT_THREE(1, "SUCCESS", "Created and Added IOCP #" + to_string(length));

	PRINT_THREE(1, "COMPLETE", " ");

	return hIOCP;
}

///This function creates a new RIO Completion Queue with IOCP Queue and Completion Key specified (For Multi-CQ systems with multi-IOCP)
CQ_Handler RIOManager::CreateCQ(int size, HANDLE hIOCP, COMPLETION_KEY completionKey) 
{
	CQ_Handler cqHandler;
	OVERLAPPED overlapped;
	RIO_NOTIFICATION_COMPLETION rioNotificationCompletion;

	PRINT_FOUR(0, "RIO MANAGER", "CreateCQ", "Creating RIO Completion Queue. . .");

	rioNotificationCompletion.Type = RIO_IOCP_COMPLETION;
	rioNotificationCompletion.Iocp.IocpHandle = hIOCP;
	rioNotificationCompletion.Iocp.CompletionKey = (void*)completionKey;
	rioNotificationCompletion.Iocp.Overlapped = &overlapped;

	cqHandler.rio_CQ = rioFunctions.RIOCreateCompletionQueue(
		size,	//MAX_PENDING_RECEIVES + MAX_PENDING_SENDS
		&rioNotificationCompletion);
	if (cqHandler.rio_CQ == RIO_INVALID_CQ) 
	{
		PRINT_WIN_ERROR(1, "ERROR", "CreateCQ failed to create an RIO Completion Queue.");
		return cqHandler;
	}

	InitializeCriticalSectionAndSpinCount(&cqHandler.criticalSection, rioSpinCount); //Add Spin Count Parameter here

	//*** Need to Store this RIO_CQ handle/Critical Section into the RIO Manager Instance ***
	rioCQList.push_back(cqHandler);

#ifdef _DEBUG
	int length = rioCQList.size();
#endif // _DEBUG
	PRINT_THREE(1, "SUCCESS", "Created and Added RIO CQ #" + to_string(length));


	PRINT_THREE(1, "COMPLETE", " ");


	return cqHandler;
}

///This function creates a new RIO Completion Queue with default IOCP Queue but custom Completion Key (For creating Multi-CQ system with one IOCP)
CQ_Handler RIOManager::CreateCQ(int size, COMPLETION_KEY completionKey) 
{
	return CreateCQ(size, GetMainIOCP(), completionKey);
}

///This function creates a new RIO Completion Queue with IOCP Queue specified (For creating main-CQ in Multi-IOCP system)
CQ_Handler RIOManager::CreateCQ(int size, HANDLE hIOCP) 
{
	return CreateCQ(size, hIOCP, CK_RIO);
}

///This function creates a new RIO Completion Queue with default values (For creating main-CQ for main-IOCP queue)
CQ_Handler RIOManager::CreateCQ(int size) 
{
	return CreateCQ(size, GetMainIOCP());
}

///This function creates a new RIO Socket of various types
int RIOManager::CreateRIOSocket(SOCKET_TYPE socketType, int serviceType, int port, SOCKET newSocket, CQ_Handler receiveCQ, CQ_Handler sendCQ, HANDLE hIOCP,
	int serviceMaxClients, int serviceMaxAccepts, int serviceRQMaxReceives, int serviceRQMaxSends, bool isAddressRequired) {

	// ##################################
	//			Create Socket
	// ##################################

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
	int option = TRUE;

	PRINT_FOUR(0, "RIO MANAGER", "CreateRIOSocket", "Creating new RIO Socket. . .");

	switch (socketType) 
	{
		//Non-accepted Socket Cases
	case UDPSocket:

		PRINT_THREE(1, "TYPE", "UDP Listening Socket at port #" + to_string(port));

		type = SOCK_DGRAM;
		ipProto = IPPROTO_UDP;
		controlCode = SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER;
		isListener = false;
		requiresBind = true;
		newSocket = WSASocket(AF_INET, type, ipProto, NULL, 0, WSA_FLAG_REGISTERED_IO);
		break;

	case TCPListener:

		PRINT_THREE(1, "TYPE", "TCP Listening Socket at port #" + to_string(port));

		type = SOCK_STREAM;
		ipProto = IPPROTO_TCP;
		controlCode = SIO_GET_EXTENSION_FUNCTION_POINTER;
		isListener = true;
		requiresBind = true;
		newSocket = WSASocket(AF_INET, type, ipProto, NULL, 0, WSA_FLAG_OVERLAPPED | WSA_FLAG_REGISTERED_IO);
		/*if (setsockopt(newSocket, IPPROTO_TCP, TCP_NODELAY, (const char*)&option, sizeof(option)) != 0) 
		{
			PRINT_WIN_ERROR(1, "ERROR", "setsockopt failed.");
			return -11;
		}*/
		break;

		//Accepted Socket Cases
	case TCPConnection:

		PRINT_THREE(1, "TYPE", "New TCP Connection for service #" + to_string(serviceType));

		controlCode = SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER;
		isListener = false;
		requiresBind = false;
		/*if (setsockopt(newSocket, IPPROTO_TCP, TCP_NODELAY, (const char*)&option, sizeof(option)) != 0) 
		{
			PRINT_WIN_ERROR(1, "ERROR", "setsockopt failed.");
			return -11;
		}*/

		if (serviceRQMaxReceives == 0) 
		{ //If the function didn't supply maximum values, need to get these from the service
			EnterCriticalSection(&serviceListCriticalSection);
			ServiceList::iterator iter = serviceList.find(serviceType);
			if (iter != serviceList.end()) 
			{
				ConnectionServerService* connServ = &iter->second;
				serviceRQMaxReceives = connServ->serviceRQMaxReceives;
				serviceRQMaxSends = connServ->serviceRQMaxSends;
			}
			else 
			{
				PRINT_THREE(1, "ERROR", "Service for new socket does not exist.");
				LeaveCriticalSection(&serviceListCriticalSection);
				return -10; //Invalid service type
			}
			LeaveCriticalSection(&serviceListCriticalSection);
		}

		break;

	default:
		PRINT_THREE(1, "ERROR", "Invalid Socket Type.");
		return -1; //Incorrect socket Type
	}


	if (newSocket == INVALID_SOCKET) 
	{
		PRINT_WIN_ERROR(1, "ERROR", "WSASocket failed to generate socket.");
		return -2;
	}

	// ##################################
	//				Bind
	// ##################################

	if (requiresBind) 
	{
		if (SOCKET_ERROR == ::bind(newSocket, reinterpret_cast<struct sockaddr *>(&socketAddress), sizeof(socketAddress))) {
			PRINT_WIN_ERROR(1, "ERROR", "Bind failed.");
			return -3;
		}
	}

	// ##################################
	//				Listen
	// ##################################

	if (isListener) 
	{			//******TCP Listeners******

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
			PRINT_WIN_ERROR(1, "ERROR", "WSAIoctl failed to load extensions.");
			return -4;
		}

		if (SOCKET_ERROR == listen(newSocket, 100)) 
		{	//MAX_LISTEN_BACKLOG_SERVER
			PRINT_WIN_ERROR(1, "ERROR", "Listen failed.");
			return -5;
		}

		hIOCP = ::CreateIoCompletionPort(
			(HANDLE)newSocket,
			hIOCP,
			(ULONG_PTR)CK_ACCEPT,			//////////
			0);								//MAX_CONCURRENT_THREADS
		if (NULL == hIOCP) 
		{
			PRINT_WIN_ERROR(1, "ERROR", "New socket could not be added to IOCP queue.");
			return -6;
		}

		//Create a new service to represent this new listening socket
		if (CreateNewService(serviceType, port, serviceMaxClients, serviceMaxAccepts, serviceRQMaxReceives, serviceRQMaxSends, isAddressRequired, false, newSocket, acceptExFunction) < 0) {
			PRINT_THREE(1, "ERROR", "Could not register new service.");
			return -8;
		}
		//Post-Initial accepts???
		FillAcceptStructures(serviceType, serviceMaxAccepts);
	}

	else 
	{		//********Non-Listeners******

		PRINT_THREE(1, "Non-Listen", "Additional setup. . .");

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
			PRINT_WIN_ERROR(1, "ERROR", "WSAIoctl failed to load extensions.");
			return -4;
		}

		//Create Critical Section
		CRITICAL_SECTION criticalSection;
		InitializeCriticalSectionAndSpinCount(&criticalSection, rioSpinCount); //Add Spin Count Parameter here

		int socketContext = (int)newSocket;

		PRINT_THREE(2, "RIO RQ", "Creating request queue. . .");

		rio_RQ = rioFunctions.RIOCreateRequestQueue(
			newSocket, serviceRQMaxReceives, 1,				//MAX_PENDING_RECEIVES_UDP, MAX_PENDING_SENDS_UDP
			serviceRQMaxSends, 1, receiveCQ.rio_CQ,
			sendCQ.rio_CQ, &socketContext);		//Need to define socket context!!!
		if (rio_RQ == RIO_INVALID_RQ) 
		{
			PRINT_WIN_ERROR(1, "ERROR", "Failed to generate RIO RQ.");
			return -7;
		}

		//Add a socket to a service and Post Initial Receives
		if (socketType == UDPSocket) 
		{
			if (CreateNewService(serviceType, port, 0, 0, serviceRQMaxReceives, serviceRQMaxSends, false, true, newSocket, rio_RQ, criticalSection) < 0) {
				PRINT_THREE(1, "ERROR", "Could not register new service.");
				return -8;
			}
			for (int y = 0; y < serviceRQMaxReceives; y++) 
			{
				if (!PostReceiveOnUDPService(serviceType, RIO_MSG_DEFER)) 
				{
					PRINT_WIN_ERROR(2, "ERROR", "Failed to Post initial Receive on new UDP service.");
				}
			}
			if (!rioFunctions.RIOReceive(rio_RQ, NULL, 0, RIO_MSG_COMMIT_ONLY, NULL)) 
			{
				PRINT_WIN_ERROR(2, "ERROR", "Failed to commit initial receives on new UDP service.");
			}
		}
		else 
		{
			PRINT_THREE(2, "AddEntryToService", "Addinging service entry. . .");
			if (AddEntryToService(serviceType, socketContext, rio_RQ, newSocket, criticalSection) < 0) 
			{
				PRINT_THREE(1, "ERROR", "Could add entry to service.");
				return -9;
			}
			PRINT_THREE(2, "Post TCP", "Positing initial TCP receives. . .");
			for (int y = 0; y < (serviceRQMaxReceives); y++) 
			{
				if (!PostReceiveOnTCPService(serviceType, (int)newSocket, RIO_MSG_DEFER)) 
				{
					PRINT_WIN_ERROR(2, "ERROR", "Failed to Post Receive on new TCP entry.");
				}
			}
			if (!rioFunctions.RIOReceive(rio_RQ, NULL, 0, RIO_MSG_COMMIT_ONLY, NULL)) 
			{
				PRINT_WIN_ERROR(2, "ERROR", "Failed to commit initial receives on new TCP entry.");
			}
		}
	}

	PRINT_THREE(1, "COMPLETE", " ");

	return 0;
}


int RIOManager::CreateRIOSocket(SOCKET_TYPE socketType, int serviceType, SOCKET relevantSocket, CQ_Handler receiveCQ, CQ_Handler sendCQ) 
{
	return CreateRIOSocket(socketType, serviceType, relevantSocket, 0, receiveCQ, sendCQ, GetMainIOCP(),
		0, 0, 0, 0, false);
}


int RIOManager::CreateRIOSocket(SOCKET_TYPE socketType, int serviceType, int port,
	int serviceMaxClients, int serviceMaxAccepts, int serviceRQMaxReceives, int serviceRQMaxSends, bool isAddressRequired) 
{
	SOCKET socket = INVALID_SOCKET;
	return CreateRIOSocket(socketType, serviceType, port, socket, GetMainRIOCQ(), GetMainRIOCQ(), GetMainIOCP(),
		serviceMaxClients, serviceMaxAccepts, serviceRQMaxReceives, serviceRQMaxSends, isAddressRequired);
}

int RIOManager::CreateRIOSocket(SOCKET_TYPE socketType, int serviceType, SOCKET newSocket) 
{
	return CreateRIOSocket(socketType, serviceType, 0, newSocket, GetMainRIOCQ(), GetMainRIOCQ(), GetMainIOCP(),
		0, 0, 0, 0, false);
}

int RIOManager::CreateRIOSocket(SOCKET_TYPE socketType, int serviceType, int port) 
{
	SOCKET socket = INVALID_SOCKET;
	return CreateRIOSocket(socketType, serviceType, port, socket, GetMainRIOCQ(), GetMainRIOCQ(), GetMainIOCP(),
		0, 0, 0, 0, false);
}

///This function allows one to customize the receive/send CQ of a particular service.
int RIOManager::SetServiceCQs(int typeCode, CQ_Handler receiveCQ, CQ_Handler sendCQ) 
{
	//Find the service entry
	EnterCriticalSection(&serviceListCriticalSection);
	ServiceList::iterator iter = serviceList.find(typeCode);
	if (iter == serviceList.end()) 
	{
		LeaveCriticalSection(&serviceListCriticalSection);
		return -1;		//Service doesn't exist
	}
	LeaveCriticalSection(&serviceListCriticalSection);

	//Get the service entry
	ConnectionServerService service;
	service = iter->second;

	service.receiveCQ = receiveCQ;
	service.sendCQ = sendCQ;

	return 0;
}

///This function gets the RIO results from a particular RIO CQ.
int RIOManager::GetCompletedResults(vector<ExtendedRioBuf*>& results, RIORESULT* rioResults, CQ_Handler cqHandler) 
{


	EnterCriticalSection(&(cqHandler.criticalSection));
	int numResults = rioFunctions.RIODequeueCompletion(cqHandler.rio_CQ, rioResults, dequeueCount); ////Maximum array size
	LeaveCriticalSection(&(cqHandler.criticalSection));

	if (numResults == RIO_CORRUPT_CQ) 
	{
		PRINT_WIN_ERROR(1, "ERROR", "RIO_CORRUPT_CQ upon RIODequeueCompletion.");
		return -1;
	}
	else if (numResults == 0) 
	{
		PRINT_THREE(1, "ERROR", "No RIORESULTs found during RIODequeueCompletion.");
		return numResults;
	}

	ExtendedRioBuf* tempRIOBuf;
	results.clear();					//Clear the thread's results list
	
	for (int i = 0; i < numResults; i++)
	{
		tempRIOBuf = reinterpret_cast<ExtendedRioBuf*>(rioResults[i].RequestContext);
		//Check rioresult structure for errors
		//NOTE - Information on RIORESULT's Status values is unclear
		//When a client/server force closes, error code 10054 is received (WSAECONNRESET - connection reset by pear)
		//10022 (WSAEINVAL - Invalid Argument)
		if (tempRIOBuf == nullptr) 
		{
			PRINT_THREE(1, "ERROR", "Could not obtain RequestContext from completion. . .");
			PRINT_THREE(2, "Source Code", to_string(rioResults[i].SocketContext));
			PRINT_THREE(2, "Bytes Transfered", to_string(rioResults[i].BytesTransferred));
			PRINT_THREE(2, "STATUS", to_string(rioResults[i].Status));
			continue;
		}
		if (rioResults[i].Status != NO_ERROR) 
		{
			if (CloseServiceEntry(tempRIOBuf->srcType, tempRIOBuf->socketContext) >= 0) 
			{
				PRINT_THREE(1, "ERROR", "ERROR from RIORESULT Status");
				PRINT_THREE(2, "Source Code", to_string(rioResults[i].SocketContext));
				PRINT_THREE(2, "Bytes Transfered", to_string(rioResults[i].BytesTransferred));
				PRINT_THREE(2, "STATUS", to_string(rioResults[i].Status));
			}
			bufferManager.FreeBuffer(tempRIOBuf);
		} else if (rioResults[i].BytesTransferred <= 0) 
		{
			if (CloseServiceEntry(tempRIOBuf->srcType, tempRIOBuf->socketContext) >= 0) 
			{
				PRINT_THREE(1, "ERROR", "RIORESULT Bytes Transferred <= 0");
				PRINT_THREE(2, "Source Code", to_string(rioResults[i].SocketContext));
				PRINT_THREE(2, "Bytes Transfered", to_string(rioResults[i].BytesTransferred));
				PRINT_THREE(2, "STATUS", to_string(rioResults[i].Status));
			}
			bufferManager.FreeBuffer(tempRIOBuf);
		}
		else 
		{	//Passed RIORESULT Tests
			/*EnterCriticalSection(&consoleCriticalSection);
			cout << rioResults[i].BytesTransferred << endl;
			cout << rioResults[i].Status << endl;
			LeaveCriticalSection(&consoleCriticalSection);*/


			//Need to ensure that the service entry related to completion is still valid, otherwise, we shouldn't process the result
			ConnectionServerService* connServ;
			ServiceList::iterator iterServ = serviceList.find(tempRIOBuf->srcType);
			connServ = &iterServ->second;
			
			if (!(connServ->isUDPService)) 
			{	//Need to check only for TCP service
				EnterCriticalSection(&connServ->socketListCriticalSection);
				SocketList* sockList = connServ->socketList;
				LeaveCriticalSection(&connServ->socketListCriticalSection);
				if (sockList->find(tempRIOBuf->socketContext) == sockList->end()) 
				{
					//If we get here, it means that a result completed successfully, but during that time
					//the service entry was closed for some reason
					//-> We need to then clear the buffer that was used
					bufferManager.FreeBuffer(tempRIOBuf);
					continue;
				}
			}

			results.push_back(tempRIOBuf);
		}
	}

	return numResults;
}

int RIOManager::GetCompletedResults(vector<ExtendedRioBuf*>& results, RIORESULT* rioResults) 
{
	return GetCompletedResults(results, rioResults, GetMainRIOCQ());
}


//struct Instruction 
//{
//	InstructionType type;
//	int socketContext;		//Destination Code
//	DestinationType destinationType;
//	EXTENDED_RIO_BUF* buffer;
//};

int RIOManager::ProcessInstruction(Instruction instruction) 
{
	ServiceList::iterator iter;
	SocketList::iterator sockIter;
	SocketList* sockList;
	ConnectionServerService* service;

	switch (instruction.type) 
	{

	case SEND:

		//PrintMessageFormatter(1, "InstructionType", "SEND Instruction received to " + to_string(instruction.destinationType));

		EnterCriticalSection(&serviceListCriticalSection);
		iter = serviceList.find(instruction.destinationType);
		if (iter == serviceList.end()) 
		{
			PRINT_THREE(1, "ERROR", "Send to service does not exist.");
			PRINT_THREE(2, "DST TYPE", to_string(instruction.destinationType));
			PRINT_THREE(2, "DST CODE", to_string(instruction.socketContext));
			PRINT_THREE(2, "SRC TYPE", to_string(instruction.buffer->srcType));
			PRINT_THREE(2, "SRC CODE", to_string(instruction.buffer->socketContext));
			PRINT_THREE(2, "OP TYPE", to_string(instruction.buffer->operationType));
			PRINT_THREE(2, "MSG LENGTH", to_string(instruction.buffer->messageLength));

			bufferManager.FreeBuffer(instruction.buffer);
			LeaveCriticalSection(&serviceListCriticalSection);
			return -1;		//Service doesn't exist
		}
		LeaveCriticalSection(&serviceListCriticalSection);

		//Get the service entry
		ConnectionServerService* service;
		service = &iter->second;

		sockList = service->socketList;

		if (sockList->empty()) 
		{
			PRINT_THREE(1, "ERROR", "Send to service has no entries.");
			PRINT_THREE(2, "DST TYPE", to_string(instruction.destinationType));
			PRINT_THREE(2, "DST CODE", to_string(instruction.socketContext));
			bufferManager.FreeBuffer(instruction.buffer);
			return -2;		//No sockets in the list
		}

		RQ_Handler* rqHandler;

		if (instruction.socketContext == 0) 
		{		//No location specification

			//Check if the service requires a specific address
			if (service->isAddressRequired) 
			{
				PRINT_THREE(1, "ERROR", "No specific destination must be specified for SEND. Required on Service at port #" + to_string(service->port));
				PRINT_THREE(2, "DST TYPE", to_string(instruction.destinationType));
				PRINT_THREE(2, "DST CODE", to_string(instruction.socketContext));
				bufferManager.FreeBuffer(instruction.buffer);
				return -5;		//Specific destination required
			}

			//Round-Robin Mechanism using iterator
			//NOTE - We already checked if sockList is empty above
			//NOTE - When a sockList entry is closed, the iterator position is changed

			EnterCriticalSection(&service->socketListCriticalSection);
			SocketList::iterator robinIter = sockList->find(service->roundRobinLocation);
			LeaveCriticalSection(&service->socketListCriticalSection);
			if (robinIter == sockList->end()) 
			{	//Doesn't contain stored location
				sockIter = sockList->begin();
			}
			else 
			{								//Has stored location, need to assign and then switch to next location
				sockIter = robinIter;
				++robinIter;					//Goto next element
				if (robinIter == sockList->end()) 
				{
					robinIter = sockList->begin();
				}
				EnterCriticalSection(&service->roundRobinCriticalSection);
				service->roundRobinLocation = robinIter->first;
				LeaveCriticalSection(&service->roundRobinCriticalSection);
			}

			/*EnterCriticalSection(&consoleCriticalSection);
			cout << "Round-Robin Send to SOCKETCOTEXT: " << sockIter->first << endl;
			LeaveCriticalSection(&consoleCriticalSection);*/

		}
		else 
		{										//Specific location

			//Look for specified destination
			EnterCriticalSection(&service->socketListCriticalSection);
			sockIter = sockList->find(instruction.socketContext);
			LeaveCriticalSection(&service->socketListCriticalSection);

			//Check if the specified destination was found
			if (sockIter == sockList->end()) 
			{

				//Two cases if the destination wasn't found
				// 1 - If isAddressRequired flag is checked, report failure
				// 2 - If not, enable Round-Robin sending
				if (service->isAddressRequired) 
				{
					PRINT_THREE(1, "ERROR", "Specified destination not found for SEND.");
					PRINT_THREE(1, "ERROR", "Round-robin sending not allowed on Service at port #" + to_string(service->port));
					PRINT_THREE(2, "DST TYPE", to_string(instruction.destinationType));
					PRINT_THREE(2, "DST CODE", to_string(instruction.socketContext));
					bufferManager.FreeBuffer(instruction.buffer);
					return -3;		//Specified location not found, round-robin not allowed
				}

				EnterCriticalSection(&service->socketListCriticalSection);
				SocketList::iterator robinIter = sockList->find(service->roundRobinLocation);
				LeaveCriticalSection(&service->socketListCriticalSection);
				if (robinIter == sockList->end()) 
				{	//Doesn't contain stored location
					sockIter = sockList->begin();
				}
				else 
				{								//Has stored location, need to assign and then switch to next location
					sockIter = robinIter;
					++robinIter;					//Goto next element
					if (robinIter == sockList->end()) 
					{
						robinIter = sockList->begin();
					}
					EnterCriticalSection(&service->roundRobinCriticalSection);
					service->roundRobinLocation = robinIter->first;
					LeaveCriticalSection(&service->roundRobinCriticalSection);
				}
				//sockIter = sockList->begin();
				LeaveCriticalSection(&service->roundRobinCriticalSection);
				/*EnterCriticalSection(&consoleCriticalSection);
				cout << "Round-Robin Send to SOCKETCOTEXT: " << sockIter->first << endl;
				LeaveCriticalSection(&consoleCriticalSection);*/
			}

		}

		//We now have the location to send
		rqHandler = &sockIter->second;
		EnterCriticalSection(&rqHandler->criticalSection);
		instruction.buffer->operationType = OP_SEND;
		if (!rioFunctions.RIOSend(rqHandler->rio_RQ, instruction.buffer, 1, 0, instruction.buffer)) 
		{

			PRINT_WIN_ERROR(1, "ERROR", "RIOSend failed.");
			PRINT_THREE(2, "DST TYPE", to_string(instruction.destinationType));
			PRINT_THREE(2, "DST CODE", to_string(instruction.socketContext));

			bufferManager.FreeBuffer(instruction.buffer);
			LeaveCriticalSection(&rqHandler->criticalSection);
			return -4;			//RIOSend failed
		}
		LeaveCriticalSection(&rqHandler->criticalSection);

		break;

	case RECEIVE:
		//Determine what location the receive needs to be placed on and if it's a UDP receive (service) or TCP receive (service entry)

		//PrintMessageFormatter(1, "InstructionType", "RECEIVE Instruction received.");

		EnterCriticalSection(&serviceListCriticalSection);
		iter = serviceList.find(instruction.destinationType);
		if (iter == serviceList.end()) 
		{

			PRINT_THREE(1, "ERROR", "Receive from service does not exist.");
			PRINT_THREE(2, "DST TYPE", to_string(instruction.destinationType));
			PRINT_THREE(2, "DST CODE", to_string(instruction.socketContext));
			LeaveCriticalSection(&serviceListCriticalSection);
			return -1;		//Service doesn't exist
		}
		LeaveCriticalSection(&serviceListCriticalSection);

		//Get the service entry
		service = &iter->second;

		//Determine if service is UDP or TCP
		if (service->isUDPService) 
		{
			PostReceiveOnUDPService(instruction.destinationType);
		}
		else 
		{
			PostReceiveOnTCPService(instruction.destinationType, instruction.socketContext);
		}

		break;

	case CLOSESOCKET:
		CloseServiceEntry(instruction.destinationType, instruction.socketContext);
		break;

	case FREEBUFFER:
		//cout << "RIO Manager found FreeBuffer command" << endl;
		bufferManager.FreeBuffer(instruction.buffer);
		break;
	}

	return 0;
}

///This function processes an AcceptEx completion by creating a new RIOSocket with the appropriate settings.
int RIOManager::NewConnection(ExtendedOverlapped* extendedOverlapped) 
{
	CQ_Handler serviceCQs[2];

	//Find the service entry
	EnterCriticalSection(&serviceListCriticalSection);
	ServiceList::iterator iter = serviceList.find((*extendedOverlapped).serviceType);
	if (iter == serviceList.end()) 
	{
		LeaveCriticalSection(&serviceListCriticalSection);
		return -1;		//Service doesn't exist
	}
	LeaveCriticalSection(&serviceListCriticalSection);

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

int RIOManager::RIONotifyIOCP(RIO_CQ rioCQ) 
{
	return rioFunctions.RIONotify(rioCQ);
}

void RIOManager::AssignConsoleCriticalSection(CRITICAL_SECTION critSec) 
{
	consoleCriticalSection = critSec;
}

int RIOManager::ConfigureNewSocket(ExtendedOverlapped* extendedOverlapped) 
{
	EnterCriticalSection(&serviceListCriticalSection);
	ServiceList::iterator iter = serviceList.find(extendedOverlapped->serviceType);
	LeaveCriticalSection(&serviceListCriticalSection);
	ConnectionServerService* connServ = &iter->second;

	PRINT_THREE(1, "setsockopt", "Running SO_UPDATE_ACCEPT_CONTEXT on new connection.");

	if (SOCKET_ERROR == setsockopt(extendedOverlapped->relevantSocket, 
				SOL_SOCKET, 
				SO_UPDATE_ACCEPT_CONTEXT, 
				(char*) &connServ->listeningSocket, sizeof(connServ->listeningSocket))) 
	{
		PRINT_WIN_ERROR(1, "ERROR", "setsockopt(SO_UPDATE_ACCEPT_CONTEXT) failed.");
		return -1;
	}

	return 1;

	GUID getAcceptExSockID = WSAID_GETACCEPTEXSOCKADDRS;
	LPFN_GETACCEPTEXSOCKADDRS getAcceptSockFunc;

	if (NULL != WSAIoctl(
		socketRIO,
		SIO_GET_EXTENSION_FUNCTION_POINTER,
		&getAcceptExSockID,
		sizeof(GUID),
		(void**)&getAcceptSockFunc,
		sizeof(getAcceptSockFunc),
		&dwBytes,
		NULL,
		NULL))
	{
		PRINT_THREE(1, "ERROR", "WSAIoctl failed to retrieve GetAcceptExSockaddrs.");
		return -2;
	}


	//if (!(acceptExFunction(			////////////
	//	GetListeningSocket((*extendedOverlapped).serviceType),
	//	(*extendedOverlapped).relevantSocket,
	//	extendedOverlapped->buffer,
	//	0,							//No read
	//	sizeof(sockaddr_in) + 16,
	//	sizeof(sockaddr_in) + 16,	//MSDN specifies that dwRemoteAddressLength "Cannot be zero."
	//	&bytes,
	//	extendedOverlapped
	//)))

	sockaddr* local = NULL; 
	sockaddr* remote = NULL;
	int sizeLocal = 0;
	int sizeRemote = 0;

	getAcceptSockFunc(
		extendedOverlapped->buffer,
		0,
		sizeof(sockaddr_in) + 16,
		sizeof(sockaddr_in) + 16,
		&local,
		&sizeLocal,
		&remote,
		&sizeRemote);

	EnterCriticalSection(&consoleCriticalSection);
	cout << sizeLocal << endl;
	cout << sizeRemote << endl;
	LeaveCriticalSection(&consoleCriticalSection);

//
//	//PTSTR buffer;
//	//InetNtop(AF_INET, &(local->sa_data), buffer, sizeof(buffer));
//
//	PRINT_THREE(1, "getAcceptSock", "Unloading the address information. . .");
//	//PRINT_THREE(2, "Local Address", *buffer);
//


	return 0;
}



int RIOManager::ResetAcceptCall(ExtendedOverlapped* extendedOverlapped) 
{

	PRINT_THREE(3, "ResetAcceptCall", "Looking for Service #" + to_string(extendedOverlapped->serviceType));

	//Find the service entry
	EnterCriticalSection(&serviceListCriticalSection);
	ServiceList::iterator iter = serviceList.find(extendedOverlapped->serviceType);
	if (iter == serviceList.end()) 
	{

		PRINT_THREE(3, "Error", "Did not find Service #" + to_string(extendedOverlapped->serviceType));
		LeaveCriticalSection(&serviceListCriticalSection);
		return INVALID_SOCKET;		//Service doesn't exist
	}
	LeaveCriticalSection(&serviceListCriticalSection);

	//Get the service entry
	ConnectionServerService* service;
	service = &iter->second;

	extendedOverlapped->relevantSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_IP, NULL, 0, WSA_FLAG_OVERLAPPED | WSA_FLAG_REGISTERED_IO);
	if (extendedOverlapped->relevantSocket == INVALID_SOCKET) 
	{
		PRINT_THREE(3, "Error", "Could not make new socket");
		return -2;
	}
	PRINT_THREE(3, "SUCCESS", " ");

	return BeginAcceptEx(extendedOverlapped, service->acceptExFunction);
}

///This function goes through the service list and prints relevant information
void RIOManager::PrintServiceInformation() 
{
	ConnectionServerService* connectionServerService;

	PRINT_THREE(1, "PrintServiceInformation", "Printing all service information. . .");
	int i = 1;
	int j = 1;

	//iterate through all registered services
	EnterCriticalSection(&serviceListCriticalSection);
	for (auto it = serviceList.begin(); it != serviceList.end(); ++it) 
	{
		//Close the service's listening socket
		connectionServerService = &it->second;

		PRINT_THREE(2, "LOOP #" + to_string(i), "Service Information at port #" + to_string((*connectionServerService).port));
		
		if (connectionServerService->isUDPService) 
		{
			PRINT_THREE(3, "TYPE", "UDP");
		}
		else 
		{
			PRINT_THREE(3, "TYPE", "TCP");
		}

		PRINT_THREE(3, "# Pend Accepts", to_string((connectionServerService->acceptStructs.size())));

		PRINT_THREE(3, "# Connections", to_string((connectionServerService->socketList->size())));
		
		SocketList* sockList = connectionServerService->socketList;
		EnterCriticalSection(&connectionServerService->socketListCriticalSection);
		//iterate through all sockets associated with the service
		for (auto it = (*sockList).begin(); it != (*sockList).end(); ++it) 
		{
			PRINT_THREE(4, "Connect #" + to_string(j), to_string(it->first));
			j++;
		}
		LeaveCriticalSection(&connectionServerService->socketListCriticalSection);
		i++;
	}
	LeaveCriticalSection(&serviceListCriticalSection);
}


void RIOManager::PrintBufferUsageStatistics() 
{
	bufferManager.PrintBufferState();
	return;
}

///This function closes all resources associated with the RIOManager.
void RIOManager::Shutdown() 
{

	PRINT_FOUR(0, "RIO MANAGER", "SHUTDOWN", "Initializing shutdown sequence. . .");

	
	CloseAllSockets();
	CloseCQs();
	CloseIOCPHandles();


	//Close down critical section dump
	for (std::vector<CRITICAL_SECTION>::iterator it = criticalSectionDump.begin(); it != criticalSectionDump.end(); ++it) 
	{
		DeleteCriticalSection(&(*it));
	}


	DeleteCriticalSection(&serviceListCriticalSection);

	bufferManager.ShutdownCleanup(rioFunctions);
	

	PRINT_THREE(1, "COMPLETE", " ");

}


//////PRIVATE FUNCTIONS

///This function gets the main IOCP



///This function creates a new service in the RIO Manager service list.
///Note that the receive and send CQs are set to the default value.
int RIOManager::CreateNewService(int typeCode, int portNumber, int maxClients, int serviceMaxAccepts, int serviceRQMaxReceives, int serviceRQMaxSends, bool isAddressRequired, bool isUDPService, SOCKET listeningSocket, RIO_RQ udpRQ, CRITICAL_SECTION udpCriticalSection, LPFN_ACCEPTEX acceptExFunction) 
{
	if (serviceList.find(typeCode) != serviceList.end()) 
	{
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
	service.isUDPService = isUDPService;
	service.isAddressRequired = isAddressRequired;
	service.roundRobinLocation = 0;
	service.maxClients = maxClients;
	service.serviceMaxAccepts = serviceMaxAccepts;
	service.serviceRQMaxReceives = serviceRQMaxReceives;
	service.serviceRQMaxSends = serviceRQMaxSends;

	InitializeCriticalSectionAndSpinCount(&service.roundRobinCriticalSection, rioSpinCount);
	InitializeCriticalSectionAndSpinCount(&service.socketListCriticalSection, rioSpinCount);

	EnterCriticalSection(&serviceListCriticalSection);
	serviceList.insert(std::pair<DWORD, ConnectionServerService>(typeCode, service));
	LeaveCriticalSection(&serviceListCriticalSection);

	return 0;

}

int RIOManager::CreateNewService(int typeCode, int portNumber, int maxClients, int serviceMaxAccepts, int serviceRQMaxReceives, int serviceRQMaxSends, bool isAddressRequired, bool isUDPService, SOCKET listeningSocket, RIO_RQ udpRQ, CRITICAL_SECTION udpCriticalSection) 
{
	return CreateNewService(typeCode, portNumber, maxClients, serviceMaxAccepts, serviceRQMaxReceives, serviceRQMaxSends, isAddressRequired, isUDPService, listeningSocket, udpRQ, udpCriticalSection, nullptr);
}

int RIOManager::CreateNewService(int typeCode, int portNumber, int maxClients, int serviceMaxAccepts, int serviceRQMaxReceives, int serviceRQMaxSends, bool isAddressRequired, bool isUDPService, SOCKET listeningSocket, LPFN_ACCEPTEX acceptExFunction) 
{
	CRITICAL_SECTION emptyCriticalSection;
	InitializeCriticalSectionAndSpinCount(&emptyCriticalSection, rioSpinCount);
	return CreateNewService(typeCode, portNumber, maxClients, serviceMaxAccepts, serviceRQMaxReceives, serviceRQMaxSends, isAddressRequired, isUDPService, listeningSocket, RIO_INVALID_RQ, emptyCriticalSection, acceptExFunction);
}


///This function creates a new service in the RIO Manager service list.
///Note that the receive and send CQs are set to the default value.
int RIOManager::CreateNewService(int typeCode, int portNumber, int maxClients, int serviceMaxAccepts, int serviceRQMaxReceives, int serviceRQMaxSends, bool isAddressRequired, bool isUDPService, SOCKET listeningSocket) 
{
	CRITICAL_SECTION emptyCriticalSection;
	InitializeCriticalSectionAndSpinCount(&emptyCriticalSection, rioSpinCount);
	return CreateNewService(typeCode, portNumber, maxClients, serviceMaxAccepts, serviceRQMaxReceives, serviceRQMaxSends, isAddressRequired, isUDPService, listeningSocket, RIO_INVALID_RQ, emptyCriticalSection, nullptr);
}



int RIOManager::AddEntryToService(int typeCode, int socketContext, RIO_RQ rioRQ, SOCKET socket, CRITICAL_SECTION criticalSection) 
{
	//Find the service entry
	EnterCriticalSection(&serviceListCriticalSection);
	ServiceList::iterator iter = serviceList.find(typeCode);
	if (iter == serviceList.end()) 
	{
		LeaveCriticalSection(&serviceListCriticalSection);
		return -1;		//Service doesn't exist
	}
	LeaveCriticalSection(&serviceListCriticalSection);

	//Get the service entry
	ConnectionServerService service;
	service = iter->second;

	//Get a pointer to the service list in which we will add to
	SocketList* socketList;
	socketList = service.socketList;

	EnterCriticalSection(&service.socketListCriticalSection);
	if ((*socketList).find(socketContext) != (*socketList).end()) 
	{
		LeaveCriticalSection(&service.socketListCriticalSection);			//Be sure to leave CritSec before returning...
		return -2;		//Particular socket entry already exists
	}
	LeaveCriticalSection(&service.socketListCriticalSection);

	if (rioRQ == NULL || rioRQ == RIO_INVALID_RQ) 
	{
		return -3;		//Invalid RIO_RQ
	}

	//Update the round-robin location if this is the first service
	if (service.roundRobinLocation == 0) 
	{
		EnterCriticalSection(&service.roundRobinCriticalSection);
		service.roundRobinLocation = socketContext;
		LeaveCriticalSection(&service.roundRobinCriticalSection);
	}

	//Add the socket context/ RQ pair into the service
	RQ_Handler rqHandler;
	rqHandler.rio_RQ = rioRQ;
	rqHandler.socket = socket;
	rqHandler.criticalSection = criticalSection;
	EnterCriticalSection(&service.socketListCriticalSection);
	(*socketList).insert(std::pair<int, RQ_Handler>(socketContext, rqHandler));
	LeaveCriticalSection(&service.socketListCriticalSection);

	return 0;
}


SOCKET RIOManager::GetListeningSocket(int typeCode) 
{

	//Find the service entry
	EnterCriticalSection(&serviceListCriticalSection);
	ServiceList::iterator iter = serviceList.find(typeCode);
	if (iter == serviceList.end()) 
	{
		PRINT_THREE(3, "Error", "GetListeningSocket(); Did not find Service #" + to_string(typeCode));
		LeaveCriticalSection(&serviceListCriticalSection);
		return INVALID_SOCKET;		//Service doesn't exist
	}
	LeaveCriticalSection(&serviceListCriticalSection);

	//Get the service entry
	ConnectionServerService service;
	service = iter->second;

	if (service.listeningSocket == NULL) 
	{
		PRINT_THREE(3, "Error", "GetListeningSocket(); Invalid Socket with Service #" + to_string(typeCode));
		return INVALID_SOCKET;		//Socket Not Assigned
	}

	return service.listeningSocket;
}


int RIOManager::FillAcceptStructures(int typeCode, int numStruct) 
{

	//Find the service entry
	//EnterCriticalSection(&serviceListCriticalSection);
	ServiceList::iterator iter = serviceList.find(typeCode);
	if (iter == serviceList.end()) 
	{
		PRINT_THREE(3, "Error", "FillAcceptStructures(); Did not find Service #" + to_string(typeCode));
		//LeaveCriticalSection(&serviceListCriticalSection);
		return INVALID_SOCKET;		//Service doesn't exist
	}
	//LeaveCriticalSection(&serviceListCriticalSection);

	//Get the service entry
	ConnectionServerService* service;
	service = &iter->second;

	ExtendedOverlapped* exOver;

	for (int i = 0; i < numStruct; i++) 
	{
		exOver = new ExtendedOverlapped();
		exOver->serviceType = typeCode;
		exOver->relevantSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_IP, NULL, 0, WSA_FLAG_OVERLAPPED | WSA_FLAG_REGISTERED_IO);
		if (exOver->relevantSocket == INVALID_SOCKET) 
		{
			PRINT_WIN_ERROR(3, "Error", "WSASocket failed during FillAcceptStructures(); Number of accepts posted = " + to_string(i));
			return i;
		}
		service->acceptStructs.push_back(*exOver);

		int test = BeginAcceptEx(exOver, service->acceptExFunction);
		if (test != 0) 
		{	//AcceptEx failed (except for WSA_IO_PENDING)
			PRINT_THREE(3, "Error", "BeginAcceptEx() failed during FillAcceptStructures(); Number of accepts posted = " + to_string(i));
			return i;
		}
	}

	return numStruct;
}


int RIOManager::BeginAcceptEx(ExtendedOverlapped* extendedOverlapped, LPFN_ACCEPTEX acceptExFunction) 
{
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
		if (GetLastError() == WSA_IO_PENDING) 
		{
				//This is a normal error message that just means that there is already another acceptEx waiting to complete
				//Our current acceptEx was registered successfully
		}
		else 
		{	//On an unknown error we will print an error message and determine what happened
			PRINT_WIN_ERROR(1, "ERROR", "AcceptEx call failed.");
			return -1;
		}
	}

	return 0;
}

///Gets the first IOCP handle in the list of IOCPs
HANDLE RIOManager::GetMainIOCP() 
{
	if (iocpList.empty()) 
	{
		return INVALID_HANDLE_VALUE;
	}
	return iocpList.front();
}

///This function gets the first RIO CQ from the list
CQ_Handler RIOManager::GetMainRIOCQ() 
{
	if (rioCQList.empty()) 
	{
		return CQ_Handler();
	}
	return rioCQList.front();
}



bool RIOManager::PostReceiveOnUDPService(int serviceType, DWORD flags) 
{

	ExtendedRioBuf* rioBuf = bufferManager.GetBuffer();
	if (rioBuf == nullptr) 
	{
		PRINT_THREE(1, "ERROR", "Could not post receive. No Buffers available. UDP Service #" + to_string(serviceType));
		return false;
	}

	EnterCriticalSection(&serviceListCriticalSection);
	ServiceList::iterator iter = serviceList.find(serviceType);
	ConnectionServerService connServ = iter->second;
	LeaveCriticalSection(&serviceListCriticalSection);

	rioBuf->srcType = (SRC_DEST_TYPE)serviceType;

	bool result;

	rioBuf->operationType = OP_RECEIVE;

	EnterCriticalSection(&connServ.udpCriticalSection);
	mySeries.write_flag(_T("RIOReceive"));
	result = rioFunctions.RIOReceive(connServ.udpRQ, rioBuf, 1, flags, rioBuf);
	LeaveCriticalSection(&connServ.udpCriticalSection);
	
	if (result == false) 
	{
		bufferManager.FreeBuffer(rioBuf);
	}

	return result;
}

bool RIOManager::PostReceiveOnUDPService(int serviceType) 
{
	return PostReceiveOnUDPService(serviceType, 0);
}



bool RIOManager::PostReceiveOnTCPService(int serviceType, int destinationCode, DWORD flags) 
{

	
	if (destinationCode == 0) 
	{
		PRINT_THREE(1, "ERROR", "Could not post receive. Erroneous destination code on TCP Service of value #" + to_string(serviceType));
		return false;
	}
	
	
	ExtendedRioBuf* rioBuf = bufferManager.GetBuffer();
	if (rioBuf == nullptr) 
	{
		PRINT_THREE(1, "ERROR", "Could not post receive. No Buffers available. TCP Service #" + to_string(serviceType));
		PRINT_THREE(2, "DST CODE", to_string(destinationCode));
		return false;
	}

	
	EnterCriticalSection(&serviceListCriticalSection);
	ServiceList::iterator iter = serviceList.find(serviceType);
	ConnectionServerService connServ = iter->second;
	LeaveCriticalSection(&serviceListCriticalSection);

	
	mySeries.write_flag(_T("Point 1"));
	EnterCriticalSection(&connServ.socketListCriticalSection);
	mySeries.write_flag(_T("Point 2"));
	SocketList::iterator iterSL = connServ.socketList->find(destinationCode);
	mySeries.write_flag(_T("Point 3"));
	if (iterSL == connServ.socketList->end()) 
	{
		mySeries.write_flag(_T("Point 4"));
		//Service entry no longer exists and has been cleared
		PRINT_THREE(1, "ERROR", "Post Receive Fail. Entry no longer exists on TCP Service #" + to_string(serviceType));
		PRINT_THREE(2, "DST CODE", to_string(destinationCode));
		mySeries.write_flag(_T("Point 5"));
		LeaveCriticalSection(&connServ.socketListCriticalSection);
		mySeries.write_flag(_T("Point 6"));
		bufferManager.FreeBuffer(rioBuf);
		mySeries.write_flag(_T("Point 7"));
		return false;
	}
	mySeries.write_flag(_T("Point 8"));
	RQ_Handler rqHandler = iterSL->second;
	mySeries.write_flag(_T("Point 9"));
	LeaveCriticalSection(&connServ.socketListCriticalSection);
	Sleep(0);
	mySeries.write_flag(_T("Point 10"));


	
	rioBuf->srcType = (SRC_DEST_TYPE)serviceType;

	rioBuf->operationType = OP_RECEIVE;

	//if (serviceType == 1)
	//	PRINT_THREE(3, "Test Point", "4");

	rioBuf->socketContext = destinationCode;

	bool result;

	
	if (rqHandler.rio_RQ == RIO_INVALID_RQ || rqHandler.socket == INVALID_SOCKET) 
	{
		PRINT_THREE(1, "ERROR", "Post Receive Fail. Entry exists, but RQ or Socket invalid on TCP Service #" + to_string(serviceType));
		PRINT_THREE(2, "DST CODE", to_string(destinationCode));
		bufferManager.FreeBuffer(rioBuf);
		return false;
	}

	
	EnterCriticalSection(&rqHandler.criticalSection);
	mySeries.write_flag(_T("RIOReceive"));
	result = rioFunctions.RIOReceive(rqHandler.rio_RQ, rioBuf, 1, flags, rioBuf);
	LeaveCriticalSection(&rqHandler.criticalSection);

	
	if (result == false) 
	{
		bufferManager.FreeBuffer(rioBuf);
	}

	
	return result;
}

bool RIOManager::PostReceiveOnTCPService(int serviceType, int destinationCode) 
{
	return PostReceiveOnTCPService(serviceType, destinationCode, 0);
}


//********CLOSING/CLEANUP FUNCTIONS********

///This function goes through the list of IOCP handles and closes them all for proper cleanup
void RIOManager::CloseIOCPHandles() 
{

	PRINT_THREE(1, "CloseIOCPHandles", "Closing all IOCP Handles. . .");
	int i = 1;


	while (!iocpList.empty()) 
	{
		HANDLE goodbyeIOCP = iocpList.front();

		PRINT_THREE(2, "LOOP", "Closing IOCP Handle #" + to_string(i));
		i++;

		CloseHandle(goodbyeIOCP);
		iocpList.pop_front();
	}

	return;
}

///This function closes all RIO Completion Queues and their corresponding critical sections.
void RIOManager::CloseCQs() 
{
	CQ_Handler goodbyeRIOCQ;


	PRINT_THREE(1, "CloseCQs", "Closing all RIO CQs and critical sections. . .");
	int i = 1;


	while (!rioCQList.empty()) 
	{
		goodbyeRIOCQ = rioCQList.front();
		rioCQList.pop_front();

		PRINT_THREE(2, "LOOP", "Closing RIO_CQ #" + to_string(i));

		rioFunctions.RIOCloseCompletionQueue(goodbyeRIOCQ.rio_CQ);

		PRINT_THREE(2, "LOOP", "Closing Critical Section #" + to_string(i));
		i++;

		DeleteCriticalSection(&goodbyeRIOCQ.criticalSection);
	}
	return;
}

///This function closes a specific entry on a specific service
int RIOManager::CloseServiceEntry(int typeCode, int socketContext) 
{
	ConnectionServerService* connectionServerService;
	SocketList* sockList;
	RQ_Handler* rqHandler;

	//Find the service entry
	EnterCriticalSection(&serviceListCriticalSection);
	ServiceList::iterator iter = serviceList.find(typeCode);
	if (iter == serviceList.end()) 
	{
		//PRINT_THREE(1, "ERROR", "Close Service Entry: Can't find service #" + to_string(typeCode));
		LeaveCriticalSection(&serviceListCriticalSection);
		return -1;		//Service doesn't exist
	}
	connectionServerService = &iter->second;
	LeaveCriticalSection(&serviceListCriticalSection);

	sockList = connectionServerService->socketList;

	//Find the specific entry
	EnterCriticalSection(&connectionServerService->socketListCriticalSection);
	SocketList::iterator sockIter = sockList->find(socketContext);
	if (sockIter == sockList->end()) 
	{
		//PRINT_THREE(1, "ERROR", "Service #" + to_string(typeCode), "Close Service Entry: Can't find entry #" + to_string(socketContext));
		LeaveCriticalSection(&connectionServerService->socketListCriticalSection);
		return -2;		//Service doesn't exist
	}
	rqHandler = &sockIter->second;
	EnterCriticalSection(&rqHandler->criticalSection);
	closesocket(rqHandler->socket);
	LeaveCriticalSection(&rqHandler->criticalSection);
	sockList->erase(socketContext);
	LeaveCriticalSection(&connectionServerService->socketListCriticalSection);

	PRINT_THREE(1, "Service #" + to_string(typeCode), "Closing connection with entry #" + to_string(socketContext));

	

	//Cannot delete the critical section because other threads my still be processing the critical section
	criticalSectionDump.push_back(rqHandler->criticalSection);

	return 0;
}

///This function closes all sockets stored within the service list.
void RIOManager::CloseAllSockets() 
{
	ConnectionServerService* connectionServerService;
	RQ_Handler* rqHandler;


	PRINT_THREE(1, "CloseAllSockets", "Closing all sockets (and RIO RQs). . .");
	int i = 1;
	int j = 1;


	//iterate through all registered services
	//EnterCriticalSection(&serviceListCriticalSection);
	for (auto it = serviceList.begin(); it != serviceList.end(); ++it) 
	{
		//Close the service's listening socket
		connectionServerService = &it->second;


		PRINT_THREE(2, "LOOP #" + to_string(i), "Closing Service at port #" + to_string((*connectionServerService).port));


		closesocket((*connectionServerService).listeningSocket);

		SocketList* sockList = (*connectionServerService).socketList;
		//iterate through all sockets associated with the service
		EnterCriticalSection(&connectionServerService->socketListCriticalSection);
		for (auto it = (*sockList).begin(); it != (*sockList).end(); ++it) 
		{


			PRINT_THREE(3, "LOOP #" + to_string(i), "Closing Service's Socket #" + to_string(j));
			j++;


			//Close each socket
			rqHandler = &it->second;
			closesocket((*rqHandler).socket);
			DeleteCriticalSection(&rqHandler->criticalSection);
		}
		LeaveCriticalSection(&connectionServerService->socketListCriticalSection);

		DeleteCriticalSection(&connectionServerService->socketListCriticalSection);

		i++;

	}
	//LeaveCriticalSection(&serviceListCriticalSection);
}

///This function prints a message to console with a specified format (two boxes).
void RIOManager::PrintMessageFormatter(int level, const string &type, const string &subtype, string message) 
{
	EnterCriticalSection(&consoleCriticalSection);
	if (level == 0) 
	{
		printf_s("\n");
	}

	//Initial Spacing based on "level"
	for (int i = 0; i < level; i++) 
	{
		printf_s("               ");
	}

	//Print Boxes
	PrintBox(type);
	PrintBox(subtype);

	//Print Message
	printf_s("%s\n", message.c_str());

	LeaveCriticalSection(&consoleCriticalSection);
	return;
}

///This function prints a message to console with a specified format (one box).
void RIOManager::PrintMessageFormatter(int level, const string &type, string message) 
{
	EnterCriticalSection(&consoleCriticalSection);

	if (level == 0) 
	{
		printf_s("\n");
	}

	//Initial Spacing based on "level"
	for (int i = 0; i < level; i++) 
	{
		printf_s("               ");
	}

	//Print Boxes
	PrintBox(type);

	//Print Message
	printf_s("%s\n", message.c_str());

	LeaveCriticalSection(&consoleCriticalSection);
	return;
}

///This function prints a box with a word inside.
void RIOManager::PrintBox(string word) 
{
	int length;
	printf_s("[");

	if (word.empty()) 
	{
		printf_s("STRING ERROR]\n");
		return;
	}

	length = word.length();
	if (length < 12) 
	{
		printf_s("%s", word.c_str());
		for (int x = 0; x < (12 - length); x++) 
		{
			printf_s(" ");
		}
	}
	else 
	{
		printf_s("%s", word.substr(0, 12).c_str());
	}
	printf_s("] ");
}

///This function gets the last windows error and prints it using our message formatter.
void RIOManager::PrintWindowsErrorMessage() 
{
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
