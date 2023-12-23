#include "General.h"

//https://github.com/GiovanniDicanio/WinReg

//GLOBAL TODO: Перевести все функции на код возврата GetLastError() и 0 в случае успеха
//Повысить стабильность SetAndCreateRegeditAPODevice
//Переименовать на нормальные названия функций - пример(SetAndCreateRegeditAPODevice - плохое название)

/*Adming Privilige request by ShellExecuteExW*/
bool RegisterAPO(bool IsInstall)
{
	winreg::RegKey CLSIDKey, APOKey;
	//bool IsSuccess = /*true*/false;

	//KEY_WOW64_64KEY
	const auto CLSIDReg = CLSIDKey.TryOpen(CLSIDPath.RegeditKey, std::format(L"{}\\{}", CLSIDPath.RegeditPath, APOGUID_STRING_WITH_CURLY_BRACES), KEY_QUERY_VALUE);
	const auto APOReg = APOKey.TryOpen(APORegPath.RegeditKey, std::format(L"{}\\{}", APORegPath.RegeditPath, APOGUID_STRING_WITH_CURLY_BRACES), KEY_QUERY_VALUE);

	bool IsSuccess = CLSIDReg.IsOk() && APOReg.IsOk();

	if (!CLSIDReg || !APOReg)
	{
		SHELLEXECUTEINFOW SE{};

		const std::wstring ServiceParameters = std::format(L"/s{} {}", !IsInstall ? L" /u" : L"", DllName);  //if directory regsvr32 == directory MicFusionBridge.dll

		SE.cbSize = sizeof(SE);
		SE.fMask = SEE_MASK_NOCLOSEPROCESS;
		SE.hwnd = nullptr;
		SE.lpVerb = L"runas";
		//SE.lpFile = L"regsvr32.exe";
		SE.lpFile = L"C:\\Windows\\System32\\regsvr32.exe";
		SE.nShow = SW_SHOWNORMAL;
		SE.lpParameters = ServiceParameters.c_str();

		if (ShellExecuteExW(&SE))
		{
			if (SE.hProcess)
			{
				const DWORD WaitValue =  WaitForSingleObject(SE.hProcess, INFINITE);
				if (WaitValue != WAIT_OBJECT_0)
				{
					SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("WaitForSingleObject(regsvr32.exe, INFINITE) failed, error: %s"),
						ParseWaitForSingleObjectValue(WaitValue).c_str()));
				}

				DWORD ExitCode;
				if (GetExitCodeProcess(SE.hProcess, &ExitCode))
				{
					if (ExitCode != 0)
					{
						SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("[%s] GetExitCodeProcess return code: %s"),
							SE.lpFile, [&ExitCode]() -> std::wstring
							{
								std::wstring result;

								//https://stackoverflow.com/questions/22094309/regsvr32-exit-codes-documentation
								switch (ExitCode)
								{
								case 1:
									result = L"FAIL_ARGS(1) - Invalid Argument";
									break;
								case 2:
									result = L"FAIL_OLE(2) - OleInitialize Failed";
									break;
								case 3:
									result = L"FAIL_LOAD(3) - LoadLibrary Failed";
									break;
								case 4:
									result = L"FAIL_ENTRY(4) - GetProcAddress failed";
									break;
								case 5:
									result = L"FAIL_REG(5) - DllRegisterServer(current case) or DllUnregisterServer failed.";
									break;
								default:
									result = std::format(L"UNKOWN_CODE({})", ExitCode);
									break;
								}
								return result;
							}().c_str()));

						IsSuccess = false;
					}
					else
					{
						IsSuccess = true;
					}
				}
				else
				{
					SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("[%s] GetExitCodeProcess error: %s"),
						SE.lpFile, ParseWindowsError(GetLastError()).c_str()));
					IsSuccess = false;
				}

				LogingBooleanWindowsError(CloseHandle(SE.hProcess));
			}
			else
			{
				SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("[%s] SE.hProcess == NULL, error: %s"),
					SE.lpFile, ParseWindowsError(GetLastError()).c_str()));
				IsSuccess = false;
			}
		}
		else
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("[%s] ShellExecuteExW error: %s"),
				SE.lpFile, ParseWindowsError(GetLastError()).c_str()));
			IsSuccess = false;
		}
	}
	else
	{
		if (CLSIDReg.Failed())
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("CLSID: %s"),
				ParseWindowsError(CLSIDReg.Code()).c_str()));
		}
		if (APOReg.Failed())
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("APO: %s"),
				ParseWindowsError(APOReg.Code()).c_str()));
		}
	}

	return IsSuccess;
}

//Need admin privelege, check this: IsUserAnAdmin
int TakeOwnership(const std::wstring& DeviceRegeditKey)
{
	wil::unique_handle TokenHandle;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY/*TODO: возможно лишний ключ, ченкуть это по функции OpenProcessToken*/, &TokenHandle))
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("OpenProcessToken error: %s"),
			ParseWindowsError(GetLastError()).c_str()));
		return -1;
	}

	LUID Luid;
	if (!LookupPrivilegeValueW(nullptr, SE_TAKE_OWNERSHIP_NAME, &Luid))
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("LookupPrivilegeValueW error: %s"),
			ParseWindowsError(GetLastError()).c_str()));
		return -2;
	}

	TOKEN_PRIVILEGES TokenPrivilgeges{
		.PrivilegeCount = 1,
		.Privileges = {{.Luid = Luid, .Attributes = SE_PRIVILEGE_ENABLED }}};
	if (!AdjustTokenPrivileges(TokenHandle.get(), FALSE, &TokenPrivilgeges, sizeof(TokenPrivilgeges), nullptr, nullptr))
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("AdjustTokenPrivileges error: %s"),
			ParseWindowsError(GetLastError()).c_str()));
		return -3;
	}

	winreg::RegKey KeyHandle;
	if (auto KeyHandleReturnedValue = KeyHandle.TryOpen(HKEY_LOCAL_MACHINE, DeviceRegeditKey, WRITE_OWNER);  //KEY_WOW64_64KEY
		KeyHandleReturnedValue.Failed())
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("[%s] TryOpen with WRITE_OWNER error: %s"),
			DeviceRegeditKey.c_str(), ParseWindowsError(KeyHandleReturnedValue.Code()).c_str()));
		return -4;
	}

	wil::unique_sid AdministratorGroupSID;
	//DOMAIN_ALIAS_RID_USERS - Local User
	//DOMAIN_ALIAS_RID_ADMINS - Admin
	if (SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
		!AllocateAndInitializeSid(&NtAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
		0, 0, 0, 0, 0, 0, &AdministratorGroupSID))
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("AllocateAndInitializeSid error: %s"),
			ParseWindowsError(GetLastError()).c_str()));
		return -5;
	}

	SECURITY_DESCRIPTOR SDPointer{};
	if (!InitializeSecurityDescriptor(&SDPointer, SECURITY_DESCRIPTOR_REVISION))
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("InitializeSecurityDescriptor error: %s"),
			ParseWindowsError(GetLastError()).c_str()));
		return -6;
	}
	if (!SetSecurityDescriptorOwner(&SDPointer, AdministratorGroupSID.get(), FALSE))
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("SetSecurityDescriptorOwner error: %s"),
			ParseWindowsError(GetLastError()).c_str()));
		return -7;
	}

	if (const LSTATUS Status = RegSetKeySecurity(KeyHandle.Get(), OWNER_SECURITY_INFORMATION, &SDPointer/*.get()*/);
		Status != ERROR_SUCCESS)
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("[%s] RegSetKeySecurity error: %s"),
			DeviceRegeditKey.c_str(), ParseWindowsError(Status).c_str()));
		return -8;
	}

	TokenPrivilgeges.Privileges[0].Attributes = 0;

	if (!AdjustTokenPrivileges(TokenHandle.get(), FALSE, &TokenPrivilgeges, sizeof(TOKEN_PRIVILEGES), nullptr, nullptr))
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("AdjustTokenPrivileges error: %s"),
			ParseWindowsError(GetLastError()).c_str()));
		return -9;
	}

	return 0;
}


