#pragma once
#include <Windows.h>
#include "ObjectPool.h"
#include "Log.h"


// 디버깅을 위한 함수
// 64비트 값을 64자리 이진 wide 문자열로 변환 (선행 0 포함)
inline void to_bin64(uint64_t v, wchar_t out[80])
{
	int pos = 0;
	for (int i = 63; i >= 0; --i)
	{
		out[pos++] = (v & (1ULL << i)) ? L'1' : L'0';

		// 4비트마다 '/' 삽입, 단 마지막 그룹 뒤에는 넣지 않음
		if (i % 4 == 0 && i != 0)
			out[pos++] = L'/';
	}
	out[pos] = L'\0';
}


//-----------------------------------------------------
// 메모리 디버깅 관련
//-----------------------------------------------------
struct st_MemoryDebug
{
	ULONGLONG cnt;
	ULONGLONG threadId;
	ULONGLONG type;         // 1 : Enqueue  /  2: Dequeue
	ULONGLONG stamp;        // Enqueue : tail Stamp  /  Dequeue : head Stamp
	ULONGLONG enqueueCASNum;
	void* beforePtr;    // Enqueue : before tail        /  Dequeue : after head
	void* afterPtr;     // Enqueue : after tail        /  Dequeue : after head
	ULONGLONG value;
};

#define MD_ENTRY_NUM 200
st_MemoryDebug g_MD[MD_ENTRY_NUM];
ULONG g_MDCnt = 0;
ULONG g_queueChangeCnt = 0;
void SaveMemoryDebugEntry(ULONG cnt, ULONG threadId,ULONG type, ULONG stamp, ULONG enqueueCASCnt,void* bp, void* ap, ULONGLONG value)
{
	int index = InterlockedIncrement(&g_MDCnt) % MD_ENTRY_NUM;
	g_MD[index] = st_MemoryDebug{ cnt, threadId, type, stamp, enqueueCASCnt, bp, ap, value };
}



//-----------------------------------------------------
// 락 프리 큐 구현부
//-----------------------------------------------------
template<typename T>
class CLockFreeQueue
{
public:
	class Node
	{
	public:
		Node()
		{
			next = nullptr;
		}
	public:
		T data;
		Node* next;
	};

public:
	CLockFreeQueue() : nodePool(false)
	{
		size = 0;
		head = nodePool.allocObject();
		tail = head;
		memset(g_MD, 0, sizeof(st_MemoryDebug) * MD_ENTRY_NUM);
	}

