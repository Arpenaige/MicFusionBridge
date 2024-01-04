#include "stdafx.h"

#include "MicFusionBridge.h"

namespace DoNotUseThisNamespace
{
	constexpr bool IsGUIDIsEqual(const GUID val1, const GUID val2)
	{
		return val1.Data1 == val2.Data1 &&
			val1.Data2 == val2.Data2 &&
			val1.Data3 == val2.Data3 &&
			val1.Data4[0] == val2.Data4[0] &&
			val1.Data4[1] == val2.Data4[1] &&
			val1.Data4[2] == val2.Data4[2] &&
			val1.Data4[3] == val2.Data4[3] &&
			val1.Data4[4] == val2.Data4[4] &&
			val1.Data4[5] == val2.Data4[5] &&
			val1.Data4[6] == val2.Data4[6] &&
			val1.Data4[7] == val2.Data4[7];
	}

	static_assert(IsGUIDIsEqual(APO_GUID, __uuidof(MicFusionBridge)), "GUID is not equal, please make APO_GUID == __uuidof(MicFusionBridge)");
}


//https://learn.microsoft.com/ru-ru/windows/win32/api/audioenginebaseapo/ne-audioenginebaseapo-apo_flag
long MicFusionBridge::instCount = 0;
const CRegAPOProperties<1> MicFusionBridge::regProperties(
	__uuidof(MicFusionBridge), L"MicFusionBridge", L"Copyright (c) 2023 Arpenaige | MIT License | https://github.com/Arpenaige/MicFusionBridge", 1, 0, __uuidof(IAudioProcessingObject),
	(APO_FLAG)(APO_FLAG_SAMPLESPERFRAME_MUST_MATCH | APO_FLAG_FRAMESPERSECOND_MUST_MATCH | APO_FLAG_BITSPERSAMPLE_MUST_MATCH | APO_FLAG_INPLACE));

MicFusionBridge::MicFusionBridge(IUnknown* pUnkOuter)
	: CBaseAudioProcessingObject(regProperties)
{
	SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("MicFusionBridge constructor, pUnkOuter: %p, instCount: %d"),
		pUnkOuter, InterlockedExchangeAdd(&instCount, 0)));

	refCount = 1;
	if (pUnkOuter != NULL)
		this->pUnkOuter = pUnkOuter;
	else
		this->pUnkOuter = reinterpret_cast<IUnknown*>(static_cast<INonDelegatingUnknown*>(this));

	InterlockedIncrement(&instCount);
}

MicFusionBridge::~MicFusionBridge()
{
	SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("~MicFusionBridge destructor, instCount = %li"),
		InterlockedExchangeAdd(&instCount, 0)));

	InterlockedDecrement(&instCount);

	if (SoxrObject)
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("[LOGIC ERROR] SoxrObject don't deallocated in UnlockForProcess")));
		soxr_delete(SoxrObject);
		SoxrObject = nullptr;
	}
}

