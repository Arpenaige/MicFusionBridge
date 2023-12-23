#include "APOManager.h"

#include "Singleton.hpp"
#include "WindowsUtil.h"
#include "Common.h"

#include <intrin.h>

#include <wil/com.h>
#include <wil/resource.h>


//Требуются права админа(будут запрошены окном)
int ManageAPO(ManageAPOParams MAPOE, std::wstring AddedParameters)
{
    auto GetControlAPOExeFile = []() -> std::wstring
    {
        if (const std::wstring CurrentPath = std::format(L"C:\\Windows\\System32\\{}", L"MicFusionBridgeControl.exe");
            GetFileAttributesW(CurrentPath.c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            return CurrentPath;
        }

        //WARNING: в данный момент не реализовано(скорее всего будет добавлено вместе с установщиком)
        const std::wstring BaseSoftwarePath = std::format(L"SOFTWARE\\{}", APOName);
        winreg::RegKey KeyHandleBaseSoftware;
        if (auto KeyHandleReturnedValue = KeyHandleBaseSoftware.TryOpen(HKEY_LOCAL_MACHINE, BaseSoftwarePath, KEY_QUERY_VALUE);  //KEY_WOW64_64KEY
            !KeyHandleReturnedValue.Failed())
        {
            std::wstring MicFusionBridgeControlPath;
            if (const auto Code = GetValueFunction(KeyHandleBaseSoftware, L"MicFusionBridgeControlPath", MicFusionBridgeControlPath);
                Code == ERROR_SUCCESS)
            {
                return MicFusionBridgeControlPath;
            }
            else
            {
                SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("GetValueFunction(MicFusionBridgeControlPath) error: %s"),
                    ParseWindowsError(Code).c_str()));
            }
        }
        else
        {
            SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("TryOpen(%s) error: %s"),
                BaseSoftwarePath.c_str(), ParseWindowsError(KeyHandleReturnedValue.Code()).c_str()));
        }

        return L"";
    };

    std::wstring ControlAPOExeFilePath = GetControlAPOExeFile();
    if (ControlAPOExeFilePath.empty())
    {
        return INT_MAX;
    }

	SHELLEXECUTEINFOW SEW;
	memset(&SEW, 0, sizeof(SEW));

	SEW.cbSize = sizeof(SEW);
	SEW.fMask = SEE_MASK_NOCLOSEPROCESS;
	SEW.hwnd = nullptr;
	SEW.lpVerb = L"runas";
	SEW.lpFile = ControlAPOExeFilePath.c_str();   //TODO: Resolve this, когда перейду на инсталлятор(мб через реестр или еще как то)
	SEW.nShow = SW_HIDE/*SW_SHOW*/;

	switch (MAPOE)
	{
	case ManageAPOParams::RegisterAPO:
		SEW.lpParameters = L"--RegisterAPO i";
		break;
	case ManageAPOParams::UnRegisterAPO:
		SEW.lpParameters = L"--RegisterAPO u";
		break;
    case ManageAPOParams::RestartWindowsAudio:
        SEW.lpParameters = L"--RestartWindowsAudio";
        break;
    case ManageAPOParams::Install_DeInstall_AudioDevices:
        AddedParameters.insert(0, L"--Install_DeInstall_AudioDevices ");
        SEW.lpParameters = AddedParameters.c_str();
        break;
    case ManageAPOParams::SetFixMonoValue:
        AddedParameters.insert(0, L"--SetFixMonoValue ");
        SEW.lpParameters = AddedParameters.c_str();
        break;
	default:
		SEW.lpParameters = L"";
		break;
	}

	if (ShellExecuteExW(&SEW))
	{
		wil::unique_handle ProcessHandle;
		if (SEW.hProcess)
		{
			ProcessHandle.reset(SEW.hProcess);

			if (WaitForSingleObject(ProcessHandle.get(), INFINITE) == WAIT_FAILED)
			{
                SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("WaitForSingleObject error: %s"),
					ParseWindowsError(GetLastError()).c_str()));
				return INT_MIN;
			}

			DWORD ExitCode;
			if (GetExitCodeProcess(ProcessHandle.get(), &ExitCode))
			{
				return ExitCode;
			}
			else
			{
                SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("GetExitCodeProcess error: %s"),
					ParseWindowsError(GetLastError()).c_str()));
				return INT_MIN;
			}
		}
		else
		{
            SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("hProcess == NULL: %s"),
				ParseWindowsError(GetLastError()).c_str()));
			return INT_MIN;
		}
	}
	else
	{
        SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("ShellExecuteExW error: %s"),
			ParseWindowsError(GetLastError()).c_str()));
		return INT_MIN;
	}

    //TODO: make mapping file 

	return INT_MIN;
}


