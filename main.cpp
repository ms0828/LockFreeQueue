#include <iostream>
#include <process.h>
#include <conio.h>
#include "Log.h"
#include "LockFreeQueue.h"
using namespace std;

#define dfTestNum 3
#define dfThreadNum 5


CLockFreeQueue<int> g_Queue;

HANDLE g_TestStartEvent;
HANDLE g_PopStartEvent;
HANDLE g_EnqueueEndEvents[dfThreadNum];
HANDLE g_DequeueEndEvents[dfThreadNum];
HANDLE g_CycleEndEvents[dfThreadNum];

struct ThreadArg
{
	HANDLE EnqueueEndEvent;
	HANDLE DequeueEndEvent;
	HANDLE cycleEndEvent;
};


SRWLOCK g_TestLock = SRWLOCK_INIT;

//-------------------------------------------------------------
// �׽�Ʈ 1��
// 1. ��� �����尡 �ڽ��� TestNum ������ŭ Enqueue�� ���� �� �ڽ��� Enqueue�� ��ŭ Dequeue ����
//    - ������ �� ������ �ݺ�
//--------------------------------------------------------------
unsigned int EnqueueAndDequeueProc1(void* arg)
{
	srand(time(nullptr));

	WaitForSingleObject(g_TestStartEvent, INFINITE);

	while (1)
	{
		for (int i = 0; i < dfTestNum; ++i)
		{
			int a = 1;
			g_Queue.Enqueue(a);	
		}

		for (int i = 0; i < dfTestNum; ++i)
		{
			int ret;
			bool popRet = g_Queue.Dequeue(ret);
			if (popRet == false)
			{
				_LOG(dfLOG_LEVEL_DEBUG, L"[ Dequeue Error] Dequeue is fail\n");
				printf("[ Dequeue Error] Dequeue is fail\n");
				//exit(1);
			}
			
			//--------------------------------------------------------
			// ABA ���� �׽�Ʈ
			// - ��� Ǯ�� ��� ��ȯ ��, ������ 0���� �ʱ�ȭ �� ��ȯ
			// - �ٵ� LockFreeStack���� pop�� ��� �����Ͱ� 0�̶�� top�� ��� Ǯ�� ��ȯ�� ��带 ����Ű�� �ִ� ��
			//--------------------------------------------------------
			/*if (ret != 1)
			{
				_LOG(dfLOG_LEVEL_DEBUG, L"[ Dequeue Error] Dequeue Ret == %d\n", ret);
				printf("[ Dequeue Error] Dequeue Ret == %d\n", ret);
				exit(1);
			}*/
		}

		//printf("ok\n");
	}

	return 0;
}


