#pragma once
#include <future>
#include <experimental\resumable>
#include <mutex>
#include <thread>
#include <chrono>
#include <functional>
#include <vector>
#include <Windows.h>


namespace Async
{
	class AwaitableStateError : public std::exception
	{};
	class AwaitableTimeoutError : public std::exception
	{};

	template <typename T>
	class AwaitableState
	{
	public:
		T _result;
		bool _hasResult = false;
		bool _hasException = false;
		bool _isReady = false;
		std::mutex mutex;
		std::exception_ptr _exception;
		std::condition_variable cond;
	public:
		std::vector<std::function<void()>> callback;
		void SetResult(const T& v)
		{
			std::unique_lock<std::mutex> lock(mutex);
			if (_isReady)
			{
				throw AwaitableStateError();
			}
			_result = v;
			_isReady = true;
			_hasResult = true;
			cond.notify_all();

			lock.unlock();
			PTP_WORK work = CreateThreadpoolWork([](PTP_CALLBACK_INSTANCE Instance, PVOID Context, PTP_WORK Work)
			{
				AwaitableState<T>* self = static_cast<AwaitableState<T>*>(Context);
				if (self->_isReady != 0 && self->_isReady != 1)
				{
					DebugBreak();
				}
				for (const auto& fn : self->callback)
				{
					fn();
				}

			}, this, NULL);
			SubmitThreadpoolWork(work);
		}

		void SetResult(T&& v)
		{
			std::unique_lock<std::mutex> lock(mutex);
			if (_isReady)
			{
				throw AwaitableStateError();
			}
			_result = std::move(v);
			_isReady = true;
			_hasResult = true;
			cond.notify_all();

			lock.unlock();
			PTP_WORK work = CreateThreadpoolWork([](PTP_CALLBACK_INSTANCE Instance, PVOID Context, PTP_WORK Work)
			{
				AwaitableState<T>* self = static_cast<AwaitableState<T>*>(Context);
				for (const auto& fn : self->callback)
				{
					fn();
				}
			}, this, NULL);
			SubmitThreadpoolWork(work);
		}

		void SetException(const std::exception_ptr& exp)
		{
			std::unique_lock<std::mutex> lock(mutex);
			if (_isReady)
			{
				throw AwaitableStateError();
			}
			_exception = exp;
			_isReady = true;
			_hasException = true;

			lock.unlock();
			cond.notify_all();
			PTP_WORK work = CreateThreadpoolWork([](PTP_CALLBACK_INSTANCE Instance, PVOID Context, PTP_WORK Work)
			{
				AwaitableState<T>* self = static_cast<AwaitableState<T>*>(Context);
				for (const auto& fn : self->callback)
				{
					fn();
				}
			}, this, NULL);
			SubmitThreadpoolWork(work);
		}

		bool IsReady()
		{
			std::unique_lock<std::mutex> lock(mutex);
			return _isReady;
		}

		bool HasResult()
		{
			std::unique_lock<std::mutex> lock(mutex);
			return _hasResult;
		}

		bool HasException()
		{
			std::unique_lock<std::mutex> lock(mutex);
			return _hasException;
		}

		T&& Get()
		{
			while (!_isReady)
			{
				std::this_thread::yield();
			}
			if (_hasException)
			{
				std::rethrow_exception(_exception);
			}
			return std::move(_result);
		}

		template <typename _Clock, typename _Dur>
		T&& GetUntil(const std::chrono::time_point<_Clock, _Dur>& time)
		{
			std::unique_lock<std::mutex> lock(mutex);
			if (cond.wait_until(lock, time, [this]() { return this->_isReady; }))
			{
				return std::move(Get());
			}
			throw AwaitableTimeoutError();
		}

		template <typename _Rep, typename _Per>
		T&& GetFor(const std::chrono::duration<_Rep, _Per>& time)
		{
			std::unique_lock<std::mutex> lock(mutex);
			if (cond.wait_for(lock, time, [this]() { return this->_isReady; }))
			{
				return std::move(Get());
			}

			throw AwaitableTimeoutError();
		}

		void Wait()
		{
			while (!_isReady)
			{
				std::this_thread::yield();
			}
		}

		template <typename _Clock, typename _Dur>
		bool WaitUntil(const std::chrono::time_point<_Clock, _Dur>& time)
		{
			std::unique_lock<std::mutex> lock(mutex);
			return cond.wait_until(lock, time, [this]() { return this->_isReady; });
		}

		template <typename _Rep, typename _Per>
		bool WaitFor(const std::chrono::duration<_Rep, _Per>& time)
		{
			std::unique_lock<std::mutex> lock(mutex);
			return cond.wait_for(lock, time, [this]() { return this->_isReady; });
		}