DWORD WINAPI CaptureMicrophoneFunc(LPVOID lpParam)
{
    DWORD ErrorCodeReturn = 0;

    if (const HRESULT hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);  //COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE
        FAILED(hr))
    {
        SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("CoInitializeEx error: %s"),
            ParseWindowsError(hr).c_str()));

        return 1;
    }
    auto CoUnInitializeScopeExit = wil::scope_exit([] { ::CoUninitialize(); });

    wil::com_ptr_nothrow<IMMDeviceEnumerator> pEnumerator;
    if (const HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&pEnumerator));
        FAILED(hr))
    {
        SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("CoCreateInstance(__uuidof(MMDeviceEnumerator)) error: %s"),
            ParseWindowsError(hr).c_str()));
        return 2;
    }

    wil::com_ptr_nothrow<IMMDevice> pDevice;
    //TODO: если по какой то причине Default Device не доступен, найти любой другой доступный и захватить его
    if (const HRESULT hr = pEnumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &pDevice);
        FAILED(hr))
    {
        SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("GetDefaultAudioEndpoint(eCapture) error: %s"),
            ParseWindowsError(hr).c_str()));
        return 3;
    }

    wil::unique_cotaskmem_ptr<wchar_t[]> pGuid;
    if (const HRESULT hr = pDevice->GetId(wil::out_param(pGuid));
        FAILED(hr))
    {
        SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("pDevice->GetId error: %s"),
            ParseWindowsError(hr).c_str()));
        return 4;
    }
    //std::wcout << "AudioEndpoint GUID: " << pGuid.get() << '\n';

    wil::com_ptr_nothrow<IAudioClient> pAudioClient;
    if (const HRESULT hr = pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, wil::out_param_ptr<void**>(pAudioClient));
        FAILED(hr))
    {
        SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("pDevice->Activate(__uuidof(IAudioClient)) error: %s"),
            ParseWindowsError(hr).c_str()));
        return 5;
    }

    wil::unique_cotaskmem_ptr<WAVEFORMATEX> DeviceWaveFormat;   //on my microphone wFormatTag == WAVE_FORMAT_EXTENSIBLE(65534)
    if (const HRESULT hr = pAudioClient->GetMixFormat(out_param_ptr<WAVEFORMATEX**>(DeviceWaveFormat));
        FAILED(hr))
    {
        SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("pAudioClient->GetMixFormat() error: %s"),
            ParseWindowsError(hr).c_str()));
        return 6;
    }

    static_assert(sizeof(float) == 4);
    if (DeviceWaveFormat.get()->wBitsPerSample / 8 != sizeof(float))
    {
        SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("DeviceWaveFormat BitsPerSample != 32: %s"),
            std::format(L"wBitsPerSample: {}, cbSize: {}, nChannels: {}, nAvgBytesPerSec: {}, nSamplesPerSec: {}, nBlockAlign: {}, wFormatTag: {}",  
                DeviceWaveFormat.get()->wBitsPerSample,
                DeviceWaveFormat.get()->cbSize,
                DeviceWaveFormat.get()->nChannels,
                DeviceWaveFormat.get()->nAvgBytesPerSec,
                DeviceWaveFormat.get()->nSamplesPerSec,
                DeviceWaveFormat.get()->nBlockAlign,
                DeviceWaveFormat.get()->wFormatTag
                ).c_str()));

        return 7;
    }

    if (const HRESULT hr = pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 0, 0, DeviceWaveFormat.get(), nullptr);   //80070057
        FAILED(hr))
    {
        SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("ppAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED) error: %s"),
            ParseWindowsError(hr).c_str()));
        return 8;
    }

    wil::com_ptr_nothrow<IAudioCaptureClient> pAudioCaptureClient;
    //if (const HRESULT hr = pAudioClient->GetService(__uuidof(IAudioCaptureClient), wil::out_param_ptr<void**>(pAudioCaptureClient));
    if (const HRESULT hr = pAudioClient->GetService(IID_PPV_ARGS(&pAudioCaptureClient));
        FAILED(hr))
    {
        SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("pAudioClient->GetService(__uuidof(IAudioCaptureClient)) error: %s"),
            ParseWindowsError(hr).c_str()));
        return 9;
    }

    if (const HRESULT hr = pAudioClient->Start();
        FAILED(hr))
    {
        SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("pAudioClient->Start() error: %s"),
            ParseWindowsError(hr).c_str()));
        return 10;
    }

    std::unique_ptr<float> DumpBuffer = std::make_unique_for_overwrite<float>();
    volatile float* DumpBufferPtr = DumpBuffer.get();    //volatile - so that the compiler doesn't optimize the following code
    UINT32 FramesAvailablePrev{};
    const float OneSamplePerMillisec = 1'000.f / DeviceWaveFormat->nSamplesPerSec;

    while (true)
    {
        BYTE* pData{};
        UINT32 FramesAvailable{};
        DWORD Flags{};

        if (const HRESULT hr = pAudioCaptureClient->GetBuffer(&pData, &FramesAvailable, &Flags, nullptr, nullptr);
            FAILED(hr))
        {
            SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("pAudioCaptureClient->GetBuffer() error: %s"),
                ParseWindowsError(hr).c_str()));
            ErrorCodeReturn = 11;
            break;
        }

        if (!(Flags & AUDCLNT_BUFFERFLAGS_SILENT))
        {
            const float* Samples = reinterpret_cast<const float*>(pData);
            float SamplesSum = 0.f;
            for (size_t i = 0; i < FramesAvailable * DeviceWaveFormat->nChannels; i++)
            {
                SamplesSum += Samples[i] * Samples[i];
            }

            if (FramesAvailable > 0)
            {
                const float RMS = std::sqrt(SamplesSum / FramesAvailable / DeviceWaveFormat->nChannels);
                const float Decibel = 20.f * std::log10(RMS);
                *DumpBufferPtr = Decibel;

                //std::cout << std::format("decibel: {}\n", Decibel);

                FramesAvailablePrev = FramesAvailable;
            }
            else
            {
                Sleep(std::round(OneSamplePerMillisec * FramesAvailablePrev / 2.f));
            }
        }

        if (const HRESULT hr = pAudioCaptureClient->ReleaseBuffer(FramesAvailable);
            FAILED(hr))
        {
            SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("AudioCaptureClient->ReleaseBuffer() error: %s"),
                ParseWindowsError(hr).c_str()));
            ErrorCodeReturn = 12;
            break;
        }

        if (std::atomic_bool* pCloseThread = reinterpret_cast<std::atomic_bool*>(lpParam);
            !pCloseThread || pCloseThread->load(std::memory_order_relaxed))
        {
            ErrorCodeReturn = !pCloseThread ? 13 : 0;
            break;
        }
    }

    if (const HRESULT hr = pAudioClient->Stop();
        FAILED(hr))
    {
        SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("pAudioClient->Stop() error: %s"),
            ParseWindowsError(hr).c_str()));
        ErrorCodeReturn += 14;
    }

    return ErrorCodeReturn;
}


