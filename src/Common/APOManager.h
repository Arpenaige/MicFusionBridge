#pragma once
#include <Windows.h>

#include "WinReg.hpp"
#include "GlobalConstValues.hpp"
#include "SCSPQueue.h"
#include "Message.h"
#include "GlobalSystemTimer.hpp"
//#include "Singleton.hpp"
//#include "WindowsUtil.h"
//#include "Common.h"

//#include <mmdeviceapi.h>
//#include <Functiondiscoverykeys_devpkey.h>
//#include <Audioclient.h>

#include <wil/com.h>
#include <wil/resource.h>



////TODO: class
//class ErrorInfo
//{
//public:
//	ErrorInfo();
//	~ErrorInfo();
//
//private:
//	int64_t WinErrorCode;   // if 0 - no error and not display error
//	int32_t InternalCodeError;
//	std::wstring ErrorDescription;
//};
////TODO: подумать над названием


enum class ManageAPOParams
{
	RegisterAPO,
	UnRegisterAPO,
	RestartWindowsAudio,
	Install_DeInstall_AudioDevices,
	SetFixMonoValue
};

int ManageAPO(ManageAPOParams MAPOE, std::wstring AddedParameters = L"");

DWORD WINAPI CaptureMicrophoneFunc(LPVOID lpParam);

bool APOIsLoaded();

int CheckAPOStatus();

class APOManager
{
public:
	APOManager();
	~APOManager();

	void SendSamplesAsync(float* pAudioBuffer, size_t AudioBufferSize, float SampleRate, int16_t NumberOfChannels, uint16_t BitMask);

private:
	static DWORD WINAPI WorkerThreadStatic(LPVOID lpThreadParametr);

	DWORD /*WINAPI*/ WorkerThread();

	int Connect(bool IsNeedLogging);//Connect to APO /*Call only from WorkerThread*/

	void TerminateWorkerThread();

private:
	//WARNING: The mega order is important, otherwise if AudioBufferElementCount comes after AudioBuffer, then AudioBufferElementCount will not be initialized
	const size_t AudioBufferElementCount = 1024 * 256 / sizeof(float);	//256 Kb Buffer
	SCSPQueue<float> AudioBuffer;

	wil::unique_handle LocalEventSync;
	wil::unique_handle WorkerThreadHandle;

	std::atomic_bool StartingClassDestructor = false;
	std::atomic_bool IsThreadWorking = false;

	std::atomic<uint64_t> SampleRateChannelCountBitMask; //0-31 bits contains floating point of SampleRate, 32-47 bits contains ChannelCount, 48-63 bits contains BitMask

	std::atomic_bool IsAPOConnected = false;

public:
	//For vst use
	class APOAnswerDevices
	{
	public:
		APOAnswerDevices() {}
		~APOAnswerDevices() {}
		
		template<bool UseTryLock = true>
		std::vector<APOAnswerInfo> load()
		{
			if constexpr(UseTryLock)
			{
				std::unique_lock<std::mutex> lock(m, std::try_to_lock);
				if (lock.owns_lock())
				{
					std::vector<APOAnswerInfo> APOAnswersInfoVecCopy = APOAnswersInfoVec;
					return APOAnswersInfoVecCopy;
				}
				else
				{
					return {};
				}
			}
			else
			{
				std::lock_guard<std::mutex> lock(m);
				std::vector<APOAnswerInfo> APOAnswersInfoVecCopy = APOAnswersInfoVec;
				return APOAnswersInfoVecCopy;
			}
		}

		void store(const std::vector<APOAnswerInfo>& APOAnswersInfoVec)
		{
			std::lock_guard<std::mutex> lock(m);
			this->APOAnswersInfoVec = APOAnswersInfoVec;
		}

	private:
		std::vector<APOAnswerInfo> APOAnswersInfoVec;
		std::mutex m;
	};
	APOAnswerDevices APOAnswerDevice;

private:



	//WARNING: THIS VARIABLE FOR ONLY USE IN WorkerThread!!!!!!
	//bool IsNeedConnect = true;
	wil::unique_handle MappedFileHandle;
	/*LPCTSTR*/wil::unique_mapview_ptr<void> pMappedBuffer;
	wil::unique_handle MessageEmptiedEventHandle;
	wil::unique_handle MessageSentEventHandle;
};