#pragma AVRT_CODE_BEGIN
APOAnswerInfo MicFusionBridge::ResampleAndWriteSCSPQueue(const float* pSamples, int32_t InputFrameCount, float InputFrameRate, int32_t InputChannelCount, uint16_t BitMask)
{
	LocalBitMask.store(BitMask);

	const auto InputSampleCount = static_cast<uint64_t>(InputFrameCount) * static_cast<uint64_t>(InputChannelCount);

	bool QueueWritingError = false;

	int32_t LastSampleCountInQueue = 0;

	const auto IsEqualInRange = [](float x, float y, float range) -> bool
	{
		return std::abs(x - y) <= range;
	};

	if (APOChannelCount == 0 ||
		IsEqualInRange(APOFrameRate, 0.f, 1.0f))
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("SOXR params incorrect: ChannelCount = %lu, OutputFrameRate = %f"),
			APOChannelCount, APOFrameRate));
		return {};
	}

	const bool ResamplingIsNotNeeded = (APOChannelCount == InputChannelCount && IsEqualInRange(APOFrameRate, InputFrameRate, 2.f));

	//TODO: check if InputFrameRate is incorrect
	if ((!SoxrObject ||
		APOChannelCount != SoxrChannelCount ||
		APOFrameRate != SoxrOutputFrameRate ||
		InputFrameRate != SoxrInputFrameRate) &&
		!ResamplingIsNotNeeded)
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("Create New SOXR Object")));
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_INFO(0, TM("Old SOXR params: ChannelCount = %lu, OutputFrameRate = %f, InputFrameRate = %f"),
			SoxrChannelCount, SoxrOutputFrameRate, SoxrInputFrameRate));
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_INFO(0, TM("New SOXR params: ChannelCount = %lu, OutputFrameRate = %f, InputFrameRate = %f"),
			APOChannelCount, APOFrameRate, InputFrameRate));

		if (SoxrObject)
		{
			soxr_delete(SoxrObject);
		}

		SoxrChannelCount = APOChannelCount;
		SoxrOutputFrameRate = APOFrameRate;
		SoxrInputFrameRate = InputFrameRate;

		soxr_error_t error;

		SoxrObject = soxr_create(
			SoxrInputFrameRate, SoxrOutputFrameRate, SoxrChannelCount, /* Input rate, output rate, # of channels. */
			&error,                                                      /* To report any error during creation. */
			&SoxrIOSpec, &SOXRQualitySpec, NULL);                        /* Use custom configuration.*/

		if (error)
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("error of create soxr_create: %s"),
				ParseWindowsError(GetLastError()).c_str()));
			return {};
		}
	}

	if (ResamplingIsNotNeeded)
	{
		LastSampleCountInQueue = m_pSCSPQueue->WriteQueue(pSamples, InputSampleCount);
		if (LastSampleCountInQueue < 0)
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_DEBUG(0, TM("[1] LastSampleCountInQueue < 0 -> no free space to write in queue")));
		}
	}
	else
	{
		const size_t CurrentInputBufferSize = InputFrameCount * APOChannelCount;
		if (InputBufferSize < CurrentInputBufferSize)
		{
			InputBuffer = std::make_unique_for_overwrite<float[]>(CurrentInputBufferSize);
			InputBufferSize = CurrentInputBufferSize;
		}

		float* pInputBuffer = InputBuffer.get();

		if (APOChannelCount == 1 && InputChannelCount == 2)
		{
			for (size_t i = 0, j = 0; j < InputSampleCount; i++, j+=2)
			{
				if (i >= InputBufferSize)
				{
					SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("i >= InputBufferSize")));

					//TODO: log, отлов ошибки, навсякий случай, после проверки убрать.
				}

				//CAudioLimiter
				pInputBuffer[i] = (pSamples[j] + pSamples[j + 1]) * 0.5f;
			}
		}
		else if (APOChannelCount == 2 && InputChannelCount == 1)
		{
			for (size_t i = 0, j = 0; i < InputSampleCount; i++, j+=2)
			{
				if (j >= InputBufferSize)
				{
					SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("j >= InputBufferSize")));

					//TODO: log, отлов ошибки, навсякий случай, после проверки убрать.
				}

				pInputBuffer[j] = pInputBuffer[j + 1] = pSamples[i];
			}
		}
		else if(APOChannelCount == InputChannelCount)
		{
			for (size_t i = 0; i < InputSampleCount; i++)
			{
				pInputBuffer[i] = pSamples[i];
			}
		}
		else
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_ERROR(0, TM("Channel Count > 2, Input Channel Count = %d, APO Channel Count = %lu"),
				InputChannelCount, APOChannelCount));

			return {};
		}

		if (IsEqualInRange(APOFrameRate, InputFrameRate, 2.f))
		{
			LastSampleCountInQueue = m_pSCSPQueue->WriteQueue(pInputBuffer, CurrentInputBufferSize);
			if (LastSampleCountInQueue < 0)
			{
				SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_DEBUG(0, TM("[2] LastSampleCountInQueue < 0 -> no free space to write in queue")));
			}
		}
		else
		{
			const size_t CurrentOutputBufferSize = CurrentInputBufferSize * (APOFrameRate / InputFrameRate) + 2.5;

			if (OutputBufferSize < CurrentOutputBufferSize)
			{
				OutputBuffer = std::make_unique_for_overwrite<float[]>(CurrentOutputBufferSize);
				OutputBufferSize = CurrentOutputBufferSize;
			}

			float* pOutputBuffer = OutputBuffer.get();

			size_t idone, odone;

			soxr_error_t error = soxr_process(SoxrObject,
				pInputBuffer, CurrentInputBufferSize / APOChannelCount,
				&idone,
				pOutputBuffer, CurrentOutputBufferSize / APOChannelCount,
				&odone);

			//SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_DEBUG(0, TM("[1] odone = %llu"),
			//	odone));

			if (!error)
			{
				LastSampleCountInQueue = m_pSCSPQueue->WriteQueue(pOutputBuffer, odone * APOChannelCount);
				if (LastSampleCountInQueue < 0)
				{
					SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_DEBUG(0, TM("[3] LastSampleCountInQueue < 0 -> no free space to write in queue")));
				}
			}
			else
			{
				SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("first soxr_process failed: %s"),
					ParseWindowsError(GetLastError()).c_str()));
			}

			//Getting leftovers
			error = soxr_process(SoxrObject,
				nullptr, 0,
				&idone,
				pOutputBuffer, CurrentOutputBufferSize / APOChannelCount,
				&odone);

			//SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_DEBUG(0, TM("[2] odone = %llu"),
			//	odone));

			if (!error)
			{
				if (odone)
				{
					LastSampleCountInQueue = m_pSCSPQueue->WriteQueue(pOutputBuffer, odone * APOChannelCount);
					if (LastSampleCountInQueue < 0)
					{
						SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_DEBUG(0, TM("[4] LastSampleCountInQueue < 0 -> no free space to write in queue")));
					}
				}
			}
			else
			{
				SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("second soxr_process failed: %s"),
					ParseWindowsError(GetLastError()).c_str()));
			}
		}
	}

	//const int32_t APONeedSampleCountLatency = static_cast<double>(APOFrameRate) / 1000.0 * APOLatency * APOChannelCount;
	//const int32_t APONeedSampleCountLatency = APOFrameRate / 1000.0 * APOLatency * APOChannelCount;
	//const int32_t APONeedSampleCountLatency = APOFrameRate / 1000.0 * (APOProcessLatency * 1.5f/*if latency 10 -> 10 * 1.5 = 15.0 total latency*/) * APOChannelCount;
	//const int32_t APOMaxSampleCountLatency = APOFrameRate / 1000.0 * (APOProcessLatency * 10.0f/*if latency 10 -> 10 * 10.0 = 100.0 total max latency*/) * APOChannelCount;
	const int32_t APONeedSampleCountLatency = APOFrameRate  * (APOProcessLatency * 1.5f/*if latency 10 -> 10 * 1.5 = 15.0 total latency*/) * APOChannelCount;
	const int32_t APOMaxSampleCountLatency = APOFrameRate * (APOProcessLatency * 5.0f/*if latency 5.0 -> 5.0 * 10.0 = 50.0 total max latency*/) * APOChannelCount;
	//if (LastSampleCountInQueue > APONeedSampleCountLatency && status == APOStatus::WaitingForMixing)
	if (std::abs(LastSampleCountInQueue) > APONeedSampleCountLatency && !CanMixing.load(std::memory_order_seq_cst))
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("samples are accumulated, start mixing, |LastSampleCountInQueue| = %d"),
			std::abs(LastSampleCountInQueue)));

		//status = APOStatus::APOSucces;
		CanMixing.store(true, std::memory_order_seq_cst);

		//Возможно решение проблемы с рессемплингом, когда идет перезахват музыки начальной, новая музыка идет сюда, и рессемплер выдает немного старой.
		// //WTF ??? Зачем после накопления я удаляю объект ?)
		//if (SoxrObject)
		//{
		//	soxr_delete(SoxrObject);
		//	SoxrObject = nullptr;
		//}
	}
	else if (std::abs(LastSampleCountInQueue) > APOMaxSampleCountLatency)
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("Need shifting queue, current size: %d, Shifting size: %d"),
			LastSampleCountInQueue, APONeedSampleCountLatency & -1));
		m_pSCSPQueue->ShiftAllQueue(APONeedSampleCountLatency & -1 /*make even*/, APOChannelCount);
	}

	const APOAnswerInfo Answer{ .LastTimeAPOProcessCallWithMix = LastAPOPorcessCallAndMixSystemGlobalTime.load(std::memory_order_relaxed),
						  .AudioDeviceGUID = CurrentAudioDeviceGUID.load()};
	return Answer;
}
#pragma AVRT_CODE_END