bool APOIsLoaded()
{
    wil::unique_handle APOMutex(OpenMutexW(SYNCHRONIZE, 0, APOInstanceRunningMutexString));
    if (!APOMutex.is_valid())
    {
        SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_ERROR(0, TM("OpenMutexW(SYNCHRONIZE) error: %s, If code == 2, maybe %s do not loaded"),
            ParseWindowsError(GetLastError()).c_str(), DllName));
    }
    return APOMutex.is_valid();
}


int CheckAPOStatus()
{
    bool IsCLSIDRegistered = false, IsApoRegistered = false, IsAPOFileExist = false,
        IsDeviceMismatch = true, IsDeviceConnected = false, AtLeastOneDeviceUsed = false,
        IsDRMProtectionNeeded = true, IsDRMProtectedAudioDGEnabled = false;

    //Capture microphone device to make sure audiodg.exe is loaded accurately.
    std::atomic_bool CloseThread{ false };
    SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_INFO(0, TM("atomic_bool - is Lock free ? : %s"),
        CloseThread.is_lock_free() ? L"true" : L"false"));

    wil::unique_handle CaptureMicrophoneThreadHandle(CreateThread(nullptr, 0, CaptureMicrophoneFunc, &CloseThread, NULL, nullptr));
    if (!CaptureMicrophoneThreadHandle.is_valid())
    {
        SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("CreateThread error: %s"),
            ParseWindowsError(GetLastError()).c_str()));
    }
    //Do Work..
    Sleep(500);
    //Check CLSID and APO pathes for registration, if false - need RegisterAPO(true)
    {
        winreg::RegKey CLSIDKey, APOKey;

        IsCLSIDRegistered = CLSIDKey.TryOpen(CLSIDPath.RegeditKey, std::format(L"{}\\{}", CLSIDPath.RegeditPath, APOGUID_STRING_WITH_CURLY_BRACES), KEY_QUERY_VALUE).IsOk();
        IsApoRegistered = APOKey.TryOpen(APORegPath.RegeditKey, std::format(L"{}\\{}", APORegPath.RegeditPath, APOGUID_STRING_WITH_CURLY_BRACES), KEY_QUERY_VALUE).IsOk();
    }

    //Check if APO driver file is exist in default path C:\Windows\System32
    IsAPOFileExist = GetFileAttributesW(std::format(L"C:\\Windows\\System32\\{}", DllName).c_str()) != INVALID_FILE_ATTRIBUTES;


    //Get captured devices and check registration as APO
    const std::wstring BaseSoftwarePath = std::format(L"SOFTWARE\\{}", APOName);
    winreg::RegKey KeyHandleBaseSoftware;
    if (auto KeyHandleReturnedValue = KeyHandleBaseSoftware.TryOpen(HKEY_LOCAL_MACHINE, BaseSoftwarePath, KEY_QUERY_VALUE);  //KEY_WOW64_64KEY
        !KeyHandleReturnedValue.Failed())
    {
        std::vector<std::wstring> CapturedDevices;
        if (const auto Code = GetValueFunction(KeyHandleBaseSoftware, L"CapturedDevices", CapturedDevices);
            Code == ERROR_SUCCESS)
        {
            size_t WritedDeviceCount = 0;
            for (const std::wstring& AudioDeviceGUID : CapturedDevices)
            {
                WritedDeviceCount += APOIsWritedInRegedit(AudioDeviceGUID);
            }

            AtLeastOneDeviceUsed = WritedDeviceCount > 0;
            IsDeviceMismatch = WritedDeviceCount != CapturedDevices.size();
        }
        else
        {
            SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_ERROR(0, TM("GetValueFunction(CapturedDevices) error: %s"),
                ParseWindowsError(Code).c_str()));
        }
    }
    else
    {
        SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("TryOpen(%s) error: %s"),
            BaseSoftwarePath.c_str(), ParseWindowsError(KeyHandleReturnedValue.Code()).c_str()));

        //return KeyHandleReturnedValue.Code();
    }

    IsDeviceConnected = APOIsLoaded();

    //HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\Audio\DisableProtectedAudioDG
    IsDRMProtectedAudioDGEnabled = DisableProtectedAudioDGIs(1);


    //End Work..
    if (CaptureMicrophoneThreadHandle.is_valid())
    {
        CloseThread.store(true, std::memory_order_relaxed);

        if (const DWORD WaitValue = WaitForSingleObject(CaptureMicrophoneThreadHandle.get(), INFINITE);
            WaitValue != WAIT_OBJECT_0)
        {
            SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("WaitForSingleObject returned %lu, error: %s"),
                WaitValue, ParseWindowsError(GetLastError()).c_str()));
        }

        DWORD ExitCode = 0;
        if (GetExitCodeThread(CaptureMicrophoneThreadHandle.get(), &ExitCode))
        {
            if (ExitCode != 0)
            {
                SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_ERROR(0, TM("Check error for CaptureMicrophoneFunc above")));
            }
        }
        else
        {
            SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("GetExitCodeThread return 0, error: %s"),
                ParseWindowsError(GetLastError()).c_str()));
        }
    }

    //TODO: преобразовать функцию(с входным параметром - bool и выходным параметром std::wstriong) - что бы ее еще можно было 
    //использовать как получения строки с подробными статусами всего(это возможно чуть дальше пре-альфа, но близко к этому)
    //TODO: соотвественно добавить окно в VST о выводе информации

    return !(IsCLSIDRegistered && IsApoRegistered && IsAPOFileExist && !IsDeviceMismatch && IsDeviceConnected && AtLeastOneDeviceUsed && 
        !(IsDRMProtectedAudioDGEnabled ^ IsDRMProtectionNeeded) /**/);
}




