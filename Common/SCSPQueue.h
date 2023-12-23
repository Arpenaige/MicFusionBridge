#include <iostream>
#include <atomic>
#include <cassert>
#include <mutex>
#include <algorithm>
#include <numbers>

#include "Singleton.hpp"
#include "WindowsUtil.h"

template<typename T>
class SCSPQueue
{
public:
	SCSPQueue(const size_t QueueSize) :
		m_uiElementCount(QueueSize), m_uiAvailableElements(0)
	{
		m_pBase = new T[m_uiElementCount];

		m_pHead = m_pTail = m_pBase;

		//SetProcessWorkingSetSize
		//TODO: make with wil
		//VirtualLockMemoryStatus = VirtualLock(m_pBase, m_uiElementCount * sizeof(T));
		//if (!VirtualLockMemoryStatus)
		//{
		//	SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("VirtualLock() failed, error: %s"),
		//		ParseWindowsError(GetLastError()).c_str()));
		//}
	}

	~SCSPQueue()
	{
		//if (VirtualLockMemoryStatus)
		//{
		//	if (!VirtualUnlock(m_pBase, m_uiElementCount * sizeof(T)))
		//	{
		//		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_CRITICAL(0, TM("VirtualUnlock() failed, error: %s"),
		//			ParseWindowsError(GetLastError()).c_str()));
		//	}
		//}
		delete[] m_pBase;
	}

	//BETA: Experemential function, may cause phase change or eq sound
	void ClickReduction(size_t SampleToApply, uint32_t ChannelCount)
	{
		SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_INFO(0, TM("Click Reduction")));
		//Click reduction
		T* HeadCopy = m_pHead;
		const T* End = m_pBase + m_uiElementCount;
		//HeadCopy = HeadCopy == End ? m_pBase : HeadCopy;

		const float Step = (1.f / SampleToApply) * ChannelCount;
		float GainIterator = 1.f;
		float AbsisArgument = 1.f;
		for (size_t i = 0; i < SampleToApply; i += ChannelCount)
		{
			for (size_t j = 0; j < ChannelCount; j++)
			{
				*(HeadCopy++) *= GainIterator;
				if (HeadCopy >= End)
				{
					HeadCopy = m_pBase;
				}
			}

			AbsisArgument -= Step;
			//GainIterator = AbsisArgument * AbsisArgument;  //Using x^2 smoothing function
			GainIterator = std::sin(AbsisArgument * (std::numbers::pi_v<float> / 2.f));  //Using cos(x * PI/2) smoothing function
		}
	}


#pragma AVRT_CODE_BEGIN
	int32_t ReadQueue(T* PtrToWrite, size_t NeedReadElements /*in Elements, not bytes*/, bool AddValues = false, bool fill/*TODO: rename*/ = false, uint32_t ChannelCount = 2)
	{
		std::lock_guard lock(ShiftingMutex);

		const T* Head = m_pHead;
		const size_t AvailElementsInQueue = m_uiAvailableElements.load();    //IMPORTANT TODO LATE(pre-alpha+): очень важно сделать правильный порядок атомарности(например memory_order_acquire)

		if (fill && NeedReadElements > AvailElementsInQueue)
		{
			const float FillRatio = static_cast<float>(AvailElementsInQueue) / NeedReadElements;

			SafePointerDereference(Singleton<VariableMainLogger>::GetInstance().GetTracePtr(), P7_INFO(0, TM("AvailElementsInQueue / NeedReadElements = %f"),
				FillRatio));

			NeedReadElements = AvailElementsInQueue;
			if (NeedReadElements > 0 && !(NeedReadElements % ChannelCount) && FillRatio > 0.1f)
			{
				ClickReduction(NeedReadElements, ChannelCount);
				//TODO: сделать корректный return после применения данной функции, скорее всего нужно вернуть 0 или -1, и пусть копит семплы заново
				//TODO: тоже самое сделать со стартом, нужно будет вернуть все таки Status, три статуса, StartMixing, AlreadyMixing и еще что то
				//https://www.desmos.com/calculator/eamkleeyqh
			}
		}

		if (NeedReadElements <= AvailElementsInQueue && NeedReadElements > 0)
		{
			if (Head + NeedReadElements <= m_pBase + m_uiElementCount)
			{
				if (AddValues)
				{
					std::transform(PtrToWrite, PtrToWrite + NeedReadElements, Head, PtrToWrite, std::plus<>{});
				}
				else
				{
					memcpy(PtrToWrite, Head, NeedReadElements * sizeof(T));
				}
			}
			else
			{
				const size_t SizeToRead = (m_pBase + m_uiElementCount) - Head; //TODO: rename this

				if (AddValues)
				{
					std::transform(PtrToWrite, PtrToWrite + SizeToRead, Head, PtrToWrite, std::plus<>{});
					std::transform(PtrToWrite + SizeToRead, PtrToWrite + NeedReadElements, m_pBase, PtrToWrite + SizeToRead, std::plus<>{});
				}
				else
				{
					memcpy(PtrToWrite, Head, SizeToRead * sizeof(T));
					memcpy(PtrToWrite + SizeToRead, m_pBase, (NeedReadElements - SizeToRead) * sizeof(T));
				}
			}


			m_pHead = m_pBase + ((Head - m_pBase) + NeedReadElements) % m_uiElementCount;
			m_uiAvailableElements.fetch_sub(NeedReadElements);

			////assert(((int64_t)AvailElementsInQueue - (int64_t)NeedReadElements) > -1);

			//return m_uiAvailableElements.load();
			////return AvailElementsInQueue - NeedReadElements;

			if (AvailElementsInQueue != NeedReadElements)
			{
				return m_uiAvailableElements.load();
			}
			else
			{
				return 0;
			}
		}

		return -1;
	}