//-------------------------------------------------------------
// �׽�Ʈ 2��
// 1. ��� �����尡 ���ÿ� �ڽ��� TestNum ������ŭ Enqueue�� ����
// 2. ��� �������� Enqueue�� ������ ���ÿ� Dequeue ����
//--------------------------------------------------------------
unsigned int EnqueueAndDequeueProc2(void* arg)
{
	ThreadArg* threadArg = (ThreadArg*)arg;
	HANDLE pushEndEvent = threadArg->EnqueueEndEvent;
	HANDLE popEndEvent = threadArg->DequeueEndEvent;
	HANDLE cycleEndEvent = threadArg->cycleEndEvent;

	while (1)
	{
		WaitForSingleObject(g_TestStartEvent, INFINITE);

		//--------------------------------------------------
		// ������ Enqueue ����
		//--------------------------------------------------
		for (int i = 0; i < dfTestNum; ++i)
		{
			int a = 1;
			AcquireSRWLockExclusive(&g_TestLock);
			g_Queue.Enqueue(a);
			ReleaseSRWLockExclusive(&g_TestLock);
		}
		_LOG(dfLOG_LEVEL_DEBUG, L"[Check] A Thread Complete Push \n");
		printf("[Check] A Thread Complete Push \n");
		SetEvent(pushEndEvent);



		//---------------------------------------------------
		// ��� ������ Enqueue�� ������ ��ٸ���
		//---------------------------------------------------
		WaitForMultipleObjects(dfThreadNum, g_EnqueueEndEvents, true, INFINITE);
		printf("[Check] All Thread Pop Start! \n");
		// ��� �����尡 ������ ����, ������ ������ �����ϴ� ������ StartEvent ����
		ResetEvent(g_TestStartEvent);



		WaitForSingleObject(g_PopStartEvent, INFINITE);

		//--------------------------------------------------
		// ��� �����尡 Dequeue ����
		//--------------------------------------------------
		for (int i = 0; i < dfTestNum; ++i)
		{
			int ret;
			bool popRet = g_Queue.Dequeue(ret);
			if (popRet == false)
			{
				__debugbreak();
				//_LOG(dfLOG_LEVEL_DEBUG, L"[ Dequeue Error] Dequeue is fail\n");
				//printf("[ Dequeue Error] Dequeue is fail\n");
				//exit(1);
			}


			//--------------------------------------------------------
			// ABA ���� �׽�Ʈ
			// - ��� Ǯ�� ��� ��ȯ ��, ������ 0���� �ʱ�ȭ
			// - �ٵ� Dequeue�� ��� �����Ͱ� 0�̶�� Head�� ��� Ǯ�� ��ȯ�� ��带 ����Ű�� �ִ� ��
			//--------------------------------------------------------
			if (ret != 1)
			{
				//_LOG(dfLOG_LEVEL_DEBUG, L"[Dequeue Error] Dequeue Ret == %d\n", ret);
				//printf("[Dequeue Error] Dequeue Ret == %d\n", ret);
				__debugbreak();
				//exit(1);
			}

		}
		SetEvent(popEndEvent);
		_LOG(dfLOG_LEVEL_DEBUG, L"[Check] A Thread Complete Pop \n");
		printf("[Check] A Thread Complete Pop \n");
		//---------------------------------------------------
		// ��� ������ Pop ������ ��ٸ���
		//---------------------------------------------------
		WaitForMultipleObjects(dfThreadNum, g_DequeueEndEvents, true, INFINITE);

		

		//---------------------------------------------------
		// �� ���� ť�� ��� Ǯ ī��Ʈ ���� 
		//---------------------------------------------------
		if (g_Queue.nodePool.GetPoolCnt() != dfTestNum * dfThreadNum)
		{
			printf("[Error] Node Pool Count = %ld \n", g_Queue.nodePool.GetPoolCnt());

			//exit(1);
		}

		SetEvent(cycleEndEvent);
	}



	return 0;
}




unsigned int EnqueueAndDequeueProc3(void* arg)
{
	WaitForSingleObject(g_TestStartEvent, INFINITE);

	int id = (int)arg;

	if (id == 0)
	{
		int ret;
		g_Queue.Dequeue(ret);
	}

	else
	{
		g_Queue.Enqueue(1);
	}

	return 0;
}