		void AddCallback(const std::function<void()>& cb)
		{
			std::unique_lock<std::mutex> lock(mutex);
			if (!_isReady)
			{
				callback.emplace_back(cb);
			}
			else
			{
				PTP_WORK work = CreateThreadpoolWork([](PTP_CALLBACK_INSTANCE Instance, PVOID Context, PTP_WORK Work)
				{
					AwaitableState<T>* self = static_cast<AwaitableState<T>*>(Context);
					for (const auto& fn : self->callback)
					{
						fn();
					}
				}, this, NULL);
				SubmitThreadpoolWork(work);
			}
		}

		void AddCallback(std::function<void()>&& cb)
		{
			std::unique_lock<std::mutex> lock(mutex);
			if (!_isReady)
			{
				callback.emplace_back(std::move(cb));
			}
			else
			{
				lock.unlock();
				cb();
			}
		}

		~AwaitableState() noexcept
		{
		}
	};

	template <>
	class AwaitableState<void>
	{
		bool _hasResult = false;
		bool _hasException = false;
		bool _isReady = false;
		std::exception_ptr _exception;
		std::condition_variable cond;
	public:
		std::mutex mutex;
		std::vector<std::function<void()>> callback;
		void SetResult()
		{
			std::unique_lock<std::mutex> lock(mutex);
			if (_isReady)
			{
				throw AwaitableStateError();
			}
			_isReady = true;
			_hasResult = true;
			cond.notify_all();

			PTP_WORK work = CreateThreadpoolWork([](PTP_CALLBACK_INSTANCE Instance, PVOID Context, PTP_WORK Work)
			{
				AwaitableState<void>* self = static_cast<AwaitableState<void>*>(Context);
				for (const auto& fn : self->callback)
				{
					fn();
				}
			}, this, NULL);
			SubmitThreadpoolWork(work);
		}
		
		void SetException(const std::exception_ptr& exp)
		{
			std::unique_lock<std::mutex> lock(mutex);
			if (_isReady)
			{
				throw AwaitableStateError();
			}
			_exception = exp;
			_isReady = true;
			_hasException = true;
			cond.notify_all();

			PTP_WORK work = CreateThreadpoolWork([](PTP_CALLBACK_INSTANCE Instance, PVOID Context, PTP_WORK Work)
			{
				AwaitableState<void>* self = static_cast<AwaitableState<void>*>(Context);
				for (const auto& fn : self->callback)
				{
					fn();
				}
			}, this, NULL);
			SubmitThreadpoolWork(work);
		}

		bool IsReady()
		{
			std::unique_lock<std::mutex> lock(mutex);
			return _isReady;
		}

		bool HasResult()
		{
			std::unique_lock<std::mutex> lock(mutex);
			return _hasResult;
		}

		bool HasException()
		{
			std::unique_lock<std::mutex> lock(mutex);
			return _hasException;
		}

		void Get()
		{
			while (!_isReady)
			{
				std::this_thread::yield();
			}
			if (_hasException)
			{
				std::rethrow_exception(_exception);
			}
			return;
		}

		template <typename _Clock, typename _Dur>
		void GetUntil(const std::chrono::time_point<_Clock, _Dur>& time)
		{
			std::unique_lock<std::mutex> lock(mutex);
			if (cond.wait_until(lock, time, [this]() { return this->_isReady; }))
			{
				Get();
				return;
			}
			throw AwaitableTimeoutError();
		}

		template <typename _Rep, typename _Per>
		void GetFor(const std::chrono::duration<_Rep, _Per>& time)
		{
			std::unique_lock<std::mutex> lock(mutex);
			if (cond.wait_for(lock, time, [this]() { return this->_isReady; }))
			{
				Get();
				return;
			}
			throw AwaitableTimeoutError();
		}

		void Wait()
		{
			while (!_isReady)
			{
				std::this_thread::yield();
			}
		}

		template <typename _Clock, typename _Dur>
		bool WaitUntil(const std::chrono::time_point<_Clock, _Dur>& time)
		{
			std::unique_lock<std::mutex> lock(mutex);
			return cond.wait_until(lock, time, [this]() { return this->_isReady; });
		}

		template <typename _Rep, typename _Per>
		bool WaitFor(const std::chrono::duration<_Rep, _Per>& time)
		{
			std::unique_lock<std::mutex> lock(mutex);
			return cond.wait_for(lock, time, [this]() { return this->_isReady; });
		}

