#include "common.h"

#include "../Common/Singleton.hpp"
#include "../Common/WindowsUtil.h"

#include <wil/com.h>
#include <wil/resource.h>

//template<bool UsedInAPOContext>
bool APOIsWritedInRegedit(const std::wstring& AudioDeviceGUID)
{
	const std::wstring DeviceGUIDPathFxProperties = std::format(L"{}\\{}\\{}", MicrophoneDevicesPath.RegeditPath, AudioDeviceGUID, L"FxProperties");

	winreg::RegKey KeyHandle;
	if (auto KeyHandleReturnedValue = KeyHandle.TryOpen(MicrophoneDevicesPath.RegeditKey, DeviceGUIDPathFxProperties, GENERIC_READ);  //KEY_WOW64_64KEY
		KeyHandleReturnedValue.Failed())
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("[%s] TryOpen with GENERIC_READ error: %s"),
			AudioDeviceGUID.c_str(), ParseWindowsError(KeyHandleReturnedValue.Code()).c_str()));
		return false;
	}

	for (const wchar_t* EffectGUIDString : { APOGFXEffectGUID, APOMFXEffectGUID, APOMultiMFXEffectGUID })
	{
		//TODO: удалить потом
		if (wcslen(EffectGUIDString) > wcslen(APOMultiMFXEffectGUID))
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("wcslen(value) > wcslen(APOMultiMFXEffectGuid) == true")));

			return false;
		}

		//Второе условие - подстраховка на случай если константые указатели вдруг будут разными по значению
		if (EffectGUIDString != APOMultiMFXEffectGUID || !std::wcsstr(EffectGUIDString, APOMultiMFXEffectGUID))     //REG_SZ
		//if (value != APOMultiMFXEffectGuid || std::wstring(value).find(std::wstring(APOMultiMFXEffectGuid)) != std::wstring::npos)     //REG_SZ
		{
			//TODO: refactor to GetValueFunction
			const auto FXGUIDRegeditString = KeyHandle.TryGetStringValue(EffectGUIDString);
			if (!FXGUIDRegeditString.IsValid())
				//if (!FXGUIDString)
			{
				SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("[%s] TryGetStringValue: %s, for FX GUID: %s"),
					AudioDeviceGUID.c_str(), ParseWindowsError(FXGUIDRegeditString.GetError().Code()).c_str(), EffectGUIDString));
				continue;
			}

			//TODO: refactor to GUIDIsEqual
			CLSID CurrentGUID;
			if (const HRESULT hrs = CLSIDFromString(FXGUIDRegeditString.GetValue().c_str(), &CurrentGUID);
				SUCCEEDED(hrs))
			{
				if (CurrentGUID == APO_GUID)
				{
					return true;
				}

				continue;
			}
			else
			{
				SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("CLSIDFromString(%s) error: %s"),
					FXGUIDRegeditString.GetValue().c_str(), ParseWindowsError(hrs).c_str()));
			}

			if (FXGUIDRegeditString.GetValue().find(APOGUID_STRING_WITH_CURLY_BRACES) != std::wstring::npos)
			{
				return true;  //our APO in GFX/MFX mode finded as installed on AudioDeviceGUID microphone
			}
		}
		else //REG_MULTI_SZ case
		{
			//TODO: refactor to GetValueFunction
			const auto FXGUIDRegeditStrings = KeyHandle.TryGetMultiStringValue(EffectGUIDString);
			if (!FXGUIDRegeditStrings.IsValid())
			{
				SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("[%s] TryGetMultiStringValue: %s, for FX GUID: %s"),
					AudioDeviceGUID.c_str(), ParseWindowsError(FXGUIDRegeditStrings.GetError().Code()).c_str(), EffectGUIDString));
				continue;
			}

			for (const std::wstring& CycleSTR : FXGUIDRegeditStrings.GetValue())
			{
				//TODO: refactor to GUIDIsEqual
				CLSID CurrentGUID;
				if (const HRESULT hrs = CLSIDFromString(CycleSTR.c_str(), &CurrentGUID);
					SUCCEEDED(hrs))
				{
					if (CurrentGUID == APO_GUID)
					{
						return true;
					}

					continue;
				}
				else
				{
					SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("CLSIDFromString(%s) error: %s"),
						CycleSTR.c_str(), ParseWindowsError(hrs).c_str()));
				}

				if (CycleSTR.find(APOGUID_STRING_WITH_CURLY_BRACES) != std::wstring::npos)
				{
					return true;  //our APO in MultiMFX mode finded as installed on AudioDeviceGUID microphone
				}
				else
				{
					SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("[%s] GFX contains other APO effect GUID: %s"),
						AudioDeviceGUID.c_str(), CycleSTR.c_str()));
				}
			}
		}
	}

	return false;
}

AudioDevice::AudioDevice()
{

}