//Need admin privelege, check this: IsUserAnAdmin
int MakeWritable(const std::wstring& key, bool DeletePrivelege = false, DWORD nSubAuthority1 = DOMAIN_ALIAS_RID_ADMINS)
{
	winreg::RegKey KeyHandle;
	if (auto KeyHandleReturnedValue = KeyHandle.TryOpen(HKEY_LOCAL_MACHINE, key, READ_CONTROL | WRITE_DAC);  //KEY_WOW64_64KEY
		KeyHandleReturnedValue.Failed())
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("[%s] TryOpen with WRITE_OWNER error: %s"),
			key.c_str(), ParseWindowsError(KeyHandleReturnedValue.Code()).c_str()));
		return -1;
	}

	DWORD descriptorSize = 0;
	//SafePointerDereference(Singleton<ControlAPOLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("[%s] RegGetKeySecurity(get descriptor size) error: %s"),
	//	key.c_str(), ParseWindowsError(RegGetKeySecurity(KeyHandle.Get(), DACL_SECURITY_INFORMATION, NULL, &descriptorSize)).c_str()));

	RegGetKeySecurity(KeyHandle.Get(), DACL_SECURITY_INFORMATION, NULL, &descriptorSize);

	const HANDLE ProcessHeap = GetProcessHeap();
	if (!ProcessHeap)
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("GetProcessHeap error: %s"),
			ParseWindowsError(GetLastError()).c_str()));
		return -2;
	}

	wil::unique_process_heap_ptr<void> oldSd(HeapAlloc(ProcessHeap, HEAP_ZERO_MEMORY /*| HEAP_GENERATE_EXCEPTIONS*/, descriptorSize));
	if (!oldSd)
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(),P7_CRITICAL(0, TM("HeapAlloc error: %s"),
			ParseWindowsError(GetLastError()).c_str()));
		return -3;
	}

	if (const LSTATUS Status = RegGetKeySecurity(KeyHandle.Get(), DACL_SECURITY_INFORMATION, oldSd.get(), &descriptorSize);
		Status != ERROR_SUCCESS)
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("[%s] RegGetKeySecurity error: %s"),
			key.c_str(), ParseWindowsError(Status).c_str()));
		return -4;
	}

	BOOL aclPresent, aclDefaulted;
	PACL oldAcl = NULL;
	if (!GetSecurityDescriptorDacl(oldSd.get(), &aclPresent, &oldAcl, &aclDefaulted))
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("GetSecurityDescriptorDacl error: %s"),
			ParseWindowsError(GetLastError()).c_str()));
		return -5;
	}

	wil::unique_sid Sid;
	//DOMAIN_ALIAS_RID_USERS - Local User
	//DOMAIN_ALIAS_RID_ADMINS - Admin
	SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
	if (!AllocateAndInitializeSid(&NtAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, /*DOMAIN_ALIAS_RID_ADMINS*/nSubAuthority1,
		0, 0, 0, 0, 0, 0, &Sid))
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("AllocateAndInitializeSid error: %s"),
			ParseWindowsError(GetLastError()).c_str()));
		return -6;
	}

	EXPLICIT_ACCESS ea
	{
		.grfAccessPermissions = DeletePrivelege ? 0ul : KEY_ALL_ACCESS, .grfAccessMode = DeletePrivelege ? REVOKE_ACCESS : SET_ACCESS, .grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT,
		.Trustee {.TrusteeForm = TRUSTEE_IS_SID, .TrusteeType = TRUSTEE_IS_GROUP, .ptstrName = (LPWSTR)Sid.get()}
	};

	//wil::unique_hlocal_ptr<ACL> acl;
	wil::unique_any<PACL, decltype(&::LocalFree), ::LocalFree> acl;
	if (SetEntriesInAclW(1, &ea, oldAcl, &acl) != ERROR_SUCCESS)
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("SetEntriesInAclW error: %s"),
			ParseWindowsError(GetLastError()).c_str()));
		return -7;
	}

	SECURITY_DESCRIPTOR sd{};
	if (!InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION))
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("InitializeSecurityDescriptor error: %s"),
			ParseWindowsError(GetLastError()).c_str()));
		return -8;
	}
	if (!SetSecurityDescriptorDacl(&sd, TRUE, acl.get(), FALSE))
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("SetSecurityDescriptorDacl error: %s"),
			ParseWindowsError(GetLastError()).c_str()));
		return -9;
	}

	if (const LSTATUS Status = RegSetKeySecurity(KeyHandle.Get(), DACL_SECURITY_INFORMATION, &sd);
		Status != ERROR_SUCCESS)
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("[%s] SetSecurityDescriptorDacl error: %s"),
			key.c_str(), ParseWindowsError(Status).c_str()));
		return -10;
	}

	return 0;
}


//Need admin privelege, check this: IsUserAnAdmin
//REGEDIT: HKEY_LOCAL_MACHINE\SOFTWARE\MicFusionBridge
LSTATUS DeleteRecordFromSoftwareRegeditAPO(const std::wstring& DeviceGUID)
{
	SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("DeleteRecordFromSoftwareRegeditAPO(%s)"),
		DeviceGUID.c_str()));

	const std::wstring BaseSoftwarePath = std::format(L"SOFTWARE\\{}", APOName);

	winreg::RegKey KeyHandleBaseSoftware;
	if (auto KeyHandleReturnedValue = KeyHandleBaseSoftware.TryOpen(HKEY_LOCAL_MACHINE, BaseSoftwarePath, KEY_WRITE | DELETE | KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE);  //KEY_WOW64_64KEY
		KeyHandleReturnedValue.Failed())
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("TryOpen(%s) error: %s"),
			BaseSoftwarePath.c_str(), ParseWindowsError(KeyHandleReturnedValue.Code()).c_str()));
		return KeyHandleReturnedValue.Code();
	}

	std::vector<std::wstring> CapturedDevices;
	if (const auto Code = GetValueFunction(KeyHandleBaseSoftware, L"CapturedDevices", CapturedDevices);
		Code == ERROR_SUCCESS)
	{
		CLSID DeviceGUIDConverted; 
		const HRESULT HRConvert = CLSIDFromString(DeviceGUID.c_str(), &DeviceGUIDConverted);
		if (FAILED(HRConvert))
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("[%s\\CapturedDevices], CLSIDFromString(%s) error: %s"),
				BaseSoftwarePath.c_str(), DeviceGUID.c_str(), ParseWindowsError(HRConvert).c_str()));
		}

		CapturedDevices.erase(std::remove_if(CapturedDevices.begin(), CapturedDevices.end(),
			[&](const std::wstring& GUIDValue)
			{
				if (SUCCEEDED(HRConvert))
				{
					return GUIDIsEqual(DeviceGUIDConverted, GUIDValue);
				}
				return GUIDValue.find(DeviceGUID) != std::wstring::npos;
			}), CapturedDevices.end());

		if (const auto Code2 = SetValueFunction(KeyHandleBaseSoftware, L"CapturedDevices", CapturedDevices);
			Code2 != ERROR_SUCCESS)
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("[%s] SetValueFunction(CapturedDevices) error: %s"),
				BaseSoftwarePath.c_str(), ParseWindowsError(Code2).c_str()));
			return Code2;
		}
	}
	else
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("GetValueFunction(CapturedDevices) error: %s"),
			ParseWindowsError(Code).c_str()));
		return Code;
	}

	if (auto DeleteKeyReturnedValue = KeyHandleBaseSoftware.TryDeleteTree(DeviceGUID.c_str());
		DeleteKeyReturnedValue.Failed())
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("[%s] TryDeleteTree(%s) error: %s"),
			BaseSoftwarePath.c_str(), DeviceGUID.c_str() , ParseWindowsError(DeleteKeyReturnedValue.Code()).c_str()));
		return DeleteKeyReturnedValue.Code();
	}

	return ERROR_SUCCESS;
}