APOManager::APOManager() : AudioBuffer(AudioBufferElementCount)
{
    LocalEventSync.reset(CreateEventExW(nullptr, nullptr, CREATE_EVENT_INITIAL_SET, EVENT_ALL_ACCESS));
    if (!LocalEventSync.is_valid())
    {
        SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("LOCAL CreateEventExW() failed, error: %s"),
            ParseWindowsError(GetLastError()).c_str())); P7_Flush();
        //throw std::runtime_error("LocalEventSync create error");
        //TODO: что делать ? кидать исключение ??
    }

    WorkerThreadHandle.reset(CreateThread(nullptr, 0, WorkerThreadStatic, this, 0, nullptr));
    if(!WorkerThreadHandle.is_valid())
    {
        SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("CreateThread() failed, error: %s"),
            ParseWindowsError(GetLastError()).c_str())); P7_Flush();
        //throw std::runtime_error("WorkerThreadHandle create error");
        //TODO: что делать ? кидать исключение ??
    }

    if (WorkerThreadHandle.is_valid())
    {
        if (!SetThreadPriority(WorkerThreadHandle.get(), THREAD_PRIORITY_TIME_CRITICAL))
        {
            SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("SetThreadPriority failed, error: %s"),
                ParseWindowsError(GetLastError()).c_str()));
        }
    }
}

APOManager::~APOManager()
{
    SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("~APOManager")));

    DWORD ExitCode;
    const BOOL ExitCodeIsSuccess = GetExitCodeThread(WorkerThreadHandle.get(), &ExitCode);
    if (IsThreadWorking.load(std::memory_order_seq_cst) || WaitForSingleObject(WorkerThreadHandle.get(), 0) == WAIT_TIMEOUT ||
        (ExitCodeIsSuccess && ExitCode == STILL_ACTIVE))  //TODO: узнать работает ли поток через HANDLE - hMainThread, системным вызовом именно
    {
        TerminateWorkerThread();
    }
}

void APOManager::SendSamplesAsync(float* pAudioBuffer, size_t AudioBufferSize, float SampleRate, int16_t NumberOfChannels, uint16_t BitMask)
{
    if (!AudioBufferSize || !IsAPOConnected.load(std::memory_order_seq_cst))
    {
        return;
    }

    if (const int32_t WritedCount = this->AudioBuffer.WriteQueue(pAudioBuffer, AudioBufferSize);
        WritedCount < 0)
    {
        SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("[VST3] WritedCount < 0 -> no free space to write in queue")));
    }

    //////pack 0-31 bits contains floating point of SampleRate, 32-63 bits contains ChannelCount into one atomic variable SampleRateChannelCountLocal
    ////uint64_t SampleRateChannelCountLocal;
    //////rule following strict aliasing
    ////memcpy(&SampleRateChannelCountLocal, &SampleRate, sizeof(SampleRate));
    ////memcpy(reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(&SampleRateChannelCountLocal) + sizeof(SampleRate)), &NumberOfChannels, sizeof(NumberOfChannels));

    ////SampleRateChannelCount.store(SampleRateChannelCountLocal);



    //pack 0-31 bits contains floating point of SampleRate, 32-47 bits contains ChannelCount, 48-63 bits contains BitMask into one atomic variable SampleRateChannelCountLocal
    uint64_t SampleRateChannelCountBitMaskLocal;
    //rule following strict aliasing
    memcpy(reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(&SampleRateChannelCountBitMaskLocal)), &SampleRate, sizeof(SampleRate));
    memcpy(reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(&SampleRateChannelCountBitMaskLocal) + sizeof(SampleRate)), &NumberOfChannels, sizeof(NumberOfChannels));
    memcpy(reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(&SampleRateChannelCountBitMaskLocal) + sizeof(SampleRate) + sizeof(NumberOfChannels)), &BitMask, sizeof(BitMask));

    SampleRateChannelCountBitMask.store(SampleRateChannelCountBitMaskLocal);

    LogingBooleanWindowsError(SetEvent(LocalEventSync.get()));
}