AudioDevice::~AudioDevice()
{

}


std::vector<AudioDevice> GetAllMicrohponeDevices()
{
	std::vector<AudioDevice> AudioDevices;

	if (const HRESULT hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);  //COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE
		FAILED(hr))
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("CoInitializeEx error: %s"),
			ParseWindowsError(hr).c_str()));
		return AudioDevices;
	}
	auto CoUnInitializeScopeExit = wil::scope_exit([] { ::CoUninitialize(); });

	wil::com_ptr_nothrow<IMMDeviceEnumerator> pEnumerator;
	if (const HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&pEnumerator));
		FAILED(hr))
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("CoCreateInstance(__uuidof(MMDeviceEnumerator)) error: %s"),
			ParseWindowsError(hr).c_str()));
		return AudioDevices;
	}

	wil::com_ptr_nothrow<IMMDeviceCollection> pDeviceCollection;
	if (const HRESULT hr = pEnumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &pDeviceCollection);
		FAILED(hr))
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("EnumAudioEndpoints(eCapture) error: %s"),
			ParseWindowsError(hr).c_str()));
		return AudioDevices;
	}

	std::wstring DeafultDeviceGUID;
	{
		wil::com_ptr_nothrow<IMMDevice> pDefaultAudioDevice;
		if (HRESULT hr = pEnumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &pDefaultAudioDevice);
			SUCCEEDED(hr))
		{
			wil::unique_cotaskmem_ptr<wchar_t[]> DeviceId;
			if (hr = pDefaultAudioDevice->GetId(wil::out_param(DeviceId));
				SUCCEEDED(hr))
			{
				DeafultDeviceGUID = DeviceId.get();
			}
			else
			{
				SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("pDefaultAudioDevice->GetId() error: %s"),
					ParseWindowsError(hr).c_str()));
				//DON'T return
			}
		}
		else
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("pEnumerator->GetDefaultAudioEndpoint(eCapture, eConsole) error: %s"),
				ParseWindowsError(hr).c_str()));
			//DON'T return
		}
	}

	UINT deviceCount;
	if (const HRESULT hr = pDeviceCollection->GetCount(&deviceCount);
		FAILED(hr))
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("pDeviceCollection->GetCount() error: %s"),
			ParseWindowsError(hr).c_str()));
		return AudioDevices;
	}

	for (UINT DeviceIndex = 0; DeviceIndex < deviceCount; DeviceIndex++)
	{
		wil::com_ptr_nothrow<IMMDevice> pDevice;
		if (const HRESULT hr = pDeviceCollection->Item(DeviceIndex, &pDevice);
			FAILED(hr))
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("pDeviceCollection->Item() error: %s"),
				ParseWindowsError(hr).c_str()));
			continue;
		}

		wil::unique_cotaskmem_ptr<wchar_t[]> DeviceId;
		if (const HRESULT hr = pDevice->GetId(wil::out_param(DeviceId));
			FAILED(hr))
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("pDevice->GetId() error: %s"),
				ParseWindowsError(hr).c_str()));
			continue;
		}

		//TODO: вынести регулярку в GlobalConstValues
		std::wregex rgx(L"\\{[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}\\}");  //Regex for extract guid like this: {...}
		std::wsmatch match;
		std::wstring deviceIdSTR(DeviceId.get());
		std::wstring ParsedGUID;

		if (std::regex_search(deviceIdSTR, match, rgx))
		{
			ParsedGUID = match[0].str();
		}
		else
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("regex_search(%s) error"),
				deviceIdSTR.c_str()));
			continue;
		}

		wil::com_ptr_nothrow<IPropertyStore> pPropertyStore;
		if (const HRESULT hr = pDevice->OpenPropertyStore(STGM_READ, &pPropertyStore);
			FAILED(hr))
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("pDevice->OpenPropertyStore() error: %s"),
				ParseWindowsError(hr).c_str()));
			continue;
		}

		wil::unique_prop_variant friendlyName;
		//PROPERTYKEY PKEY_AUDIO_ENDPOINT_FORM_FACTOR = { 0x1da5d803, 0xd492, 0x4edd, {0x8c, 0x23, 0xe0, 0xc0, 0xff, 0xee, 0x7f, 0x0e}, 0 };
		//PROPERTYKEY PKEY_AudioEndpoint_Disable_SysFx = { 0x1da5d803, 0xd492, 0x4edd, {0x8c, 0x23, 0xe0, 0xc0, 0xff, 0xee, 0x7f, 0x0e}, 5 };
		if (const HRESULT hr = pPropertyStore->GetValue(PKEY_Device_FriendlyName/*PKEY_AUDIO_ENDPOINT_FORM_FACTOR*/, &friendlyName);
			FAILED(hr))
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("pPropertyStore->GetValue() error: %s"),
				ParseWindowsError(hr).c_str()));
			continue;
		}

		std::wstring DeviceName;

		if (friendlyName.vt == VT_LPWSTR /*&& friendlyName.vt != VT_EMPTY*/)
		{
			DeviceName = friendlyName.pwszVal;
		}
		else
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("friendlyName.vt != VT_LPWSTR for PKEY_Device_FriendlyName error: friendlyName.vt == %lu"),
				static_cast<DWORD>(friendlyName.vt)));
			continue;
		}

		AudioDevice AudioDevice;
		AudioDevice.DeviceGUID = ParsedGUID;
		AudioDevice.IsDefaultDevice = (DeafultDeviceGUID == deviceIdSTR);  //TODO: FIXME
		AudioDevice.DeviceName = DeviceName;
		AudioDevice.IsWritedInSoftwareRegedit = DeviceWritedInSoftwareRegedit(ParsedGUID);
		AudioDevice.IsWritedInAPORegedit = APOIsWritedInRegedit(ParsedGUID);
		AudioDevice.IsNeedUpdate = AudioDevice.IsWritedInSoftwareRegedit && !AudioDevice.IsWritedInAPORegedit;         //Здесь тоже использовать NeedFix
		AudioDevice.IsInstalledSuccessfully = AudioDevice.IsWritedInSoftwareRegedit && AudioDevice.IsWritedInAPORegedit;   //Добавить что то типа NeedFix
		//NeedFix это если например у девайса есть PKEY_AUDIO_ENDPOINT_DISABLE_SYS_FX или !RegisterAPO или !DisableProtectedAudioDGIs
		//А может вынести последние две в отдельный функционал(так как не относятся к аудиоустройству - RegisterAPO и DisableProtectedAudioDGIs)

		AudioDevice.IsNeedUpdateCopy = AudioDevice.IsNeedUpdate;
		AudioDevice.IsNeedInstalling = AudioDevice.IsInstalledSuccessfully;

		AudioDevices.push_back(AudioDevice);  //TODO: maybe std::move ?
	}

	return AudioDevices;
}