//Need admin privelege, check this: IsUserAnAdmin
//REGEDIT: HKEY_LOCAL_MACHINE\SOFTWARE\MicFusionBridge
LSTATUS CreateSoftwareRegeditGUIDAndWritePreviousAPO(const std::wstring& DeviceGUID, const std::wstring& PrevAPO, bool WritePrevAPO = true)
{
	SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("CreateSoftwareRegeditGUIDAndWritePreviousAPO(%s, %s, %s)"),
		DeviceGUID.c_str(), PrevAPO.c_str(), WritePrevAPO  ? L"true":  L"false"));

	const std::wstring BaseSoftwarePath = std::format(L"SOFTWARE\\{}", APOName);
	const std::wstring BaseSoftwarePathGUID = std::format(L"SOFTWARE\\{}\\{}", APOName, DeviceGUID);

	SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("BaseSoftwarePath: %s"),
		BaseSoftwarePath.c_str()));
	SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("BaseSoftwarePathGUID: %s"),
		BaseSoftwarePathGUID.c_str()));

	auto CreateSubKey = [&](winreg::RegKey& Key, const std::wstring& RegeditPath) ->winreg::RegResult
	{
		winreg::RegResult KeyHandleReturnedValue;
		if (KeyHandleReturnedValue = Key.TryOpen(HKEY_LOCAL_MACHINE, RegeditPath, GENERIC_READ | KEY_QUERY_VALUE | KEY_WRITE);  //KEY_WOW64_64KEY
			KeyHandleReturnedValue.Failed())
		{
			KeyHandleReturnedValue = Key.TryCreate(HKEY_LOCAL_MACHINE, RegeditPath, GENERIC_READ | KEY_QUERY_VALUE | KEY_WRITE);
			if (KeyHandleReturnedValue.Failed())
			{
				SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("TryCreate(%s) error: %s"),
					RegeditPath.c_str(), ParseWindowsError(KeyHandleReturnedValue.Code()).c_str()));
			}

			if (KeyHandleReturnedValue.IsOk() && RegeditPath == BaseSoftwarePath)
			{
				if (const int MakeWritableCode = MakeWritable(/*BaseSoftwarePath*/RegeditPath, false, DOMAIN_ALIAS_RID_USERS);
					MakeWritableCode != 0)
				{
					SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("MakeWritable(%s) with code = %d error: %s"),
						RegeditPath.c_str(), MakeWritableCode, ParseWindowsError(GetLastError()).c_str()));
					return winreg::RegResult{ ERROR_DATATYPE_MISMATCH }; //TODO: поменять ошибку
				}
			}
		}
		return KeyHandleReturnedValue;
	};

	winreg::RegKey KeyHandleBaseSoftware;
	winreg::RegKey KeyHandleBaseSoftwareGUID;
	if (auto KeyHandleBaseSoftwareReturnedValue = CreateSubKey(KeyHandleBaseSoftware, BaseSoftwarePath);
		KeyHandleBaseSoftwareReturnedValue.Failed())
	{
		return KeyHandleBaseSoftwareReturnedValue.Code();
	}

	if (auto KeyHandleBaseSoftwareGUIDReturnedValue = CreateSubKey(KeyHandleBaseSoftwareGUID, BaseSoftwarePathGUID);
		KeyHandleBaseSoftwareGUIDReturnedValue.Failed())
	{
		return KeyHandleBaseSoftwareGUIDReturnedValue.Code();
	}

	std::vector<std::wstring> CapturedDevices;
	if (const auto Code = GetValueFunction(KeyHandleBaseSoftware, L"CapturedDevices", CapturedDevices);
		Code == ERROR_SUCCESS)
	{
		CLSID DeviceGUIDConverted;
		const HRESULT HRConvert = CLSIDFromString(DeviceGUID.c_str(), &DeviceGUIDConverted);
		if (FAILED(HRConvert))
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("[%s\\CapturedDevices], CLSIDFromString(%s) error: %s"),
				BaseSoftwarePath.c_str(), DeviceGUID.c_str(), ParseWindowsError(HRConvert).c_str()));
		}

		if (!std::any_of(CapturedDevices.begin(), CapturedDevices.end(), [&](const std::wstring& GUIDValue)
			{
				if (SUCCEEDED(HRConvert))
				{
					return GUIDIsEqual(DeviceGUIDConverted, GUIDValue);
				}
				return GUIDValue.find(DeviceGUID) != std::wstring::npos;  //TODO: может удалить ?
			}))
		{
			CapturedDevices.push_back(DeviceGUID);
		}
		else
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("[%s]\\CapturedDevices find relative value: %s"),
				BaseSoftwarePath.c_str(), DeviceGUID.c_str()));
		}

		if (const auto Code2 = SetValueFunction(KeyHandleBaseSoftware, L"CapturedDevices", CapturedDevices);
			Code2 != ERROR_SUCCESS)
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("[%s] SetValueFunction(CapturedDevices) error: %s"),
				BaseSoftwarePath.c_str(), ParseWindowsError(Code2).c_str()));
			return Code2;
		}
	}
	else if(Code == ERROR_FILE_NOT_FOUND)
	{
		//SafePointerDereference(Singleton<ControlAPOLogger>::GetInstance().GetTracePtr(), Set_Verbosity(0, EP7TRACE_LEVEL_TRACE));
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_INFO(0, TM("Create CapturedDevices key")));
		//SafePointerDereference(Singleton<ControlAPOLogger>::GetInstance().GetTracePtr(), Set_Verbosity(0, EP7TRACE_LEVEL_WARNING));

		if (const auto Code2 = SetValueFunction(KeyHandleBaseSoftware, L"CapturedDevices", std::vector<std::wstring>{ DeviceGUID });
			Code2 != ERROR_SUCCESS)
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("[%s] TrySetMultiStringValue(CapturedDevices) error: %s"),
				BaseSoftwarePathGUID.c_str(), ParseWindowsError(Code2).c_str()));
			return Code2;
		}
	}
	else
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("[%s] GetValueFunction(CapturedDevices) error: %s"),
			BaseSoftwarePathGUID.c_str(), ParseWindowsError(Code).c_str()));
		return Code;
	}

	if (WritePrevAPO)
	{
		if (const auto Code = SetValueFunction(KeyHandleBaseSoftwareGUID, L"PreviousAPO", PrevAPO);
			Code != ERROR_SUCCESS)
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("[%s] SetValueFunction(PreviousAPO) error: %s"),
				BaseSoftwarePathGUID.c_str(), ParseWindowsError(Code).c_str()));
			return Code;
		}
	}

	return ERROR_SUCCESS;
}