DWORD WINAPI APOManager::WorkerThreadStatic(LPVOID lpThreadParametr)
{
    APOManager* pThis = static_cast<APOManager*>(lpThreadParametr);

    pThis->IsThreadWorking.store(true, std::memory_order_release);
    auto ResetThreadScopeExit = wil::scope_exit([&pThis] { pThis->IsThreadWorking.store(false, std::memory_order_release); });

    return pThis->WorkerThread();
}

DWORD APOManager::WorkerThread()
{
    while (!StartingClassDestructor.load(std::memory_order_seq_cst))
    {
        static size_t TryingAPOConnectCount = 1;
        auto IncrementScopeExit = wil::scope_exit([] { ++TryingAPOConnectCount; });

        //TODO: capture microphones

        if (const int ConnectStatus = Connect(TryingAPOConnectCount <= 5llu);
            ConnectStatus != 0)
        {
            SafePointerDereference(TryingAPOConnectCount <= 5llu ? Singleton<VariableMainLogger>::GetInstance().GetTracePtr() : nullptr, P7_INFO(0, TM("Attempt Connect to APO status, error: %d"),
                ConnectStatus));
            //IsNeedConnect = true;
        }
        else
        {
            //IsNeedConnect = false;
            break;
        }

        static GlobalSystemTimer<float> gst;

        const float TimeTarget = gst.GetOffsetedSystemTimeFromStart() + 0.5f  /*500 ms*/;
        while (TimeTarget >= gst.GetOffsetedSystemTimeFromStart() &&
            !StartingClassDestructor.load(std::memory_order_seq_cst))
        {
            Sleep(5);
        }

        if (StartingClassDestructor.load(std::memory_order_seq_cst))
        {
            return 1;
        }
    }

    SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_INFO(0, TM("Connect to APO success")));

    {
        //Reset accumulated buffer
        std::vector<float> CurrentAudioBuffer = AudioBuffer.ReadAllQueue();
        if (CurrentAudioBuffer.empty() || CurrentAudioBuffer.size() < 2)  //Tell compiler throw away code
        {
            SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_INFO(0, TM("[WASTE MESSAGE] Empty")));
        }
    }

    IsAPOConnected.store(true, std::memory_order_seq_cst);

    while (!StartingClassDestructor.load(std::memory_order_seq_cst))
    {
        if (const DWORD WaitValue = WaitForSingleObject(LocalEventSync.get(), INFINITE);
            WaitValue != WAIT_OBJECT_0)
        {
            SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("WaitForSingleObject(LocalEventSync) failed, error: %s"),
                ParseWaitForSingleObjectValue(WaitValue).c_str()));
            continue;
        }

        if (StartingClassDestructor.load(std::memory_order_seq_cst))
        {
            SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("StartingClassDestructor first"))); P7_Flush();
            break;
        }

        //Возможно сюда переместить std::vector<float> CurrentAudioBuffer = AudioBuffer.ReadAllQueue(); и если CurrentAudioBuffer.empty() == true, то continue;
        std::vector<float> CurrentAudioBuffer = AudioBuffer.ReadAllQueue();
        //TODO: может здесь получать сначала размер очереди, а потом в самом конце уже очередь
        if (CurrentAudioBuffer.empty())
        {
            continue;
        }

        if (const DWORD WaitValue = WaitForSingleObject(/*hMessageEmptiedEvent*/MessageEmptiedEventHandle.get(), INFINITE);
            WaitValue != WAIT_OBJECT_0)
        {
            SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("WaitForSingleObject(MessageEmptiedEventHandle) failed, error: %s"),
                ParseWaitForSingleObjectValue(WaitValue).c_str()));
            //LogingBooleanWindowsError(SetEvent(MessageSentEventHandle.get()));      //TODO: может заиспользовать wil::scope_exit
            //continue;
        }

        //МЕГАААААА ВАЖНО TODO: тут СКОРЕЕ ВСЕГО, можно будет прочитать сообщение(главное выставить флаг в APOEvents.cpp:324 и выше и там примерно записывать) из ResampleAndWriteSCSPQueue(это будет
        //массив аудиустройств(первое число - размера массива int16_t)(обязательно проверить на выход за пределы(< 0 или > BufferSize в сумме) и что число должно быть меньше чем кол-во установленных аудиоустройств)
        //объект(struct будет таким - первое это float/double LastAPOPorcessCallAndMixSystemGlobalTime, второе GUID - я думаю это будет либо строка либо GUID, в случае строки ограничить размер 39(вроде бы
        //столько требуется на строку с GUID + поправку на sizeof(wchar_t) и того 39 * sizeof(wchar_t) = 78, так что лучше думаю сохранять GUID в классическом виде)))

        //А дальше думаю тут можно хранить, а затем возвращать атомарные переменные через геттеры в этом классе, и уже в интерфейсе(в VST установить SetUpdateTimeHz(60 - что то типа того) 
        //и уже выставлять лампочку зеленую/оранжевую и так далее)
        //Мы же тут можем в целом читать с mutex, ну потому что WorkerThread не будет блокировать ProcessBlock, а в коде обновление интерфейса можно не надолго и заблокировать его
        //Или можно построить схему с try_lock и lock_guard/unique_lock тут надо поискать будет уже

        SendSamplesStructRequest* pThisSendSamplesStruct = static_cast<SendSamplesStructRequest*>(pMappedBuffer.get());

        if (pThisSendSamplesStruct->HasAnswer)
        {
            const int32_t AnswerSize = pThisSendSamplesStruct->AnswerSize;
            if (AnswerSize * sizeof(APOAnswerInfo) + sizeof(SendSamplesStructRequest) < MappingObjectForEventAPOBufferSize)
            {
                APOAnswerInfo* pAPOAnswerInfo = reinterpret_cast<APOAnswerInfo*>(pThisSendSamplesStruct + 1);
                std::vector<APOAnswerInfo> APOAnswersInfoVec;
                for (size_t i = 0; i < AnswerSize; i++)
                {
                    APOAnswerInfo APOInfo;
                    memcpy(&APOInfo, pAPOAnswerInfo, sizeof(APOInfo));
                    APOAnswersInfoVec.push_back(APOInfo);
                }
                APOAnswerDevice.store(APOAnswersInfoVec);
            }
            else
            {
                //error overflow
                SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("passed argument cause read buffer overrun: [HasAnswer: %s, AnswerSize: %d]"),
                    pThisSendSamplesStruct->HasAnswer ? L"True" : L"False", AnswerSize));
            }

            pThisSendSamplesStruct->HasAnswer = false;
        }

        auto MessageSentEventScopeExit = wil::scope_exit([&] { LogingBooleanWindowsError(SetEvent(MessageSentEventHandle.get())); });

        if (StartingClassDestructor.load(std::memory_order_seq_cst))
        {
            //TODO: надо бы вроде бы сделать сообщение отличное от MessageType::SendSamples
            pThisSendSamplesStruct->MessageType = MessageType::EmptySamples;
            //LogingBooleanWindowsError(SetEvent(MessageSentEventHandle.get()));      //TODO: может заиспользовать wil::scope_exit
            break;
        }

        //static bool IsColdStarted = false;
        if (pThisSendSamplesStruct->MessageType == MessageType::ColdStart /*&& !IsColdStarted*/)   //УЛЬТРА ВАЖНАЯ ШТУКА ИЗ-ЗА ХОЛОСТОГО ХОДА ТАК СКАЗАТЬ
        {
            SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_INFO(0, TM("pThisSendSamplesStruct->MessageType != MessageType::SendSamples, but MessageType = %d"),
                static_cast<int>(pThisSendSamplesStruct->MessageType)));
            pThisSendSamplesStruct->MessageType = MessageType::EmptySamples;
            //LogingBooleanWindowsError(SetEvent(MessageSentEventHandle.get()));      //TODO: может заиспользовать wil::scope_exit
            //IsColdStarted = true;
            continue;
        }

        //std::vector<float> CurrentAudioBuffer = AudioBuffer.ReadAllQueue();
        ////SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("Audio Buffer send: %d samples"),
        ////    CurrentAudioBuffer.size()));
        //if (CurrentAudioBuffer.empty())
        //{
        //    SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_INFO(0, TM("CurrentAudioBuffer is empty, skip")));
        //    pThisSendSamplesStruct->MessageType = MessageType::EmptySamples;
        //    //LogingBooleanWindowsError(SetEvent(MessageSentEventHandle.get()));      //TODO: может заиспользовать wil::scope_exit
        //    continue;
        //}


        //Unpack 0-31 bits contains floating point of SampleRate, 32-47 bits contains ChannelCount, 48-63 bits contains BitMask from one atomic variable SampleRateChannelCountBitMask
        const uint64_t SampleRateChannelCountBitMaskValue = SampleRateChannelCountBitMask.load();
        float SampleRate;
        int16_t ChannelCount;
        uint16_t BitMask;
        //rule following strict aliasing
        memcpy(&SampleRate, &SampleRateChannelCountBitMaskValue, sizeof(SampleRate));
        memcpy(&ChannelCount, reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(&SampleRateChannelCountBitMaskValue) + sizeof(SampleRate)), sizeof(ChannelCount));
        memcpy(&BitMask, reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(&SampleRateChannelCountBitMaskValue) + sizeof(SampleRate) + sizeof(ChannelCount)), sizeof(BitMask));

        pThisSendSamplesStruct->ChannelCount = ChannelCount;
        pThisSendSamplesStruct->SampleRate = SampleRate;
        pThisSendSamplesStruct->BitMask = BitMask;
        pThisSendSamplesStruct->MessageType = MessageType::SendSamples;    //if CurrentAudioBuffer.empty() - EmptySamples


        if (ChannelCount <= 0 || SampleRate <= 0)
        {
            SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_ERROR(0, TM("incorrect(negative) passed argument: [ChannelCount: %d or SampleRate: %f]"),
                static_cast<int32_t>(ChannelCount), SampleRate));
            //TODO maybe: make ChannelCount  = SampleRate = 0;
