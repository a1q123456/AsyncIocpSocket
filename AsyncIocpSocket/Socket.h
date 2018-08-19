#pragma once

#include "EAddressFamily.h"
#include "EAddressType.h"
#include "EProtocolType.h"
#include "SocketError.h"
#include <experimental\coroutine>
#include <future>
#include <string>
#include <type_traits>
#include "Await.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "Mswsock.lib")

namespace Net::Sockets
{
	class Socket
	{
	private:
		static constexpr uint32_t portMax = 65535;
		static constexpr uint32_t portMin = 0;
		EAddressFamily addressFamily;
		ESocketType socketType;
		EProtocolType protocol;
		SOCKET _socket = INVALID_SOCKET;
		PTP_IO _io = nullptr;
		bool server_mode;
		bool client_mode;
		bool disposed = false;
		mutable std::mutex mutex;

		Socket(SOCKET socket);
		void _dispose();
	public:
		Socket(EAddressFamily addressFamily, ESocketType addressType, EProtocolType protocol) noexcept;
		Socket(const Socket&) = delete;
		Socket() noexcept {}
		Socket& operator=(const Socket&) = delete;
		Socket& operator=(Socket&&) noexcept;
		Socket(Socket&& another) noexcept;
		void Bind(std::string ip, uint32_t port);
		void Listen(int backlog);
		bool IsConnected() const noexcept;
		Async::Awaiter<Socket> AcceptAsync();
		Async::Awaiter<int> ConnectAsync(std::string ip, uint32_t port);
		Async::Awaiter<int> ReceiveAsync(std::byte* buffer, std::size_t size);

		template<std::size_t size>
		Async::Awaiter<int> ReceiveAsync(std::byte (&buffer)[size])
		{
			return ReceiveAsync(buffer, size);
		}
		Async::Awaiter<int> SendAsync(std::byte* buffer, std::size_t size);

		template<std::size_t size>
		Async::Awaiter<int> SendAsync(std::byte(&buffer)[size])
		{
			return SendAsync(buffer, size);
		}

		virtual ~Socket() noexcept;
		
		void Dispose();
	};
}