//REGEDIT: HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\Audio
//Так же не забывать откатывать изменения, если что не удалось в процессе
///*void*/int SetAndCreateRegeditAPODevice(const std::wstring& DeviceGUID)
int SetAndCreateRegeditAPODevice(const std::wstring& DeviceGUID)
{
	SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("SetAndCreateRegeditAPODevice(%s)"),
		DeviceGUID.c_str()));

	if (DeviceGUID.empty())
	{
		return -1;
	}

	//TODO: очень важное TODO, возвращаться из функции только если прям критикал ошибка, по возможности сделать код возврата в начале функции по типу ExitCode = 0, и в случае ошибки присваивать его
	//потому что иногда нужно продолжить исполнение функции не смотря ни на что, потому что что-то нуждается в фиксе, а мы возвращаем это как ошибку

	//{
	//	//TODO: check < Windows 10, does it work without this ?
	//	if (!DisableProtectedAudioDGIs(1))
	//	{
	//		winreg::RegKey WindowsAudioKey;
	//		if (auto WindowsAudioKeyReturnedValue = WindowsAudioKey.TryCreate(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Audio", KEY_QUERY_VALUE | KEY_SET_VALUE | KEY_CREATE_SUB_KEY);  //KEY_WOW64_64KEY
	//			!WindowsAudioKeyReturnedValue.Failed())
	//		{
	//			DWORD DisableProtectedAudioDG = 1;
	//			if (const auto Code = SetValueFunction(WindowsAudioKey, L"DisableProtectedAudioDG", DisableProtectedAudioDG);
	//				Code != ERROR_SUCCESS)
	//			{
	//				SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_ERROR(0, TM("SetValueFunction(DisableProtectedAudioDG == 1) error: %s"),
	//					ParseWindowsError(Code).c_str()));
	//				return -25;
	//			}
	//		}
	//		else
	//		{
	//			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("TryOpen error: %s"),
	//				ParseWindowsError(WindowsAudioKeyReturnedValue.Code()).c_str()));
	//		}
	//	}
	//}

	if (!RegisterAPO())
	{
		return -50;
	}

	const std::wstring DeviceGUIDPathFxProperties = std::format(L"{}\\{}\\{}", MicrophoneDevicesPath.RegeditPath, DeviceGUID, L"FxProperties");
	const std::wstring BaseDeviceGUIDPath = std::format(L"{}\\{}", MicrophoneDevicesPath.RegeditPath, DeviceGUID);

	//Check if FxProperties is exist, if not, try to create
	winreg::RegKey KeyHandleFxProperties;
	if (auto KeyHandleReturnedValue = KeyHandleFxProperties.TryOpen(MicrophoneDevicesPath.RegeditKey, DeviceGUIDPathFxProperties, GENERIC_READ | KEY_QUERY_VALUE | KEY_SET_VALUE);  //KEY_WOW64_64KEY
		KeyHandleReturnedValue.Failed())
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("FxProperties was not exist in [%s] path with error: %s"),
			DeviceGUIDPathFxProperties.c_str(), ParseWindowsError(KeyHandleReturnedValue.Code()).c_str()));

		if (KeyHandleReturnedValue = KeyHandleFxProperties.TryCreate(MicrophoneDevicesPath.RegeditKey, DeviceGUIDPathFxProperties, GENERIC_READ | KEY_QUERY_VALUE | KEY_SET_VALUE);  //KEY_WOW64_64KEY  //maybe KEY_WRITE
			KeyHandleReturnedValue.Failed())
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("FxProperties was not create, trying to create Admin privilege with all access in [%s] path, prev error: %s"),
				DeviceGUIDPathFxProperties.c_str(), ParseWindowsError(KeyHandleReturnedValue.Code()).c_str()));

			if (TakeOwnership(BaseDeviceGUIDPath) != 0 || MakeWritable(BaseDeviceGUIDPath) != 0)
			{
				SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_ERROR(0, TM("Check error near")));
				return -1;
			}

			if (KeyHandleReturnedValue = KeyHandleFxProperties.TryCreate(MicrophoneDevicesPath.RegeditKey, DeviceGUIDPathFxProperties, GENERIC_READ | KEY_QUERY_VALUE | KEY_SET_VALUE);  //KEY_WOW64_64KEY  //maybe KEY_WRITE
				KeyHandleReturnedValue.Failed())
			{
				SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("[%s] after TakeOwnership and MakeWritable creting key was running with error: %s"),
					DeviceGUIDPathFxProperties.c_str(), ParseWindowsError(KeyHandleReturnedValue.Code()).c_str()));
				return -2;
			}
		}

		//TODO: это скорее большая ошибка(лучше всего оставить для прав админа возможность делать все что угодно с FxProperties)
		//////Delete administrator privilege with all access, after creating FxProperties sub path
		////if (MakeWritable(BaseDeviceGUIDPath, true) != 0)
		////{
		////	SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_ERROR(0, TM("Check error near")));
		////	return -3;  //TODO: может и не стоит возвращать как ошибку
		////}
	}

	//Force delete disabling key FX
	if (const auto DisableSysFX = KeyHandleFxProperties.TryQueryValueType(PKEY_AUDIO_ENDPOINT_DISABLE_SYS_FX);
		DisableSysFX.IsValid())
	{
		if(const auto DeleteStatus = KeyHandleFxProperties.TryDeleteValue(PKEY_AUDIO_ENDPOINT_DISABLE_SYS_FX);
			DeleteStatus.Failed())
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("[%s] TryDeleteValue(): %s"),
				DeviceGUIDPathFxProperties.c_str(), ParseWindowsError(DeleteStatus.Code()).c_str()));
			return -3;
		}
	}

	if (!APOIsWritedInRegedit(DeviceGUID))
	{
		enum class APOInstallType
		{
			MultiMFX,
			MFX,
			GFX
		};
		APOInstallType APOInstallType = APOInstallType::GFX;

		auto CheckCompliance = [&DeviceGUIDPathFxProperties](const winreg::RegExpected<DWORD>& Value, DWORD NeedType, const wchar_t* APOGUIDType) -> bool
		{
			if (Value.IsValid())
			{
				if (Value.GetValue() != NeedType)
				{
					SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("[%s] device, APOMultiMFXEffectGUID: type is: %lu, but not %lu"),
						DeviceGUIDPathFxProperties.c_str(), Value.GetValue(), NeedType));
					return false;
				}
			}
			return true;
		};

		bool UncomplianceNotFind = true;
		//TODO: так же проверять перед установкой, вдруг появились новые типа APO, например был GFX, стал вдруг MFX/MultiMFX, значит надо обновить конфигурацию и НЕ использовать GFX
		//Не забыть удалить наше старое значение
		//Я думаю что стоит сначала выбрать тип установки, потом удалить все более низшие уровни с НАШИМ APO, по типу тип установки MultiMFX -> проверяем MFX затем GFX
		//Если просто MFX - то удаляем GFX

		const auto MultiMFXTypeID = KeyHandleFxProperties.TryQueryValueType(APOMultiMFXEffectGUID);
		UncomplianceNotFind &= CheckCompliance(MultiMFXTypeID, REG_MULTI_SZ, APOMultiMFXEffectGUID);

		const auto MFXTypeID = KeyHandleFxProperties.TryQueryValueType(APOMFXEffectGUID);
		UncomplianceNotFind &= CheckCompliance(MultiMFXTypeID, REG_SZ, APOMFXEffectGUID);

		const auto GFXTypeID = KeyHandleFxProperties.TryQueryValueType(APOGFXEffectGUID);
		UncomplianceNotFind &= CheckCompliance(MultiMFXTypeID, REG_SZ, APOGFXEffectGUID);

		if (!UncomplianceNotFind)
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("UncomplianceNotFind find, check near")));
			return -4;
		}

		//TODO: add check of supporting MFX with check Windows version
		if (MultiMFXTypeID.IsValid())
		{
			APOInstallType = APOInstallType::MultiMFX;
		}
		else if (MFXTypeID.IsValid())
		{
			APOInstallType = APOInstallType::MFX;
		}
		else if (GFXTypeID.IsValid())
		{
			APOInstallType = APOInstallType::GFX;
		}
		else
		{
			APOInstallType = APOInstallType::GFX;
		}


		auto InstallAPO = [&]() -> int
		{
			if (APOInstallType == APOInstallType::MultiMFX)   //REG_MULTI_SZ case
			{
				const wchar_t* APO_TYPE_STRING = APOMultiMFXEffectGUID;

				std::vector<std::wstring> MultiMFXGetMultiStrings;
				if (const auto Code = GetValueFunction(KeyHandleFxProperties, APO_TYPE_STRING, MultiMFXGetMultiStrings);
					Code == ERROR_SUCCESS)
				{
					if (std::any_of(MultiMFXGetMultiStrings.begin(), MultiMFXGetMultiStrings.end(), [&](const std::wstring& GUIDValue)
						{
							return GUIDIsEqual(APO_GUID, GUIDValue);
						}))
					{
						SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("[%s] device is already installed as Multi MFX"),
							DeviceGUIDPathFxProperties.c_str()));    //logic error, because we check if (!APOIsWritedInRegedit(DeviceGUID))
						return 1;
					}
				}
				else
				{
					SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("[%s] GetValueFunction() error: %s"),
						DeviceGUIDPathFxProperties.c_str(), ParseWindowsError(Code).c_str()));
					return 4;
				}
				MultiMFXGetMultiStrings.push_back(APOGUID_STRING_WITH_CURLY_BRACES);

				if (const auto Code = SetValueFunction(KeyHandleFxProperties, APO_TYPE_STRING, MultiMFXGetMultiStrings);
					Code == ERROR_SUCCESS)
				{
					if (const LSTATUS CreateStatus = CreateSoftwareRegeditGUIDAndWritePreviousAPO(DeviceGUID, GUIDEmpty, false);
						CreateStatus != ERROR_SUCCESS)
					{
						SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("[%s] device, CreateSoftwareRegeditGUIDAndWritePreviousAPO error: %s"),
							DeviceGUIDPathFxProperties.c_str(), ParseWindowsError(CreateStatus).c_str()));

						MultiMFXGetMultiStrings.pop_back();   //pop_back APOGUID_STRING_WITH_CURLY_BRACES
						if (const auto Code2 = SetValueFunction(KeyHandleFxProperties, APO_TYPE_STRING, MultiMFXGetMultiStrings);
							Code2 != ERROR_SUCCESS)
						{
							SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("[%s] device, SetValueFunction(APOMultiMFXEffectGUID) error: %s"),
								DeviceGUIDPathFxProperties.c_str(), ParseWindowsError(Code2).c_str()));
						}
						return 2;
					}

					return 0;  //Successfully writed as MultiMFX
				}
				else
				{
					SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("[%s] device, SetValueFunction(APOMultiMFXEffectGUID) error: %s"),
						DeviceGUIDPathFxProperties.c_str(), ParseWindowsError(Code).c_str()));
					return 3;
				}

				return 5;
			}
			else if (APOInstallType == APOInstallType::MFX || APOInstallType == APOInstallType::GFX)
			{
				const wchar_t* APO_TYPE_STRING =
					APOInstallType == APOInstallType::MFX ? APOMFXEffectGUID : APOGFXEffectGUID;

				std::wstring G_MFXGetString;
				if (const auto Code = GetValueFunction(KeyHandleFxProperties, APO_TYPE_STRING, G_MFXGetString);
					Code == ERROR_SUCCESS)
				{
					SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("Previous APO value: %s"),
						G_MFXGetString.c_str()));

					if (GUIDIsEqual(APO_GUID, G_MFXGetString))
					{
						SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("[%s] device is already installed as MFX/GFX"),
							DeviceGUIDPathFxProperties.c_str()));    //logic error, because we check if (!APOIsWritedInRegedit(DeviceGUID))
						return 1;
					}

					//Set our APO GUID
					if (const auto Code2 = SetValueFunction(KeyHandleFxProperties, APO_TYPE_STRING, std::wstring(APOGUID_STRING_WITH_CURLY_BRACES));
						Code2 != ERROR_SUCCESS)
					{
						SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("[%s] device, SetValueFunction(APOMultiMFXEffectGUID) error: %s"),
							DeviceGUIDPathFxProperties.c_str(), ParseWindowsError(Code2).c_str()));
						return 2;
					}

					bool PreviousGUIDIsValid = false;
					CLSID G_MFXGUID;
					const HRESULT hrs = CLSIDFromString(G_MFXGetString.c_str(), &G_MFXGUID);   //if guid convertable succes - store as previous APO GUID to call this in APO
					PreviousGUIDIsValid = SUCCEEDED(hrs);  //TODO: check { and }, check if GUID != empty and check string !G_MFXGetString.empty()

					SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("Previous GUID: [%s] Is Valid: %s"),
						G_MFXGetString.c_str(), PreviousGUIDIsValid ? L"True" : L"False"));

					if (const LSTATUS CreateStatus = CreateSoftwareRegeditGUIDAndWritePreviousAPO(DeviceGUID, PreviousGUIDIsValid ? G_MFXGetString : L"", PreviousGUIDIsValid ? true : false);
						CreateStatus != ERROR_SUCCESS)
					{
						//Restore previous value
						if (const auto Code2 = SetValueFunction(KeyHandleFxProperties, APO_TYPE_STRING, G_MFXGetString);
							Code2 != ERROR_SUCCESS)
						{
							SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("[%s] device, SetValueFunction(GFX/MFX Effect GUID) error: %s"),
								DeviceGUIDPathFxProperties.c_str(), ParseWindowsError(Code2).c_str()));
						}
						return 2;
					}

					return 0;  //Successfully writed as GFX/MFX
				}
				else if (Code == ERROR_FILE_NOT_FOUND)
				{
					//Create key
					if (const auto Code2 = SetValueFunction(KeyHandleFxProperties, APO_TYPE_STRING, std::wstring(APOGUID_STRING_WITH_CURLY_BRACES));
						Code2 != ERROR_SUCCESS)
					{
						SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("[%s] device, SetValueFunction(GFX/MFX Effect GUID) error: %s"),
							DeviceGUIDPathFxProperties.c_str(), ParseWindowsError(Code2).c_str()));
						return 2;
					}

					if (const LSTATUS CreateStatus = CreateSoftwareRegeditGUIDAndWritePreviousAPO(DeviceGUID, L"", false);
						CreateStatus != ERROR_SUCCESS)
					{
						//Restore previous value
						if (const auto APODeleteKeyStatus = KeyHandleFxProperties.TryDeleteValue(APO_TYPE_STRING);
							APODeleteKeyStatus.Failed())
						{
							SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("[%s] device, SetValueFunction(GFX/MFX Effect GUID) error: %s"),
								DeviceGUIDPathFxProperties.c_str(), ParseWindowsError(APODeleteKeyStatus.Code()).c_str()));
						}
						return 2;
					}

					return 0;  //Successfully writed as GFX/MFX
				}
				else
				{
					SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("[%s] GetValueFunction() error: %s"),
						DeviceGUIDPathFxProperties.c_str(), ParseWindowsError(Code).c_str()));
					return 4;
				}
			}

			return 10;   //Error
		};


		return InstallAPO();
	}
	//else TODO: Если записано в Software Regedit, то надо восстановить конфигурацию, которая была до этого
	return 20;
}