#pragma AVRT_CODE_END


	//TODO: apply smooth function for 1 APOPorcess buffer and do change with new sound
	//TODO: mutex to ReadQueue and this function, ideally spin lock
	void ShiftAllQueue(size_t /*SamplesToSafe*/KeepSamples /*first samples count we want keep*/, uint32_t ChannelCount = 2)
	{
		//TODO: mutex with ReadQueue
		std::lock_guard lock(ShiftingMutex);

		const T* Head = m_pHead;
		const size_t AvailElementsInQueue = m_uiAvailableElements.load();    //IMPORTANT TODO LATE(pre-alpha+): очень важно сделать правильный порядок атомарности(например memory_order_acquire)

		if (AvailElementsInQueue > KeepSamples)
		{
			ClickReduction(KeepSamples, ChannelCount);

			//m_pHead = m_pBase + ((Head - m_pBase) + NeedReadElements) % m_uiElementCount;
			//m_pTail = (m_pHead + KeepSamples) % m_uiElementCount;
			//m_pTail = m_pBase + ((Head - static_cast<T*>(nullptr)) + KeepSamples) % m_uiElementCount;
			m_pTail = m_pBase + ((Head - m_pBase) + KeepSamples) % m_uiElementCount;
			m_uiAvailableElements.store(KeepSamples);
		}
	}


	std::vector<float> ReadAllQueue()
	{
		const T* Head = m_pHead;
		const size_t NeedReadElements = m_uiAvailableElements.load();    //IMPORTANT TODO LATE(pre-alpha+): очень важно сделать правильный порядок атомарности(например memory_order_acquire)
		std::vector<float> QueueVector;

		if (NeedReadElements <= 0)
		{
			return QueueVector;
		}
		QueueVector.reserve(NeedReadElements);

		if (Head + NeedReadElements <= m_pBase + m_uiElementCount)
		{
			//memcpy(PtrToWrite, Head, NeedReadElements * sizeof(T));
			std::copy(&Head[0], &Head[NeedReadElements], std::back_inserter(QueueVector));
		}
		else
		{
			const size_t SizeToRead = (m_pBase + m_uiElementCount) - Head; //TODO: rename this

			//memcpy(PtrToWrite, Head, SizeToRead * sizeof(T));
			//memcpy(PtrToWrite + SizeToRead, m_pBase, (NeedReadElements - SizeToRead) * sizeof(T));

			std::copy(&Head[0], &Head[SizeToRead], std::back_inserter(QueueVector));
			std::copy(&m_pBase[0], &m_pBase[NeedReadElements - SizeToRead], std::back_inserter(QueueVector));
		}

		m_pHead = m_pBase + ((Head - m_pBase) + NeedReadElements) % m_uiElementCount;
		m_uiAvailableElements.fetch_sub(NeedReadElements);

		//assert(((int64_t)AvailElementsInQueue - (int64_t)NeedReadElements) > -1);

		return QueueVector;
	}


#pragma AVRT_CODE_BEGIN
	int32_t WriteQueue(const T* PtrToRead, size_t NeedWriteElements /*in Elements, not bytes*/)
	{
		const size_t AvailElementsInQueue = m_uiAvailableElements.load();    //IMPORTANT TODO LATE(pre-alpha+): очень важно сделать правильный порядок атомарности(например memory_order_acquire)
		T* Tail = m_pTail;

		if (NeedWriteElements <= m_uiElementCount - AvailElementsInQueue && NeedWriteElements > 0)
		{
			if (Tail + NeedWriteElements <= m_pBase + m_uiElementCount)
			{
				memcpy(Tail, PtrToRead, NeedWriteElements * sizeof(T));
			}
			else
			{
				const size_t SizeToWrite = (m_pBase + m_uiElementCount) - Tail; //TODO: rename this
				memcpy(Tail, PtrToRead, SizeToWrite * sizeof(T));
				memcpy(m_pBase, PtrToRead + SizeToWrite, (NeedWriteElements - SizeToWrite) * sizeof(T));
			}


			m_pTail = m_pBase + ((Tail - m_pBase) + NeedWriteElements) % m_uiElementCount;
			m_uiAvailableElements.fetch_add(NeedWriteElements);

			return m_uiAvailableElements.load();
			//return AvailElementsInQueue + NeedWriteElements;
		}

		//WARN: not implement if we not have free space to write, just return false.
		//return m_uiAvailableElements.load();
		//return 0;
		//return -1;
		return -static_cast<int32_t>(m_uiAvailableElements.load());
	}
#pragma AVRT_CODE_END

private:
	T* m_pBase;

	//All Sizes mean count of Elements, not bytes

	T* m_pHead;
	T* m_pTail;
	std::atomic<size_t> m_uiAvailableElements;   //Available Elements to read.

	const size_t m_uiElementCount;
	std::mutex ShiftingMutex;

	//bool VirtualLockMemoryStatus;
};