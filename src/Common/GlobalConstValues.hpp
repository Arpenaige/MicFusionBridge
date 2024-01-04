#pragma once


struct RegeditValuesIncapsulate
{
	const wchar_t* RegeditPath;
	const HKEY RegeditKey;
};

//https://stackoverflow.com/questions/10731428/how-to-define-macro-to-convert-concatenated-char-string-to-wchar-t-string-in-c
#define _L(x)  __L(x)
#define __L(x) L ## x

//#define g_ProjectName "";
#define g_ProjectName "MicFusionBridge"
//#define g_WProjectName L"MicFusionBridge";
#define g_WProjectName _L(g_ProjectName)

#define g_WProjectAPOGUID L"{A0A97884-B1A0-466E-8C6D-69D71016BFAA}"


constexpr const wchar_t* APOGUID_STRING_WITHOUT_CURLY_BRACES = L"A0A97884-B1A0-466E-8C6D-69D71016BFAA";
constexpr const wchar_t* APOGUID_STRING_WITH_CURLY_BRACES = g_WProjectAPOGUID;
constexpr GUID APO_GUID = { 0xA0A97884, 0xB1A0, 0x466E, {0x8C, 0x6D, 0x69, 0xD7, 0x10, 0x16, 0xBF, 0xAA} };



//constexpr const wchar_t* APOName = L"";
//constexpr const wchar_t* APOName = L"MicFusionBridge";
constexpr const wchar_t* APOName = g_WProjectName;

//constexpr const wchar_t* DllName = L"MinimalAPO.dll";
//constexpr const wchar_t* DllName = L"MicFusionBridge.dll";
constexpr const wchar_t* DllName = g_WProjectName L".dll";

constexpr const wchar_t* CreateMappingMutexString = L"Global\\CreateMappingMutex" g_WProjectName g_WProjectAPOGUID;
constexpr const wchar_t* APOInstanceRunningMutexString = L"Global\\APOInstanceRunningMutex" g_WProjectName g_WProjectAPOGUID;

constexpr const wchar_t* MessageEmptiedEvent = L"Global\\MessageEmptiedEvent" g_WProjectName g_WProjectAPOGUID;
constexpr const wchar_t* MessageSentEvent = L"Global\\MessageSentEvent" g_WProjectName g_WProjectAPOGUID;

constexpr const wchar_t* MappingObjectForEventAPOString = L"Global\\MappingObjectForEventAPO" g_WProjectName g_WProjectAPOGUID;
//constexpr DWORD MappingObjectForEventAPOBufferSize = 4096 * 4;
constexpr DWORD MappingObjectForEventAPOBufferSize = 1024 * 1024;//1 Mb buffer

//// {EACD2258-FCAC-4FF4-B36D-419E924A6D79}
//const GUID EQUALIZERAPO_PRE_MIX_GUID = { 0xeacd2258, 0xfcac, 0x4ff4, {0xb3, 0x6d, 0x41, 0x9e, 0x92, 0x4a, 0x6d, 0x79} };
//// {EC1CC9CE-FAED-4822-828A-82A81A6F018F}
//const GUID EQUALIZERAPO_POST_MIX_GUID = { 0xec1cc9ce, 0xfaed, 0x4822, {0x82, 0x8a, 0x82, 0xa8, 0x1a, 0x6f, 0x01, 0x8f} };

//WARNING: FOR VST INTERNALY USE
constexpr const wchar_t* OneInstanceAppMutexString = L"Global\\OneInstanceMutex" g_WProjectName g_WProjectAPOGUID;




//bool IsGUIDEQual(const std::wstring& GUIDString, const GUID& GUIDValue)
//{
//	CLSID GUIDConverted;
//	if (const HRESULT hr = CLSIDFromString(GUIDString.c_str(), &GUIDConverted);
//		SUCCEEDED(hr))
//	{
//		return GUIDConverted == GUIDValue;
//	}
//	else
//	{
//		std::cout << "Error of CLSIDFromString HRESULT = " << hr << '\n';
//	}
//
//	return false;
//}
//std::cout << "IsGUIDEQual(): " << IsGUIDEQual(APOGuid, APO_GUID) << '\n';
//If generated new GUID, check if it is equal, by calling function higher, output should be is 1(or true)

////constexpr const wchar_t* APOGuid = L"{0129658B-8ED4-47E7-BFA5-E2933B128766}";
//constexpr const wchar_t* APOGuid = L"{A0A97884-B1A0-466E-8C6D-69D71016BFAA}";
////constexpr GUID APO_GUID = { 0x0129658B, 0x8ED4, 0x47E7, {0xBF, 0xA5, 0xE2, 0x93, 0x3B, 0x12, 0x87, 0x66} };
//constexpr GUID APO_GUID = { 0x0129658B, 0x8ED4, 0x47E7, {0xBF, 0xA5, 0xE2, 0x93, 0x3B, 0x12, 0x87, 0x66} };
////TODO: может поменять на такое ?? __uuidof(MinimalAPO)





//APO REGEDIT Path
//constexpr const wchar_t* CLSIDPath = L"CLSID";									//HKEY_CLASSES_ROOT
//constexpr const wchar_t* APORegPath = L"AudioEngine\\AudioProcessingObjects";   //HKEY_CLASSES_ROOT

const RegeditValuesIncapsulate CLSIDPath{ .RegeditPath = L"CLSID", .RegeditKey = HKEY_CLASSES_ROOT };
const RegeditValuesIncapsulate APORegPath{ .RegeditPath = L"AudioEngine\\AudioProcessingObjects", .RegeditKey = HKEY_CLASSES_ROOT };

//constexpr const wchar_t* MicrophoneDevicesPath = L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\MMDevices\\Audio\\Capture";
const RegeditValuesIncapsulate MicrophoneDevicesPath{ .RegeditPath = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\MMDevices\\Audio\\Capture", .RegeditKey = HKEY_LOCAL_MACHINE };



constexpr const wchar_t* APOEffectsBaseGuid = L"{D04E05A6-594B-4fb6-A80D-01AF5EED7D1D},";
constexpr const wchar_t* APOGFXEffectGUID   = L"{D04E05A6-594B-4fb6-A80D-01AF5EED7D1D},2";
constexpr const wchar_t* APOMFXEffectGUID   = L"{D04E05A6-594B-4fb6-A80D-01AF5EED7D1D},6";
constexpr const wchar_t* APOMultiMFXEffectGUID   = L"{D04E05A6-594B-4fb6-A80D-01AF5EED7D1D},14";
//constexpr const wchar_t* APOEFXEffectGuid   = L"{D04E05A6-594B-4fb6-A80D-01AF5EED7D1D},7";

constexpr const wchar_t* APOSupportMFXEffectGUID = L"{D3993A3F-99C2-4402-B5EC-A92A0367664B},6";
//constexpr const wchar_t* AUDIO_SIGNALPROCESSINGMODE_DEFAULT = L"{C18E2F7E-933D-4965-B7D1-1EEF228D2AF3}";
constexpr const wchar_t* AUDIO_SIGNALPROCESSINGMODE_DEFAULT_STRING = L"{C18E2F7E-933D-4965-B7D1-1EEF228D2AF3}";

constexpr const wchar_t* GUIDEmpty = L"{00000000-0000-0000-0000-000000000000}";


constexpr const wchar_t* PKEY_AUDIO_ENDPOINT_DISABLE_SYS_FX = L"{1DA5D803-D492-4EDD-8C23-E0C0FFEE7F0E},5";



#undef _L
#undef __L