int DeleteRegeditAPODevice(const std::wstring& DeviceGUID)
{
	SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("DeleteRegeditAPODevice(%s)"),
		DeviceGUID.c_str()));

	if (APOIsWritedInRegedit(DeviceGUID))
	{
		const std::wstring DeviceGUIDPathFxProperties = std::format(L"{}\\{}\\{}", MicrophoneDevicesPath.RegeditPath, DeviceGUID, L"FxProperties");

		//size_t DeletedDevices = 0;

		winreg::RegKey KeyHandle;
		if (auto KeyHandleReturnedValue = KeyHandle.TryOpen(MicrophoneDevicesPath.RegeditKey, DeviceGUIDPathFxProperties, GENERIC_READ | KEY_QUERY_VALUE | KEY_SET_VALUE);  //KEY_WOW64_64KEY
			KeyHandleReturnedValue.Failed())
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("[%s] TryOpen with GENERIC_READ error: %s"),
				DeviceGUID.c_str(), ParseWindowsError(KeyHandleReturnedValue.Code()).c_str()));
			return 2;
		}

		for (const wchar_t* EffectGUIDString : { APOGFXEffectGUID, APOMFXEffectGUID, APOMultiMFXEffectGUID })
		{
			//TODO: удалить потом
			if (wcslen(EffectGUIDString) > wcslen(APOMultiMFXEffectGUID))
			{
				SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("wcslen(value) > wcslen(APOMultiMFXEffectGuid) == true")));
				continue;
			}

			//Второе условие - подстраховка на случай если константые указатели вдруг будут разными по значению
			if (EffectGUIDString != APOMultiMFXEffectGUID || !std::wcsstr(EffectGUIDString, APOMultiMFXEffectGUID))     //REG_SZ
			//if (value != APOMultiMFXEffectGuid || std::wstring(value).find(std::wstring(APOMultiMFXEffectGuid)) != std::wstring::npos)     //REG_SZ
			{
				std::wstring FXGUIDStr;
				if (const LSTATUS GetStatus = GetValueFunction(KeyHandle, EffectGUIDString, FXGUIDStr);
					GetStatus != ERROR_SUCCESS)
				{
					SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("[%s] GetValueFunction(%s), for FX GUID: %s"),
						DeviceGUID.c_str(), ParseWindowsError(GetStatus).c_str(), EffectGUIDString));
					continue;
				}

				if (GUIDIsEqual(APO_GUID, FXGUIDStr))
				{
					if (const LSTATUS DeleteOrSetStatus =
						EffectGUIDString == APOGFXEffectGUID ?
						KeyHandle.TryDeleteValue(EffectGUIDString).Code() :                      //Delete key if GFX
						SetValueFunction(KeyHandle, EffectGUIDString, std::wstring(GUIDEmpty));  //Set {0000...} GUID if EFX
						DeleteOrSetStatus == ERROR_SUCCESS)
					{
						//DeletedDevices++;
						const LSTATUS DeleteRecordStatus = DeleteRecordFromSoftwareRegeditAPO(DeviceGUID);
						if (DeleteRecordStatus != ERROR_SUCCESS)
						{
							SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("[%s] DeleteRecordFromSoftwareRegeditAPO(%s), for FX GUID: %s"),
								DeviceGUID.c_str(), ParseWindowsError(DeleteRecordStatus).c_str(), EffectGUIDString));
						}
						return DeleteRecordStatus;
					}
					else
					{
						SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("[%s] TryDeleteValue/SetValueFunction(%s), for FX GUID: %s"),
							DeviceGUID.c_str(), ParseWindowsError(DeleteOrSetStatus).c_str(), EffectGUIDString));
						return 10;
					}
				}
			}
			else //REG_MULTI_SZ case
			{
				std::vector<std::wstring> FXGUIDVecStr;
				if (const LSTATUS GetStatus = GetValueFunction(KeyHandle, EffectGUIDString, FXGUIDVecStr);
					GetStatus != ERROR_SUCCESS)
				{
					SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("[%s] GetValueFunction(%s), for FX GUID: %s"),
						DeviceGUID.c_str(), ParseWindowsError(GetStatus).c_str(), EffectGUIDString));
					continue;
				}

				if (std::erase_if(FXGUIDVecStr, [](std::wstring FXGUIDStr)
					{
						return GUIDIsEqual(APO_GUID, FXGUIDStr);
					}))   //return count of erased elements
				{
					if (const LSTATUS SetStatus = SetValueFunction(KeyHandle, EffectGUIDString, FXGUIDVecStr);
						SetStatus != ERROR_SUCCESS)
					{
						SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("[%s] SetValueFunction(%s), for FX GUID: %s"),
							DeviceGUID.c_str(), ParseWindowsError(SetStatus).c_str(), EffectGUIDString));
						return 10;
						//continue;
					}

					const LSTATUS DeleteRecordStatus = DeleteRecordFromSoftwareRegeditAPO(DeviceGUID);
					if (DeleteRecordStatus != ERROR_SUCCESS)
					{
						SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("[%s] DeleteRecordFromSoftwareRegeditAPO(%s), for FX GUID: %s"),
							DeviceGUID.c_str(), ParseWindowsError(DeleteRecordStatus).c_str(), EffectGUIDString));
					}
					return DeleteRecordStatus;
				}
			}
		}
	}

	return 20;
}


