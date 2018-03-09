#pragma once

namespace IO::Networking::Sockets
{
	enum class EProtocolType: int
	{
		IP = 0,
		Icmp = 1,
		Igmp = 2,
		Ggp = 3,
		Tcp = 6,
		Pup = 12,
		Udp = 17,
		Idp = 22,
		UnofficialNetDisk = 77,
		Raw = 255
	};
}