#include "stdafx.h"

#include "APOEvents.h"

APOEvents::APOEvents()
{
	//P7_Trace_Register_Thread(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), TM("APOEvents THREAD"), GetCurrentThreadId());      //TODO: SafePointerDereference

	SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("APOEvents")));

	wchar_t ProcessName[MAX_PATH];
	if (GetModuleFileNameW(GetModuleHandleW(nullptr), ProcessName, MAX_PATH))
	{
		//https://stackoverflow.com/a/9658875 maybe use _wcslwr
		int i = 0;
		while (ProcessName[i] != L'\0'/* && i < MAX_PATH*/)
		{
			ProcessName[i++] = std::towlower(ProcessName[i]);
		}

		if (i >= MAX_PATH)
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("i >= MAX_PATH"),
				ProcessName));
		}

		if (wcsstr(ProcessName, L"audiodg.exe"))  //TODO: скорее всего не будет работать в Windows <10, потому что там AudioSrv не OWN_PROCESS
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("ProcessName: %s"),
				ProcessName));

			SYSTEM_INFO SI;
			GetSystemInfo(&SI);
			std::wstring SIString = std::format(L"GetSystemInfo(dwOemId = {} dwPageSize = {} lpMinimumApplicationAddress = {} lpMaximumApplicationAddress = {} dwActiveProcessorMask = {} "
				"dwNumberOfProcessors = {} dwProcessorType = {} dwAllocationGranularity = {} wProcessorLevel = {} wProcessorRevision = {})",
				SI.dwOemId,
				SI.dwPageSize,
				SI.lpMinimumApplicationAddress,
				SI.lpMaximumApplicationAddress,
				SI.dwActiveProcessorMask,
				SI.dwNumberOfProcessors,
				SI.dwProcessorType,
				SI.dwAllocationGranularity,
				SI.wProcessorLevel,
				SI.wProcessorRevision);

			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("ProcessName: %s"),
				SIString.c_str()));


			hMainThread.reset(CreateThread(nullptr, /*0*//*4096*//*524'288*/131'072, onUpdateStatic, this, 0, nullptr));
			if (!hMainThread.is_valid())
			{
				SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("CreateThread failed, error: %s"),
					ParseWindowsError(GetLastError()).c_str()));
			}

			if (hMainThread.is_valid())
			{
				if (!SetThreadPriority(hMainThread.get(), THREAD_PRIORITY_TIME_CRITICAL))
				{
					SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("SetThreadPriority failed, error: %s"),
						ParseWindowsError(GetLastError()).c_str()));
				}
			}
		}
		else
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_INFO(0, TM("Attempt create thread from ProcessName: %s"),
				ProcessName));
		}
	}
	else
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("GetModuleFileNameW error: %s"),
			ParseWindowsError(GetLastError()).c_str()));
	}

	P7_Flush();
}

APOEvents::~APOEvents()
{
	SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("~APOEvents")));

	if (m_ListOfAPO.size()  > 0)
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("[LOGIC ERROR] m_ListOfAPO.size()  > 0 in desturctor m_ListOfAPO must be empty")));
	}

	DWORD ExitCode;
	const BOOL ExitCodeIsSuccess = GetExitCodeThread(hMainThread.get(), &ExitCode);
	if (IsThreadWorking.load(std::memory_order_seq_cst) || WaitForSingleObject(hMainThread.get(), 0) == WAIT_TIMEOUT ||
		(ExitCodeIsSuccess && ExitCode == STILL_ACTIVE))
	{
		TerminateWorkingThread();
	}

	//P7_Trace_Unregister_Thread(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), GetCurrentThreadId());   //TODO: SafePointerDereference
}



void APOEvents::AddAPOPointer(MicFusionBridge* pAPO)
{
	SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("AddAPOPointer")));

	size_t ListSize;

	{
		std::lock_guard<std::mutex> lock(m_MutexListOfSCSPQueue);

		m_ListOfAPO.push_back(pAPO);

		ListSize = m_ListOfAPO.size();
	}

	SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_DEBUG(0, TM("m_ListOfAPO.size() = %llu"),
		ListSize));
}

