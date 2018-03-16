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

namespace IO::Networking::Sockets
{
	enum class ESocketLineBreak
	{
		CR,
		LF,
		CRLF
	};

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
		Socket(SOCKET socket);
	public:
		Socket(EAddressFamily addressFamily, ESocketType addressType, EProtocolType protocol) noexcept;
		Socket(const Socket&) = delete;
		Socket() noexcept {}
		Socket& operator=(const Socket&) = delete;
		Socket& operator=(Socket&&) noexcept;
		Socket(Socket&& another) noexcept;
		void Bind(std::string ip, uint32_t port);
		void Listen(int backlog);
		Async::Awaiter<int> ConnectAsync(std::string ip, uint32_t port);

		Async::Awaiter<int> ReceiveAsync(std::byte* buffer, std::size_t size);

		template<std::size_t size>
		Async::Awaiter<int> ReceiveAsync(std::byte (&buffer)[size])
		{
			return ReceiveAsync(buffer, size);
		}

		template<typename CharT>
		Async::Awaiter<int> ReceiveLineAsync(std::basic_string<CharT>& line, ESocketLineBreak br = ESocketLineBreak::CRLF)
		{
			static_assert(std::is_integral_v<CharT>, "CharT must be an integral type");
			constexpr const char CR = '\r';
			constexpr const char LF = '\n';
			std::byte buffer[sizeof(CharT)];
			while (true)
			{
				co_await ReceiveAsync(buffer);
				CharT character;
				memcpy_s(&character, sizeof(CharT), buffer, sizeof(CharT));
				line += character;
				
				if (br == ESocketLineBreak::CRLF && buffer[0] == CR)
				{
					std::byte buffer2[sizeof(CharT)];
					co_await ReceiveAsync(buffer2);

					CharT character2;
					memcpy_s(&character2, sizeof(CharT), buffer2, sizeof(CharT));
					line += character2;

					if (character2 == LF)
					{
						co_return 1;
					}
				}
				else if (character == CR && ESocketLineBreak::CR == br)
				{
					co_return 1;
				}
				else if (character == LF && ESocketLineBreak::LF == br)
				{
					co_return 1;
				}
				else
				{
					throw std::logic_error("value of parameter br is out of range");
				}
			}
		}

		Async::Awaiter<int> SendAsync(std::byte* buffer, std::size_t size);

		template<std::size_t size>
		Async::Awaiter<int> SendAsync(std::byte(&buffer)[size])
		{
			return SendAsync(buffer, size);
		}
		
		Async::Awaiter<Socket> AcceptAsync();

		virtual ~Socket() noexcept;
		void Dispose();
	};
}