HRESULT MicFusionBridge::QueryInterface(const IID& iid, void** ppv)
{
	LogGUID(iid);

	P7_Flush();


	return pUnkOuter->QueryInterface(iid, ppv);
}

ULONG MicFusionBridge::AddRef()
{
	SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("AddRef")));

	P7_Flush();

	return pUnkOuter->AddRef();
}

ULONG MicFusionBridge::Release()
{
	SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("Release")));

	P7_Flush();

	return pUnkOuter->Release();
}

HRESULT MicFusionBridge::GetLatency(HNSTIME* pTime)
{
	SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("GetLatency")));

	if (!pTime)
		return E_POINTER;

	if (!m_bIsLocked)
		return APOERR_ALREADY_UNLOCKED;

	*pTime = 0;

	if (pPreviousAPO)
	{
		if (const HRESULT hr = pPreviousAPO->GetLatency(pTime);
			FAILED(hr))
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_ERROR(0, TM("pPreviousAPO->GetLatency() error: %s"),
				ParseWindowsError(hr).c_str()));
		}
		else
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_INFO(0, TM("pTime After pPreviousAPO: %lli"),
				*pTime));
		}
	}

	P7_Flush();

	return S_OK;
}

HRESULT MicFusionBridge::Initialize(UINT32 cbDataSize, BYTE* pbyData)
{
	SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_INFO(0, TM("cbDataSize: %li, this: %p"),
		cbDataSize, this));

	if ((NULL == pbyData) && (0 != cbDataSize))
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_ERROR(0, TM("(NULL == pbyData) && (0 != cbDataSize)")));
		return E_INVALIDARG;
	}
	if ((NULL != pbyData) && (0 == cbDataSize))
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_ERROR(0, TM("(NULL != pbyData) && (0 == cbDataSize)")));
		return E_POINTER;
	}
	if (cbDataSize != sizeof(APOInitSystemEffects))
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_ERROR(0, TM("cbDataSize != sizeof(APOInitSystemEffects)")));
		return E_INVALIDARG;
	}

	//https://stackoverflow.com/a/48718199