		void AddCallback(const std::function<void()>& cb)
		{
			std::unique_lock<std::mutex> lock(mutex);
			if (!_isReady)
			{
				callback.emplace_back(cb);
			}
			else
			{
				lock.unlock();
				cb();
				
			}
		}

		void AddCallback(std::function<void()>&& cb)
		{
			std::unique_lock<std::mutex> lock(mutex);
			if (!_isReady)
			{
				callback.emplace_back(std::move(cb));
			}
			else
			{
				lock.unlock();
				cb();
			}
		}

		~AwaitableState()
		{
		}
	};

	template <typename T>
	class Awaiter
	{
		using AwaitableStateType = std::shared_ptr<AwaitableState<T>>;
		AwaitableStateType state;
	public:
		Awaiter(const Awaiter&) = delete;

		Awaiter(Awaiter&& other)
		{
			operator=(std::move(other));
		}

		Awaiter& operator=(Awaiter&& other)
		{
			state = std::move(other.state);
			return *this;
		}

		Awaiter(AwaitableStateType state) : state(state) {}
		bool await_ready()
		{
			return state->IsReady();
		}

		void await_suspend(std::experimental::coroutine_handle<> resumeCb)
		{
			state->AddCallback([=]() {
				resumeCb();
			});
		}

		T&& await_resume()
		{
			return std::move(state->Get());
		}

		virtual ~Awaiter()
		{

		}

		void Then(std::function<void()> cb)
		{
			state->AddCallback([=]() {
				cb();
			});
		}

		T&& Get()
		{
			return std::move(state->Get());
		}

		template <typename _Clock, typename _Dur>
		T&& GetUntil(const std::chrono::time_point<_Clock, _Dur>& time)
		{
			return std::move(state->GetUntil(time));
		}

		template <typename _Rep, typename _Per>
		T&& GetFor(const std::chrono::duration<_Rep, _Per>& time)
		{
			return std::move(state->GetFor(time));
		}

		void Wait()
		{
			state->Wait();
			return;
		}

		template <typename _Clock, typename _Dur>
		bool WaitUntil(const std::chrono::time_point<_Clock, _Dur>& time)
		{
			return state->WaitUntil(time);
		}

		template <typename _Rep, typename _Per>
		bool WaitFor(const std::chrono::duration<_Rep, _Per>& time)
		{
			return state->WaitFor(time);
		}

		template <typename ...AwaiterT>
		static void WaitAll(AwaiterT&& ...aw)
		{
			(aw.Wait(), ...);
		}

		template <typename ...AwaiterT, typename _Rep, typename _Per>
		static bool WaitForAll(const std::chrono::duration<_Rep, _Per>& time, AwaiterT&& ...aw)
		{
			return (aw.WaitFor(time) && ...);
		}

		template <typename ...AwaiterT, typename _Clock, typename _Dur>
		static bool WaitUntilAll(const std::chrono::time_point<_Clock, _Dur>& time, AwaiterT&& ...aw)
		{
			return (aw.WaitUntil(time) && ...);
		}
	};

	template <>
	class Awaiter<void>
	{
		using AwaitableStateType = std::shared_ptr<AwaitableState<void>>;
		AwaitableStateType state;
	public:
		Awaiter(const Awaiter&) = delete;

		Awaiter(Awaiter&& other)
		{
			operator=(std::move(other));
		}

		Awaiter& operator=(Awaiter&& other)
		{
			state = std::move(other.state);
		}

		Awaiter(AwaitableStateType state) : state(state) {}
		bool await_ready()
		{
			return state->IsReady();
		}

		void await_suspend(std::experimental::coroutine_handle<> resumeCb)
		{
			state->AddCallback([=]() {
				resumeCb();
			});
		}

		void await_resume()
		{
			state->Get();
			return;
		}

		virtual ~Awaiter()
		{

		}

		void Get()
		{
			state->Get();
			return;
		}

		void Then(std::function<void()> cb)
		{
			state->AddCallback([=]() {
				cb();
			});
		}

		template <typename _Clock, typename _Dur>
		void GetUntil(const std::chrono::time_point<_Clock, _Dur>& time)
		{
			state->GetUntil(time);
			return;
		}

		template <typename _Rep, typename _Per>
		void GetFor(const std::chrono::duration<_Rep, _Per>& time)
		{
			state->GetFor(time);
			return;
		}

		void Wait()
		{
			state->Wait();
			return;
		}

		template <typename _Clock, typename _Dur>
		bool WaitUntil(const std::chrono::time_point<_Clock, _Dur>& time)
		{
			return state->WaitUntil(time);
		}

