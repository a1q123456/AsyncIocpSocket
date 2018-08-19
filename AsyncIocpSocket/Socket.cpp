#include "stdafx.h"
#include "Socket.h"
#include "SocketError.h"
#include <Mswsock.h>

using namespace Net::Sockets;

struct MyOverlapped : public WSAOVERLAPPED
{
	void* state;
};

struct AsyncIoState
{
	Async::Awaitable<int> completionSource;
	std::function<void()> disconnectCallback;
	bool isConnecting = false;
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
		state->disconnectCallback();
		state->completionSource.SetException(std::make_exception_ptr<SocketError>(IoResult));
	}
	else if (NumberOfBytesTransferred == 0 && !state->isConnecting)
	{
		state->disconnectCallback();
		state->completionSource.SetException(std::make_exception_ptr<SocketError>(WSAECONNRESET));
	}
	else
	{
		state->completionSource.SetResult(NumberOfBytesTransferred);
	}
	
	delete state;
	delete myOverlapped;
}

Net::Sockets::Socket::Socket(SOCKET socket) : _socket(socket), server_mode(false), client_mode(false)
{
	initializeWsa();
	_io = CreateThreadpoolIo((HANDLE)socket, IoCallback, this, NULL);
}

Net::Sockets::Socket::Socket(EAddressFamily addressFamily, ESocketType addressType, EProtocolType protocol) noexcept :
	addressFamily(addressFamily),
	socketType(addressType),
	protocol(protocol),
	_socket(INVALID_SOCKET),
	server_mode(false),
	client_mode(false)
{
	initializeWsa();
}

Socket& Net::Sockets::Socket::operator=(Socket&& another) noexcept
{
	std::lock_guard<std::mutex> lock(mutex);
	_dispose();
	disposed = false;
	client_mode = another.client_mode;
	server_mode = another.server_mode;
	addressFamily = another.addressFamily;
	socketType = another.socketType;
	protocol = another.protocol;
	_socket = another._socket;
	another._socket = INVALID_SOCKET;
	_io = another._io;
	another._io = nullptr;
	initializeWsa();
	return *this;
}

Net::Sockets::Socket::Socket(Socket&& another) noexcept
{
	operator=(std::move(another));
}

