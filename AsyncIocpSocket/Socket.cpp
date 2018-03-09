#include "stdafx.h"
#include "Socket.h"
#include "StaticMap.h"
#include "SocketError.h"

using namespace IO::Networking::Sockets;

struct MyOverlapped : public WSAOVERLAPPED
{
	void* state;
};

struct AsyncIoState
{
	Async::Awaitable<int> completionSource;
	std::function<void()> disconnectCallback;
};

struct AsyncAcceptState
{
	AsyncAcceptState(Socket&& socket, char* buffer) : clientSocket(std::move(socket)), buffer(buffer) {}
	Async::Awaitable<Socket> completionSource;
	Socket clientSocket;
	char* buffer;
};

void initializeWsa()
{
	WORD versionRequested = MAKEWORD(2, 2);
	WSADATA wsaData;
	WSAStartup(versionRequested, &wsaData);
	if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
		/* Tell the user that we could not find a usable */
		/* WinSock DLL.                                  */
		throw SocketError(_T("Could not find a usable version of Winsock.dll\n"));
		WSACleanup();
	}
}

void WINAPI AcceptCallback(
	_Inout_     PTP_CALLBACK_INSTANCE Instance,
	_Inout_opt_ PVOID                 Context,
	_Inout_opt_ PVOID                 Overlapped,
	_In_        ULONG                 IoResult,
	_In_        ULONG_PTR             NumberOfBytesTransferred,
	_Inout_     PTP_IO                Io
)
{

	LPWSAOVERLAPPED wsaOverlapped = static_cast<LPWSAOVERLAPPED>(Overlapped);
	MyOverlapped* overlapped = static_cast<MyOverlapped*>(wsaOverlapped);
	AsyncAcceptState* state = static_cast<AsyncAcceptState*>(overlapped->state);
	state->completionSource.SetResult(std::move(state->clientSocket));
	
	delete[] state->buffer; 
	delete state;
	delete overlapped;
}

void WINAPI IoCallback(
	_Inout_     PTP_CALLBACK_INSTANCE Instance,
	_Inout_opt_ PVOID                 Context,
	_Inout_opt_ PVOID                 Overlapped,
	_In_        ULONG                 IoResult,
	_In_        ULONG_PTR             NumberOfBytesTransferred,
	_Inout_     PTP_IO                Io
)
{
	LPWSAOVERLAPPED wsaOverlapped = static_cast<LPWSAOVERLAPPED>(Overlapped);
	MyOverlapped* myOverlapped = static_cast<MyOverlapped*>(wsaOverlapped);
	AsyncIoState* state = static_cast<AsyncIoState*>(myOverlapped->state);
	if (IoResult != 0)
	{
		state->completionSource.SetException(std::make_exception_ptr<SocketError>(IoResult));
		delete state;
		delete myOverlapped;
	}
	if (NumberOfBytesTransferred == 0)
	{
		state->disconnectCallback();
		state->completionSource.SetException(std::make_exception_ptr<SocketError>(WSAECONNRESET));
	}
	else
	{
		state->completionSource.SetResult(0);
	}
	
	delete state;
	delete myOverlapped;
}

IO::Networking::Sockets::Socket::Socket(SOCKET socket) : _socket(socket), server_mode(false), client_mode(false)
{
	initializeWsa();
	_io = CreateThreadpoolIo((HANDLE)socket, IoCallback, this, NULL);
}

IO::Networking::Sockets::Socket::Socket(EAddressFamily addressFamily, ESocketType addressType, EProtocolType protocol) noexcept :
	addressFamily(addressFamily),
	socketType(addressType),
	protocol(protocol),
	_socket(INVALID_SOCKET),
	server_mode(false),
	client_mode(false)
{
	initializeWsa();
}

Socket& IO::Networking::Sockets::Socket::operator=(Socket&& another) noexcept
{
	Dispose();
	client_mode = another.client_mode;
	server_mode = another.server_mode;
	_socket = another._socket;
	addressFamily = another.addressFamily;
	socketType = another.socketType;
	protocol = another.protocol;
	another._socket = INVALID_SOCKET;
	_io = another._io;
	another._io = nullptr;
	initializeWsa();
	return *this;
}

