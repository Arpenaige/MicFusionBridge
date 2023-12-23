#include "stdafx.h"
#include "MicFusionBridge.h"
#include "ClassFactory.h"

long ClassFactory::lockCount = 0;

ClassFactory::ClassFactory()
{
	SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("ClassFactory")));

	refCount = 1;
}

HRESULT __stdcall ClassFactory::QueryInterface(const IID& iid, void** ppv)
{
	SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("QueryInterface")));
	LogGUID(iid);

	if (iid == __uuidof(IUnknown) || iid == __uuidof(IClassFactory))
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("iid is IUnknown or IClassFactory")));

		*ppv = static_cast<IClassFactory*>(this);
	}
	else
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_WARNING(0, TM("iid is NOT IUnknown or IClassFactory")));

		*ppv = NULL;
		return E_NOINTERFACE;
	}

	reinterpret_cast<IUnknown*>(*ppv)->AddRef();
	return S_OK;
}

ULONG __stdcall ClassFactory::AddRef()
{
	SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("refCount: %li"), InterlockedExchangeAdd(&refCount, 0)));

	return InterlockedIncrement(&refCount);
}

ULONG __stdcall ClassFactory::Release()
{
	SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("refCount: %li"), InterlockedExchangeAdd(&refCount, 0)));

	if (InterlockedDecrement(&refCount) == 0)
	{
		delete this;
		return 0;
	}

	return refCount;
}

HRESULT __stdcall ClassFactory::CreateInstance(IUnknown* pUnknownOuter, const IID& iid, void** ppv)
{
	//SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("CreateInstance")));
	LogGUID(iid);

	if (pUnknownOuter != NULL && iid != __uuidof(IUnknown))
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_ERROR(0, TM("pUnknownOuter != NULL && iid != __uuidof(IUnknown)")));

		return E_NOINTERFACE;
	}

	MicFusionBridge* apo = new MicFusionBridge(pUnknownOuter);
	if (apo == NULL)
	//if (apo == nullptr)
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_ERROR(0, TM("new MicFusionBridge(pUnknownOuter) return NULL")));

		return E_OUTOFMEMORY;
	}

	HRESULT hr = apo->NonDelegatingQueryInterface(iid, ppv);

	if (FAILED(hr))
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_ERROR(0, TM("NonDelegatingQueryInterface error: %s"),
			ParseWindowsError(hr).c_str()));
	}

	apo->NonDelegatingRelease();
	return hr;
}

HRESULT __stdcall ClassFactory::LockServer(BOOL bLock)
{
	SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_TRACE(0, TM("%s")
		, bLock ? L"True" : L"False"));

	if (bLock)
		InterlockedIncrement(&lockCount);
	else
		InterlockedDecrement(&lockCount);

	return S_OK;
}
