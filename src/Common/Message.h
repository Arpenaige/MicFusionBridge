#pragma once
#include <cstdint>
#include <algorithm>

#include <guiddef.h>


//TODO: по всем структурам надо align сделать одинаковый чтобы потом проблем не было.

//Уникальный ID я думаю можно использовать для однозначной идентификации, я думаю про Initial мы как то задаем его, либо где то генерируем, а потом если придет еще один init то вернуть текущий ID, чтобы понять с кем общаться.


//enum class MessageType : uint32_t   //TODO: make enum class
//{
//	Initial,
//	DeInitial,
//	GetSamplesInfo,
//	StartMixing,
//	StopMixing,
//	UpdateSettings,  //ChangeDb, громкость того, что миксим, 0 db - оригинальная громкость, + - умножаем семпл(если 3, то на 2, если 6 то на 4 и тд) и с - точно так же., так же можно и добавить настройку что нужно миксить или же заменять данные.
//	SendSamples,      //Ответ обязателен и при том сообщение о том по какой причине APO данные не принял данные обяательно, одна из причин например, что вызвался UnclokForProcess а затем например LockForProcess, поэтому мог поменяться
//					 //семплрейт, а ресемплить нужно только на клиенте, но никак в драйвере APO. А ответ должен иметь формат такой SampleRateIsChanged, то есть если хоть и был UnlockForProcess а затем LockForProcess без смены семплрейта,
//					 //то ничего менять не нужно скорее всего, ну или возвращать клинету текущий семплрейт. Возвращает как минимум bool и uint32_t - SampleRate.
//	ColdStart,
//	GetLogInfo,      //Получить Log информацию и записать ее в файл, TODO: подебажить прям в APO можно ли создать как то файл
//	GetDebugInfo     //Можно например сюда получать информацию о последних временах исполнения APO(накапливать в очередь например). Для профилирования и оптимизации кода.
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
	int32_t FramesCount;    //Без учета количества каналов.
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