void APOEvents::RemoveAPOPointer(MicFusionBridge* pAPO)
{
	SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("RemoveAPOPointer")));

	bool IsListNotFound = false;
	size_t ListSize;

	{
		//TODO: maybe make spin lock
		std::lock_guard<std::mutex> lock(m_MutexListOfSCSPQueue);  //TODO: проверить, можно ли делать объект класса std::lock_guard константым

		const auto FoundedPointer = std::find(m_ListOfAPO.begin(), m_ListOfAPO.end(), pAPO);
		if (FoundedPointer != m_ListOfAPO.end())
		{
			m_ListOfAPO.erase(FoundedPointer);
		}
		else
		{
			IsListNotFound = true;
		}

		ListSize = m_ListOfAPO.size();
	}

	if (IsListNotFound)
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("pAPO not found in m_ListOfAPO")));
	}

	SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_DEBUG(0, TM("m_ListOfAPO.size() = %llu"),
		ListSize));
}


DWORD WINAPI APOEvents::onUpdateStatic(LPVOID lpThreadParametr)
{
	APOEvents* This = static_cast<APOEvents*>(lpThreadParametr);

	This->IsThreadWorking.store(true, std::memory_order_release);
	auto ResetThreadScopeExit = wil::scope_exit([&This] { This->IsThreadWorking.store(false, std::memory_order_release); });

	SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_INFO(0, TM("onUpdateStatic")));

	SECURITY_DESCRIPTOR sd{};  //TODO: мб переделать чтобы не все операции с Event были доступны, например сделать доступными только SetEvent и WaitForSingleObject
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
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("SetSecurityDescriptorDacl failed, error: %s"),
			ParseWindowsError(GetLastError()).c_str()));
		return 2;
	}
	SECURITY_ATTRIBUTES sa{ .nLength = sizeof(sa), .lpSecurityDescriptor = &sd, .bInheritHandle = TRUE };




	//Global interprocess mutex to protect CreateFileMappingW, CreateEventW
	//-------Start of Global interprocess mutex----------
	wil::unique_handle hMutexCreateMapping(CreateMutexW(&sa, FALSE, CreateMappingMutexString));
	if (!hMutexCreateMapping.is_valid())
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("CreateMutexW failed, error: %s"),
			ParseWindowsError(GetLastError()).c_str()));
		return 3;
	}
	auto ReleaseMutexScope = wil::scope_exit([&hMutexCreateMapping]
		{
			if (!ReleaseMutex(hMutexCreateMapping.get()))   //TODO: а надо ли вообще это делать ??? именно там где WaitValue != WAIT_OBJECT_0(СКОРЕЕ ВСЕГО - ДА)
			{
				SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("ReleaseMutex failed, error: %s"),
					ParseWindowsError(GetLastError()).c_str()));
			}
			hMutexCreateMapping.reset();
		});
	if (const DWORD WaitValue = WaitForSingleObject(hMutexCreateMapping.get(), INFINITE);
		WaitValue != WAIT_OBJECT_0)
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("WaitForSingleObject failed, error: %s"),
			ParseWaitForSingleObjectValue(WaitValue).c_str()));
		return 4;
	}




	//TODO: важно: https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-createfilemappingw#:~:text=Therefore%2C%20to%20fully,required%20access%20rights.
	This->hMapFile.reset(OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, MappingObjectForEventAPOString));
	if (!This->hMapFile.is_valid())
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_INFO(0, TM("OpenFileMappingW error: %s"),
			ParseWindowsError(GetLastError()).c_str()));

		This->hMapFile.reset(CreateFileMappingW(INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE, 0, 
			MappingObjectForEventAPOBufferSize, MappingObjectForEventAPOString));
	}
	if (!This->hMapFile.is_valid())
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_INFO(0, TM("CreateFileMappingW error: %s"),
			ParseWindowsError(GetLastError()).c_str()));
		return 5;
	}


	This->pMappedBuffer.reset(MapViewOfFile(This->hMapFile.get(), FILE_MAP_READ | FILE_MAP_WRITE, 0, 0,
		MappingObjectForEventAPOBufferSize));
	if (!This->pMappedBuffer)
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("MapViewOfFile failed, error: %s"),
			ParseWindowsError(GetLastError()).c_str()));
		return 6;
	}


	This->hMessageEmptiedEvent.reset(CreateEventW(&sa, FALSE, FALSE, MessageEmptiedEvent));
	if (!This->hMessageEmptiedEvent.is_valid())
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("CreateEventW(MessageEmptiedEvent) failed, error: %s"),
			ParseWindowsError(GetLastError()).c_str()));
		return 7;
	}
	This->hMessageSentEvent.reset(CreateEventW(&sa, FALSE, FALSE, MessageSentEvent));
	if (!This->hMessageSentEvent.is_valid())
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("CreateEventW(MessageSentEvent) failed, error: %s"),
			ParseWindowsError(GetLastError()).c_str()));
		return 8;
	}
	ReleaseMutexScope.reset();
	//-------End of Global interprocess mutex----------



	This->hAPOMutex.reset(CreateMutexW(&sa, 0, APOInstanceRunningMutexString));
	if (!This->hAPOMutex.is_valid())
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("CreateMutexW failed, error: %s"),
			ParseWindowsError(GetLastError()).c_str()));
		return 9;
	}

	return This->onUpdate();
	//FreeLibraryAndExitThread(static_cast<HMODULE>(lpThreadParametr), EXIT_SUCCESS);
}


