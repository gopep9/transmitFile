#include <WinSock2.h>
#include <Windows.h>
#include <process.h>
#include <iostream>
#include <string>
#include <stdio.h>
#include <sstream>
#include <vector>
#pragma comment(lib,"ws2_32.lib")

typedef unsigned(__stdcall * PTHREAD_START) (void *);

#define chBEGINTHREADEX(psa, cbStack, pfnStartAddr, \
pvParam, fdwCreate, pdwThreadID) \
((HANDLE)_beginthreadex(\
(void *)(psa), \
(unsigned)(cbStack), \
(PTHREAD_START)(pfnStartAddr), \
(void *)(pvParam), \
(unsigned)(fdwCreate), \
(unsigned *)(pdwThreadID)))

CRITICAL_SECTION g_cs;
CONDITION_VARIABLE g_cv;

static std::string downloadPath;

std::wstring Utf8ToUnicode(const std::string& strUtf8)//返回值是wstring
{
	int len = MultiByteToWideChar(CP_UTF8, 0, strUtf8.c_str(), -1, NULL, 0);
	wchar_t *strUnicode = new wchar_t[len];
	wmemset(strUnicode, 0, len);
	MultiByteToWideChar(CP_UTF8, 0, strUtf8.c_str(), -1, strUnicode, len);

	std::wstring strTemp(strUnicode);//此时的strTemp是Unicode编码
	delete[]strUnicode;
	strUnicode = NULL;
	return strTemp;
}

std::string UnicodeToUtf8(const std::wstring& strUnicode)
{
	int len = WideCharToMultiByte(CP_UTF8, 0, strUnicode.c_str(), -1, NULL, 0, NULL, NULL);
	char *strUtf8 = new char[len];
	memset(strUtf8, 0, len);
	WideCharToMultiByte(CP_UTF8, 0, strUnicode.c_str(), -1, strUtf8, len, NULL, NULL);

	std::string strTemp(strUtf8);
	delete[]strUtf8;
	strUtf8 = NULL;
	return strTemp;
}

std::wstring GbkToUnicode(const std::string& strGbk)//返回值是wstring
{
	int len = MultiByteToWideChar(CP_ACP, 0, strGbk.c_str(), -1, NULL, 0);
	wchar_t *strUnicode = new wchar_t[len];
	wmemset(strUnicode, 0, len);
	MultiByteToWideChar(CP_ACP, 0, strGbk.c_str(), -1, strUnicode, len);

	std::wstring strTemp(strUnicode);//此时的strTemp是Unicode编码
	delete[]strUnicode;
	strUnicode = NULL;
	return strTemp;
}

