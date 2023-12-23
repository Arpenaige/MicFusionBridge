#include "stdafx.h"

#include <string>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <locale.h>

#include <chrono>
#include <fstream>
#include <vector>
#include <mutex>
#include <random>

#include "MicFusionBridge.h"
#include "ClassFactory.h"

static HINSTANCE hModule;

BOOL WINAPI DllMain(HINSTANCE hModule, DWORD dwReason, void* lpReserved)
{
	if (dwReason == DLL_PROCESS_ATTACH)
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("DLL_PROCESS_ATTACH(%p, %lu, %p)"),
			hModule, dwReason, lpReserved));

		::hModule = hModule;

		const bool isDisabledThreadLibraryCalls = DisableThreadLibraryCalls(hModule);
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_INFO(0, TM("DisableThreadLibraryCalls: %s"),
			isDisabledThreadLibraryCalls ? L"True" : L"False"));

		g_pEvents = new APOEvents;  //TODO: или call_once может заиспользовать да и вообще перевести на unique_ptr думаю лучше
	}
	else if (dwReason == DLL_PROCESS_DETACH)
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("DLL_PROCESS_DETACH(%p, %lu, %p)"),
			hModule, dwReason, lpReserved));

		if (lpReserved)
		{

		}

		delete g_pEvents;
		g_pEvents = nullptr;

		//delete logger;
		//logger = nullptr;
	}
	else if (dwReason == DLL_THREAD_ATTACH)
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("DLL_THREAD_ATTACH(%p, %lu, %p)"),
			hModule, dwReason, lpReserved));
	}
	else if (dwReason == DLL_THREAD_DETACH)
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("DLL_THREAD_DETACH(%p, %lu, %p)"),
			hModule, dwReason, lpReserved));
	}
	else
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("DLL_UNKNOWN_(AT/DE)TACH(%p, %lu, %p)"),
			hModule, dwReason, lpReserved));
	}

	P7_Flush();

	return TRUE;
}

STDAPI DllCanUnloadNow()
{
	SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("DllCanUnloadNow")));

	SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_INFO(0, TM("instCount: %li lockCount:%li"),
		MicFusionBridge::instCount, ClassFactory::lockCount));

	P7_Flush();

	if (MicFusionBridge::instCount == 0 && ClassFactory::lockCount == 0)  //TODO: maybe g_pEvents == nullptr ????? а может не надо(потому что только потом вызовется DllMain с DLL_PROCESS_DETACH),
		//надо изучить бы этот вопрос
	{
		if (!g_pEvents)
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("DllCanUnloadNow CRITICAL because, g_pEvents == %p"),
				g_pEvents));
		}

		P7_Flush();

		return S_OK;
	}
	else
	{
		return S_FALSE;
	}

	//SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("DllCanUnloadNow else branch, g_pEvents = %p"), g_pEvents));
}

STDAPI DllGetClassObject(const CLSID& clsid, const IID& iid, void** ppv)
{
	SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("DllGetClassObject")));

	LogGUID(clsid);
	LogGUID(iid);

	if (clsid != __uuidof(MicFusionBridge))
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_WARNING(0, TM("clsid != __uuidof(MicFusionBridge)")));
		return CLASS_E_CLASSNOTAVAILABLE;
	}

	ClassFactory* factory = new ClassFactory();
	if (factory == NULL)
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_ERROR(0, TM("new factory return NULL")));
		return E_OUTOFMEMORY;
	}

	HRESULT hr = factory->QueryInterface(iid, ppv);
	if (FAILED(hr))
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_ERROR(0, TM("QueryInterface error parsed: %s"),
			ParseWindowsError(hr).c_str()));
	}

	factory->Release();

	P7_Flush();

	return hr;
}

STDAPI DllRegisterServer()
{
	SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("DllRegisterServer")));

	P7_Flush();

	wchar_t filename[1024];
	GetModuleFileNameW(hModule, filename, sizeof(filename) / sizeof(wchar_t));

	HRESULT hr = RegisterAPO(MicFusionBridge::regProperties);
	if (FAILED(hr))
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_ERROR(0, TM("RegisterAPO error parsed: %s"),
			ParseWindowsError(hr).c_str()));

		P7_Flush();

		UnregisterAPO(__uuidof(MicFusionBridge));
		return hr;
	}

	wchar_t* guid;
	hr = StringFromCLSID(__uuidof(MicFusionBridge), &guid);
	if (FAILED(hr))
	{
		//TODO: не должно быть ошибки, но лучше проверить в логах
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_ERROR(0, TM("StringFromCLSID error parsed: %s"),
			ParseWindowsError(hr).c_str()));

		P7_Flush();
	}
	std::wstring guidString(guid);
	CoTaskMemFree(guid);

	HKEY keyHandle;
	RegCreateKeyExW(HKEY_LOCAL_MACHINE, (L"SOFTWARE\\Classes\\CLSID\\" + guidString).c_str(), 0, NULL, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, NULL, &keyHandle, NULL);
	//const wchar_t* value = L"MicFusionBridge";
	const wchar_t* value = APOName;
	RegSetValueExW(keyHandle, L"", 0, REG_SZ, (const BYTE*)value, (DWORD)((wcslen(value) + 1) * sizeof(wchar_t)));
	RegCloseKey(keyHandle);

	RegCreateKeyExW(HKEY_LOCAL_MACHINE, (L"SOFTWARE\\Classes\\CLSID\\" + guidString + L"\\InprocServer32").c_str(), 0, NULL, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, NULL, &keyHandle, NULL);
	value = filename;
	RegSetValueExW(keyHandle, L"", 0, REG_SZ, (const BYTE*)value, (DWORD)((wcslen(value) + 1) * sizeof(wchar_t)));
	value = L"Both";
	RegSetValueExW(keyHandle, L"ThreadingModel", 0, REG_SZ, (const BYTE*)value, (DWORD)((wcslen(value) + 1) * sizeof(wchar_t)));
	RegCloseKey(keyHandle);


	//static std::random_device rd;
	//static std::mt19937_64 gen(rd());
	//static std::uniform_int_distribution<DWORD> dist(0, UINT32_MAX);

	//RegCreateKeyExW(HKEY_CURRENT_USER, L"SOSIXYU", 0, NULL, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, NULL, &keyHandle, NULL);
	//const DWORD value1 = dist(gen);
	//RegSetValueExW(keyHandle, L"Random", 0, REG_DWORD, (BYTE*)&value1, sizeof(DWORD));
	//RegCloseKey(keyHandle);

	P7_Flush();

	return S_OK;
}

STDAPI DllUnregisterServer()
{
	SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("DllUnregisterServer")));

	wchar_t* guid;
	StringFromCLSID(__uuidof(MicFusionBridge), &guid);
	std::wstring guidString(guid);
	CoTaskMemFree(guid);

	RegDeleteKeyExW(HKEY_LOCAL_MACHINE, (L"SOFTWARE\\Classes\\CLSID\\" + guidString + L"\\InprocServer32").c_str(), KEY_WOW64_64KEY, 0);
	RegDeleteKeyExW(HKEY_LOCAL_MACHINE, (L"SOFTWARE\\Classes\\CLSID\\" + guidString).c_str(), KEY_WOW64_64KEY, 0);

	HRESULT hr = UnregisterAPO(__uuidof(MicFusionBridge));

	P7_Flush();

	return hr;
}