#ifndef _WIN64
	static_assert(false, "Next code work only in Windows x64");
#endif
	CONTEXT Context = { 0 };
	RtlCaptureContext(&Context);
	DWORD64 ImageBase = 0;
	RUNTIME_FUNCTION* pLookupedFunction = RtlLookupFunctionEntry(Context.Rip, &ImageBase, NULL);
	if (pLookupedFunction)
	{
		constexpr size_t StackFramesCount = 256;
		std::unique_ptr<void* []> Stack = std::make_unique_for_overwrite<void* []>(StackFramesCount);
		const WORD FramesCount = RtlCaptureStackBackTrace(0, StackFramesCount, Stack.get(), NULL);

		const uintptr_t MicFusionBridgeInitialize_BeginAddress = ImageBase + pLookupedFunction->BeginAddress;
		const uintptr_t MicFusionBridgeInitialize_EndAddress = ImageBase + pLookupedFunction->EndAddress;

		for (int32_t i = 1; i < FramesCount; i++)
		{
			const uintptr_t CurrStackAdress = reinterpret_cast<uintptr_t>(Stack[i]);

			//If call stack have return adress to any place of our function, so we are in an infinite Initialize call between APOs
			if (MicFusionBridgeInitialize_BeginAddress <= CurrStackAdress &&
				MicFusionBridgeInitialize_EndAddress > CurrStackAdress)
			{
				SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_INFO(0, TM("An attempt of infinite initialization was prevented")));
				return APOERR_ALREADY_INITIALIZED;
			}
		}
	}
	else
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("RtlLookupFunctionEntry failed, check compiled code / compiler flags, why SEH is not implemented")));
	}

	ResetPreviousPointers();

	const PROPERTYKEY PKEY_AudioEndpoint_GUID_CUSTOM{ 0x1da5d803, 0xd492, 0x4edd, 0x8c, 0x23, 0xe0, 0xc0, 0xff, 0xee, 0x7f, 0x0e, 4 };     //Taken from PKEY_AudioEndpoint_GUID
	wil::unique_prop_variant AudioEndpointGUID;
	if (const HRESULT hr = reinterpret_cast<APOInitSystemEffects*>(pbyData)->pAPOEndpointProperties->GetValue(PKEY_AudioEndpoint_GUID_CUSTOM, &AudioEndpointGUID);
	//if (const HRESULT hr = reinterpret_cast<APOInitSystemEffects*>(pbyData)->pAPOEndpointProperties->GetValue(PKEY_AudioEndpoint_GUID, &AudioEndpointGUID);   //FIXME: LNK2001 for [PKEY_AudioEndpoint_GUID] (m.b. find .lib)
		FAILED(hr))
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_ERROR(0, TM("pAPOEndpointProperties->GetValue(PKEY_AudioEndpoint_GUID) error: %s"),
			ParseWindowsError(hr).c_str()));
		return hr;
	}

	//std::wstring DeviceGUIDStr;
	if (AudioEndpointGUID.vt == VT_LPWSTR)
	{
		CurrentAudioDeviceGUIDString = AudioEndpointGUID.pwszVal;
		GUID DeviceGUID;
		if (const HRESULT hr = CLSIDFromString(CurrentAudioDeviceGUIDString.c_str(), &DeviceGUID);
			SUCCEEDED(hr))
		{
			CurrentAudioDeviceGUID.store(DeviceGUID);
			LogGUID(DeviceGUID);
		}
		else
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("CLSIDFromString(%s) error: %s"),
				CurrentAudioDeviceGUIDString.c_str(), ParseWindowsError(hr).c_str()));
		}
	}
	else
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_ERROR(0, TM("pAPOEndpointProperties->GetValue type is not VT_LPWSTR : %lu"),
			static_cast<DWORD>(AudioEndpointGUID.vt)));
	}

	SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("DeviceGUID: %s"),
		CurrentAudioDeviceGUIDString.c_str()));


	auto GetPreviousAPOGUID = [](const std::wstring& DeviceGUID) -> std::wstring
	{
		if (DeviceGUID.empty())
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("DeviceGUID is empty")));
			return {};
		}

		const std::wstring BaseSoftwarePathGUID = std::format(L"SOFTWARE\\{}\\{}", APOName, DeviceGUID);

		winreg::RegKey KeyHandleBaseSoftwareGUID;
		if (auto KeyHandleReturnedValue = KeyHandleBaseSoftwareGUID.TryOpen(HKEY_LOCAL_MACHINE, BaseSoftwarePathGUID, GENERIC_READ | KEY_QUERY_VALUE);
			KeyHandleReturnedValue.IsOk())
		{
			std::wstring PreviousAPO;
			if (const auto Code = GetValueFunction(KeyHandleBaseSoftwareGUID, L"PreviousAPO", PreviousAPO);
				Code == ERROR_SUCCESS)
			{
				SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("PreviousAPOGUID: %s"),
					PreviousAPO.c_str()));
				return PreviousAPO;
			}
			else
			{
				SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("GetValueFunction(PreviousAPO) error: %s"),
					ParseWindowsError(Code).c_str()));
				return {};
			}
		}
		else
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_ERROR(0, TM("TryOpen(%s) error: %s"),
				BaseSoftwarePathGUID.c_str(), ParseWindowsError(KeyHandleReturnedValue.Code()).c_str()));
			return {};
		}
	};


	if (const std::wstring PreviousAPOGUID = GetPreviousAPOGUID(CurrentAudioDeviceGUIDString);
		!PreviousAPOGUID.empty() && PreviousAPOGUID != GUIDEmpty && APOIsWritedInRegedit(CurrentAudioDeviceGUIDString) /*If our APO is main*/)
	{
		auto ResetScropeExit = wil::scope_exit([&] { ResetPreviousPointers(); SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("reset prev pointers"))); });

		CLSID PreviousAPOGUIDCLSIDValue;
		if (const HRESULT HRConvert = CLSIDFromString(PreviousAPOGUID.c_str(), &PreviousAPOGUIDCLSIDValue);
			FAILED(HRConvert))
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("CLSIDFromString(%s) error: %s"),
				PreviousAPOGUID.c_str(), ParseWindowsError(HRConvert).c_str()));
			return S_OK;
		}

		if (const HRESULT hr = CoCreateInstance(PreviousAPOGUIDCLSIDValue, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pPreviousAPO));
			FAILED(hr))
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("CoCreateInstance(PreviousAPOGUIDCLSIDValue) error: %s"),
				ParseWindowsError(hr).c_str()));
			return S_OK;
		}

		//Dangerous code and not tested
		//const uintptr_t pVTablePreviousAPO = *reinterpret_cast<uintptr_t*>(pPreviousAPO.get());
		//const void* pQueryInterface = reinterpret_cast<void*>(*reinterpret_cast<uintptr_t*>(pVTablePreviousAPO));

		//GetModuleName(reinterpret_cast<PVOID>(getMethodVoidPointer(pQueryInterface)));

		if (const HRESULT hr = pPreviousAPO->QueryInterface(IID_PPV_ARGS(&pPreviousRT));
			FAILED(hr))
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("pPreviousAPO->QueryInterface(__uuidof(IAudioProcessingObjectRT)) error: %s"),
				ParseWindowsError(hr).c_str()));
			return S_OK;
		}

		if (const HRESULT hr = pPreviousAPO->QueryInterface(IID_PPV_ARGS(&pPreviousConfiguration));
			FAILED(hr))
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("pPreviousAPO->QueryInterface(__uuidof(IAudioProcessingObjectRT)) error: %s"),
				ParseWindowsError(hr).c_str()));
			return S_OK;
		}

		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("Before Initialize")));
		if (const HRESULT hr = pPreviousAPO->Initialize(cbDataSize, pbyData);
			FAILED(hr))
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("Inside Initialize")));

			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("pPreviousAPO->Initialize(cbDataSize, pbyData) error: %s"),
				ParseWindowsError(hr).c_str()));
			return S_OK;
		}
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("After Initialize")));
		ResetScropeExit.release();   //don't call ResetPreviousPointers

		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("After that wil::scope_exit doesn't call {ResetPreviousPointers}")));
	}

	P7_Flush();

	return S_OK;
}