void Net::Sockets::Socket::Bind(std::string ip, uint32_t port)
{
	std::lock_guard<std::mutex> lock(mutex);
	if (disposed)
	{
		throw SocketError(_T("Already disposed"));
	}
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

void Net::Sockets::Socket::Listen(int backlog)
{
	std::lock_guard<std::mutex> lock(mutex);
	if (disposed)
	{
		throw SocketError(_T("Already disposed"));
	}
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

Async::Awaiter<int> Net::Sockets::Socket::ConnectAsync(std::string ip, uint32_t port)
{
	std::lock_guard<std::mutex> lock(mutex);
	if (disposed)
	{
		throw SocketError(_T("Already disposed"));
	}
	if (_socket != INVALID_SOCKET || server_mode || client_mode)
	{
		throw std::logic_error("cannot connect because of socket state not correct");
	}
	MyOverlapped* overlapped = new MyOverlapped;
	ZeroMemory(overlapped, sizeof(MyOverlapped));
	AsyncIoState* state = new AsyncIoState;
	overlapped->state = state;
	auto ret = state->completionSource.GetAwaiter();

	addrinfo hints, *result;
	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = static_cast<int>(addressFamily);
	hints.ai_socktype = static_cast<int>(socketType);
	hints.ai_protocol = static_cast<int>(protocol);
	hints.ai_flags = AI_PASSIVE;
	int iResult = getaddrinfo(ip.c_str(), std::to_string(port).c_str(), &hints, &result);
	if (iResult != 0) 
	{
		state->completionSource.SetException(std::make_exception_ptr<SocketError>(iResult));
		delete state;
		delete overlapped;
		return ret;
	}
	_socket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
	if (_socket == INVALID_SOCKET)
	{
		int errCode = WSAGetLastError();
		freeaddrinfo(result);
		delete state;
		delete overlapped;
		state->completionSource.SetException(std::make_exception_ptr<SocketError>(errCode));
		return ret;
	}
	sockaddr_in addr;
	ZeroMemory(&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = 0;

	iResult = bind(_socket, (SOCKADDR*)&addr, sizeof(addr));
	if (iResult == SOCKET_ERROR)
	{
		int errCode = WSAGetLastError();
		freeaddrinfo(result);
		closesocket(_socket);
		delete state;
		delete overlapped;
		state->completionSource.SetException(std::make_exception_ptr<SocketError>(errCode));
		return ret;
	}
	_io = CreateThreadpoolIo((HANDLE)_socket, IoCallback, NULL, NULL);
	GUID guid = WSAID_CONNECTEX;
	LPFN_CONNECTEX ConnectExPtr = NULL;
	DWORD numBytes = 0;
	if (WSAIoctl(_socket, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid), &ConnectExPtr, sizeof(ConnectExPtr), &numBytes, NULL, NULL) != 0)
	{
		freeaddrinfo(result);
		closesocket(_socket);
		int errCode = WSAGetLastError();
		delete state;
		delete overlapped;
		state->completionSource.SetException(std::make_exception_ptr<SocketError>(errCode));
		return ret;
	}

	state->disconnectCallback = [=] { Dispose(); };
	state->isConnecting = true;
	client_mode = true;

	StartThreadpoolIo(_io);
	if (!ConnectExPtr(_socket, result->ai_addr, (int)result->ai_addrlen, NULL, 0, NULL, overlapped))
	{
		int errCode = WSAGetLastError();
		if (errCode != WSA_IO_PENDING)
		{
			CancelThreadpoolIo(_io);
			closesocket(_socket);
			freeaddrinfo(result);
			delete state;
			delete overlapped;
			state->completionSource.SetException(std::make_exception_ptr<SocketError>(errCode));
			return ret;
		}
	}

	freeaddrinfo(result);
	return ret;
}

Async::Awaiter<int> Net::Sockets::Socket::ReceiveAsync(std::byte * buffer, std::size_t size)
{
	std::lock_guard<std::mutex> lock(mutex);
	if (disposed)
	{
		throw SocketError(_T("Already disposed"));
	}
	if (_socket == INVALID_SOCKET)
	{
		throw std::logic_error("No connection");
	}
	auto state = new AsyncIoState();
	auto retFuture = state->completionSource.GetAwaiter();
	MyOverlapped* overlapped = new MyOverlapped;
	ZeroMemory(overlapped, sizeof(MyOverlapped));
	state->disconnectCallback = [=]() { Dispose(); };
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
			CancelThreadpoolIo(_io);
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


Async::Awaiter<int> Net::Sockets::Socket::SendAsync(std::byte* buffer, std::size_t size)
{
	std::lock_guard<std::mutex> lock(mutex);
	if (disposed)
	{
		throw SocketError(_T("Already disposed"));
	}
	if (_socket == INVALID_SOCKET)
	{
		throw std::logic_error("No connection");
	}
	auto state = new AsyncIoState();
	auto retFuture = state->completionSource.GetAwaiter();
	MyOverlapped* overlapped = new MyOverlapped;
	ZeroMemory(overlapped, sizeof(MyOverlapped));
	state->disconnectCallback = [=]() { Dispose(); };
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
			CancelThreadpoolIo(_io);
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

Async::Awaiter<Socket> Net::Sockets::Socket::AcceptAsync()
{
	std::lock_guard<std::mutex> lock(mutex);
	if (disposed)
	{
		throw SocketError(_T("Already disposed"));
	}
	if (_socket == INVALID_SOCKET)
	{
		throw std::logic_error("No connection");
	}
	constexpr const int bufLen = (sizeof(sockaddr_in) + 16) * 2;
	char* buf = new char[bufLen];
	
	MyOverlapped* overlapped = new MyOverlapped;
	ZeroMemory(overlapped, sizeof(MyOverlapped));

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
	StartThreadpoolIo(_io);
	auto acceptRet = AcceptEx(_socket, accept_socket, buf, 0, sizeof(sockaddr_in) + 16, sizeof(sockaddr_in) + 16, NULL, baseOverlapped);
	if (acceptRet == FALSE)
	{
		int errCode = WSAGetLastError();
		if (errCode != ERROR_IO_PENDING)
		{
			CancelThreadpoolIo(_io);
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
	return retFuture;
}

bool Socket::IsConnected() const noexcept
{
	std::lock_guard<std::mutex> lock(mutex);
	return _socket != INVALID_SOCKET;
}

void Socket::Dispose()
{
	std::lock_guard<std::mutex> lock(mutex);
	_dispose();
}

void Socket::_dispose()
{
	if (_socket != INVALID_SOCKET)
	{
		closesocket(_socket);
		_socket = INVALID_SOCKET;
		server_mode = false;
		client_mode = false;
	}
	if (_io != nullptr)
	{
		CloseThreadpoolIo(_io);
		_io = nullptr;
	}
	disposed = true;
}

Socket::~Socket()
{
	Dispose();
}
