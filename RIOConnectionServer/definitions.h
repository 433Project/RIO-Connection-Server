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
	OP_RECEIVE,
	OP_SEND
};

enum COMPLETION_KEY {
	CK_QUIT,
	CK_RIO,
	CK_ACCEPT,
	CK_GETINFO,
	CK_COUNTER,
	CK_BUFINFO
};
enum SrcDstType
{
	MATCHING_SERVER = 0,
	MATCHING_CLIENT,
	ROOM_SERVER,
	PACKET_GENERATOR,
	MONITORING_SERVER,
	CONFIG_SERVER,
	CONNECTION_SERVER
};

enum Command
{
	MATCH_REQUEST = 0,
	MATCH_RESPONSE,
	LATENCY,
	HEALTH_CHECK,
	MSLIST_REQUEST,
	MSLIST_RESPONSE,
	PG_START,
	PG_END,
	ROOM_CREATE_REQUEST,
	ROOM_CREATE_RESPONSE,
	ROOM_JOIN_REQUEST,
	ROOM_JOIN_RESPONSE,
	GAME_START,
	GAME_END,
};

enum Status
{
	SUCCESS = 0,
	FAIL,
	NONE
};


struct Body
{
	Command command;
	Status status;
	char* data;
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
	SrcDstType srcType;
	int socketContext;
	char* buffer;
	int messageLength;

};

struct Instruction {
	InstructionType type;
	int socketContext;		//Destination Code
	SrcDstType destinationType;
	EXTENDED_RIO_BUF* buffer;
};


