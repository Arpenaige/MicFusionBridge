#pragma once
#include <Unknwn.h>
#include <audioenginebaseapo.h>
#include <BaseAudioProcessingObject.h>

#include <iostream>
#include <fstream>
#include <vector>
#include <array>
#include <mutex>
#include <random>
#include <set>
#include <queue>
#include <functional>
#include <memory>
#include <format>

#include <soxr.h>

#include <Windows.h>
#include <sddl.h>
#include <combaseapi.h>
#include <dbghelp.h>

#include "APOEvents.h"

#include "Common/Singleton.hpp"
#include "Common/Common.h"
#include "Common/SCSPQueue.h"
#include "Common/GlobalSystemTimer.hpp"

#include "Common/Message.h"
#include "Common/GlobalConstValues.hpp"

#include <wil/com.h>
#include <wil/resource.h>

#include "P7/P7_Client.h"
#include "P7/P7_Trace.h"


/*Release X64 были такие, но не собиралось:
* libcmt.lib
  libcmtd.lib
  msvcrtd.lib
*/

#define LogGUID(iid) {static_assert(sizeof(OLECHAR) == 2, "[BUG] Sizeof(OLECHAR) must be 2"); \
OLECHAR str[39] = {}; \
std::ignore = StringFromGUID2(iid, str, sizeof(str) / sizeof(OLECHAR)); \
SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_INFO(0, TM("GUID: %s"), str));}

#define LogUNCOMPRESSEDAUDIOFORMAT(FType, Format) { \
SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_INFO(0, TM("[%s] dwBytesPerSampleContainer: %lu"), FType, Format.dwBytesPerSampleContainer)); \
SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_INFO(0, TM("[%s] dwChannelMask: %lu"), FType, Format.dwChannelMask)); \
SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_INFO(0, TM("[%s] dwSamplesPerFrame: %lu"), FType, Format.dwSamplesPerFrame)); \
SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_INFO(0, TM("[%s] dwValidBitsPerSample: %lu"), FType, Format.dwValidBitsPerSample)); \
SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_INFO(0, TM("[%s] fFramesPerSecond: %f"), FType, Format.fFramesPerSecond)); }

interface INonDelegatingUnknown
{
	virtual HRESULT NonDelegatingQueryInterface(REFIID riid, LPVOID* ppv) PURE;
	virtual ULONG NonDelegatingAddRef(void) PURE;
	virtual ULONG NonDelegatingRelease(void) PURE;
};

class __declspec (uuid("A0A97884-B1A0-466E-8C6D-69D71016BFAA"))
	MicFusionBridge : public CBaseAudioProcessingObject, public IAudioSystemEffects, public INonDelegatingUnknown
{
public:
	MicFusionBridge(IUnknown * pUnkOuter);
	virtual ~MicFusionBridge();

	// IUnknown
	virtual HRESULT __stdcall QueryInterface(const IID& iid, void** ppv);
	virtual ULONG __stdcall AddRef();
	virtual ULONG __stdcall Release();

	// IAudioProcessingObject
	virtual HRESULT __stdcall GetLatency(HNSTIME* pTime);
	virtual HRESULT __stdcall Initialize(UINT32 cbDataSize, BYTE* pbyData);
	virtual HRESULT __stdcall IsInputFormatSupported(IAudioMediaType* pOutputFormat,
		IAudioMediaType* pRequestedInputFormat, IAudioMediaType** ppSupportedInputFormat);

	// IAudioProcessingObjectConfiguration
	virtual HRESULT __stdcall LockForProcess(UINT32 u32NumInputConnections,
		APO_CONNECTION_DESCRIPTOR** ppInputConnections, UINT32 u32NumOutputConnections,
		APO_CONNECTION_DESCRIPTOR** ppOutputConnections);
	virtual HRESULT __stdcall UnlockForProcess(void);

	// IAudioProcessingObjectRT
	virtual void __stdcall APOProcess(UINT32 u32NumInputConnections,
		APO_CONNECTION_PROPERTY** ppInputConnections, UINT32 u32NumOutputConnections,
		APO_CONNECTION_PROPERTY** ppOutputConnections);

	// INonDelegatingUnknown
	virtual HRESULT __stdcall NonDelegatingQueryInterface(const IID& iid, void** ppv);
	virtual ULONG __stdcall NonDelegatingAddRef();
	virtual ULONG __stdcall NonDelegatingRelease();

	APOAnswerInfo ResampleAndWriteSCSPQueue(const float* GivedSamplesPtr, int32_t FramesCount, float FramesRate, int32_t ChannelCount, uint16_t BitMask);


	static const CRegAPOProperties<1> regProperties;
	static long instCount;

private:
	long refCount;
	IUnknown* pUnkOuter;
	uint32_t APOChannelCount = 0;
	uint32_t u32MaxFrameCount = 0;
	float APOFrameRate = 0.f;

	float APOProcessLatency = 0.f;    //in seconds

	bool StartingClassDestructor = false;
	std::atomic<bool> WaitExitingThread = true;

	std::unique_ptr<float[]> InputBuffer;
	size_t InputBufferSize = 0;

	std::unique_ptr<float[]> OutputBuffer;
	size_t OutputBufferSize = 0;

	std::unique_ptr<SCSPQueue<float>> m_pSCSPQueue;

	soxr_t SoxrObject = nullptr;
	uint32_t SoxrChannelCount = 0;
	float SoxrInputFrameRate = 0;
	float SoxrOutputFrameRate = 0;

	const soxr_io_spec_t SoxrIOSpec = soxr_io_spec(SOXR_FLOAT32_I, SOXR_FLOAT32_I);
	const soxr_quality_spec_t SOXRQualitySpec = soxr_quality_spec(SOXR_VHQ, SOXR_VR);

	//const soxr_io_spec_t SoxrIOSpec = soxr_io_spec(SOXR_FLOAT32_I, SOXR_FLOAT32_I);
	//const soxr_quality_spec_t SOXRQualitySpec = soxr_quality_spec(SOXR_MQ, SOXR_VR);

	std::atomic<uint16_t> LocalBitMask = static_cast<uint16_t>(0);

	//const double APOLatency = /*100.0*//*20.0*/11.0;  //in milliseconds
	//const double APOLatency = /*100.0*//*20.0*/16.0;  //in milliseconds
	//const double APOLatency = 20.5;  //in milliseconds
	//TODO: make dynamic latency 2  * APO latency(getting in LockForProcess - u32MaxFrameCount)

	std::atomic<bool> CanMixing = false;
	//std::atomic<APOStatus> status = APOStatus::WaitingForMixing;

	bool APOLoggedOnce{ false };

	wil::com_ptr_nothrow<IAudioProcessingObject> pPreviousAPO;
	wil::com_ptr_nothrow<IAudioProcessingObjectRT> pPreviousRT;
	wil::com_ptr_nothrow<IAudioProcessingObjectConfiguration> pPreviousConfiguration;
	void ResetPreviousPointers() { pPreviousAPO.reset(); pPreviousRT.reset(); pPreviousConfiguration.reset(); }

	std::atomic<float> LastAPOPorcessCallAndMixSystemGlobalTime = 0.f;  //TODO: reset to 0 in UnlockForProcess

	GlobalSystemTimer<float> GSystemTimer;

	bool ApplyFixMonoSound = false;

	std::atomic<GUID> CurrentAudioDeviceGUID;
	std::wstring CurrentAudioDeviceGUIDString;


	//Only for purpose test
	LONG LockUnlockProcessCounter = 0;
};
//TODO: optimize order of members in class