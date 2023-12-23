#pragma once
#include <Windows.h>

#include "Singleton.hpp"


template<typename T>
struct GlobalSystemTimer
{
	GlobalSystemTimer()
	{
		if (LARGE_INTEGER nFrequency;
			QueryPerformanceFrequency(&nFrequency))
		{
			TimerFrequency = nFrequency.QuadPart;
		}
		else
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("QueryPerformanceFrequency() error: %s"),
				ParseWindowsError(GetLastError()).c_str()));
		}

		if (!QueryPerformanceCounter(&StartTime))
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("QueryPerformanceCounter() error: %s"),
				ParseWindowsError(GetLastError()).c_str()));
		}
	}
	GlobalSystemTimer(const GlobalSystemTimer&) = delete;
	GlobalSystemTimer(GlobalSystemTimer&&) = delete;
	GlobalSystemTimer& operator=(const GlobalSystemTimer&) = delete;
	GlobalSystemTimer& operator=(GlobalSystemTimer&&) = delete;

	T GetGlobalSystemTime() const
	{
		if (TimerFrequency)
		{
			LARGE_INTEGER CurrentTime;
			if (QueryPerformanceCounter(&CurrentTime))
			{
				if constexpr (!std::is_floating_point_v<T>)
				{
					return static_cast<T>(CurrentTime.QuadPart) / (TimerFrequency / 1'000'000);   //Use microseconds
				}
				else
				{
					return static_cast<T>(CurrentTime.QuadPart) / TimerFrequency;
				}
			}
			else
			{
				LoggerTimeout(SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("QueryPerformanceCounter() error: %s"),
					ParseWindowsError(GetLastError()).c_str())), 100.f /*seconds*/);
			}
		}
		return {};
	}

	T GetOffsetedSystemTimeFromStart() const
	{
		if (TimerFrequency)
		{
			LARGE_INTEGER CurrentTime;
			if (QueryPerformanceCounter(&CurrentTime))
			{
				if constexpr (!std::is_floating_point_v<T>)
				{
					return static_cast<T>(CurrentTime.QuadPart - StartTime.QuadPart) / (TimerFrequency / 1'000'000);   //Use microseconds
				}
				else
				{
					return static_cast<T>(CurrentTime.QuadPart - StartTime.QuadPart) / TimerFrequency;
				}
			}
			else
			{
				LoggerTimeout(SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("QueryPerformanceCounter() error: %s"),
					ParseWindowsError(GetLastError()).c_str())), 100.f /*seconds*/);
			}
		}
		return {};
	}

private:
	decltype(LARGE_INTEGER::QuadPart) TimerFrequency = 0;
	LARGE_INTEGER StartTime = 0;
};