		template <typename _Rep, typename _Per>
		bool WaitFor(const std::chrono::duration<_Rep, _Per>& time)
		{
			return state->WaitFor(time);
		}

		template <typename ...AwaiterT>
		static void WaitAll(AwaiterT&& ...aw)
		{
			(aw.Wait(), ...);
		}

		template <typename ...AwaiterT, typename _Rep, typename _Per>
		static bool WaitForAll(const std::chrono::duration<_Rep, _Per>& time, AwaiterT&& ...aw)
		{
			return (aw.WaitFor(time) && ...);
		}

		template <typename ...AwaiterT, typename _Clock, typename _Dur>
		static bool WaitUntilAll(const std::chrono::time_point<_Clock, _Dur>& time, AwaiterT&& ...aw)
		{
			return (aw.WaitUntil(time) && ...);
		}

	};

	template <typename T>
	class Awaitable
	{
		using AwaitableStateType = std::shared_ptr<AwaitableState<T>>;
		AwaitableStateType state = nullptr;
	public:
		Awaitable() : state(std::make_shared<AwaitableState<T>>())
		{
		}

		Awaitable(const Awaitable&) = delete;

		template<class _Alloc>
		Awaitable(std::allocator_arg_t, const _Alloc& _Al) : state(std::allocate_shared<AwaitableState<T>, _Alloc>(_Al))
		{

		}
		
		Awaitable(Awaitable&& _Other) noexcept
		{
			operator=(std::move(_Other));
		}

		Awaitable& operator=(Awaitable&& _Other) noexcept
		{
			state = std::move(_Other.state);
			return *this;
		}

		Awaitable& operator=(const Awaitable&) = delete;

		void SetResult(const T& res)
		{
			state->SetResult(res);
		}

		void SetResult(T&& res)
		{
			state->SetResult(std::move(res));
		}

		void SetException(std::exception_ptr exp)
		{
			state->SetException(exp);
		}

		Awaiter<T> GetAwaiter()
		{
			return Awaiter<T>(state);
		}

		virtual ~Awaitable()
		{

		}
	};

	template <>
	class Awaitable<void>
	{
		std::shared_ptr<AwaitableState<void>> state = nullptr;;
	public:
		Awaitable() : state(std::make_shared<AwaitableState<void>>())
		{
		}

		Awaitable(const Awaitable&) = delete;

		template<class _Alloc>
		Awaitable(std::allocator_arg_t, const _Alloc& _Al) : state(std::allocate_shared<AwaitableState<void>, _Alloc>(_Al))
		{

		}

		Awaitable(Awaitable&& _Other) noexcept
		{
			operator=(std::move(_Other));
		}

		Awaitable& operator=(Awaitable&& _Other) noexcept
		{
			state = std::move(_Other.state);
			return *this;
		}

		Awaitable& operator=(const Awaitable&) = delete;


		void SetResult()
		{
			state->SetResult();
		}

		void SetException(std::exception_ptr exp)
		{
			state->SetException(exp);
		}

		Awaiter<void> GetAwaiter()
		{
			return Awaiter<void>(state);
		}

		virtual ~Awaitable()
		{

		}
	};
}


namespace std::experimental {
	template<class _Ty, class... _ArgTypes>
	struct coroutine_traits<Async::Awaiter<_Ty>, _ArgTypes...>
	{	// defines resumable traits for functions returning future<_Ty>
		struct promise_type
		{
			Async::Awaitable<_Ty> _MyPromise;

			Async::Awaiter<_Ty> get_return_object()
			{
				return (_MyPromise.GetAwaiter());
			}

			bool initial_suspend() const
			{
				return (false);
			}

			bool final_suspend() const
			{
				return (false);
			}

			template<class _Ut>
			void return_value(_Ut&& _Value)
			{
				_MyPromise.SetResult(_STD forward<_Ut>(_Value));
			}

			void set_exception(exception_ptr _Exc)
			{
				_MyPromise.SetException(_STD move(_Exc));
			}
		};
	};

	template<class... _ArgTypes>
	struct coroutine_traits<Async::Awaiter<void>, _ArgTypes...>
	{	// defines resumable traits for functions returning future<void>
		struct promise_type
		{
			Async::Awaitable<void> _MyPromise;

			Async::Awaiter<void> get_return_object()
			{
				return (_MyPromise.GetAwaiter());
			}

			bool initial_suspend() const
			{
				return (false);
			}

			bool final_suspend() const
			{
				return (false);
			}

			void return_void()
			{
				_MyPromise.SetResult();
			}

			void set_exception(std::exception_ptr _Exc)
			{
				_MyPromise.SetException(_STD move(_Exc));
			}
		};
	};
}