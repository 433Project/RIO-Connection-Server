#pragma once
#include "stdafx.h"

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
	CK_QUIT,
	CK_RIO,
	CK_ACCEPT,
	CK_GETINFO
};

enum DestinationType
{
	MATCHING_SERVER = 0,
	MATCHING_CLIENT,
	ROOM_MANAGER,
	PACKET_GENERATOR,
	MONITORING_SERVER
};

struct EXTENDED_OVERLAPPED : public OVERLAPPED {
	int serviceType;
	SOCKET relevantSocket;
	char* buffer = new char[(2*(sizeof(sockaddr_in))+32)];
};

struct CQ_Handler {
	CQ_Handler() {
		rio_CQ = RIO_INVALID_CQ;
	}

	RIO_CQ rio_CQ;
	CRITICAL_SECTION criticalSection;
};

struct RQ_Handler {
	RIO_RQ rio_RQ;
	SOCKET socket;
	CRITICAL_SECTION criticalSection;
};


struct EXTENDED_RIO_BUF : public RIO_BUF
{
	OperationType operationType;
	DestinationType srcType;
	int socketContext;
	char* buffer;
	int messageLength;

};

struct Instruction {
	InstructionType type;
	int socketContext;		//Destination Code
	DestinationType destinationType;
	EXTENDED_RIO_BUF* buffer;
	int length;
};


