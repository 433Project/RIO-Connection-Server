// stdafx.h : 자주 사용하지만 자주 변경되지는 않는
// 표준 시스템 포함 파일 또는 프로젝트 관련 포함 파일이
// 들어 있는 포함 파일입니다.
//
// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once

#pragma comment(lib, "ws2_32.lib")

#define NOMINMAX

#include "targetver.h"

#include <stdio.h>
#include <tchar.h>

#include <WinSock2.h>
#include <MSWsock.h>
#include <iostream>

using std::cout; 
using std::endl;
