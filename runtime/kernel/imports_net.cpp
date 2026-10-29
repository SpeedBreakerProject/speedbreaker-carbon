// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// The network stack (xam's NetDll_* exports): a console with its cable
// unplugged. NFS: Carbon starts XNet and Winsock at boot and runs a network
// thread that waits on Winsock events; Most Wanted never called any of it.
// The stack starts, the link is down, the title has no address, and any
// socket fails with WSAENETDOWN, so the game takes its offline path.
// Semantics: Winsock's, and Xenia's xam_net.cc for the Xbox specifics.
// WSAEVENTs are kernel event handles (manual reset), as on the console.
// The guest's error codes and values are X_WSA...: on Windows, <windows.h>
// (winerror.h) defines WSAENETDOWN and its kin as macros.
#include <stdafx.h>
#include <random>
#include <set>
#include "dispatcher.h"
#include "function.h"

uint32_t NtClose(uint32_t handle);

using namespace dispatcher;

namespace
{
    constexpr uint32_t X_WSAENETDOWN = 10050;
    constexpr uint32_t X_WSA_INVALID_HANDLE = 6;
    constexpr uint32_t X_WSA_WAIT_FAILED = 0xFFFFFFFF;
    constexpr uint32_t X_INVALID_SOCKET = 0xFFFFFFFF;
    constexpr uint32_t X_SOCKET_ERROR = 0xFFFFFFFF;
    constexpr uint32_t XNET_GET_XNADDR_NONE = 0x00000001;

    thread_local uint32_t t_lastError = 0;

    // A line the first time each export is used (they run in loops).
    void Once(const char* name, const char* what)
    {
        static std::mutex mutex;
        static std::set<std::string> seen;
        std::lock_guard lock(mutex);
        if (seen.insert(name).second)
            fprintf(stderr, "[net] %s: %s (no network: cable unplugged)\n", name, what);
    }

    // WSADATA as the guest lays it out.
    struct X_WSADATA
    {
        be<uint16_t> version;
        be<uint16_t> highVersion;
        char description[257];
        char systemStatus[129];
        be<uint16_t> maxSockets;
        be<uint16_t> maxUdpDatagram;
        be<uint32_t> vendorInfo;
    };
}

uint32_t NetDll_XNetStartup(uint32_t caller, void* params)
{
    Once("XNetStartup", "started");
    return 0;
}

uint32_t NetDll_XNetCleanup(uint32_t caller)
{
    Once("XNetCleanup", "stopped");
    return 0;
}

uint32_t NetDll_WSAStartup(uint32_t caller, uint32_t version, X_WSADATA* data)
{
    Once("WSAStartup", "started");
    if (data != nullptr)
    {
        memset(data, 0, sizeof(*data));
        data->version = uint16_t(version);
        data->highVersion = 0x0202;
        strcpy(data->description, "WinSock 2.0");
        strcpy(data->systemStatus, "Not Ready");
    }
    t_lastError = 0;
    return 0;
}

uint32_t NetDll_WSACleanup(uint32_t caller)
{
    return 0;
}

uint32_t NetDll_WSAGetLastError()
{
    return t_lastError;
}

void NetDll_WSASetLastError(uint32_t error)
{
    t_lastError = error;
}

uint32_t NetDll_XNetGetEthernetLinkStatus(uint32_t caller)
{
    Once("XNetGetEthernetLinkStatus", "no link");
    return 0;
}

uint32_t NetDll_XNetGetTitleXnAddr(uint32_t caller, uint8_t* address)
{
    Once("XNetGetTitleXnAddr", "no address");
    if (address != nullptr)
        memset(address, 0, 36);  // XNADDR
    return XNET_GET_XNADDR_NONE;
}

uint32_t NetDll_XNetGetDebugXnAddr(uint32_t caller, uint8_t* address)
{
    if (address != nullptr)
        memset(address, 0, 36);
    return XNET_GET_XNADDR_NONE;
}

// Session keys (XNKID + XNKEY): a table on the console, no network needed,
// so these work with the cable unplugged too. A new key is random bytes.
uint32_t NetDll_XNetCreateKey(uint32_t caller, uint8_t* keyId, uint8_t* exchangeKey)
{
    static std::mt19937 rng{ std::random_device{}() };
    for (uint8_t* p : { keyId, exchangeKey })
        if (p != nullptr)
            for (int i = 0; i < (p == keyId ? 8 : 16); i++)
                p[i] = uint8_t(rng());
    if (keyId != nullptr)
        keyId[0] &= 0x0F;  // the type nibble: XNET_XNKID_SYSTEM_LINK (a title-made key)
    return 0;
}

uint32_t NetDll_XNetRegisterKey(uint32_t caller, const uint8_t* keyId, const uint8_t* exchangeKey)
{
    return 0;
}