enum class ParsingDWValueType
{
	dwServiceType,
	dwCurrentState,
	dwControlsAccepted
};
std::wstring ParseDWORDValue(DWORD dwValue, ParsingDWValueType ParsingType)
{
	static const std::vector<std::pair<DWORD, const wchar_t*>> ServiceType =
	{
		{SERVICE_KERNEL_DRIVER, L"SERVICE_KERNEL_DRIVER"},
		{SERVICE_FILE_SYSTEM_DRIVER, L"SERVICE_FILE_SYSTEM_DRIVER"},
		{SERVICE_ADAPTER, L"SERVICE_ADAPTER"},
		{SERVICE_RECOGNIZER_DRIVER, L"SERVICE_RECOGNIZER_DRIVER"},
		{SERVICE_WIN32_OWN_PROCESS, L"SERVICE_WIN32_OWN_PROCESS"},
		{SERVICE_WIN32_SHARE_PROCESS, L"SERVICE_WIN32_SHARE_PROCESS"},
		{SERVICE_USER_SERVICE, L"SERVICE_USER_SERVICE"},
		{SERVICE_USERSERVICE_INSTANCE, L"SERVICE_USERSERVICE_INSTANCE"},
		{SERVICE_INTERACTIVE_PROCESS, L"SERVICE_INTERACTIVE_PROCESS"},
		{SERVICE_PKG_SERVICE, L"SERVICE_PKG_SERVICE"}
	};

	static const std::vector<std::pair<DWORD, const wchar_t*>> CurrentState =
	{
		{SERVICE_STOPPED, L"SERVICE_STOPPED"},
		{SERVICE_START_PENDING, L"SERVICE_START_PENDING"},
		{SERVICE_STOP_PENDING, L"SERVICE_STOP_PENDING"},
		{SERVICE_RUNNING, L"SERVICE_RUNNING"},
		{SERVICE_CONTINUE_PENDING, L"SERVICE_CONTINUE_PENDING"},
		{SERVICE_PAUSE_PENDING, L"SERVICE_PAUSE_PENDING"},
		{SERVICE_PAUSED, L"SERVICE_PAUSED"}
	};

	static const std::vector<std::pair<DWORD, const wchar_t*>> ControlsAccepted =
	{
		{SERVICE_ACCEPT_STOP, L"SERVICE_ACCEPT_STOP"},
		{SERVICE_ACCEPT_PAUSE_CONTINUE, L"SERVICE_ACCEPT_PAUSE_CONTINUE"},
		{SERVICE_ACCEPT_SHUTDOWN, L"SERVICE_ACCEPT_SHUTDOWN"},
		{SERVICE_ACCEPT_PARAMCHANGE, L"SERVICE_ACCEPT_PARAMCHANGE"},
		{SERVICE_ACCEPT_NETBINDCHANGE, L"SERVICE_ACCEPT_NETBINDCHANGE"},
		{SERVICE_ACCEPT_HARDWAREPROFILECHANGE, L"SERVICE_ACCEPT_HARDWAREPROFILECHANGE"},
		{SERVICE_ACCEPT_POWEREVENT, L"SERVICE_ACCEPT_POWEREVENT"},
		{SERVICE_ACCEPT_SESSIONCHANGE, L"SERVICE_ACCEPT_SESSIONCHANGE"},
		{SERVICE_ACCEPT_PRESHUTDOWN, L"SERVICE_ACCEPT_PRESHUTDOWN"},
		{SERVICE_ACCEPT_TIMECHANGE, L"SERVICE_ACCEPT_TIMECHANGE"},
		{SERVICE_ACCEPT_TRIGGEREVENT, L"SERVICE_ACCEPT_TRIGGEREVENT"},
		{SERVICE_ACCEPT_USER_LOGOFF, L"SERVICE_ACCEPT_USER_LOGOFF"},
		{SERVICE_ACCEPT_LOWRESOURCES, L"SERVICE_ACCEPT_LOWRESOURCES"},
		{SERVICE_ACCEPT_SYSTEMLOWRESOURCES, L"SERVICE_ACCEPT_SYSTEMLOWRESOURCES"}
	};

	std::wstring Result;
	const decltype(ServiceType)* VectorPointer = nullptr;
	switch (ParsingType)
	{
	case ParsingDWValueType::dwServiceType:
		VectorPointer = &ServiceType;
		[[fallthrough]];
	case ParsingDWValueType::dwControlsAccepted:
		VectorPointer = !VectorPointer ? &ControlsAccepted : VectorPointer;
		{
			int CountOfFindedFlags = 0;
			for (size_t i = 0; i < VectorPointer->size(); i++)
			{
				if (VectorPointer->operator[](i).first & dwValue)
				{
					Result += VectorPointer->operator[](i).second;
					Result += L" | ";
					CountOfFindedFlags++;
				}
			}

			if (const int PopCount = std::popcount(dwValue); 
				CountOfFindedFlags != PopCount)
			{
				SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("CountOfFindedFlags(%d) != PopCount(%d) error, add support for new type for value(%lu) and type == %d"),
					CountOfFindedFlags, PopCount, dwValue, static_cast<int>(ParsingType)));
			}
		}
		break;

	case ParsingDWValueType::dwCurrentState:
		for (size_t i = 0; i < CurrentState.size(); i++)
		{
			if (CurrentState[i].first == dwValue)
			{
				Result = CurrentState[i].second;
				Result += L" | ";
				break;
			}
		}
		break;

	default:
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("ParsingType unkown value == %d"),
			static_cast<int>(ParsingType)));
		break;
	}
	if (Result.size() > 2)
	{
		Result.erase(Result.size() - 3, 3);
	}

	return std::format(L"{} = {{ {} }}", dwValue, Result);
}


