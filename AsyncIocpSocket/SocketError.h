#pragma once
#include <string>
#include <exception>

namespace Net::Sockets
{
	template<typename CharT>
	class BasicSocketError : public std::exception
	{
		std::basic_string<CharT> data;
	public:
		BasicSocketError(int errCode)
		{
			CharT buffer[256];
			if constexpr (std::is_same_v<CharT, TCHAR>)
			{
				FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, errCode, 0, buffer, sizeof(buffer), NULL);
			}
			else if constexpr (std::is_same_v<CharT, char>)
			{
				FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, errCode, 0, buffer, sizeof(buffer), NULL);
			}
			else if constexpr (std::is_same_v<CharT, char16_t>)
			{
				FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, errCode, 0, buffer, sizeof(buffer), NULL);
			}
			else
			{
				static_assert(false, "CharT not support");
			}
			data = buffer;
		}
		BasicSocketError(const CharT* msg): data(msg)
		{
			
		}

		BasicSocketError(const std::basic_string<CharT> msg) : data(msg)
		{

		}

		std::basic_string<CharT> Message() const
		{
			return data;
		}
		const char* what() const override
		{
			return "Use BasicSocketError<T>::Message instead of what()";
		}
	};

	using SocketError = BasicSocketError<TCHAR>;
}