#pragma once

#include "targetver.h"

#define _CRTDBG_MAP_ALLOC  
#include <stdio.h>
#include <tchar.h>
#include <Ws2tcpip.h>
#include <WinSock2.h>
#include <Windows.h>
#include <experimental\coroutine>
#include <future>
#include <tuple>
#include <string>
#include <iostream>
#include <unordered_map>
#include <crtdbg.h> 
#include <algorithm>
#include <ctime>
#include <chrono>

#ifdef _RESUMABLE_FUNCTIONS_SUPPORTED
#include <experimental/resumable>
#endif


#include <Mswsock.h>

#ifdef _DEBUG
#define new new ( _NORMAL_BLOCK , __FILE__ , __LINE__ )
#endif