#pragma AVRT_CODE_BEGIN
DWORD APOEvents::onUpdate()
{
	SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_INFO(0, TM("onUpdate")));

	//((SendSamplesStructRequest*)pMappedBuffer.get())->MessageType = MessageType::GetDebugInfo;   //TODO: придумать и заменить на сообщение по типу холостой ход
	static_cast<SendSamplesStructRequest*>(pMappedBuffer.get())->MessageType = MessageType::ColdStart;
	LogingBooleanWindowsError(SetEvent(hMessageEmptiedEvent.get()));

	while (!StartingClassDestructor.load(std::memory_order_seq_cst))
	{
		//TODO: обернуть все обращения к памяти в __try __except и лоигровать в случае чего(а возможно еще что то залогировать дополнительное)
		// Wait for a message to become available
		const DWORD WaitValue = WaitForSingleObject(hMessageSentEvent.get(), INFINITE);
		if (WaitValue != WAIT_OBJECT_0)
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_ERROR(0, TM("WaitForSingleObject(hMessageSentEvent, INFINITE) returned: %s"),
				ParseWaitForSingleObjectValue(WaitValue).c_str()));
		}

		if (StartingClassDestructor.load(std::memory_order_seq_cst))
		{
			break;
		}

		Message* CurrentMessage = static_cast<Message*>(pMappedBuffer.get());
		//bool Succes = true;
		switch (CurrentMessage->MessageType)
		{
		case MessageType::ColdStart:
			//TODO: ColdStart, maybe skip
			break;

		case MessageType::EmptySamples:
			//Skip
			break;

		case MessageType::SendSamples:
		{
			SendSamplesStructRequest* pThisSendSamplesStruct = static_cast<SendSamplesStructRequest*>(pMappedBuffer.get());

			const auto ChannelCount = pThisSendSamplesStruct->ChannelCount;
			const auto SampleRate = pThisSendSamplesStruct->SampleRate;
			const auto FramesCount = pThisSendSamplesStruct->FramesCount;
			const uint16_t BitMask = pThisSendSamplesStruct->BitMask;

			//const auto ChannelCount = std::atomic_ref<decltype(pThisSendSamplesStruct->ChannelCount)>{ pThisSendSamplesStruct->ChannelCount }.load();
			//const auto SampleRate = std::atomic_ref<decltype(pThisSendSamplesStruct->SampleRate)>{ pThisSendSamplesStruct->SampleRate }.load();
			//const auto FramesCount = std::atomic_ref<decltype(pThisSendSamplesStruct->FramesCount)>{ pThisSendSamplesStruct->FramesCount }.load();

			//const auto GivedSamplesCount = static_cast<uint64_t>(FramesCount) * static_cast<uint64_t>(ChannelCount);

			if (FramesCount == 0)
			{
				break;
			}

			if (ChannelCount <= 0 || SampleRate <= 0 || FramesCount < 0)
			{
				SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("incorrect(negative) passed argument: [ChannelCount: %d or SampleRate: %f or FramesCount: %d]"),
					ChannelCount, SampleRate, FramesCount));
				break;
			}

			if (static_cast<uint64_t>(ChannelCount) * static_cast<uint64_t>(FramesCount) * sizeof(float) + sizeof(SendSamplesStructRequest) > MappingObjectForEventAPOBufferSize)
			{
				SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("passed argument cause read buffer overrun: [ChannelCount: %d, FramesCount: %d]"),
					ChannelCount, FramesCount));
				break;
			}

			const float* pSamples = reinterpret_cast<float*>(pThisSendSamplesStruct + 1);

			std::vector<APOAnswerInfo> APOAnswersInfoVec;
			{
				std::lock_guard<std::mutex> lock(m_MutexListOfSCSPQueue);
				for (auto& ElementOfAPO : m_ListOfAPO)
				{
					APOAnswersInfoVec.push_back(ElementOfAPO->ResampleAndWriteSCSPQueue(pSamples, FramesCount, SampleRate, ChannelCount, BitMask));
				}
			}

			if (APOAnswersInfoVec.size() * sizeof(APOAnswerInfo) + sizeof(SendSamplesStructRequest) < MappingObjectForEventAPOBufferSize)
			{
				pThisSendSamplesStruct->HasAnswer = true;
				pThisSendSamplesStruct->AnswerSize = APOAnswersInfoVec.size();
				APOAnswerInfo* pAPOAnswerInfo = reinterpret_cast<APOAnswerInfo*>(pThisSendSamplesStruct + 1);
				for (const auto& APOAnswer : APOAnswersInfoVec)
				{
					memcpy(pAPOAnswerInfo, &APOAnswer, sizeof(*pAPOAnswerInfo));
					pAPOAnswerInfo++;
				}
			}

			//TODO:
			//pThisSendSamplesStruct->ChannelCount = 0;
			//pThisSendSamplesStruct->SampleRate = 0;
			//pThisSendSamplesStruct->FramesCount = 0;
			//pThisSendSamplesStruct->BitMask = 0;

			break;
		}
		default:
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("WTF ????, message->MessageType is unknown: %d"),
				(int)CurrentMessage->MessageType));
			break;
		}

		LogingBooleanWindowsError(SetEvent(hMessageEmptiedEvent.get()));
	}

	return 0;
}
#pragma AVRT_CODE_END