#undef max
            ChannelCount = std::max(ChannelCount, 0i16);
            SampleRate = std::max(SampleRate, 0.f);
            //TODO: pThisSendSamplesStruct->MessageType = MessageType::EmptySamples;
            //LogingBooleanWindowsError(SetEvent(MessageSentEventHandle.get()));      //TODO: может заиспользовать wil::scope_exit
            continue;
        }
        pThisSendSamplesStruct->FramesCount = CurrentAudioBuffer.size() / /*pThisSendSamplesStruct->ChannelCount*/ChannelCount;   //avoid potentially divide by zero

        if (static_cast<uint64_t>(ChannelCount) * CurrentAudioBuffer.size() * sizeof(float) + sizeof(SendSamplesStructRequest) > MappingObjectForEventAPOBufferSize)
        {
            SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_ERROR(0, TM("passed argument cause read buffer overrun: [ChannelCount: %d, FramesCount: %d]"),
                static_cast<int32_t>(ChannelCount), pThisSendSamplesStruct->FramesCount));
            //LogingBooleanWindowsError(SetEvent(MessageSentEventHandle.get()));      //TODO: может заиспользовать wil::scope_exit
            continue;
        }


        //float* PtrToWriting = reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(pThisSendSamplesStruct) + sizeof(SendSamplesStructRequest));
        //memcpy(PtrToWriting, CurrentAudioBuffer.data(), CurrentAudioBuffer.size() * sizeof(float));

        //SendSamplesStructRequest* PtrToWriting = reinterpret_cast<SendSamplesStructRequest*>(reinterpret_cast<SendSamplesStructRequest*>(pThisSendSamplesStruct) + 1);
        //memcpy(PtrToWriting, CurrentAudioBuffer.data(), CurrentAudioBuffer.size() * sizeof(float));


        memcpy((pThisSendSamplesStruct + 1), CurrentAudioBuffer.data(), CurrentAudioBuffer.size() * sizeof(float));



        //LogingBooleanWindowsError(SetEvent(MessageSentEventHandle.get()));
    }

    return 0;
}