HRESULT MicFusionBridge::IsInputFormatSupported(IAudioMediaType* pOutputFormat,
	IAudioMediaType* pRequestedInputFormat, IAudioMediaType** ppSupportedInputFormat)
{
	SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("IsInputFormatSupported")));

	if (!pRequestedInputFormat)
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_ERROR(0, TM("!pRequestedInputFormat")));
		return E_POINTER;
	}

	UNCOMPRESSEDAUDIOFORMAT inFormat;
	HRESULT hr = pRequestedInputFormat->GetUncompressedAudioFormat(&inFormat);
	if (FAILED(hr))
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_ERROR(0, TM("GetUncompressedAudioFormat(&inFormat) error: %s"),
			ParseWindowsError(hr).c_str()));
		return hr;
	}
	else
	{
		LogGUID(inFormat.guidFormatType);
		LogUNCOMPRESSEDAUDIOFORMAT(L"IN", inFormat);
	}


	UNCOMPRESSEDAUDIOFORMAT outFormat;
	hr = pOutputFormat->GetUncompressedAudioFormat(&outFormat);
	if (FAILED(hr))
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_ERROR(0, TM("GetUncompressedAudioFormat(&outFormat)error: %s"),
			ParseWindowsError(hr).c_str()));
		return hr;
	}
	else
	{
		LogGUID(outFormat.guidFormatType);
		LogUNCOMPRESSEDAUDIOFORMAT(L"OUT", outFormat);
	}

	if (pPreviousAPO)
	{
		hr = pPreviousAPO->IsInputFormatSupported(pOutputFormat, pRequestedInputFormat, ppSupportedInputFormat);
		if (FAILED(hr))
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_ERROR(0, TM("pPreviousAPO->IsInputFormatSupported() error: %s"),
				ParseWindowsError(hr).c_str()));
			ResetPreviousPointers();
		}
	}

	if (!pPreviousAPO || FAILED(hr))
	{
		hr = CBaseAudioProcessingObject::IsInputFormatSupported(pOutputFormat, pRequestedInputFormat, ppSupportedInputFormat);
		if (FAILED(hr))
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_ERROR(0, TM("IsInputFormatSupported error: %s"),
				ParseWindowsError(hr).c_str()));
		}
	}

	P7_Flush();

	return hr;
}