std::wstring FormatServiceStatusProcess(const SERVICE_STATUS_PROCESS& Val)
{
	return std::format(L"dwServiceType: {} dwCurrentState: {} dwControlsAccepted: {} dwWin32ExitCode: {} dwServiceSpecificExitCode: {} dwCheckPoint: {} dwWaitHint: {} dwProcessId: {} dwServiceFlags: {}",
		ParseDWORDValue(Val.dwServiceType, ParsingDWValueType::dwServiceType),
		ParseDWORDValue(Val.dwCurrentState, ParsingDWValueType::dwCurrentState),
		ParseDWORDValue(Val.dwControlsAccepted, ParsingDWValueType::dwControlsAccepted),
		Val.dwWin32ExitCode,
		Val.dwServiceSpecificExitCode,
		Val.dwCheckPoint,
		Val.dwWaitHint,
		Val.dwProcessId,
		Val.dwServiceFlags
		);
}

std::wstring FormatServiceStatusProcess(const SERVICE_STATUS& Val)
{
	return std::format(L"dwServiceType: {} dwCurrentState: {} dwControlsAccepted: {} dwWin32ExitCode: {} dwServiceSpecificExitCode: {} dwCheckPoint: {} dwWaitHint: {}",
		ParseDWORDValue(Val.dwServiceType, ParsingDWValueType::dwServiceType),
		ParseDWORDValue(Val.dwCurrentState, ParsingDWValueType::dwCurrentState),
		ParseDWORDValue(Val.dwControlsAccepted, ParsingDWValueType::dwControlsAccepted),
		Val.dwWin32ExitCode,
		Val.dwServiceSpecificExitCode,
		Val.dwCheckPoint,
		Val.dwWaitHint
	);
}

//TODO:
//Функция перезапуска службы AudioSRV(ну точнее в зависимости от флага, запуск, остановка и думаю отдельной функцией(а может в одной через enum) - получение текущего статуса службы) и наверное еще перезапуск
int RestartAudioService()
{
	wil::unique_schandle hSCObject(OpenSCManagerW(nullptr, SERVICES_ACTIVE_DATABASE, SC_MANAGER_ALL_ACCESS));
	if (!hSCObject.is_valid())
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("OpenSCManagerW() error: %s"),
			ParseWindowsError(GetLastError()).c_str()));
		return -1;
	}

	wil::unique_schandle AudioSrvHandle(OpenServiceW(hSCObject.get(), L"AudioSrv", SERVICE_QUERY_STATUS | SERVICE_ENUMERATE_DEPENDENTS | SERVICE_START | SERVICE_STOP));
	if (!AudioSrvHandle.is_valid())
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("OpenServiceW() error: %s"),
			ParseWindowsError(GetLastError()).c_str()));
		return -2;
	}

	//sizeof(_SERVICE_STATUS);
	//sizeof(SERVICE_STATUS_PROCESS);
	SERVICE_STATUS_PROCESS ssp{};
	if (DWORD BytesNeeded; 
		!QueryServiceStatusEx(AudioSrvHandle.get(), SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&ssp), sizeof(ssp), &BytesNeeded))
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("QueryServiceStatusEx() error: %s"),
			ParseWindowsError(GetLastError()).c_str()));
		return -3;
	}

	SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("QueryServiceStatusEx(AudioSrv): { %s }"),
		FormatServiceStatusProcess(ssp).c_str()));

	if (ssp.dwServiceType != SERVICE_WIN32_OWN_PROCESS)
	{
		//TODO: должен быть == SERVICE_WIN32_OWN_PROCESS, если не так, проверить на & SERVICE_WIN32_OWN_PROCESS(если так залогировать),
		//если ничего из этого, то тоже залогировать(такого кстати не должно быть, нужно на этапе установки(а после - последующей перезагрузке) делать отдельным процессом), это для AudioSrv
		//не в Windows 10 кстати это не так, в Windows 10 должно быть обязательно ssp.dwServiceType == SERVICE_WIN32_OWN_PROCESS //https://revertservice.com/10/audiosrv/

		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("dwServiceType != SERVICE_WIN32_OWN_PROCESS && SERVICE_WIN32_OWN_PROCESS == %lu"),
			ssp.dwServiceType));
	}

	[&]() [[msvc::forceinline]] -> void
	{
		SetLastError(0);
		if (DWORD BytesNeeded{}, ServicesReturned{};
			!EnumDependentServicesW(AudioSrvHandle.get(), SERVICE_ACTIVE, nullptr, 0, &BytesNeeded, &ServicesReturned))
		{
			if (const DWORD Error = GetLastError();
				Error != ERROR_MORE_DATA)
			{
				SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("First call EnumDependentServicesW() error: %s"),
					ParseWindowsError(Error).c_str()));
				return;
			}

			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("First BytesNeeded == %lu | ServiceReturned == %lu"),
				BytesNeeded, ServicesReturned));

			std::unique_ptr<uint8_t[]> NeededBytes = std::make_unique_for_overwrite<uint8_t[]>(BytesNeeded);
			LPENUM_SERVICE_STATUSW DependenciesArray = reinterpret_cast<LPENUM_SERVICE_STATUSW>(NeededBytes.get());
			if (EnumDependentServicesW(AudioSrvHandle.get(), SERVICE_ACTIVE, DependenciesArray, BytesNeeded, &BytesNeeded, &ServicesReturned))
			{
				SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("Second BytesNeeded == %lu | ServiceReturned == %lu"),
					BytesNeeded, ServicesReturned));

				std::wstring Result;
				for (size_t i = 0; i < ServicesReturned; i++)
				{
					Result += std::format(L"{{ lpDisplayName: {} lpServiceName: {} ServiceStatus: {} }}",
						DependenciesArray[i].lpDisplayName,
						DependenciesArray[i].lpServiceName,
						FormatServiceStatusProcess(DependenciesArray[i].ServiceStatus));
				}

				SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("!!!!!! Active Dependent Services:{ %s }"),
					Result.c_str()));
			}
			else
			{
				SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("Second call EnumDependentServicesW() error: %s"),
					ParseWindowsError(GetLastError()).c_str()));
			}
		}
	}();    //In Place call lambda function, so that return cannot affect the execution of the RestartAudioService function



	const ULONGLONG StartTime = GetTickCount64();
	if (ssp.dwCurrentState == SERVICE_RUNNING)
	//if (ssp.dwCurrentState != SERVICE_STOPPED)
	{
		if (!ControlService(AudioSrvHandle.get(), SERVICE_CONTROL_STOP, reinterpret_cast<LPSERVICE_STATUS>(&ssp)))
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("ControlService(SERVICE_CONTROL_STOP) error: %s"),
				ParseWindowsError(GetLastError()).c_str()));
			return -4;
		}

		while (ssp.dwCurrentState != SERVICE_STOPPED)
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("Stopping service dwWaitHint: %lu"),
				ssp.dwWaitHint));