unsigned acceptFileThread(void *arg)
{
	EnterCriticalSection(&g_cs);
	int connectSock = *(int*)arg;
	WakeConditionVariable(&g_cv);
	LeaveCriticalSection(&g_cs);
	char buf[1025] = {};
	int readwordnum = 0;
	_wchdir(Utf8ToUnicode(downloadPath).c_str());
	while (true)
	{
		int headSize = 1024;
		std::string fileMessage;
		while (headSize > 0)
		{
			if (headSize > 1024)
			{
				readwordnum = recv(connectSock, buf, 1024, NULL);
				if (readwordnum == 0)
				{
					//读取结束
					return NULL;
				}
				fileMessage += buf;
				headSize -= readwordnum;
				memset(buf, 0, 1024);
			}
			else
			{
				readwordnum = recv(connectSock, buf, headSize, NULL);
				if (readwordnum == 0)
				{
					printf("error for transmit");
					return NULL;
				}
				fileMessage += buf;
				headSize -= readwordnum;
				memset(buf, 0, 1024);
			}
		}
		std::cout << "receive head:" << fileMessage;
		size_t nameStartPos = fileMessage.find("file:") + 5;
		size_t nameEndPos = fileMessage.find_first_of(";");
		std::string filename = fileMessage.substr(nameStartPos, nameEndPos - nameStartPos);
		size_t sizeStartPos = fileMessage.find("size:") + 5;
		size_t sizeEndPos = fileMessage.find_last_of(";");
		std::string filesize = fileMessage.substr(sizeStartPos, sizeEndPos - sizeStartPos);
		std::istringstream filesizestream(filesize);
		std::cout << "fileName:" << filename << std::endl;
		std::cout << "size:" << filesize << std::endl;
		std::wstring wFileName = Utf8ToUnicode(filename);
		//在这里创建文件夹和打开文件
		int posOfFengGeFu = 0;
		WCHAR oldCurrentPath[MAX_PATH] = {};
		if (_wgetcwd(oldCurrentPath, MAX_PATH) == NULL)//最后会不会有/号？
		{
			printf("getcwd error");
			return NULL;
		}
		wprintf(oldCurrentPath);
		if (lstrlenW(oldCurrentPath) + wFileName.size() >= MAX_PATH)
		{
			printf("error,file path out of max_path\n");
			wprintf(__TEXT("%s"), oldCurrentPath);
		}
		while ((posOfFengGeFu = filename.find("/")) != -1){
			std::string pathName = filename.substr(0, 0 + posOfFengGeFu);
			filename = filename.substr(posOfFengGeFu + 1);
			if (pathName == ".")
				continue;
			WIN32_FIND_DATA wfd;
			HANDLE hFind = FindFirstFile(Utf8ToUnicode(pathName).c_str(), &wfd);
			if (INVALID_HANDLE_VALUE != hFind && (wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
			{
				//fileexist
			}
			else
			{
				int ret = CreateDirectory(Utf8ToUnicode(pathName).c_str(), 0);
				if (ret == 0)
				{
					printf("createDirectory %s error:%d", pathName, GetLastError());
				}
			}
			_wchdir(Utf8ToUnicode(pathName).c_str());
		}
		HANDLE fileHandle = CreateFile(Utf8ToUnicode(filename).c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		int filesizeint = 0;
		filesizestream >> filesizeint;
		while (filesizeint > 0){
			if (filesizeint >= 1024)
			{
				readwordnum = recv(connectSock, buf, 1024, 0);
				if (readwordnum == 0)
				{
					printf("error for transmit");
					return NULL;
				}
				DWORD writeNum = 0;
				WriteFile(fileHandle, buf, readwordnum, &writeNum, NULL);
				if (writeNum != readwordnum)
				{
					printf("error,write num not equal read num\n");
					return NULL;
				}
				filesizeint -= readwordnum;
			}
			else{
				readwordnum = recv(connectSock, buf, filesizeint, NULL);
				if (readwordnum == 0)
				{
					printf("error for transmit");
					return NULL;
				}
				DWORD writeNum = 0;
				WriteFile(fileHandle, buf, readwordnum, &writeNum, NULL);
				if (writeNum != readwordnum)
				{
					printf("error,write num not equal read num\n");
					return NULL;
				}
				filesizeint -= readwordnum;
			}
		}
		printf("accept complete\n");
		_wchdir(oldCurrentPath);
		CloseHandle(fileHandle);
		//wFileName.find(_T"a")
	}
	return NULL;
}

int addDocumentToList(std::wstring document, std::vector<std::wstring> *fileList)
{
	BOOL bFind = TRUE;
	WIN32_FIND_DATA fileData;
	WCHAR name[MAX_PATH] = {};
	//WCHAR szOldCurDir[MAX_PATH] = {};
	//GetCurrentDirectory(sizeof(szOldCurDir), szOldCurDir);
	//_wchdir(document.c_str());
	HANDLE hFind = FindFirstFile((document + __TEXT("/*")).c_str(), &fileData);
	if (hFind == INVALID_HANDLE_VALUE){
		return NULL;
	}
	while (bFind){
		//下面的重\\改为/
		std::wstring fullFilePath = document + __TEXT("/") + fileData.cFileName;
		if (fileData.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY){
			//文件夹
			if (lstrcmpW(fileData.cFileName, __TEXT(".")) == 0
				|| lstrcmpW(fileData.cFileName, __TEXT("..")) == 0)
			{
				bFind = FindNextFile(hFind, &fileData);
				continue;
			}
			else{
				//普通文件夹
				addDocumentToList(fullFilePath, fileList);
			}
		}
		else{
			//普通文件
			fileList->push_back(fullFilePath);
		}
		bFind = FindNextFile(hFind, &fileData);
	}
	return 0;
}

int transmitFile(int sock, std::wstring transFile, std::wstring remoteFileName)
{
	LARGE_INTEGER fileSize;
	HANDLE hFile = CreateFile(transFile.c_str(), GENERIC_READ, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	GetFileSizeEx(hFile, &fileSize);
	__int64 nSize = fileSize.QuadPart;
	char buf[1024] = {};

	//要传utf8的string
	std::string headStr;
	std::stringstream headStrStream;
	headStrStream << "file:" << UnicodeToUtf8(remoteFileName) << ";" << "size:" << nSize << ";";
	headStr = headStrStream.str();
	strcpy(buf, headStr.c_str());
	send(sock, buf, 1024, NULL);
	DWORD readwordnum = 0;
	BOOL readRet = ReadFile(hFile, buf, 1024, &readwordnum, NULL);
	while (readRet&&readwordnum != 0)
	{
		send(sock, buf, readwordnum, NULL);
		readRet = ReadFile(hFile, buf, 1024, &readwordnum, NULL);
	}
	CloseHandle(hFile);
	return 0;
}


int main(int argc, char *argv[])
{
	if (argc != 4)
	{
		std::cout << "first arg is ip\n"
			<< "second arg is port\n"
			<< "third arg is path download the file\n";
		exit(1);
	}
	WORD wVersionRequested;
	WSADATA wsadata;
	int err;
	wVersionRequested = MAKEWORD(1, 1);
	err = WSAStartup(wVersionRequested, &wsadata);
	if (err != 0)
		return -1;
    
    InitializeCriticalSection(&g_cs);

	std::string remoteIpAddress = argv[1];
	std::string remotePort = argv[2];
	downloadPath = argv[3];

	SOCKET connectSock = socket(AF_INET, SOCK_STREAM, 0);
	if (connectSock == INVALID_SOCKET)
	{
		printf("socket error !");
		return 0;
	}
	sockaddr_in remoteAddr = {};
	remoteAddr.sin_family = AF_INET;
	remoteAddr.sin_port = htons(atoi(remotePort.c_str()));
	remoteAddr.sin_addr.S_un.S_addr = inet_addr(remoteIpAddress.c_str());
	size_t len = sizeof(remoteAddr);
	if (connect(connectSock, (struct sockaddr*)&remoteAddr, len) < 0)
	{
		printf("connect error !");
		exit(1);
	}
	std::cout << "send or receive(enter s or r)\n";
	std::string transmitType;
	std::getline(std::cin, transmitType);
	if (transmitType == "s")
	{
		std::cout << "enter the file or document which want to transmit\n";
		std::string localFileName;
		std::getline(std::cin, localFileName);
		std::cout << "enter the filename in remote\n";
		std::string remoteFileName;
		std::getline(std::cin, remoteFileName);
		std::wstring localFileNameW = GbkToUnicode(localFileName);
		std::wstring remoteFileNameW = GbkToUnicode(remoteFileName);
		WIN32_FIND_DATA wfd;
		HANDLE hFind = FindFirstFile(localFileNameW.c_str(), &wfd);
		if (INVALID_HANDLE_VALUE != hFind)
		{
			if (wfd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)
			{
				//目录
				std::vector<std::wstring>fileList;
				if (localFileNameW[localFileNameW.size() - 1] == __TEXT('/')
					|| localFileNameW[localFileNameW.size() - 1] == __TEXT('\\'))
				{
					localFileNameW.erase(localFileNameW.end() - 1);
					localFileName.erase(localFileName.end() - 1);
				}
				WCHAR szOldCurDir[MAX_PATH] = {};
				GetCurrentDirectory(sizeof(szOldCurDir), szOldCurDir);
				_wchdir(localFileNameW.c_str());
				addDocumentToList(__TEXT("."), &fileList);
				_wchdir(szOldCurDir);
				for (std::vector<std::wstring>::iterator it = fileList.begin(); it != fileList.end(); it++)
				{
					std::wstring tmp = *it;
					if (it->substr(0, 2) == __TEXT("./"))
						it->erase(0, 2);
					transmitFile(connectSock, localFileNameW + __TEXT("/") + *it, remoteFileNameW + __TEXT("/") + *it);
				}
			}
			else
			{
				//文件
				transmitFile(connectSock, localFileNameW, remoteFileNameW);
			}
		}
		else
		{
			printf("no such file\n");
		}

	}
	else if (transmitType == "r")
	{
		std::cout << "receive file or document from ip address: " << inet_ntoa(remoteAddr.sin_addr) << "\nand port: " << ntohs(remoteAddr.sin_port) << "\n";
		LPDWORD lpThreadId = 0;
		EnterCriticalSection(&g_cs);
		HANDLE threadHandle = chBEGINTHREADEX(NULL, 0, acceptFileThread, &connectSock, NULL, &lpThreadId);
		SleepConditionVariableCS(&g_cv, &g_cs, INFINITE);
		LeaveCriticalSection(&g_cs);
		WaitForSingleObject(threadHandle, INFINITE);
	}
	else
	{
		puts("error should input s or r");
	}
	closesocket(connectSock);
	std::cout << "done\n";

}