bool DisableProtectedAudioDGIs(DWORD Value)
{
	winreg::RegKey WindowsAudioKey;
	if (auto WindowsAudioKeyReturnedValue = WindowsAudioKey.TryOpen(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Audio", KEY_QUERY_VALUE);  //KEY_WOW64_64KEY
		!WindowsAudioKeyReturnedValue.Failed())
	{
		DWORD DisableProtectedAudioDG;
		if (const auto Code = GetValueFunction(WindowsAudioKey, L"DisableProtectedAudioDG", DisableProtectedAudioDG);
			Code == ERROR_SUCCESS)
		{
			return DisableProtectedAudioDG == Value;
		}
		else
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_ERROR(0, TM("GetValueFunction(DisableProtectedAudioDG) error: %s"),
				ParseWindowsError(Code).c_str()));
		}
	}
	else
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("TryOpen error: %s"),
			ParseWindowsError(WindowsAudioKeyReturnedValue.Code()).c_str()));
	}

	return false;
}


bool DeviceWritedInSoftwareRegedit(const std::wstring& DeviceGUID)
{
	const std::wstring BaseSoftwarePath = std::format(L"SOFTWARE\\{}", APOName);

	winreg::RegKey KeyHandleBaseSoftware;
	if (auto KeyHandleReturnedValue = KeyHandleBaseSoftware.TryOpen(HKEY_LOCAL_MACHINE, BaseSoftwarePath, KEY_QUERY_VALUE);  //KEY_WOW64_64KEY
		KeyHandleReturnedValue.Failed())
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("TryOpen(%s) error: %s"),
			BaseSoftwarePath.c_str(), ParseWindowsError(KeyHandleReturnedValue.Code()).c_str()));
		return /*KeyHandleReturnedValue.Code()*/false;
	}

	//CapturedDevices
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

		return std::any_of(CapturedDevices.begin(), CapturedDevices.end(),
			[&](const std::wstring& GUIDValue)
			{
				//TODO: refactor to GUIDIsEqual
				CLSID CurrentGUID;
				if (const HRESULT hrs = CLSIDFromString(GUIDValue.c_str(), &CurrentGUID);
					SUCCEEDED(hrs) && SUCCEEDED(HRConvert))
				{
					return CurrentGUID == DeviceGUIDConverted;
				}
				else
				{
					SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("[%s\\CapturedDevices], CLSIDFromString(%s) error: %s"),
						BaseSoftwarePath.c_str(), GUIDValue.c_str(), ParseWindowsError(hrs).c_str()));
				}

				return GUIDValue.find(DeviceGUID) != std::wstring::npos;
			});
	}
	else
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_ERROR(0, TM("GetValueFunction(CapturedDevices) error: %s"),
			ParseWindowsError(Code).c_str()));

		return /*Code*/false;
	}

	SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("Potentially dead code achieved")));
	return false;
}