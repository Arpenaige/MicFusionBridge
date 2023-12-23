#pragma once
#include <string>
#include <format>
#include <any>
#include <algorithm>

#include <Windows.h>

#include "../Common/WinReg.hpp"

#include "Singleton.hpp"

//#include <wil/resource.h>

std::string WideStringToUTF8(const wchar_t* Buffer);

std::wstring ParseWindowsError(/*const char* lpszFunction, */DWORD WinError, HMODULE hModule = 0);

std::wstring ParseWaitForSingleObjectValue(DWORD WaitValue);

#define LogingBooleanWindowsError(value) \
	if (!value) { const DWORD ErrorCode = GetLastError(); \
SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("%s, Error parsed: %s"), #value, ParseWindowsError(ErrorCode).c_str())); }


template<typename T>
DWORD GetRegFlagFromVar()
{
	if constexpr (std::is_same_v<T, DWORD>)
	{
		return REG_DWORD;
	}
	else if constexpr (std::is_same_v<T, std::wstring>)
	{
		return REG_SZ;
	}
	else if constexpr (std::is_same_v<T, std::vector<std::wstring>>)
	{
		return REG_MULTI_SZ;
	}
	else
	{
		static_assert(false, "Unsupported format");
	}
}


template<typename T>
LSTATUS/*WinErrorCapsule<LSTATUS> */GetValueFunction(const winreg::RegKey& KeyHandle, const wchar_t* KeyVal, T& ReturnedValue)      //TODO: rename to RegGetValue
{
	ReturnedValue = {};

	const auto RegQuery = KeyHandle.TryQueryValueType(KeyVal);
	if (RegQuery.IsValid())
	{
		if (RegQuery.GetValue() != GetRegFlagFromVar<T>())
		{
			return ERROR_UNSUPPORTED_TYPE;
		}

		std::any AnyValue;
		if constexpr (std::is_same_v<T, DWORD>)
		{
			AnyValue = KeyHandle.TryGetDwordValue(KeyVal);
		}
		else if constexpr (std::is_same_v<T, std::wstring>)
		{
			AnyValue = KeyHandle.TryGetStringValue(KeyVal);
		}
		else if constexpr (std::is_same_v<T, std::vector<std::wstring>>)
		{
			AnyValue = KeyHandle.TryGetMultiStringValue(KeyVal);
		}
		else
		{
			static_assert(false, "Unsupported format");
		}

		auto RegValue = std::any_cast<winreg::RegExpected<T>>(AnyValue);
		if (RegValue.IsValid())
		{
			ReturnedValue = RegValue.GetValue();
			if constexpr (std::is_same_v<T, std::vector<std::wstring>>)
			{
				//Erase empty strings before set value, because ParseMultiString(in the library) can adding empty string
				ReturnedValue.erase(std::remove_if(ReturnedValue.begin(), ReturnedValue.end(),
					[](const std::wstring& str)
					{
						return str.empty();
					}), ReturnedValue.end());
			}
			return ERROR_SUCCESS;
		}
		else
		{
			return RegValue.GetError().Code();
		}
	}
	else
	{
		return RegQuery.GetError().Code();
	}

	return ERROR_INVALID_FUNCTION;   //It shouldn't get here, because of static_assert.
}



template<typename T>
LSTATUS/*WinErrorCapsule<LSTATUS> */SetValueFunction(winreg::RegKey& KeyHandle, const wchar_t* KeyVal, const T& ValueToSet)      //TODO: rename to RegSetValue
{
	const auto RegQuery = KeyHandle.TryQueryValueType(KeyVal);
	if (RegQuery.IsValid())
	{
		if (RegQuery.GetValue() != GetRegFlagFromVar<T>())
		{
			return ERROR_UNSUPPORTED_TYPE;
		}
	}

	if constexpr (std::is_same_v<T, DWORD>)
	{
		return KeyHandle.TrySetDwordValue(KeyVal, ValueToSet).Code();
	}
	else if constexpr (std::is_same_v<T, std::wstring>)
	{
		return KeyHandle.TrySetStringValue(KeyVal, ValueToSet).Code();
	}
	else if constexpr (std::is_same_v<T, std::vector<std::wstring>>)
	{
		std::vector<std::wstring> ValueToSetCopy = ValueToSet;
		//Erase empty strings before set value, because BuildMultiString(in the library) add L'\0' at the end
		ValueToSetCopy.erase(std::remove_if(ValueToSetCopy.begin(), ValueToSetCopy.end(),
			[](const std::wstring& str)
			{
				return str.empty();
			}), ValueToSetCopy.end());
		return KeyHandle.TrySetMultiStringValue(KeyVal, ValueToSetCopy).Code();
	}
	else
	{
		static_assert(false, "Unsupported format");
	}

	return ERROR_INVALID_FUNCTION;   //It shouldn't get here, because of static_assert.
}



struct WinVer
{
	DWORD dwBuildNumber;
	DWORD dwMajorVersion;
	DWORD dwMinorVersion;
};


WinVer GetWindowsVersion();

std::wstring GetTypeOfRegeditValue(DWORD Type);

bool IsRunAsAdministator();

bool GUIDIsEqual(CLSID GUID1, std::wstring GUID2s);

//std::wstring GetCurrentModuleName();

std::wstring GetModuleName(PVOID ImageBase);