#undef min
#undef max
			Sleep(std::min(ssp.dwWaitHint, 100ul));
			if (DWORD BytesNeeded{};
				!QueryServiceStatusEx(AudioSrvHandle.get(), SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&ssp), sizeof(ssp), &BytesNeeded))
			{
				SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("Third(in stopping service code) QueryServiceStatusEx error: %s"),
					ParseWindowsError(GetLastError()).c_str()));
				return -5;
			}

			if (GetTickCount64() - StartTime > std::max(10'000ul /*milliseconds*/ , ssp.dwWaitHint))
			{
				SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("the set time for waiting for the service to stop has expired")));
				return -6;
			}
		}

		if (!StartServiceW(AudioSrvHandle.get(), 0, nullptr))
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("StartServiceW() error: %s"),
				ParseWindowsError(GetLastError()).c_str()));
			return -7;
		}
		//TODO: wait to start
	}

	return 0;    //SUCCESS
}


int SetFixMonoValue(const std::wstring& AudioDeviceGUID)
{
	if (AudioDeviceGUID.empty())
	{
		return -2;
	}

	const std::wstring BaseSoftwarePathGUID = std::format(L"SOFTWARE\\{}\\{}", APOName, AudioDeviceGUID);
	winreg::RegKey BaseSoftwareKey;
	if (auto BaseSoftwareKeyValue = BaseSoftwareKey.TryOpen(HKEY_LOCAL_MACHINE, BaseSoftwarePathGUID, GENERIC_READ | KEY_QUERY_VALUE | KEY_SET_VALUE);    //KEY_WOW64_64KEY
		BaseSoftwareKeyValue.IsOk())
	{
		DWORD FixMonoSound;
		if (const auto Code = GetValueFunction(BaseSoftwareKey, L"FixMonoSound", FixMonoSound);
			Code == ERROR_SUCCESS)
		{
			FixMonoSound = !FixMonoSound;

			if (const auto Code1 = SetValueFunction(BaseSoftwareKey, L"FixMonoSound", FixMonoSound);
				Code1 == ERROR_SUCCESS)
			{
				return FixMonoSound;
			}
			else
			{
				SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("Write key and SetValueFunction(%s, FixMonoSound) error: %s"),
					BaseSoftwarePathGUID.c_str(), ParseWindowsError(Code1).c_str()));

				return !FixMonoSound;
			}
		}
		else
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("GetValueFunction(%s, FixMonoSound) error: %s"),
				BaseSoftwarePathGUID.c_str(), ParseWindowsError(Code).c_str()));

			FixMonoSound = 1;    //true
			if (const auto Code1 = SetValueFunction(BaseSoftwareKey, L"FixMonoSound", FixMonoSound);
				Code1 == ERROR_SUCCESS)
			{
				return FixMonoSound;
			}
			else
			{
				SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("Create key and SetValueFunction(%s, FixMonoSound) error: %s"),
					BaseSoftwarePathGUID.c_str(), ParseWindowsError(Code1).c_str()));

				return !FixMonoSound;
			}

			return 0;     //false
		}
	}
	else
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("TryOpen(%s, GENERIC_READ | KEY_QUERY_VALUE) error: %s"),
			BaseSoftwarePathGUID.c_str(), ParseWindowsError(BaseSoftwareKeyValue.Code()).c_str()));

		return 0;     //false
	}

	SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("[REALISATION ERROR] Dead code reached")));
	return 0;     //false
}



int ParseMainArguments(/*int argc, char** argv*/)
{
	int argc = 0;
	wil::unique_hlocal lpMsgBuf(CommandLineToArgvW(GetCommandLineW(), &argc));
	if (!lpMsgBuf.is_valid())
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("CommandLineToArgvW() error: %s"),
			ParseWindowsError(GetLastError()).c_str()));
		return 429487;
	}
	wchar_t** argv = static_cast<LPWSTR*>(lpMsgBuf.get());

	std::wstring Params;
	for (size_t i = 0; i < argc && lpMsgBuf.is_valid(); i++)
	{
		Params += std::format(L"[argv[{}] = {}] ", i, argv[i]);
	}
	SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("ControlAPO.exe getted params: %s"),
		Params.c_str()));

	for (int i = 0; i < argc; i++)
	{
		if (!wcscmp(argv[i], L"--RegisterAPO")
			&& (i < argc - 1) && !wcscmp(argv[i + 1], L"i"))  //install
		{
			i++;

			return !RegisterAPO(true);   //return 0 - succes code, for another programm
		}
		if (!wcscmp(argv[i], L"--RegisterAPO")
			&& (i < argc - 1) && !wcscmp(argv[i + 1], L"u"))  //uninstall
		{
			i++;

			return !RegisterAPO(false);  //return 0 - succes code, for another programm
		}
		if (!wcscmp(argv[i], L"--RestartWindowsAudio"))
		{
			const int ReturnedCode = RestartAudioService();
			return ReturnedCode;
		}
		if (!wcscmp(argv[i], L"--SetFixMonoValue")
			&& (i < argc - 1))
		{
			const std::wregex rgx(L"\\{[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}\\}");  //Regex for extract guid like this: {...}
			std::wsmatch match;
			std::wstring deviceIdSTR(argv[i + 1]);
			if (std::regex_search(deviceIdSTR, match, rgx))
			{
				const int ReturnedCode = SetFixMonoValue(match[0].str());
				return ReturnedCode;
			}

			return -1;   //TODO: Refact all code to custom struct with our error, description and Win error
		}
		if (!wcscmp(argv[i], L"--Install_DeInstall_AudioDevices"))
		{
			const std::wregex rgx(L"\\{[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}\\}");  //Regex for extract guid like this: {...}

			int Succes = 0;
			for (int j = i + 1; j < argc && j < argc - 1; j+=2)
			{
				std::wsmatch match;
				std::wstring deviceIdSTR(argv[j]);

				if (std::regex_search(deviceIdSTR, match, rgx))
				{
					const std::wstring GUID = match[0].str();

					if (!wcscmp(argv[j + 1], L"i"))
					{
						if (const int Status = SetAndCreateRegeditAPODevice(GUID);
							Status != ERROR_SUCCESS)
						{
							SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("SetAndCreateRegeditAPODevice error: %d"),
								Status));
							Succes = Status;
						}
					}
					else if (!wcscmp(argv[j + 1], L"u"))
					{
						if (const int Status = DeleteRegeditAPODevice(GUID);
							Status != ERROR_SUCCESS)
						{
							SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("DeleteRegeditAPODevice error: %d"),
								Status));
							Succes = Status;
						}
					}
					else
					{
						SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("Device: %s, Unknown argument for [--Install_DeInstall_AudioDevices]: %s"),
							argv[j], argv[j + 1]));
					}
				}
				else
				{
					SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("regex_search(%s) error"),
						deviceIdSTR.c_str()));
				}
			}

			return Succes;
		}
	}

	SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("No parsed arguments: %d, Parameters: %s"),
		argc, Params.c_str()));

	return INT_MAX;
}



int main(int argc, char** argv)
{
	SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_INFO(0, TM("ControlAPO Logger Context")));

	//SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), Set_Verbosity(0, EP7TRACE_LEVEL_WARNING));

	if (argc == 1)
	{
		std::cout << "argc: " << argc << '\n';
		std::cout << "argv[0]: " << argv[0] << '\n';

		std::cout << "\nThere is no need to run this .exe file manually.\n"
			"This .exe file is needed to interact with another application.\n"
			"Please, close this application.\n\n";

		system("PAUSE");
		return 0;
	}

	//return ParseMainArguments(argc, argv);
	return ParseMainArguments();
}