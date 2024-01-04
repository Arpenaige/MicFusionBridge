#include "WindowsUtil.h"


std::string WideStringToUTF8(const wchar_t* Buffer)
{
	const int UTF8Len = WideCharToMultiByte(CP_UTF8, 0, Buffer, -1, nullptr, 0, nullptr, nullptr);
	std::string TranslatedString(UTF8Len, '\0');
	WideCharToMultiByte(CP_UTF8, 0, Buffer, -1, TranslatedString.data(), UTF8Len, nullptr, nullptr);

	return TranslatedString;
}

std::wstring ParseWindowsError(DWORD WinError, HMODULE hModule)
{
	wil::unique_hlocal lpMsgBuf;
	const DWORD StringLen = FormatMessageW(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		(hModule == 0 ? FORMAT_MESSAGE_FROM_SYSTEM : FORMAT_MESSAGE_FROM_HMODULE) |
		FORMAT_MESSAGE_IGNORE_INSERTS,
		(hModule == 0 ? nullptr : hModule),
		WinError,
		/*MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT)*/MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),   //Use only English for logging purpose
		reinterpret_cast<const LPWSTR>(&lpMsgBuf),
		0, nullptr);

	if (StringLen > 0)
	{
		std::wstring result = std::format(L"[Code: [{}] || {}]", WinError, static_cast<const LPWSTR>(lpMsgBuf.get()));
		std::replace(result.begin(), result.end(), L'\n', L' '); std::replace(result.begin(), result.end(), L'\r', L' ');
		return result;
	}
	else
	{
		return std::format(L"FormatMessageW({}) error code: {}", WinError, GetLastError());
	}
}

std::wstring ParseWaitForSingleObjectValue(DWORD WaitValue)
{
	switch (WaitValue)
	{
	case WAIT_ABANDONED:
		return L"WAIT_ABANDONED";

	case WAIT_OBJECT_0:
		return L"WAIT_OBJECT_0";

	case WAIT_TIMEOUT:
		return L"WAIT_TIMEOUT";

	case WAIT_FAILED:
		return std::format(L"WAIT_FAILED GetLastError() parsed: {}", ParseWindowsError(GetLastError()));

	default:
		return std::format(L"Unknown code: 0x{:x}", WaitValue);
	}
}

//TODO: функция не тестировалась в полной мере, протестировать(в том числе отдельно реестр)
WinVer GetWindowsVersion()
{
	WinVer ver{};

	typedef NTSTATUS(NTAPI* RtlGetVersionDef)(PRTL_OSVERSIONINFOEXW lpVersionInformation);

	const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
	if (ntdll)
	{
		const RtlGetVersionDef RtlGetVersion = reinterpret_cast<RtlGetVersionDef>(GetProcAddress(ntdll, "RtlGetVersion"));
		if (RtlGetVersion)
		{
			RTL_OSVERSIONINFOEXW WindowsVersion;

			if (!RtlGetVersion(&WindowsVersion))
			{
				ver.dwBuildNumber = WindowsVersion.dwBuildNumber;
				ver.dwMajorVersion = WindowsVersion.dwMajorVersion;
				ver.dwMinorVersion = WindowsVersion.dwMinorVersion;

				return ver;
			}
			else
			{
				SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_ERROR(0, TM("RtlGetVersion error: %s"),
					ParseWindowsError(GetLastError()).c_str()));
			}
		}
		else
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_ERROR(0, TM("GetProcAddress(RtlGetVersion) error: %s"),
				ParseWindowsError(GetLastError()).c_str()));
		}
	}
	else
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_ERROR(0, TM("GetModuleHandleW(ntdll.dll) error: %s"),
			ParseWindowsError(GetLastError()).c_str()));
	}

	SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("Try another getting Windows version")));

	//Если не удалось по какой то причине получить версию с помощью RtlGetVersion, то попробуем с помощью реестра
	winreg::RegKey KeyHandle;
	if (auto KeyHandleReturnedValue = KeyHandle.TryOpen(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", KEY_QUERY_VALUE | GENERIC_READ);  //KEY_WOW64_64KEY
		!KeyHandleReturnedValue.Failed())
	{
		DWORD CurrentMajorVersionNumber;
		DWORD CurrentMinorVersionNumber;
		std::wstring CurrentBuildNumber;


		if (const auto Code = GetValueFunction(KeyHandle, L"CurrentMajorVersionNumber", CurrentMajorVersionNumber);
			Code == ERROR_SUCCESS)
		{
			ver.dwMajorVersion = CurrentMajorVersionNumber;
		}
		else
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_ERROR(0, TM("GetValueFunction(CurrentMajorVersionNumber) error: %s"),
				ParseWindowsError(Code).c_str()));
		}


		if (const auto Code = GetValueFunction(KeyHandle, L"CurrentMinorVersionNumber", CurrentMinorVersionNumber);
			Code == ERROR_SUCCESS)
		{
			ver.dwMinorVersion = CurrentMinorVersionNumber;
		}
		else
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_ERROR(0, TM("GetValueFunction(CurrentMinorVersionNumber) error: %s"),
				ParseWindowsError(Code).c_str()));
		}


		if (const auto Code = GetValueFunction(KeyHandle, L"CurrentBuildNumber", CurrentBuildNumber);
			Code == ERROR_SUCCESS)
		{
			ver.dwBuildNumber = std::stoul(CurrentBuildNumber);
		}
		else
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_ERROR(0, TM("GetValueFunction(CurrentBuildNumber) error: %s"),
				ParseWindowsError(Code).c_str()));
		}
	}
	else
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_ERROR(0, TM("Try open HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion error: %s"),
			ParseWindowsError(KeyHandleReturnedValue.Code()).c_str()));
	}

	return ver;
}