IO::Networking::Sockets::Socket::Socket(Socket && another) noexcept
{
	operator=(std::move(another));
}

void IO::Networking::Sockets::Socket::Bind(std::string ip, uint32_t port)
{
	if (_socket != INVALID_SOCKET || client_mode)
	{
		throw std::logic_error("cannot bind because of socket state not correct");
	}
	INT getAddrInfoResult;
	addrinfo hints, *result;
	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = static_cast<int>(addressFamily);
	hints.ai_socktype = static_cast<int>(socketType);
	hints.ai_protocol = static_cast<int>(protocol);
	hints.ai_flags = AI_PASSIVE;
	getAddrInfoResult = getaddrinfo(ip.c_str(), std::to_string(port).c_str(), &hints, &result);
	if (getAddrInfoResult != 0)
	{
		throw SocketError(getAddrInfoResult);
	}
	_socket = WSASocket(result->ai_family, result->ai_socktype, result->ai_protocol, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (_socket == INVALID_SOCKET) 
	{
		int errCode = WSAGetLastError();
		freeaddrinfo(result);
		throw SocketError(errCode);
	}

	int iResult = bind(_socket, result->ai_addr, (int)result->ai_addrlen);
	if (iResult == SOCKET_ERROR) 
	{
		int errCode = WSAGetLastError();
		freeaddrinfo(result);
		closesocket(_socket);
		throw SocketError(errCode);
	}

	freeaddrinfo(result);
	server_mode = true;
}

void IO::Networking::Sockets::Socket::Listen(int backlog)
{

	if (_socket == INVALID_SOCKET || client_mode)
	{
		throw std::logic_error("cannot Listen because socket state does not correct");
	}

	if (listen(_socket, backlog) == SOCKET_ERROR) 
	{
		int errCode = WSAGetLastError();
		closesocket(_socket);
		throw SocketError(errCode);
	}
	_io = CreateThreadpoolIo((HANDLE)_socket, AcceptCallback, NULL, NULL);
}

void IO::Networking::Sockets::Socket::Connect(std::string ip, uint32_t port)
{
	if (_socket != INVALID_SOCKET || server_mode)
	{
		throw std::logic_error("cannot connect because of socket state not correct");
	}

	addrinfo hints, *result;
	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = static_cast<int>(addressFamily);
	hints.ai_socktype = static_cast<int>(socketType);
	hints.ai_protocol = static_cast<int>(protocol);
	hints.ai_flags = AI_PASSIVE;
	int iResult = getaddrinfo(ip.c_str(), std::to_string(port).c_str(), &hints, &result);
	if (iResult != 0) 
	{
		throw SocketError(iResult);
	}
	_socket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
	if (_socket == INVALID_SOCKET)
	{
		int errCode = WSAGetLastError();
		freeaddrinfo(result);
		throw SocketError(errCode);
	}
	iResult = connect(_socket, result->ai_addr, (int)result->ai_addrlen);
	if (iResult == SOCKET_ERROR) 
	{
		int errCode = WSAGetLastError();
		closesocket(_socket);
		freeaddrinfo(result);
		throw SocketError(errCode);
	}

	client_mode = true;
	freeaddrinfo(result);
	_io = CreateThreadpoolIo((HANDLE)_socket, IoCallback, NULL, NULL);
}

Async::Awaiter<int> IO::Networking::Sockets::Socket::ReceiveAsync(std::byte * buffer, std::size_t size)
{
	if (_socket == INVALID_SOCKET)
	{
		throw std::logic_error("No connection");
	}
	auto state = new AsyncIoState();
	auto retFuture = state->completionSource.GetAwaiter();
	MyOverlapped* overlapped = new MyOverlapped;
	ZeroMemory(overlapped, sizeof(MyOverlapped));
	state->disconnectCallback = [=]() { this->~Socket(); };
	WSABUF buf;
	buf.len = size;
	buf.buf = reinterpret_cast<char*>(buffer);
	DWORD flags = MSG_WAITALL;
	overlapped->state = state;

	auto wsaOverlapped = static_cast<LPWSAOVERLAPPED>(overlapped);

	StartThreadpoolIo(_io);
	auto result = WSARecv(_socket, &buf, 1, NULL, &flags, wsaOverlapped, NULL);
	if (result == SOCKET_ERROR)
	{
		if (WSAGetLastError() != WSA_IO_PENDING)
		{
			state->completionSource.SetException(std::make_exception_ptr<SocketError>(WSAGetLastError()));
			delete state;
			delete overlapped;
			state = nullptr;
			overlapped = nullptr;
			return retFuture;
		}
	}
	return retFuture;
}


Async::Awaiter<int> IO::Networking::Sockets::Socket::SendAsync(std::byte* buffer, std::size_t size)
{
	if (_socket == INVALID_SOCKET)
	{
		throw std::logic_error("No connection");
	}
	auto state = new AsyncIoState();
	auto retFuture = state->completionSource.GetAwaiter();
	MyOverlapped* overlapped = new MyOverlapped;
	ZeroMemory(overlapped, sizeof(MyOverlapped));
	state->disconnectCallback = [=]() { this->~Socket(); };
	WSABUF buf;
	buf.len = size;
	buf.buf = reinterpret_cast<char*>(buffer);
	DWORD flags = 0;
	overlapped->state = state;

	auto wsaOverlapped = static_cast<LPWSAOVERLAPPED>(overlapped);
	StartThreadpoolIo(_io);
	auto result = WSASend(_socket, &buf, 1, NULL, flags, wsaOverlapped, NULL);
	if (result == SOCKET_ERROR)
	{
		if (WSAGetLastError() != WSA_IO_PENDING)
		{
			state->completionSource.SetException(std::make_exception_ptr<SocketError>(WSAGetLastError()));
			delete state;
			delete overlapped;
			state = nullptr;
			overlapped = nullptr;
			return retFuture;
		}
	}
	return retFuture;
}

Async::Awaiter<Socket> IO::Networking::Sockets::Socket::AcceptAsync()
{
	if (_socket == INVALID_SOCKET)
	{
		throw std::logic_error("No connection");
	}
	constexpr const int bufLen = (sizeof(sockaddr_in) + 16) * 2;
	char* buf = new char[bufLen];
	
	MyOverlapped* overlapped = new MyOverlapped;
	ZeroMemory(overlapped, sizeof(MyOverlapped));
	//SOCKET accept_socket = accept(_socket, NULL, NULL);
	SOCKET accept_socket = WSASocket(static_cast<int>(addressFamily), static_cast<int>(socketType), static_cast<int>(protocol), NULL, 0, WSA_FLAG_OVERLAPPED);
	if (accept_socket == INVALID_SOCKET)
	{
		delete[] buf;
		delete overlapped;
		throw SocketError(_T("Accept Failed"));
	}
	auto state = new AsyncAcceptState(Socket(accept_socket), buf);
	auto retFuture = state->completionSource.GetAwaiter();
	overlapped->state = state;
	LPOVERLAPPED baseOverlapped = static_cast<LPOVERLAPPED>(overlapped);
	auto acceptRet = AcceptEx(_socket, accept_socket, buf, 0, sizeof(sockaddr_in) + 16, sizeof(sockaddr_in) + 16, NULL, baseOverlapped);
	if (acceptRet == FALSE)
	{
		int errCode = WSAGetLastError();
		if (errCode != ERROR_IO_PENDING)
		{
			state->completionSource.SetException(std::make_exception_ptr<SocketError>(errCode));

			delete state;
			delete overlapped;
			delete[] buf;
			state = nullptr;
			overlapped = nullptr;
			buf = nullptr;
			return retFuture;
		}
	}
	StartThreadpoolIo(_io);
	return retFuture;
}

void Socket::Dispose()
{
	if (_socket != INVALID_SOCKET)
	{
		closesocket(_socket);
		_socket = INVALID_SOCKET;
		server_mode = false;
		client_mode = false;
	}
	if (_io != NULL)
	{
		CloseThreadpoolIo(_io);
		_io = NULL;
	}
}

Socket::~Socket()
{
	Dispose();
}