int APOManager::Connect(bool IsNeedLogging)  //Connect to APO /*Call only from WorkerThread*/
{
    SECURITY_DESCRIPTOR sd{};
    if (!InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION))
    {
        SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("InitializeSecurityDescriptor failed, error: %s"),
            ParseWindowsError(GetLastError()).c_str()));
        return 1;
    }
    //https://learn.microsoft.com/en-us/cpp/code-quality/c6248?view=msvc-170
//TODO: На будущее, возможно стоит пересмотреть ACE
#pragma warning(push)
#pragma warning(disable: 6248)
    if (!SetSecurityDescriptorDacl(&sd, TRUE, nullptr, FALSE))
#pragma warning(pop) 
    {
        SafePointerDereference(IsNeedLogging ? Singleton<VariableMainLogger>::GetInstance().GetTracePtr() : nullptr, P7_CRITICAL(0, TM("SetSecurityDescriptorDacl failed, error: %s"),
            ParseWindowsError(GetLastError()).c_str()));
        return 2;
    }
    SECURITY_ATTRIBUTES sa{ .nLength = sizeof(sa), .lpSecurityDescriptor = &sd, .bInheritHandle = TRUE };



    
    //Global interprocess mutex to protect CreateFileMappingW, CreateEventW
    //-------Start of Global interprocess mutex----------
    wil::unique_handle hMutexCreateMapping(CreateMutexW(&sa, FALSE, CreateMappingMutexString));
    if (!hMutexCreateMapping.is_valid())
    {
        SafePointerDereference(IsNeedLogging ? Singleton<VariableMainLogger>::GetInstance().GetTracePtr() : nullptr, P7_CRITICAL(0, TM("CreateMutexW failed, error: %s"),
            ParseWindowsError(GetLastError()).c_str()));
        return 3;
    }
    auto ReleaseMutexScope = wil::scope_exit([&hMutexCreateMapping, &IsNeedLogging]
        {
            if (!ReleaseMutex(hMutexCreateMapping.get()))   //TODO: а надо ли вообще это делать ??? именно там где WaitValue != WAIT_OBJECT_0
            {
                SafePointerDereference(IsNeedLogging ? Singleton<VariableMainLogger>::GetInstance().GetTracePtr() : nullptr, P7_CRITICAL(0, TM("ReleaseMutex failed, error: %s"),
                    ParseWindowsError(GetLastError()).c_str()));
            }
            hMutexCreateMapping.reset();
        });
    if (const DWORD WaitValue = WaitForSingleObject(hMutexCreateMapping.get(), INFINITE);
        WaitValue != WAIT_OBJECT_0)
    {
        SafePointerDereference(IsNeedLogging ? Singleton<VariableMainLogger>::GetInstance().GetTracePtr() : nullptr, P7_CRITICAL(0, TM("WaitForSingleObject failed, error: %s"),
            ParseWaitForSingleObjectValue(WaitValue).c_str()));
        return 4;
    }




    MappedFileHandle.reset(OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, MappingObjectForEventAPOString));
    if (!MappedFileHandle.is_valid())
    {
        SafePointerDereference(IsNeedLogging ? Singleton<VariableMainLogger>::GetInstance().GetTracePtr() : nullptr, P7_INFO(0, TM("OpenFileMappingW failed, error: %s"),
            ParseWindowsError(GetLastError()).c_str()));
        //TODO ВАЖНЫЙ: Если что не отрабатывает GetLastError() == 5, потому что прав админа нет
        MappedFileHandle.reset(CreateFileMappingW(INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE, 0,
            MappingObjectForEventAPOBufferSize, MappingObjectForEventAPOString));
    }

    if (!MappedFileHandle.is_valid())
    {
        SafePointerDereference(IsNeedLogging ? Singleton<VariableMainLogger>::GetInstance().GetTracePtr() : nullptr, P7_CRITICAL(0, TM("CreateFileMappingW failed, error: %s"),
            ParseWindowsError(GetLastError()).c_str()));
        return 5;
    }

    pMappedBuffer.reset(MapViewOfFile(MappedFileHandle.get(), FILE_MAP_READ | FILE_MAP_WRITE, 0, 0,
        MappingObjectForEventAPOBufferSize));
    if (!pMappedBuffer)
    {
        SafePointerDereference(IsNeedLogging ? Singleton<VariableMainLogger>::GetInstance().GetTracePtr() : nullptr, P7_CRITICAL(0, TM("MapViewOfFile failed, error: %s"),
            ParseWindowsError(GetLastError()).c_str()));
        return 6;
    }

    MessageEmptiedEventHandle.reset(CreateEventW(&sa, FALSE, FALSE, MessageEmptiedEvent));
    if (!MessageEmptiedEventHandle.is_valid())
    {
        SafePointerDereference(IsNeedLogging ? Singleton<VariableMainLogger>::GetInstance().GetTracePtr() : nullptr, P7_CRITICAL(0, TM("CreateEventW(MessageEmptiedEvent) failed, error: %s"),
            ParseWindowsError(GetLastError()).c_str()));
        return 7;
    }

    MessageSentEventHandle.reset(CreateEventW(&sa, FALSE, FALSE, MessageSentEvent));
    if (!MessageSentEventHandle.is_valid())
    {
        SafePointerDereference(IsNeedLogging ? Singleton<VariableMainLogger>::GetInstance().GetTracePtr() : nullptr, P7_CRITICAL(0, TM("CreateEventW(MessageSentEvent) failed, error: %s"),
            ParseWindowsError(GetLastError()).c_str()));
        return 8;
    }
    ReleaseMutexScope.reset();
    //-------End of Global interprocess mutex----------

    static_cast<SendSamplesStructRequest*>(pMappedBuffer.get())->MessageType = MessageType::ColdStart;
    LogingBooleanWindowsError(SetEvent(MessageSentEventHandle.get()));


    return 0;
}