std::wstring GetTypeOfRegeditValue(DWORD Type)
{
	switch (Type)
	{
	case REG_NONE:
		return L"REG_NONE";

	case REG_SZ:
		return L"REG_SZ";

	case REG_EXPAND_SZ:
		return L"REG_EXPAND_SZ";

	case REG_BINARY:
		return L"REG_BINARY";

	case REG_DWORD:  //REG_DWORD_LITTLE_ENDIAN
		return L"REG_DWORD";

	case REG_DWORD_BIG_ENDIAN:
		return L"REG_DWORD_BIG_ENDIAN";

	case REG_LINK:
		return L"REG_LINK";

	case REG_MULTI_SZ:
		return L"REG_MULTI_SZ";

	case REG_RESOURCE_LIST:
		return L"REG_RESOURCE_LIST";

	case REG_FULL_RESOURCE_DESCRIPTOR:
		return L"REG_FULL_RESOURCE_DESCRIPTOR";

	case REG_RESOURCE_REQUIREMENTS_LIST:
		return L"REG_RESOURCE_REQUIREMENTS_LIST";

	case REG_QWORD:   //REG_QWORD_LITTLE_ENDIAN
		return L"REG_QWORD";

	default:
		return L"UNKNOWN_VALUE: " + std::to_wstring(Type);
	}
}

bool IsRunAsAdministator()
{
	SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
	wil::unique_sid AdministratorsGroup;
	if (AllocateAndInitializeSid(
		&NtAuthority,
		2,
		SECURITY_BUILTIN_DOMAIN_RID,
		DOMAIN_ALIAS_RID_ADMINS,
		0, 0, 0, 0, 0, 0,
		&AdministratorsGroup))
	{
		BOOL IsMember;
		if (CheckTokenMembership(nullptr, AdministratorsGroup.get(), &IsMember))
		{
			return IsMember;
		}
		else
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("CheckTokenMembership error: %s"),
				ParseWindowsError(GetLastError()).c_str()));
		}
	}
	else
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("AllocateAndInitializeSid error: %s"),
			ParseWindowsError(GetLastError()).c_str()));
	}

	return false;
}


bool GUIDIsEqual(CLSID GUID1, std::wstring GUID2s)
{
	CLSID GUID2;
	if (const HRESULT hr = CLSIDFromString(GUID2s.c_str(), &GUID2);
		SUCCEEDED(hr))
	{
		return GUID1 == GUID2;
	}
	else
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_ERROR(0, TM("CLSIDFromString(%s) error: %s"),
			GUID2s.c_str(), ParseWindowsError(hr).c_str()));
	}
	return false;
}


//bool GUIDIsEqual(std::wstring GUID1s, std::wstring GUID2s)
//{
//
//}


//std::wstring GetCurrentModuleName()
//{
//	HMODULE CurrentModuleHandle;
//	if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT |  GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
//		reinterpret_cast<LPCWSTR>(&GetCurrentModuleName), &CurrentModuleHandle))
//	{
//		std::wstring CurrentModulePath(MAX_PATH, L'\0');
//		if (GetModuleFileNameW(CurrentModuleHandle, CurrentModulePath.data(), CurrentModulePath.size()))
//		{
//			if (size_t Off = CurrentModulePath.find_last_of(L'\\');
//				Off != std::wstring::npos)
//			{
//				CurrentModulePath.erase(0, Off + 1llu);
//
//				if (Off = CurrentModulePath.find_last_of(L'.');
//					Off != std::wstring::npos)
//				{
//					CurrentModulePath.erase(Off, CurrentModulePath.size() - Off);
//					return CurrentModulePath;
//				}
//				else
//				{
//					SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("CurrentModulePath.find_last_of(L'.') not find for string: %s"),
//						CurrentModulePath.c_str()));
//				}
//			}
//			else
//			{
//				SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("CurrentModulePath.find_last_of(L\\\\') not find for string: %s"),
//					CurrentModulePath.c_str()));
//			}
//		}
//		else
//		{
//			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("GetModuleFileNameW()) error: %s"),
//				ParseWindowsError(GetLastError()).c_str()));
//		}
//	}
//	else
//	{
//		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0,
//			TM("GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(&GetCurrentModuleName)) error: %s"),
//			ParseWindowsError(GetLastError()).c_str()));
//	}
//
//	return {};
//}


std::wstring GetModuleName(PVOID ImageBase)
{
	HMODULE CurrentModuleHandle;
	if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT |  GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
		reinterpret_cast<LPCWSTR>(ImageBase), &CurrentModuleHandle))
	{
		std::wstring CurrentModulePath(MAX_PATH, L'\0');
		if (GetModuleFileNameW(CurrentModuleHandle, CurrentModulePath.data(), CurrentModulePath.size()))
		{
			if (size_t Off = CurrentModulePath.find_last_of(L'\\');
				Off != std::wstring::npos)
			{
				CurrentModulePath.erase(0, Off + 1llu);

				if (Off = CurrentModulePath.find_last_of(L'.');
					Off != std::wstring::npos)
				{
					CurrentModulePath.erase(Off, CurrentModulePath.size() - Off);
					return CurrentModulePath;
				}
				else
				{
					SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("CurrentModulePath.find_last_of(L'.') not find for string: %s"),
						CurrentModulePath.c_str()));
				}
			}
			else
			{
				SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("CurrentModulePath.find_last_of(L'\\\\') not find for string: %s"),
					CurrentModulePath.c_str()));
			}
		}
		else
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("GetModuleFileNameW() error: %s"),
				ParseWindowsError(GetLastError()).c_str()));
		}
	}
	else
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0,
			TM("GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(&GetCurrentModuleName)) error: %s"),
			ParseWindowsError(GetLastError()).c_str()));
	}

	return {};
}