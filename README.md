# AsyncIocpSocket
A socket class provides asynchronous IO operation and co_await support

usage:

```c++

// Server

Socket socket(EAddressFamily::InternetworkV4, ESocketType::Stream, EProtocolType::Tcp);
socket.Bind("0.0.0.0", 1568);
socket.Listen(128);

auto acceptFuture = socket.AcceptAsync();
try
{
	std::byte buffer[2] = { std::byte(1), std::byte(2) };
	auto clientSocket = co_await acceptFuture;
	co_await clientSocket.SendAsync(buffer);
	co_await clientSocket.ReceiveAsync(buffer);
}
catch (const SocketError& /* e */)
{
	printf("server exception\n");
}

// Client

Socket client(EAddressFamily::InternetworkV4, ESocketType::Stream, EProtocolType::Tcp);
client.Connect("127.0.0.1", 1568);

std::byte buffer[2] = { std::byte(3), std::byte(4) };
co_await client.SendAsync(buffer);
co_await client.ReceiveAsync(buffer);
	
```
