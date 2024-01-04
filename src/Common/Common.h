#pragma once
#include "../Common/WinReg.hpp"
#include "../Common/GlobalConstValues.hpp"
//#include "../Common/Singleton.hpp"
//#include "../Common/WindowsUtil.h"

//#include <wil/com.h>
//#include <wil/resource.h>

#include <mmdeviceapi.h>
#include <Functiondiscoverykeys_devpkey.h>
#include <Audioclient.h>

#include <string>
#include <regex>


class AudioDevice
{
private:
	AudioDevice();

public:
	~AudioDevice();

	bool GetNeedUpdate() const { return IsNeedUpdate; }
	void SetNeedUpdate(bool state) { IsNeedUpdate = state; }
	void InvertNeedUpdate() { if (!IsNeedUpdateCopy) IsNeedUpdate = !IsNeedUpdate; }

	//bool GetInstalledSuccessfully() const { return IsInstalledSuccessfully; }
	//void SetInstalledSuccessfully(bool state) { IsInstalledSuccessfully = state; }

	bool GetNeedInstalling() const { return IsNeedInstalling; }
	void SetNeedInstalling(bool state) { IsNeedInstalling = state; }

	std::wstring GetAudioDeviceName() const
	{
		return DeviceName;
	}

	std::wstring GetDeviceGUID() const
	{
		return DeviceGUID;
	}

private:
	std::wstring DeviceGUID;
	std::wstring DeviceName;
	bool IsDefaultDevice = false;

	bool IsWritedInSoftwareRegedit = false;   //SOFTWARE/MicFusionBridge/CapturedDevices
	bool IsWritedInAPORegedit = false;

	bool IsNeedUpdate = false, IsNeedUpdateCopy /*Copy used in InvertNeedUpdate*/ = false;
	bool IsInstalledSuccessfully = false;
	bool IsNeedInstalling = false;

	friend std::vector<AudioDevice> GetAllMicrohponeDevices();
};

bool APOIsWritedInRegedit(const std::wstring& AudioDeviceGUID);

std::vector<AudioDevice> GetAllMicrohponeDevices();

bool DisableProtectedAudioDGIs(DWORD Value);

bool DeviceWritedInSoftwareRegedit(const std::wstring& DeviceGUID);