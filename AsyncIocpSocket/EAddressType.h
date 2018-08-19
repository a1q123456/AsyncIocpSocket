#pragma once

namespace Net::Sockets
{
	enum class ESocketType: int
	{
		Stream = 1,
		Datagram = 2,
		Raw = 3,
		ReliablyDelivered = 4,
		SequencedPacket = 5
	};
}
