#pragma once
#include <Windows.h>
#include "ObjectPool.h"
#include "Log.h"

//-----------------------------------------------------
// 메모리 디버깅 관련
//-----------------------------------------------------
#define MD_ENTRY_NUM 1000




#define MD_TYPE_ENQUEUE 0xeeeeeeeeeeeeeeee
#define MD_TYPE_DEQUEUE 0xdddddddddddddddd
struct st_MemoryDebug
{
	st_MemoryDebug()
		: threadId(0), type(0), start(0), end(0),
		allocNode(nullptr), oldHead(nullptr), oldTail(nullptr),
		curHead(nullptr), curTail(nullptr), value(0) {}
	st_MemoryDebug(ULONGLONG _type, ULONGLONG _start, ULONGLONG _end, void* _allocNode, void* _oldNode, void* _afterNode, ULONGLONG _value)
	{
		threadId = GetCurrentThreadId();
		type = _type;
		if (type == MD_TYPE_ENQUEUE)
		{
			allocNode = _allocNode;
			oldTail = _oldNode;
			oldHead = nullptr;
			curTail = _afterNode;
			curHead = nullptr;
			value = _value;
		}
		else
		{
			allocNode = nullptr;
			oldTail = nullptr;
			oldHead = _oldNode;
			curTail = nullptr;
			curHead = _afterNode;
			value = _value;
		}
		start = _start;
		end = _end;
	}
	ULONGLONG threadId;
	ULONGLONG type;     // Enqueue / Dequeue
	ULONGLONG start;
	ULONGLONG end;
	void* allocNode;
	void* oldHead;
	void* curHead;
	void* oldTail;
	void* curTail;
	ULONGLONG value;
};



st_MemoryDebug g_MD[MD_ENTRY_NUM];
ULONG g_MDIndex = 0;
ULONGLONG g_interlockCnt = 0;

void SaveMemoryDebugEntry(ULONGLONG type, ULONGLONG start, ULONGLONG end, void* allocNode, void* oldNode, void* curNode, ULONGLONG value)
{
	int index = InterlockedIncrement(&g_MDIndex) % MD_ENTRY_NUM;
	g_MD[index] = st_MemoryDebug{type, start, end, allocNode, oldNode, curNode, value };
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

		ULONGLONG stamp = 0;
		Node* maskedT;
		while (1)
		{
			int start = InterlockedIncrement(&g_interlockCnt);

			Node* t = tail;
			maskedT = UnpackingNode(t);
			Node* nextTail = PackingNode(newNode, GetNodeStamp(t) + 1);
			
			int end = InterlockedIncrement(&g_interlockCnt);
			
			if (InterlockedCompareExchangePointer((PVOID*)&maskedT->next, newNode, nullptr) == nullptr)
			{
				SaveMemoryDebugEntry(MD_TYPE_ENQUEUE, start, end, newNode, maskedT, nullptr, 1);
				if (InterlockedCompareExchangePointer((PVOID*)&tail, nextTail, t) == t)
				{
					SaveMemoryDebugEntry(MD_TYPE_ENQUEUE, start, end, newNode, maskedT, newNode, 2);
					break;
				}
				else
				{
					// tail가 바뀐 경우
					SaveMemoryDebugEntry(MD_TYPE_ENQUEUE, start, end, newNode, maskedT, (void*)0xffffffffffffffff, 0xffffffffffffffff);
					_LOG(dfLOG_LEVEL_DEBUG, L"[ Enqueue Error ] Change tail After 1st CAS\n");
					//__debugbreak();
				}
			}
		}
		//_LOG(dfLOG_LEVEL_DEBUG, L"[ Enqueue ]\n");
		InterlockedIncrement(&size);
	}

	
	bool Dequeue(T& value)
	{
		while (1)
		{
			int start = InterlockedIncrement(&g_interlockCnt);

			Node* h = head;
			Node* maskedH = UnpackingNode(h);
			if (maskedH->next == nullptr)
				return false;
			
			Node* nextHead = PackingNode(maskedH->next, GetNodeStamp(h) + 1);
			T retValue = maskedH->next->data;
			
			int end = InterlockedIncrement(&g_interlockCnt);

			if (InterlockedCompareExchangePointer((PVOID*)&head, nextHead, h) == h)
			{
				value = retValue;
				nodePool.freeObject(maskedH);
				SaveMemoryDebugEntry(MD_TYPE_DEQUEUE, start, end, nullptr, maskedH, nextHead, retValue);
				break;
			}
		}
		//_LOG(dfLOG_LEVEL_DEBUG, L"[ Dequeue ]\n");
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