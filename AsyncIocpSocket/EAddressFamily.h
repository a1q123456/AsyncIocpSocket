#pragma once

namespace IO::Networking::Sockets
{
	enum class EAddressFamily: int
	{
		Unspecified = 0,
		LocalToHost = 1,
		InternetworkV4 = 2,
		ArpanetImpAddresses = 3,
		Pup = 4,
		MitChaos = 5,
		XeroxNSOrIPX = 6,
		IsoOrOsi = 7,
		Ecma = 8,
		Datakit = 9,
		CCITT = 10,
		Sna = 11,
		DECnet = 12,
		DirectDataLinkInterface = 13,
		Lat = 14,
		HyperLink = 15,
		AppleTalk = 16,
		NetBios = 17,
		VoiceView = 18,
		FireFox = 19,
		Unknow1 = 20,
		Banyan = 21,
		Atm = 22,
		InternetworkV6 = 23,
		Wolfpack = 24,
		IEEE1284 = 25,
		IrDA = 26,
		NetworkDesigners = 28
	};


}