HRESULT MicFusionBridge::LockForProcess(UINT32 u32NumInputConnections,
	APO_CONNECTION_DESCRIPTOR** ppInputConnections, UINT32 u32NumOutputConnections,
	APO_CONNECTION_DESCRIPTOR** ppOutputConnections)
{
	if (auto atomic_value = InterlockedIncrement(&LockUnlockProcessCounter);
		atomic_value > 1)
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("WTF ?????? LockUnlockProcessCounter > 1, LockUnlockProcessCounter: %d"),
			atomic_value));
	}

	SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("LockForProcess")));

	UNCOMPRESSEDAUDIOFORMAT outFormat;
	HRESULT hr = ppOutputConnections[0]->pFormat->GetUncompressedAudioFormat(&outFormat);
	if (FAILED(hr))
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_ERROR(0, TM("GetUncompressedAudioFormat error: %s"),
			ParseWindowsError(hr).c_str()));
		return hr;
	}
	else
	{
		LogGUID(outFormat.guidFormatType);
		LogUNCOMPRESSEDAUDIOFORMAT(L"OUT", outFormat);
	}

	if (pPreviousConfiguration)
	{
		hr = pPreviousConfiguration->LockForProcess(u32NumInputConnections, ppInputConnections, u32NumOutputConnections, ppOutputConnections);
		if (FAILED(hr))
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_ERROR(0, TM("pPreviousConfiguration->LockForProcess() error: %s"),
				ParseWindowsError(hr).c_str()));
		}
	}

	hr = CBaseAudioProcessingObject::LockForProcess(u32NumInputConnections, ppInputConnections,
			u32NumOutputConnections, ppOutputConnections);
	if (FAILED(hr))
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_ERROR(0, TM("LockForProcess error: %s"),
			ParseWindowsError(hr).c_str()));

		return hr;
	}

	APOChannelCount = outFormat.dwSamplesPerFrame;
	u32MaxFrameCount = ppInputConnections[0]->u32MaxFrameCount;
	APOFrameRate = outFormat.fFramesPerSecond;

	APOProcessLatency = u32MaxFrameCount / APOFrameRate;

	//TODO: make it for every audio device
	if (!CurrentAudioDeviceGUIDString.empty())
	{
		const std::wstring BaseSoftwarePathGUID = std::format(L"SOFTWARE\\{}\\{}", APOName, CurrentAudioDeviceGUIDString);
		winreg::RegKey BaseSoftwareKey;
		if (auto BaseSoftwareKeyValue = BaseSoftwareKey.TryOpen(HKEY_LOCAL_MACHINE, BaseSoftwarePathGUID, GENERIC_READ | KEY_QUERY_VALUE);  //KEY_WOW64_64KEY
			BaseSoftwareKeyValue.IsOk())
		{
			DWORD FixMonoSound;
			if (const auto Code = GetValueFunction(BaseSoftwareKey, L"FixMonoSound", FixMonoSound);
				Code == ERROR_SUCCESS)
			{
				ApplyFixMonoSound = FixMonoSound;

				SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_INFO(0, TM("[FixMonoSound] - GetValueFunction success")));
			}
			else
			{
				SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("[FixMonoSound] - GetValueFunction(%s, FixMonoSound) error: %s"),
					BaseSoftwarePathGUID.c_str(), ParseWindowsError(Code).c_str()));
				ApplyFixMonoSound = false;
			}
		}
		else
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("[FixMonoSound] - TryOpen(%s, GENERIC_READ | KEY_QUERY_VALUE) error: %s"),
				BaseSoftwarePathGUID.c_str(), ParseWindowsError(BaseSoftwareKeyValue.Code()).c_str()));
			ApplyFixMonoSound = false;
		}
	}
	else
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("[FixMonoSound] - CurrentAudioDeviceGUIDString is empty")));
	}

	SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("u32MaxFrameCount: %lu"),
		u32MaxFrameCount));
	SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("APOProcess Buffer Length: %f ms"),
		(static_cast<float>(u32MaxFrameCount) / APOFrameRate) * 1000.f));
	SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("ApplyFixMonoSound: %d ms"),
		static_cast<int>(ApplyFixMonoSound)));

	const size_t SCSPQueueSize = std::ceil(APOFrameRate) * APOChannelCount;   //1 Second buffer
	SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("SCSPQueue length: %llu"),
		SCSPQueueSize));

	m_pSCSPQueue = std::make_unique<SCSPQueue<float>>(SCSPQueueSize);

	g_pEvents->AddAPOPointer(this);

	P7_Flush();

	return hr;
}

