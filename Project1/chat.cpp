#include <stdio.h>
#include <stdlib.h>
#include <winsock2.h>
#include <string.h>
#include <ws2tcpip.h>
#include <string>
#include <iostream>
#include <iphlpapi.h>
#include <thread>
#include <mutex>
#include <string.h>
#include <locale.h>
#include "Define.h"
#include "List.h"
#include "ReciveBroadcastUDP.h"
#include "AcceptConnections.h"
#include "ReciveData.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "IPHLPAPI.lib")

ULONG GetBroadcastAddress(PULONG LocalMachineIP)
{
	ULONG BroadcastAddr = 0;
	ULONG Flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_INCLUDE_GATEWAYS; // побитовое или
	ULONG BuffSize = WORKING_BUFFER_SIZE;
	PIP_ADAPTER_ADDRESSES Adr = (PIP_ADAPTER_ADDRESSES)malloc(BuffSize), CurrAdr;

	ULONG RetVal = GetAdaptersAddresses(AF_INET, Flags, NULL, Adr, &BuffSize); //возвращает список структур PIP_ADAPTER_ADDRESSES
	CurrAdr = Adr; 
	if (RetVal == NO_ERROR)
	{
		BOOL GatewayIPFounded = FALSE; // если найден основной шлюз
		while ((CurrAdr != NULL) || (GatewayIPFounded))
		{
			GatewayIPFounded = (CurrAdr->FirstGatewayAddress != NULL);// если настроен основной шлюз 
			if (GatewayIPFounded)
			{
				ULONG SubnetMask = 0;
				ConvertLengthToIpv4Mask(CurrAdr->FirstUnicastAddress->OnLinkPrefixLength, &SubnetMask); // получили маску подсети

				sockaddr_in *IPInAddr = (sockaddr_in*)CurrAdr->FirstUnicastAddress->Address.lpSockaddr; // добрались до айпишника
				(*LocalMachineIP) = IPInAddr->sin_addr.s_addr;

				BroadcastAddr = (*LocalMachineIP) | (~SubnetMask);
			}
			CurrAdr = CurrAdr->Next;
		}
	}
	else
	{
		LPVOID lpMsgBuf = NULL;
		printf("Call to GetAdaptersAddresses failed with error: %d\n", RetVal);
		if (RetVal == ERROR_NO_DATA)
			printf("\tNo addresses were found for the requested parameters\n");
		else
		{

			if (FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
				NULL, RetVal, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),/* Default language*/(LPTSTR)&lpMsgBuf, 0, NULL))
			{
				printf("\tError: %s", (char*)lpMsgBuf);
			}
		}
	}

	free(Adr);
	return BroadcastAddr;
}

int SendBroadcastUDP(ULONG BroadcastAddr, ULONG MyIP)
{
	int Res = 0;
	SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock == INVALID_SOCKET)
	{
		printf_s("Socket failed with error %d\n", WSAGetLastError());
		return 1;
	}

	sockaddr_in Adr;
	Adr.sin_family = AF_INET;
	Adr.sin_port = htons((USHORT)DEFAULT_PORT); // из нормального в сетевое
	Adr.sin_addr.s_addr = BroadcastAddr;
	Res = sendto(sock, (char *)&MyIP, sizeof(MyIP), 0, (SOCKADDR *)&Adr, sizeof(Adr)); // проверка достоверности broadcast adress
	if (Res == SOCKET_ERROR) {
		printf("sendto failed with error: %d\n", WSAGetLastError());
		closesocket(sock);
		WSACleanup();
		return 1;
	}
	closesocket(sock);
	return 0;
}

void Mailing(char Data[], PList &ClientList, std::mutex &mtx)
{
	int Res;
	PList Curr = ClientList->Next;
	while (Curr != NULL)
	{
		Res = send(Curr->Socket, Data, strlen(Data) + 1, 0);
		if (Res == SOCKET_ERROR)
		{
			mtx.lock();
			Curr->Delete = TRUE;
			closesocket(Curr->Socket);
			Curr->Socket = INVALID_SOCKET;
			mtx.unlock();
		}
		Curr = Curr->Next;
	}
}

void ClearLastStr()// поднятие вверх позиции курсора
{
	HANDLE hnd = GetStdHandle(STD_OUTPUT_HANDLE);// получаю дескриптор консоли
	CONSOLE_SCREEN_BUFFER_INFO inf;
	GetConsoleScreenBufferInfo(hnd, &inf);
	inf.dwCursorPosition.Y--;
	SetConsoleCursorPosition(hnd, inf.dwCursorPosition);
}

int main()
{
	setlocale(LC_ALL, "Rus");
	SetConsoleOutputCP(1251);
	SetConsoleCP(1251);

	int Res = 0;
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		printf_s("Error");
		exit(1);
	}

	ULONG MyIP = 0;
	ULONG BroadcastAddr = GetBroadcastAddress(&MyIP); // ip
	if (BroadcastAddr == 0)
	{
		printf_s("GetBroadcastAddress failed");
		getchar();
		return 1;
	}
	char *Nickname = (char*)calloc(MAXBYTE, sizeof(char));

	do
	{
		printf_s("Your nickname: ");
		gets_s(Nickname, MAXBYTE - 1);
	} while (strlen(Nickname) <= 0);

	Nickname = (char*)realloc(Nickname, strlen(Nickname) + 1);


	printf_s("Waiting for other users\n ");


	Res = SendBroadcastUDP(BroadcastAddr, MyIP); 
	if (Res != 0)
	{
		printf_s("Unable to send broadcast request\n");
		getchar();
		return 1;
	}

	std::mutex mtx;

	BOOL Work = TRUE;
	PList ClientList = (PList)calloc(1, sizeof(List));
	ClientList->Last = ClientList;

	std::thread ListenBroadcast = std::thread(ListenBroadcastUDPThread, MyIP, std::ref(Work), std::ref(ClientList), std::ref(mtx), std::ref(Nickname));
	std::thread AcceptConnections = std::thread(AcceptConnectionsThread, MyIP, std::ref(Work), std::ref(ClientList), std::ref(mtx), std::ref(Nickname));

	while (ClientList->Next == NULL); // пока один в чате

	system("cls");

	char *msg = (char*)calloc(MAXBYTE, sizeof(char));
	SYSTEMTIME Time;
	do
	{
		msg = (char*)realloc(msg, MAXBYTE);
		do
		{
			gets_s(msg, MAXBYTE - 1);
		} while (strlen(msg) <= 0);
		GetLocalTime(&Time);

		ClearLastStr();// 
		printf_s("%02d:%02d:%02d ", Time.wHour, Time.wMinute, Time.wSecond);
		msg = (char*)realloc(msg, strlen(msg) + 1);

		printf_s("%s: %s\n", Nickname, msg);

		Mailing(msg, ClientList, mtx);
	} while (TRUE);
	Work = FALSE;

	//waiting for other threads
	if (ListenBroadcast.joinable())
	{
		ListenBroadcast.join();
	}

	if (AcceptConnections.joinable())
	{
		AcceptConnections.join();
	}

	WSACleanup();
	return 0;
}