	void Enqueue(T data)
	{
		Node* newNode = nodePool.allocObject();
		newNode->data = data;

		wchar_t beforeTail[81], afterTail[81];
		wchar_t beforeMaskedTail[81], afterMaskedTail[81];
		ULONGLONG stamp = 0;
		Node* maskedT;
		while (1)
		{
			Node* t = tail;
			maskedT = UnpackingNode(t);
			Node* nextTail = PackingNode(newNode, GetNodeStamp(t) + 1);

			stamp = GetNodeStamp(t);
			int cnt = stamp + GetNodeStamp(head);
			if (g_LogMode != ELogMode::NOLOG)
			{
				to_bin64(reinterpret_cast<uint64_t>(t), beforeTail);
				to_bin64(reinterpret_cast<uint64_t>(maskedT), beforeMaskedTail);
				to_bin64(reinterpret_cast<uint64_t>(nextTail), afterTail);
				to_bin64(reinterpret_cast<uint64_t>(newNode), afterMaskedTail);
			}

			if (InterlockedCompareExchangePointer((PVOID*)&maskedT->next, newNode, nullptr) == nullptr)
			{
				SaveMemoryDebugEntry(cnt, GetCurrentThreadId(), 1, stamp, 1 ,maskedT, newNode, data);
				if (InterlockedCompareExchangePointer((PVOID*)&tail, nextTail, t) == t)
				{
					//-----------------------------------------
					// 로그 데이터 저장
					//-----------------------------------------
					SaveMemoryDebugEntry(cnt, GetCurrentThreadId(), 1, stamp, 2, maskedT, newNode, data);
					break;
				}
				else
				{
					// tail가 바뀐 경우
					//-----------------------------------------
					// 로그 데이터 저장
					//-----------------------------------------
					SaveMemoryDebugEntry(cnt, GetCurrentThreadId(), 1, stamp, 2, maskedT, newNode, 0xffffffff);
					_LOG(dfLOG_LEVEL_DEBUG, L"[ Enqueue Error ] Change tail After 1st CAS\n");
					//__debugbreak();
				}
			}
		}

		_LOG(dfLOG_LEVEL_DEBUG, L"[ Enqueue : Tail Stamp(%lld)] \n -   beforeTail : %ls \n -      masked : %ls (%016llx) \n -      afterTail : %ls\n -      masked : %ls (%016llx) \n", stamp ,beforeTail, beforeMaskedTail, maskedT, afterTail, afterMaskedTail, newNode);

		InterlockedIncrement(&size);
	}

	
	bool Dequeue(T& value)
	{
		if (size == 0)
			return false;

		wchar_t beforeHead[81], afterHead[81];
		wchar_t beforeMaskedHead[81], afterMaskedHead[81];
		ULONGLONG stamp = 0;
		while (1)
		{
			Node* h = head;
			Node* maskedH = UnpackingNode(h);
			Node* next = maskedH->next;
			if (next == nullptr)
				return false;
			Node* nextHead = PackingNode(next, GetNodeStamp(h) + 1);
			T retValue = next->data;  // 소유권을 획득한 노드의 next의 data기 때문에 CAS 전에 캐싱해야함
			
			stamp = GetNodeStamp(h);
			Node* debugingEmtpy = next->next;
			int cnt = stamp + GetNodeStamp(tail);

			if (g_LogMode != ELogMode::NOLOG)
			{
				to_bin64(reinterpret_cast<uint64_t>(h), beforeHead);
				to_bin64(reinterpret_cast<uint64_t>(maskedH), beforeMaskedHead);
				to_bin64(reinterpret_cast<uint64_t>(nextHead), afterHead);
				to_bin64(reinterpret_cast<uint64_t>(next), afterMaskedHead);
			}
			if (InterlockedCompareExchangePointer((PVOID*)&head, nextHead, h) == h)
			{
				value = retValue;

				//-----------------------------------------
				// 로그 데이터 저장
				//-----------------------------------------
				SaveMemoryDebugEntry(cnt, GetCurrentThreadId(), 2, stamp, 0, maskedH, next, retValue);
				if (debugingEmtpy == nullptr)
				{
					SaveMemoryDebugEntry(cnt, GetCurrentThreadId(), 2, stamp, 0, maskedH, next, 0xdddddddd);
				}
				maskedH->data = 0; // 디버깅용 (반납하기 전 0으로 초기화 후 반납)
				nodePool.freeObject(maskedH);
				break;
			}
		}
		InterlockedDecrement(&size);
		return true;
	}

private:
	inline Node* PackingNode(Node* ptr, ULONGLONG stamp)
	{
		return (Node*)((ULONGLONG)ptr | (stamp << stampShift));
	}
	inline Node* UnpackingNode(Node* ptr)
	{
		return (Node*)((ULONGLONG)ptr & nodeMask);
	}
	inline ULONGLONG GetNodeStamp(Node* ptr)
	{
		return (ULONGLONG)ptr >> stampShift;
	}


public:
	Node* head;
	Node* tail;
	ULONG size;


	//--------------------------------------------
	// Node*의 하위 47비트 추출할 마스크
	//--------------------------------------------
	static const ULONGLONG nodeMask = (1ULL << 47) - 1;
	static const ULONG stampShift = 47;

	CObjectPool<CLockFreeQueue::Node> nodePool;

};