HRESULT MicFusionBridge::UnlockForProcess()
{
	if (auto atomic_value = InterlockedDecrement(&LockUnlockProcessCounter);
		atomic_value < 0 || atomic_value > 1)
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("WTF ?????? LockUnlockProcessCounter < 0 or > 1, LockUnlockProcessCounter: %d"),
			atomic_value));
	}

	LastAPOPorcessCallAndMixSystemGlobalTime.store(0.f);

	SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("UnlockForProcess")));

	if (pPreviousConfiguration)
	{
		if (const HRESULT hr = pPreviousConfiguration->UnlockForProcess(); 
			FAILED(hr))
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_ERROR(0, TM("pPreviousConfiguration->UnlockForProcess() error: %s"),
				ParseWindowsError(hr).c_str()));
		}
	}
	P7_Flush();


	g_pEvents->RemoveAPOPointer(this);

	if (SoxrObject)
	{
		soxr_delete(SoxrObject);
		SoxrObject = nullptr;
	}

	m_pSCSPQueue.reset();

	ApplyFixMonoSound = false;


	return CBaseAudioProcessingObject::UnlockForProcess();
}

#pragma AVRT_CODE_BEGIN
void MicFusionBridge::APOProcess(UINT32 u32NumInputConnections,
	APO_CONNECTION_PROPERTY** ppInputConnections, UINT32 u32NumOutputConnections,
	APO_CONNECTION_PROPERTY** ppOutputConnections)
{
	if (!APOLoggedOnce)
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("APOProcess")));

		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_INFO(0, TM("ppInputConnections[0]->u32ValidFrameCount: %lu"),
			ppInputConnections[0]->u32ValidFrameCount));
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_INFO(0, TM("ppOutputConnections[0]->u32ValidFrameCount: %lu"),
			ppOutputConnections[0]->u32ValidFrameCount));

		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_INFO(0, TM("ppInputConnections[0]->u32BufferFlags: %lu"),
			ppInputConnections[0]->u32BufferFlags));
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_INFO(0, TM("ppOutputConnections[0]->u32BufferFlags: %lu"),
			ppOutputConnections[0]->u32BufferFlags));

		APOLoggedOnce = true;
	}

	if (ppInputConnections[0]->u32BufferFlags == BUFFER_SILENT)
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_DEBUG(0, TM("WHYYYYYYYY ppInputConnections[0]->u32BufferFlags == BUFFER_SILENT")));
	}

	switch (ppInputConnections[0]->u32BufferFlags)
	{
	case BUFFER_VALID:
	case BUFFER_SILENT:
	{
		float* pInputFrames = reinterpret_cast<float*>(ppInputConnections[0]->pBuffer);
		float* pOutputFrames = reinterpret_cast<float*>(ppOutputConnections[0]->pBuffer);

		if (ppInputConnections[0]->u32ValidFrameCount != ppOutputConnections[0]->u32ValidFrameCount)
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), 
				P7_DEBUG(0, TM("ppInputConnections[0]->u32ValidFrameCount = %lu, ppOutputConnections[0]->u32ValidFrameCount = %lu"),
				ppInputConnections[0]->u32ValidFrameCount, ppOutputConnections[0]->u32ValidFrameCount));
		}

		LoggerTimeout(SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("ApplyFixMonoSound: %d\tAPOChannelCount: %lu"),
			static_cast<int>(ApplyFixMonoSound), APOChannelCount)), 1000.f /*seconds*/);

		//Currently use from left samples and copy this to right
		//for some reasone in Arturia MiniFuse 4 -> right samples not equal as 0(approximately 0.000011) and hopping db amount around ~50(+-(5-10))
		if (ApplyFixMonoSound && APOChannelCount == 2)
		{		
			const uint64_t ValidFrameCount = ppInputConnections[0]->u32ValidFrameCount;
			for (size_t i = 0; i < ValidFrameCount * APOChannelCount; i += 2)
			{
				pInputFrames[i + 1] = pInputFrames[i];
			}
		}

		const uint16_t CurrentBitMask = LocalBitMask.load(std::memory_order_seq_cst);

		if (pPreviousRT)
		{
			//if (CanMixing.load(std::memory_order_seq_cst) && (LocalBitMask.load(std::memory_order_seq_cst) & BitMaskSendSamples::APOMuteMainSignal))
			//{
			//	//First variant if we can't write in input buffer
			//	//memset(pOutputFrames, 0, ppOutputConnections[0]->u32ValidFrameCount * APOChannelCount * sizeof(float));
			//	//ppOutputConnections[0]->u32ValidFrameCount = ppInputConnections[0]->u32ValidFrameCount;
			//	//ppOutputConnections[0]->u32BufferFlags = ppInputConnections[0]->u32BufferFlags;
			//	//pPreviousRT->APOProcess(u32NumOutputConnections, ppOutputConnections, u32NumOutputConnections, ppOutputConnections);



			//	//Second variant, if we can write in input buffer
			//	memset(pInputFrames, 0, ppInputConnections[0]->u32ValidFrameCount * APOChannelCount * sizeof(float));
			//	pPreviousRT->APOProcess(u32NumInputConnections, ppInputConnections, u32NumOutputConnections, ppOutputConnections);
			//}
			//else
			//{
			//	pPreviousRT->APOProcess(u32NumInputConnections, ppInputConnections, u32NumOutputConnections, ppOutputConnections);
			//}




			if (CanMixing.load(std::memory_order_seq_cst) && (CurrentBitMask & BitMaskSendSamples::APOMuteMainSignal))
			{
				//Second variant, if we can write in input buffer
				memset(pInputFrames, 0, ppInputConnections[0]->u32ValidFrameCount * APOChannelCount * sizeof(float));
			}
			pPreviousRT->APOProcess(u32NumInputConnections, ppInputConnections, u32NumOutputConnections, ppOutputConnections);
		}
		else
		{
			memcpy(pOutputFrames, pInputFrames, ppInputConnections[0]->u32ValidFrameCount * APOChannelCount * sizeof(float));
		}

		if (CanMixing.load(std::memory_order_seq_cst) && ppInputConnections[0]->u32BufferFlags == BUFFER_VALID)
		{
			//if (m_pSCSPQueue->ReadQueue(outputFrames, ppOutputConnections[0]->u32ValidFrameCount * APOChannelCount, true, true) <= 0)
			if (m_pSCSPQueue->ReadQueue(pOutputFrames, ppOutputConnections[0]->u32ValidFrameCount * APOChannelCount, !(CurrentBitMask & BitMaskSendSamples::APOMuteMainSignal) || pPreviousRT, true, APOChannelCount) <= 0)
			{
				CanMixing.store(false, std::memory_order_seq_cst);
			}

			LastAPOPorcessCallAndMixSystemGlobalTime.store(GSystemTimer.GetGlobalSystemTime(), std::memory_order_relaxed);
		}
		else if (ppInputConnections[0]->u32BufferFlags == BUFFER_SILENT)
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_DEBUG(0, TM("ppInputConnections[0]->u32BufferFlags == BUFFER_SILENT why ????")));
		}

		ppOutputConnections[0]->u32ValidFrameCount = ppInputConnections[0]->u32ValidFrameCount;
		ppOutputConnections[0]->u32BufferFlags = ppInputConnections[0]->u32BufferFlags;

		break;
	}

	case BUFFER_INVALID:
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("ppInputConnections[0]->u32BufferFlags: BUFFER_INVALID")));
		break;

	default:
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("ppInputConnections[0]->u32BufferFlags - unknown value: %lu"),
			ppInputConnections[0]->u32BufferFlags));
		break;
	}
}
#pragma AVRT_CODE_END

