#pragma once
#include <cstdint>
#include <algorithm>

#include <guiddef.h>


//TODO: �� ���� ���������� ���� align ������� ���������� ����� ����� ������� �� ����.

//���������� ID � ����� ����� ������������ ��� ����������� �������������, � ����� ��� Initial �� ��� �� ������ ���, ���� ��� �� ����������, � ����� ���� ������ ��� ���� init �� ������� ������� ID, ����� ������ � ��� ��������.


//enum class MessageType : uint32_t   //TODO: make enum class
//{
//	Initial,
//	DeInitial,
//	GetSamplesInfo,
//	StartMixing,
//	StopMixing,
//	UpdateSettings,  //ChangeDb, ��������� ����, ��� ������, 0 db - ������������ ���������, + - �������� �����(���� 3, �� �� 2, ���� 6 �� �� 4 � ��) � � - ����� ��� ��., ��� �� ����� � �������� ��������� ��� ����� ������� ��� �� �������� ������.
//	SendSamples,      //����� ���������� � ��� ��� ��������� � ��� �� ����� ������� APO ������ �� ������ ������ ����������, ���� �� ������ ��������, ��� �������� UnclokForProcess � ����� �������� LockForProcess, ������� ��� ����������
//					 //���������, � ���������� ����� ������ �� �������, �� ����� � �������� APO. � ����� ������ ����� ������ ����� SampleRateIsChanged, �� ���� ���� ���� � ��� UnlockForProcess � ����� LockForProcess ��� ����� ����������,
//					 //�� ������ ������ �� ����� ������ �����, �� ��� ���������� ������� ������� ���������. ���������� ��� ������� bool � uint32_t - SampleRate.
//	ColdStart,
//	GetLogInfo,      //�������� Log ���������� � �������� �� � ����, TODO: ���������� ���� � APO ����� �� ������� ��� �� ����
//	GetDebugInfo     //����� �������� ���� �������� ���������� � ��������� �������� ���������� APO(����������� � ������� ��������). ��� �������������� � ����������� ����.
//};


enum class MessageType : uint32_t
{
	SendSamples = 0x10,
	ColdStart,
	EmptySamples
	//TODO: EmptySamples
};


enum class APOStatus
{
	APOSucces,
	WaitingForMixing,
	APOSamplesEnded
};



struct Message
{
	MessageType MessageType;
};

enum class BitMaskSendSamples : uint16_t
{
	//Reset = 0,
	APOMuteMainSignal = 1ul << 0ul
};
static uint16_t operator&(uint16_t LValue, BitMaskSendSamples BitMaskSendSamplesRValue)
{
	return LValue & static_cast<uint16_t>(BitMaskSendSamplesRValue);
}
static void operator|=(uint16_t& LValue, BitMaskSendSamples BitMaskSendSamplesRValue)
{
	LValue |= static_cast<uint16_t>(BitMaskSendSamplesRValue);
}

struct SendSamplesStructRequest : Message
{
	int32_t FramesCount;    //��� ����� ���������� �������.
	int32_t ChannelCount;
	float SampleRate;
	uint16_t BitMask;
	uint8_t pad1[2];

	//Answer
	bool HasAnswer;
	uint8_t pad2[3];
	int32_t AnswerSize;    //count of APOAnswerInfo struct placed in memory
};
//constexpr size_t SendSamplesStructRequestSize = sizeof(SendSamplesStructRequest);

struct APOAnswerInfo
{
	float LastTimeAPOProcessCallWithMix;
	GUID AudioDeviceGUID;
};