void APOManager::TerminateWorkerThread()
{
    SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_INFO(0, TM("TerminateWorkerThread")));

    //StartingClassDestructor = true;
    StartingClassDestructor.store(true, std::memory_order_release);
    LogingBooleanWindowsError(SetEvent(LocalEventSync.get()));
    StartingClassDestructor.store(true, std::memory_order_release);

    if (MessageEmptiedEventHandle.is_valid())
    {
        LogingBooleanWindowsError(SetEvent(MessageEmptiedEventHandle.get()));
    }

    if (const DWORD WaitValue = WaitForSingleObject(WorkerThreadHandle.get(), 1'000);
        WaitValue != WAIT_OBJECT_0)
    {
        SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_ERROR(0, TM("WaitForSingleObject(hMessageSentEvent, INFINITE) returned: %s"),
            ParseWaitForSingleObjectValue(WaitValue).c_str()));

        LogingBooleanWindowsError(SetEvent(LocalEventSync.get()));

        if (MessageEmptiedEventHandle.is_valid())
        {
            LogingBooleanWindowsError(SetEvent(MessageEmptiedEventHandle.get()));
        }

        if (const DWORD WaitValue2 = WaitForSingleObject(WorkerThreadHandle.get(), 1'000);
            WaitValue2 != WAIT_OBJECT_0)
        {
            SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_ERROR(0, TM("WaitForSingleObject(hMessageSentEvent, INFINITE) returned: %s"),
                ParseWaitForSingleObjectValue(WaitValue2).c_str()));
        }

        //if (!TerminateThread(WorkerThreadHandle.get(), 0))
        //{
        //    SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("TerminateThread(hMainThread.get(), 0) error: %s"),
        //        ParseWaitForSingleObjectValue(GetLastError()).c_str()));
        //}
    }
}