uint32_t NetDll_XNetUnregisterKey(uint32_t caller, const uint8_t* keyId)
{
    return 0;
}

uint32_t NetDll_socket(uint32_t caller, uint32_t family, uint32_t type, uint32_t protocol)
{
    Once("socket", "fails");
    t_lastError = X_WSAENETDOWN;
    return X_INVALID_SOCKET;
}

uint32_t NetDll_closesocket(uint32_t caller, uint32_t socket)
{
    t_lastError = X_WSAENETDOWN;
    return X_SOCKET_ERROR;
}

// ---- Winsock events: kernel events -------------------------------------------

uint32_t NetDll_WSACreateEvent()
{
    return GetKernelHandle(CreateKernelObject<EventObject>(true, false));
}

uint32_t NetDll_WSACloseEvent(uint32_t event)
{
    if (NtClose(event) != STATUS_SUCCESS)
    {
        t_lastError = X_WSA_INVALID_HANDLE;
        return 0;
    }
    return 1;
}

uint32_t NetDll_WSASetEvent(uint32_t event)
{
    auto* e = IsKernelObject(event) ? dynamic_cast<EventObject*>(GetKernelObject(event)) : nullptr;
    if (e == nullptr)
    {
        t_lastError = X_WSA_INVALID_HANDLE;
        return 0;
    }
    e->Set();
    return 1;
}

uint32_t NetDll_WSAResetEvent(uint32_t event)
{
    auto* e = IsKernelObject(event) ? dynamic_cast<EventObject*>(GetKernelObject(event)) : nullptr;
    if (e == nullptr)
    {
        t_lastError = X_WSA_INVALID_HANDLE;
        return 0;
    }
    e->Reset();
    return 1;
}

// WSA_WAIT_EVENT_0 + i and WSA_WAIT_TIMEOUT are the kernel's STATUS_WAIT_0 + i
// and STATUS_TIMEOUT. The timeout is in ms (0xFFFFFFFF: none).
uint32_t NetDll_WSAWaitForMultipleEvents(uint32_t count, be<uint32_t>* events, uint32_t waitAll, uint32_t timeout,
    uint32_t alertable)
{
    if (count == 0 || events == nullptr)
    {
        t_lastError = X_WSA_INVALID_HANDLE;
        return X_WSA_WAIT_FAILED;
    }
    std::vector<Waitable*> objects;
    for (uint32_t i = 0; i < count; i++)
    {
        Waitable* w = WaitableFromHandle(events[i]);
        if (w == nullptr)
        {
            t_lastError = X_WSA_INVALID_HANDLE;
            return X_WSA_WAIT_FAILED;
        }
        objects.push_back(w);
    }
    return WaitForObjects(objects, waitAll != 0, DeadlineFromMilliseconds(timeout));
}

GUEST_FUNCTION_HOOK(__imp__NetDll_XNetStartup, NetDll_XNetStartup);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetCleanup, NetDll_XNetCleanup);
GUEST_FUNCTION_HOOK(__imp__NetDll_WSAStartup, NetDll_WSAStartup);
GUEST_FUNCTION_HOOK(__imp__NetDll_WSACleanup, NetDll_WSACleanup);
GUEST_FUNCTION_HOOK(__imp__NetDll_WSAGetLastError, NetDll_WSAGetLastError);
GUEST_FUNCTION_HOOK(__imp__NetDll_WSASetLastError, NetDll_WSASetLastError);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetGetEthernetLinkStatus, NetDll_XNetGetEthernetLinkStatus);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetGetTitleXnAddr, NetDll_XNetGetTitleXnAddr);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetGetDebugXnAddr, NetDll_XNetGetDebugXnAddr);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetCreateKey, NetDll_XNetCreateKey);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetRegisterKey, NetDll_XNetRegisterKey);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetUnregisterKey, NetDll_XNetUnregisterKey);
GUEST_FUNCTION_HOOK(__imp__NetDll_socket, NetDll_socket);
GUEST_FUNCTION_HOOK(__imp__NetDll_closesocket, NetDll_closesocket);
GUEST_FUNCTION_HOOK(__imp__NetDll_WSACreateEvent, NetDll_WSACreateEvent);
GUEST_FUNCTION_HOOK(__imp__NetDll_WSACloseEvent, NetDll_WSACloseEvent);
GUEST_FUNCTION_HOOK(__imp__NetDll_WSASetEvent, NetDll_WSASetEvent);
GUEST_FUNCTION_HOOK(__imp__NetDll_WSAResetEvent, NetDll_WSAResetEvent);
GUEST_FUNCTION_HOOK(__imp__NetDll_WSAWaitForMultipleEvents, NetDll_WSAWaitForMultipleEvents);
