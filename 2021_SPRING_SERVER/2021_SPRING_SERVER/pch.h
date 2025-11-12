#pragma once
#include <iostream>
#include <Windows.h>
#include <map>
#include <thread>
#include <vector>
#include <mutex>
#include <unordered_set>
#include <unordered_map>
#include <queue>
#include <atomic>
#include <ctime>
#include <chrono>
#include <sqlext.h>

#include <WS2tcpip.h>
#include <MSWSock.h>

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "MSWSock.lib")
#pragma comment(lib, "lua54.lib")

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

using namespace std;
using namespace chrono;

#include "2021_SPRING_PROTOCOL.h"
#include "Struct.h"