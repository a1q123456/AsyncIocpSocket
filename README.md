# Introduction
A socket class provides asynchronous IO operation and `co_await` support

# Compile Requirement
* VS 2017 15.6.1 or later
* Windows SDK 10.0.16299.0 or later

# Usage

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
co_await client.ConnectAsync("127.0.0.1", 1568);

std::byte buffer[2] = { std::byte(3), std::byte(4) };
co_await client.SendAsync(buffer);
co_await client.ReceiveAsync(buffer);
```

# Methods

## Socket.h

* Bind
* Listen
* AcceptAsync
* ConnectAsync
* ReceiveAsync
* SendAsync
* ReceiveLineAsync
* Dispose

## Await.h

* Then
* Wait
* WaitFor
* WaitUntil
* Get
* GetFor
* GetUntil
* WaitAll
* WaitForAll
* WaitUntilAll


### Bind

```c++
socket.Bind(std::string ipAddress, int port);
```

### Listen

```c++
socket.Listen(int backLog);
```

### AcceptAsync

```c++
Async::Awaiter<Socket> future = socket.AcceptAsync();
Socket client = co_await future;

// or

auto future = co_await socket.AcceptAsync();
```

### ConnectAsync

```c++
co_await socket.ConnectAsync();
```

### ReceiveAsync

```c++
std::byte* buf = new std::byte[100];
socket.ReceiveAsync(buf, 100);

// or

std::byte buf[100];
socket.ReceiveAsync(buf);
```

### SendAsync

```c++
std::byte* buf = new std::byte[100];
// TODO: fill the buffer
co_await socket.SendAsync(buf, 100);

// or

std::byte buf[100];
// TODO: fill the buffer
co_await socket.SendAsync(buf);
```

### Dispose

```c++
socket.Dispose();
```

### Then

```c++
awaiter.Then([]{
	std::cout << "done" << std::endl;
};
```

### Wait

```c++
awaiter.Wait();
```

### WaitFor
```c++
#include <chrono>

using namespace std::chrono_literals;
awaiter.WaitFor(10s);
```

### WaitUntil
```c++
#include <chrono>

using namespace std::chrono_literals;

awaiter.WaitUntil(std::chrono::steady_clock::now() + 10s);
```

