#pragma once
#include "MicFusionBridge.h"

#include <cwctype>

#include "Common/WindowsUtil.h"

//#include <wil/resource.h>

class MicFusionBridge;

class APOEvents
{
public:
	APOEvents();
	~APOEvents();

	static DWORD WINAPI onUpdateStatic(LPVOID lpThreadParametr);
	DWORD onUpdate();

	void TerminateWorkingThread();

	void AddAPOPointer(MicFusionBridge* APOptr);

	void RemoveAPOPointer(MicFusionBridge* APOptr);

private:
	std::atomic_bool StartingClassDestructor = false;

	wil::unique_mapview_ptr<void> pMappedBuffer;
	wil::unique_handle hMapFile;

	wil::unique_handle hMessageEmptiedEvent;
	wil::unique_handle hMessageSentEvent;

	wil::unique_handle hAPOMutex;

	wil::unique_handle hMainThread;

	std::list<MicFusionBridge*> m_ListOfAPO;
	std::mutex m_MutexListOfSCSPQueue;

	std::atomic_bool IsThreadWorking = false;
};

extern APOEvents* g_pEvents;