HRESULT MicFusionBridge::NonDelegatingQueryInterface(const IID& iid, void** ppv)
{
	LogGUID(iid);

	if (iid == __uuidof(IUnknown))
		*ppv = static_cast<INonDelegatingUnknown*>(this);
	else if (iid == __uuidof(IAudioProcessingObject))
		*ppv = static_cast<IAudioProcessingObject*>(this);
	else if (iid == __uuidof(IAudioProcessingObjectRT))
		*ppv = static_cast<IAudioProcessingObjectRT*>(this);
	else if (iid == __uuidof(IAudioProcessingObjectConfiguration))
		*ppv = static_cast<IAudioProcessingObjectConfiguration*>(this);
	else if (iid == __uuidof(IAudioSystemEffects))
		*ppv = static_cast<IAudioSystemEffects*>(this);
	else
	{
		//SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_ERROR(0, TM("E_NOINTERFACE")));

		*ppv = NULL;
		return E_NOINTERFACE;
	}

	reinterpret_cast<IUnknown*>(*ppv)->AddRef();

	P7_Flush();


	return S_OK;
}

ULONG MicFusionBridge::NonDelegatingAddRef()
{
	SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("refCount: %li"),
		InterlockedExchangeAdd(&refCount, 0)));
	P7_Flush();

	return InterlockedIncrement(&refCount);
}

ULONG MicFusionBridge::NonDelegatingRelease()
{
	SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("refCount: %li"),
		InterlockedExchangeAdd(&refCount, 0)));

	P7_Flush();

	if (InterlockedDecrement(&refCount) == 0)
	{
		delete this;
		return 0;
	}

	return refCount;
}