void Test1()
{
	g_TestStartEvent = CreateEvent(nullptr, true, false, nullptr);
	for (int i = 0; i < dfThreadNum; i++)
	{
		HANDLE testTh = (HANDLE)_beginthreadex(nullptr, 0, EnqueueAndDequeueProc1, nullptr, 0, nullptr);
	}
	SetEvent(g_TestStartEvent);

	printf("Test Start! \n");

	while (1)
	{

	}
}
void Test2()
{
	g_TestStartEvent = CreateEvent(nullptr, true, false, nullptr);
	g_PopStartEvent = CreateEvent(nullptr, true, false, nullptr);
	ThreadArg threadArg[dfThreadNum];
	for (int i = 0; i < dfThreadNum; i++)
	{
		g_EnqueueEndEvents[i] = CreateEvent(nullptr, true, false, nullptr);
		g_DequeueEndEvents[i] = CreateEvent(nullptr, true, false, nullptr);
		g_CycleEndEvents[i] = CreateEvent(nullptr, true, false, nullptr);
		threadArg[i].EnqueueEndEvent = g_EnqueueEndEvents[i];
		threadArg[i].DequeueEndEvent = g_DequeueEndEvents[i];
		threadArg[i].cycleEndEvent = g_CycleEndEvents[i];
	}

	//---------------------------------------------------
	// �׽�Ʈ 2��
	//---------------------------------------------------
	for (int i = 0; i < dfThreadNum; i++)
	{
		HANDLE testTh = (HANDLE)_beginthreadex(nullptr, 0, EnqueueAndDequeueProc2, (void*)&threadArg[i], 0, nullptr);
	}
	SetEvent(g_TestStartEvent);
	printf("Test Start! \n");


	WaitForMultipleObjects(dfThreadNum, g_EnqueueEndEvents, true, INFINITE);


	//---------------------------------------------------
	// ��� ������ Enqueue ���� queue Size �� ������ ����
	//---------------------------------------------------
	if (g_Queue.size == dfTestNum * dfThreadNum)
		printf("After Enqueue Queue Size OK!\n");
	else
		__debugbreak();

	int qCnt = 0;
	CLockFreeQueue<int>::Node* searchNode = g_Queue.head->next;
	while (searchNode != nullptr)
	{
		int data = searchNode->data;
		if (data != 1)
			__debugbreak();
		qCnt++;
		searchNode = searchNode->next;
	}
	if(qCnt == dfTestNum * dfThreadNum)
		printf("After Enqueue qCnt OK!\n");
	else
		__debugbreak();



	while (1)
	{
		if (_kbhit())
		{
			WCHAR ControlKey = _getwch();
			if (ControlKey == L'p' || ControlKey == L'P')
				SetEvent(g_PopStartEvent);
		}


		//---------------------------------------------------
		// ��� �����尡 Cycle�� �Ϸ��ϸ� �̺�Ʈ �ʱ�ȭ �� ��� ������ ����Ŭ ����� �̺�Ʈ �ߵ�
		//---------------------------------------------------
		/*WaitForMultipleObjects(dfThreadNum, g_CycleEndEvents, true, INFINITE);
		for (int i = 0; i < dfThreadNum; ++i)
		{
			ResetEvent(g_EnqueueEndEvents[i]);
			ResetEvent(g_DequeueEndEvents[i]);
			ResetEvent(g_CycleEndEvents[i]);
		}*/
		//SetEvent(g_TestStartEvent);
	}
}
void Test3()
{
	g_TestStartEvent = CreateEvent(nullptr, true, false, nullptr);

	//---------------------------------------------------
	// �׽�Ʈ 3��
	//---------------------------------------------------
	for (int i = 0; i < dfThreadNum; i++)
	{
		HANDLE testTh = (HANDLE)_beginthreadex(nullptr, 0, EnqueueAndDequeueProc3, (void*)i, 0, nullptr);
	}
	
	while (1)
	{
		if (_kbhit())
		{
			WCHAR ControlKey = _getwch();
			if (ControlKey == L's' || ControlKey == L'S')
			{
				SetEvent(g_TestStartEvent);
				printf("Test Start! \n");
			}
		}
	
		
	}

	
	
}

int main()
{
	InitLog(dfLOG_LEVEL_DEBUG, CONSOLE);


	Test1();

	/*int vRet;
	bool fRet;
	g_Queue.Enqueue(1);
	g_Queue.Enqueue(1);
	fRet = g_Queue.Dequeue(vRet);
	if(!fRet)
		printf("fRet false \n");
	fRet = g_Queue.Dequeue(vRet);
	if (!fRet)
		printf("fRet false \n");

	g_Queue.Enqueue(1);
	fRet = g_Queue.Dequeue(vRet);
	if (!fRet)
		printf("fRet false \n");

	g_Queue.Enqueue(1);
	g_Queue.Enqueue(1);
	fRet = g_Queue.Dequeue(vRet);
	if (!fRet)
		printf("fRet false \n");
	fRet = g_Queue.Dequeue(vRet);
	if (!fRet)
		printf("fRet false \n");*/
	return 0;
}