void APOEvents::TerminateWorkingThread()
{
	SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_INFO(0, TM("TerminateWorkingThread")));

	//TODO: эта схема не иеальна, если APOEvents::onUpdate() будет постоянно ждать WaitForSingleObject, то получается что может тут чуть ли не вечно исполнятья
	//Возможное решение, сделать здесь hMessageSentEvent.reset() и затем в коде метода APOEvents::onUpdate() сделать проверку, типа если StartingClassDestructor == true, то сделать break;

	SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_INFO(0, TM("IMPORTANT Before StartingClassDestructor"))); P7_Flush();

	StartingClassDestructor.store(true, std::memory_order_release);

	if (hMessageSentEvent.is_valid())
	{
		LogingBooleanWindowsError(SetEvent(hMessageSentEvent.get()));
	}

	SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_INFO(0, TM("IMPORTANT Inside StartingClassDestructor"))); P7_Flush();

	//if (const DWORD WaitValue = WaitForSingleObject(hMainThread.get(), INFINITE);
	if (const DWORD WaitValue = WaitForSingleObject(hMainThread.get(), 1'000);
		WaitValue != WAIT_OBJECT_0)
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("WaitForSingleObject(hMessageSentEvent, INFINITE) returned: %s"),
			ParseWaitForSingleObjectValue(WaitValue).c_str()));


		if (hMessageSentEvent.is_valid())
		{
			LogingBooleanWindowsError(SetEvent(hMessageSentEvent.get()));
		}


		if (const DWORD WaitValue2 = WaitForSingleObject(hMainThread.get(), 1'000);
			WaitValue2 != WAIT_OBJECT_0)
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("Second WaitForSingleObject(hMessageSentEvent, INFINITE) returned: %s"),
				ParseWaitForSingleObjectValue(WaitValue2).c_str()));
		}

		/*if (!TerminateThread(hMainThread.get(), 0))
		{
			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("TerminateThread(hMainThread.get(), 0) error: %s"),
				ParseWaitForSingleObjectValue(GetLastError()).c_str()));
		}*/
	}

	SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_INFO(0, TM("IMPORTANT After StartingClassDestructor"))); P7_Flush();
}


APOEvents* g_pEvents;