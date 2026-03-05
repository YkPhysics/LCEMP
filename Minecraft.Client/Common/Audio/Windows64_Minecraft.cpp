// Minecraft.cpp : Defines the entry point for the application.
//

#include "stdafx.h"

#include <assert.h>
#include <ctype.h>
#include "GameConfig\Minecraft.spa.h"
#include "..\MinecraftServer.h"
#include "..\PlayerList.h"
#include "..\ServerPlayer.h"
#include "..\PlayerConnection.h"
#include "..\LocalPlayer.h"
#include "..\..\Minecraft.World\ItemInstance.h"
#include "..\..\Minecraft.World\MapItem.h"
#include "..\..\Minecraft.World\Recipes.h"
#include "..\..\Minecraft.World\Recipy.h"
#include "..\..\Minecraft.World\Language.h"
#include "..\..\Minecraft.World\StringHelpers.h"
#include "..\..\Minecraft.World\AABB.h"
#include "..\..\Minecraft.World\Vec3.h"
#include "..\..\Minecraft.World\Level.h"
#include "..\..\Minecraft.World\net.minecraft.world.level.tile.h"

#include "..\ClientConnection.h"
#include "..\User.h"
#include "..\ChatScreen.h"
#include "..\TextEditScreen.h"
#include "..\..\Minecraft.World\Socket.h"
#include "..\KeyboardMouseInput.h"
#include "..\..\Minecraft.World\ThreadName.h"
#include "..\..\Minecraft.Client\StatsCounter.h"
#include "..\ConnectScreen.h"
//#include "Social\SocialManager.h"
//#include "Leaderboards\LeaderboardManager.h"
//#include "XUI\XUI_Scene_Container.h"
//#include "NetworkManager.h"
#include "..\..\Minecraft.Client\Tesselator.h"
#include "..\..\Minecraft.Client\Options.h"
#include "Sentient\SentientManager.h"
#include "..\..\Minecraft.World\IntCache.h"
#include "..\Textures.h"
#include "Resource.h"
#include "..\..\Minecraft.World\compression.h"
#include "..\..\Minecraft.World\OldChunkStorage.h"
#include "Network\WinsockNetLayer.h"
#include <fstream>
#include <sstream>
#include <map>

#include "Xbox/resource.h"

HINSTANCE hMyInst;
LRESULT CALLBACK DlgProc(HWND hWndDlg, UINT Msg, WPARAM wParam, LPARAM lParam);
char chGlobalText[256];
uint16_t ui16GlobalText[256];

#define THEME_NAME		"584111F70AAAAAAA"
#define THEME_FILESIZE	2797568

//#define THREE_MB 3145728 // minimum save size (checking for this on a selected device)
//#define FIVE_MB 5242880 // minimum save size (checking for this on a selected device)
//#define FIFTY_TWO_MB (1024*1024*52) // Maximum TCR space required for a save (checking for this on a selected device)
#define FIFTY_ONE_MB (1000000*51) // Maximum TCR space required for a save is 52MB (checking for this on a selected device)

//#define PROFILE_VERSION 3 // new version for the interim bug fix 166 TU
#define NUM_PROFILE_VALUES	5
#define NUM_PROFILE_SETTINGS 4
DWORD dwProfileSettingsA[NUM_PROFILE_VALUES]=
{
#ifdef _XBOX
	XPROFILE_OPTION_CONTROLLER_VIBRATION,
	XPROFILE_GAMER_YAXIS_INVERSION,
	XPROFILE_GAMER_CONTROL_SENSITIVITY,
	XPROFILE_GAMER_ACTION_MOVEMENT_CONTROL,
	XPROFILE_TITLE_SPECIFIC1,
#else
	0,0,0,0,0
#endif
};

//-------------------------------------------------------------------------------------
// Time             Since fAppTime is a float, we need to keep the quadword app time
//                  as a LARGE_INTEGER so that we don't lose precision after running
//                  for a long time.
//-------------------------------------------------------------------------------------

BOOL g_bWidescreen = TRUE;

int g_iScreenWidth = 1920;
int g_iScreenHeight = 1080;

char g_Win64Username[17] = {0};
wchar_t g_Win64UsernameW[17] = {0};
bool g_Win64DedicatedServerMode = false;
extern HINSTANCE g_hInst;
extern HWND g_hWnd;

static bool g_dedicatedGuiEnabled = false;
static HWND g_hDedicatedStatus = NULL;
static HWND g_hDedicatedDetails = NULL;
static HWND g_hDedicatedLog = NULL;
static HWND g_hDedicatedStopButton = NULL;
static HWND g_hDedicatedRefreshButton = NULL;
static HWND g_hDedicatedCopyButton = NULL;
static HWND g_hDedicatedSaveToggleButton = NULL;
static HWND g_hDedicatedWhitelistToggleButton = NULL;
static HWND g_hDedicatedKickAllButton = NULL;
static HFONT g_hDedicatedFont = NULL;
static HBRUSH g_hDedicatedBgBrush = NULL;
static HBRUSH g_hDedicatedPanelBrush = NULL;
static HBRUSH g_hDedicatedLogBrush = NULL;
static DWORD g_dedicatedServerStartTick = 0;
static int g_dedicatedPort = WIN64_NET_DEFAULT_PORT;
static unsigned int g_dedicatedMaxPlayers = MINECRAFT_NET_MAX_PLAYERS;
static wstring g_dedicatedWorldName = L"Dedicated Server";
static wstring g_dedicatedBindAddress = L"0.0.0.0";
static bool g_networkManagerReady = false;

static const UINT WM_APP_DEDICATED_APPEND_LOG = WM_APP + 101;
static const UINT ID_DEDICATED_TIMER = 9001;
static const int ID_DEDICATED_STOP_BUTTON = 9002;
static const int ID_DEDICATED_REFRESH_BUTTON = 9003;
static const int ID_DEDICATED_COPY_BUTTON = 9004;
static const int ID_DEDICATED_SAVE_TOGGLE_BUTTON = 9005;
static const int ID_DEDICATED_WHITELIST_TOGGLE_BUTTON = 9006;
static const int ID_DEDICATED_KICKALL_BUTTON = 9007;

static const COLORREF DEDICATED_BG_COLOR = RGB(16, 18, 22);
static const COLORREF DEDICATED_PANEL_COLOR = RGB(28, 31, 36);
static const COLORREF DEDICATED_TEXT_COLOR = RGB(225, 232, 240);
static const COLORREF DEDICATED_MUTED_TEXT_COLOR = RGB(170, 182, 198);
static const COLORREF DEDICATED_LOG_BG_COLOR = RGB(12, 14, 18);

void Windows64_DedicatedGuiPushLog(const char *text);
static void DedicatedGuiUpdateStatus();
static void DedicatedGuiAppendLogNow(const char *text);
static void DedicatedGuiCreateControls(HWND hWnd);
static void DedicatedGuiLayout(HWND hWnd);
static void DedicatedGuiUpdateControlLabels();
static void DedicatedGuiCopyConnectInfo(HWND hWnd);
static void DedicatedGuiKickAllPlayers();

static void StandaloneWriteLogLine(const char *text)
{
	if (text == NULL)
	{
		return;
	}

	wchar_t exePath[MAX_PATH];
	if (!GetModuleFileNameW(NULL, exePath, MAX_PATH))
	{
		return;
	}

	wstring logPath(exePath);
	size_t lastSlash = logPath.find_last_of(L"\\/");
	if (lastSlash != wstring::npos)
	{
		logPath = logPath.substr(0, lastSlash + 1) + L"StandaloneDebug.log";
	}
	else
	{
		logPath = L"StandaloneDebug.log";
	}

	HANDLE file = CreateFileW(logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE)
	{
		return;
	}

	SYSTEMTIME st;
	GetLocalTime(&st);

	char prefix[64];
	_snprintf_s(prefix, sizeof(prefix), _TRUNCATE, "[%02d:%02d:%02d.%03d] ",
		st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

	DWORD bytesWritten = 0;
	WriteFile(file, prefix, (DWORD)strlen(prefix), &bytesWritten, NULL);
	WriteFile(file, text, (DWORD)strlen(text), &bytesWritten, NULL);

	size_t len = strlen(text);
	if (len == 0 || (text[len - 1] != '\n' && text[len - 1] != '\r'))
	{
		const char *newline = "\r\n";
		WriteFile(file, newline, (DWORD)strlen(newline), &bytesWritten, NULL);
	}

	CloseHandle(file);
}

static void StandaloneLog(const char *format, ...)
{
	char buffer[1024];
	va_list ap;
	va_start(ap, format);
	vsnprintf(buffer, sizeof(buffer), format, ap);
	va_end(ap);

	StandaloneWriteLogLine(buffer);
	Windows64_DedicatedGuiPushLog(buffer);
}

void Windows64_DedicatedGuiPushLog(const char *text)
{
	if (!g_dedicatedGuiEnabled || text == NULL || text[0] == 0 || g_hWnd == NULL)
	{
		return;
	}

	size_t len = strlen(text);
	char *copy = new char[len + 1];
	strcpy_s(copy, len + 1, text);

	if (!PostMessage(g_hWnd, WM_APP_DEDICATED_APPEND_LOG, 0, (LPARAM)copy))
	{
		delete[] copy;
	}
}

static void DedicatedGuiAppendLogNow(const char *text)
{
	if (g_hDedicatedLog == NULL || text == NULL || text[0] == 0)
	{
		return;
	}

	char line[1200];
	_snprintf_s(line, sizeof(line), _TRUNCATE, "%s\r\n", text);

	int wideLen = MultiByteToWideChar(CP_ACP, 0, line, -1, NULL, 0);
	if (wideLen <= 1)
	{
		return;
	}

	wchar_t *wide = new wchar_t[wideLen];
	MultiByteToWideChar(CP_ACP, 0, line, -1, wide, wideLen);

	int curLen = GetWindowTextLengthW(g_hDedicatedLog);
	if (curLen > 120000)
	{
		SendMessageW(g_hDedicatedLog, WM_SETTEXT, 0, (LPARAM)L"");
		curLen = 0;
	}

	SendMessageW(g_hDedicatedLog, EM_SETSEL, curLen, curLen);
	SendMessageW(g_hDedicatedLog, EM_REPLACESEL, FALSE, (LPARAM)wide);

	delete[] wide;
}

static void DedicatedGuiUpdateStatus()
{
	if (!g_dedicatedGuiEnabled)
	{
		return;
	}

	if (g_hDedicatedStatus != NULL)
	{
		const bool gameStarted = app.GetGameStarted();
		bool inSession = false;
		int players = 0;
		bool whitelistEnabled = false;
		if (MinecraftServer::getInstance() != NULL && MinecraftServer::getInstance()->getPlayers() != NULL)
		{
			whitelistEnabled = MinecraftServer::getInstance()->getPlayers()->isWhitelistEnabled();
		}
		if (g_networkManagerReady)
		{
			inSession = g_NetworkManager.IsInSession();
			players = g_NetworkManager.GetPlayerCount();
		}
		const wchar_t *state = gameStarted ? L"Online" : (inSession ? L"Starting" : L"Offline");
		const bool saveDisabled = app.GetGameHostOption(eGameHostOption_DisableSaving) != 0;

		wchar_t status[256];
		swprintf_s(status, L"Status: %ls    Players: %d/%u    Saving: %ls    Whitelist: %ls",
			state,
			players,
			g_dedicatedMaxPlayers,
			saveDisabled ? L"Off" : L"On",
			whitelistEnabled ? L"On" : L"Off");
		SetWindowTextW(g_hDedicatedStatus, status);
	}

	if (g_hDedicatedDetails != NULL)
	{
		DWORD uptimeSeconds = 0;
		if (g_dedicatedServerStartTick != 0)
		{
			uptimeSeconds = (GetTickCount() - g_dedicatedServerStartTick) / 1000;
		}

		const DWORD hours = uptimeSeconds / 3600;
		const DWORD minutes = (uptimeSeconds % 3600) / 60;
		const DWORD seconds = uptimeSeconds % 60;

		wchar_t details[512];
		swprintf_s(details,
			L"World: %ls    Bind: %ls    Port: %d    Uptime: %02lu:%02lu:%02lu",
			g_dedicatedWorldName.c_str(),
			g_dedicatedBindAddress.c_str(),
			g_dedicatedPort,
			hours,
			minutes,
			seconds);
		SetWindowTextW(g_hDedicatedDetails, details);
	}

	DedicatedGuiUpdateControlLabels();
}

static void DedicatedGuiUpdateControlLabels()
{
	if (!g_dedicatedGuiEnabled)
	{
		return;
	}

	const bool saveDisabled = app.GetGameHostOption(eGameHostOption_DisableSaving) != 0;
	if (g_hDedicatedSaveToggleButton != NULL)
	{
		SetWindowTextW(g_hDedicatedSaveToggleButton, saveDisabled ? L"Enable Saving" : L"Disable Saving");
	}

	bool whitelistEnabled = false;
	MinecraftServer *server = MinecraftServer::getInstance();
	if (server != NULL && server->getPlayers() != NULL)
	{
		whitelistEnabled = server->getPlayers()->isWhitelistEnabled();
	}
	if (g_hDedicatedWhitelistToggleButton != NULL)
	{
		SetWindowTextW(g_hDedicatedWhitelistToggleButton, whitelistEnabled ? L"Whitelist: ON" : L"Whitelist: OFF");
	}
}

static void DedicatedGuiCopyConnectInfo(HWND hWnd)
{
	wchar_t joinText[256];
	swprintf_s(joinText, L"%ls:%d", g_dedicatedBindAddress.c_str(), g_dedicatedPort);

	if (!OpenClipboard(hWnd))
	{
		StandaloneLog("Dedicated GUI: failed to open clipboard");
		return;
	}

	EmptyClipboard();
	const size_t chars = wcslen(joinText) + 1;
	HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, chars * sizeof(wchar_t));
	if (hMem != NULL)
	{
		void *dst = GlobalLock(hMem);
		if (dst != NULL)
		{
			memcpy(dst, joinText, chars * sizeof(wchar_t));
			GlobalUnlock(hMem);
			SetClipboardData(CF_UNICODETEXT, hMem);
			char joinTextA[256] = { 0 };
			wcstombs(joinTextA, joinText, sizeof(joinTextA) - 1);
			StandaloneLog("Dedicated GUI: copied connect address %s", joinTextA);
		}
		else
		{
			GlobalFree(hMem);
		}
	}
	CloseClipboard();
}

static void DedicatedGuiKickAllPlayers()
{
	MinecraftServer *server = MinecraftServer::getInstance();
	if (server == NULL || server->getPlayers() == NULL)
	{
		return;
	}

	vector<shared_ptr<ServerPlayer> > toKick;
	toKick.reserve(server->getPlayers()->players.size());
	for (size_t i = 0; i < server->getPlayers()->players.size(); ++i)
	{
		shared_ptr<ServerPlayer> p = server->getPlayers()->players[i];
		if (p == NULL || p->connection == NULL)
		{
			continue;
		}
		INetworkPlayer *np = p->connection->getNetworkPlayer();
		if (np != NULL && np->IsLocal())
		{
			continue;
		}
		toKick.push_back(p);
	}

	for (size_t i = 0; i < toKick.size(); ++i)
	{
		toKick[i]->connection->setWasKicked();
		toKick[i]->connection->disconnect(DisconnectPacket::eDisconnect_Kicked);
	}
	StandaloneLog("Dedicated GUI: kicked %u players", (unsigned int)toKick.size());
}

static void DedicatedGuiLayout(HWND hWnd)
{
	if (!g_dedicatedGuiEnabled || hWnd == NULL)
	{
		return;
	}

	RECT rc;
	GetClientRect(hWnd, &rc);
	const int width = rc.right - rc.left;
	const int height = rc.bottom - rc.top;

	const int margin = 14;
	const int statusH = 32;
	const int detailsH = 24;
	const int buttonH = 32;
	const int buttonGap = 8;

	const int logTop = margin + statusH + detailsH + margin;
	const int logBottom = height - margin - buttonH - margin;
	const int logH = (logBottom > logTop) ? (logBottom - logTop) : 40;

	if (g_hDedicatedStatus != NULL)
	{
		SetWindowPos(g_hDedicatedStatus, NULL, margin, margin, width - (margin * 2), statusH, SWP_NOZORDER);
	}
	if (g_hDedicatedDetails != NULL)
	{
		SetWindowPos(g_hDedicatedDetails, NULL, margin, margin + statusH, width - (margin * 2), detailsH, SWP_NOZORDER);
	}
	if (g_hDedicatedLog != NULL)
	{
		SetWindowPos(g_hDedicatedLog, NULL, margin, logTop, width - (margin * 2), logH, SWP_NOZORDER);
	}
	const int controlCount = 6;
	const int buttonW = (width - (margin * 2) - (buttonGap * (controlCount - 1))) / controlCount;
	int x = margin;
	if (g_hDedicatedRefreshButton != NULL)
	{
		SetWindowPos(g_hDedicatedRefreshButton, NULL, x, height - margin - buttonH, buttonW, buttonH, SWP_NOZORDER);
		x += buttonW + buttonGap;
	}
	if (g_hDedicatedCopyButton != NULL)
	{
		SetWindowPos(g_hDedicatedCopyButton, NULL, x, height - margin - buttonH, buttonW, buttonH, SWP_NOZORDER);
		x += buttonW + buttonGap;
	}
	if (g_hDedicatedSaveToggleButton != NULL)
	{
		SetWindowPos(g_hDedicatedSaveToggleButton, NULL, x, height - margin - buttonH, buttonW, buttonH, SWP_NOZORDER);
		x += buttonW + buttonGap;
	}
	if (g_hDedicatedWhitelistToggleButton != NULL)
	{
		SetWindowPos(g_hDedicatedWhitelistToggleButton, NULL, x, height - margin - buttonH, buttonW, buttonH, SWP_NOZORDER);
		x += buttonW + buttonGap;
	}
	if (g_hDedicatedKickAllButton != NULL)
	{
		SetWindowPos(g_hDedicatedKickAllButton, NULL, x, height - margin - buttonH, buttonW, buttonH, SWP_NOZORDER);
		x += buttonW + buttonGap;
	}
	if (g_hDedicatedStopButton != NULL)
	{
		SetWindowPos(g_hDedicatedStopButton, NULL, x, height - margin - buttonH, buttonW, buttonH, SWP_NOZORDER);
	}
}

static void DedicatedGuiCreateControls(HWND hWnd)
{
	if (!g_dedicatedGuiEnabled || hWnd == NULL)
	{
		return;
	}

	g_hDedicatedFont = CreateFontW(
		-17, 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
		DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

	if (g_hDedicatedBgBrush == NULL) g_hDedicatedBgBrush = CreateSolidBrush(DEDICATED_BG_COLOR);
	if (g_hDedicatedPanelBrush == NULL) g_hDedicatedPanelBrush = CreateSolidBrush(DEDICATED_PANEL_COLOR);
	if (g_hDedicatedLogBrush == NULL) g_hDedicatedLogBrush = CreateSolidBrush(DEDICATED_LOG_BG_COLOR);

	g_hDedicatedStatus = CreateWindowExW(
		0, L"STATIC", L"Status: Starting",
		WS_CHILD | WS_VISIBLE,
		10, 10, 100, 20,
		hWnd, NULL, g_hInst, NULL);

	g_hDedicatedDetails = CreateWindowExW(
		0, L"STATIC", L"",
		WS_CHILD | WS_VISIBLE,
		10, 34, 100, 20,
		hWnd, NULL, g_hInst, NULL);

	g_hDedicatedLog = CreateWindowExW(
		WS_EX_CLIENTEDGE, L"EDIT", L"",
		WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
		10, 60, 100, 100,
		hWnd, NULL, g_hInst, NULL);

	g_hDedicatedRefreshButton = CreateWindowExW(
		0, L"BUTTON", L"Refresh",
		WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
		10, 10, 100, 28,
		hWnd, (HMENU)ID_DEDICATED_REFRESH_BUTTON, g_hInst, NULL);

	g_hDedicatedCopyButton = CreateWindowExW(
		0, L"BUTTON", L"Copy Join IP",
		WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
		10, 10, 100, 28,
		hWnd, (HMENU)ID_DEDICATED_COPY_BUTTON, g_hInst, NULL);

	g_hDedicatedSaveToggleButton = CreateWindowExW(
		0, L"BUTTON", L"Disable Saving",
		WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
		10, 10, 100, 28,
		hWnd, (HMENU)ID_DEDICATED_SAVE_TOGGLE_BUTTON, g_hInst, NULL);

	g_hDedicatedWhitelistToggleButton = CreateWindowExW(
		0, L"BUTTON", L"Whitelist: OFF",
		WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
		10, 10, 100, 28,
		hWnd, (HMENU)ID_DEDICATED_WHITELIST_TOGGLE_BUTTON, g_hInst, NULL);

	g_hDedicatedKickAllButton = CreateWindowExW(
		0, L"BUTTON", L"Kick All",
		WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
		10, 10, 100, 28,
		hWnd, (HMENU)ID_DEDICATED_KICKALL_BUTTON, g_hInst, NULL);

	g_hDedicatedStopButton = CreateWindowExW(
		0, L"BUTTON", L"Stop Server",
		WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
		10, 10, 100, 28,
		hWnd, (HMENU)ID_DEDICATED_STOP_BUTTON, g_hInst, NULL);

	if (g_hDedicatedFont != NULL)
	{
		if (g_hDedicatedStatus != NULL) SendMessage(g_hDedicatedStatus, WM_SETFONT, (WPARAM)g_hDedicatedFont, TRUE);
		if (g_hDedicatedDetails != NULL) SendMessage(g_hDedicatedDetails, WM_SETFONT, (WPARAM)g_hDedicatedFont, TRUE);
		if (g_hDedicatedLog != NULL) SendMessage(g_hDedicatedLog, WM_SETFONT, (WPARAM)g_hDedicatedFont, TRUE);
		if (g_hDedicatedRefreshButton != NULL) SendMessage(g_hDedicatedRefreshButton, WM_SETFONT, (WPARAM)g_hDedicatedFont, TRUE);
		if (g_hDedicatedCopyButton != NULL) SendMessage(g_hDedicatedCopyButton, WM_SETFONT, (WPARAM)g_hDedicatedFont, TRUE);
		if (g_hDedicatedSaveToggleButton != NULL) SendMessage(g_hDedicatedSaveToggleButton, WM_SETFONT, (WPARAM)g_hDedicatedFont, TRUE);
		if (g_hDedicatedWhitelistToggleButton != NULL) SendMessage(g_hDedicatedWhitelistToggleButton, WM_SETFONT, (WPARAM)g_hDedicatedFont, TRUE);
		if (g_hDedicatedKickAllButton != NULL) SendMessage(g_hDedicatedKickAllButton, WM_SETFONT, (WPARAM)g_hDedicatedFont, TRUE);
		if (g_hDedicatedStopButton != NULL) SendMessage(g_hDedicatedStopButton, WM_SETFONT, (WPARAM)g_hDedicatedFont, TRUE);
	}

	SetWindowTextW(hWnd, L"Minecraft Dedicated Server");
	SetTimer(hWnd, ID_DEDICATED_TIMER, 1000, NULL);
	DedicatedGuiLayout(hWnd);
	DedicatedGuiUpdateStatus();
	DedicatedGuiAppendLogNow("Dedicated server GUI ready");
}

static LONG WINAPI StandaloneUnhandledExceptionFilter(EXCEPTION_POINTERS *exceptionInfo)
{
	if (exceptionInfo && exceptionInfo->ExceptionRecord)
	{
		StandaloneLog(
			"Unhandled exception: code=0x%08X address=0x%p flags=0x%08X",
			exceptionInfo->ExceptionRecord->ExceptionCode,
			exceptionInfo->ExceptionRecord->ExceptionAddress,
			exceptionInfo->ExceptionRecord->ExceptionFlags
		);
	}
	else
	{
		StandaloneLog("Unhandled exception: exception info unavailable");
	}
	return EXCEPTION_CONTINUE_SEARCH;
}

static bool IsFilePath(const wstring &path)
{
	DWORD attrs = GetFileAttributesW(path.c_str());
	return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static void ConfigureStandaloneWorkingDirectory()
{
	wchar_t exePath[MAX_PATH];
	if (!GetModuleFileNameW(NULL, exePath, MAX_PATH))
	{
		StandaloneLog("ConfigureStandaloneWorkingDirectory: GetModuleFileNameW failed");
		return;
	}

	wstring exeDir(exePath);
	size_t lastSlash = exeDir.find_last_of(L"\\/");
	if (lastSlash == wstring::npos)
	{
		StandaloneLog("ConfigureStandaloneWorkingDirectory: executable path has no directory separator");
		return;
	}
	exeDir = exeDir.substr(0, lastSlash);

	wstring root = exeDir;
	for (int i = 0; i < 8; ++i)
	{
		wstring mediaFromClient = root + L"\\Minecraft.Client\\Common\\Media\\MediaWindows64.arc";
		wstring mediaLocal = root + L"\\Common\\Media\\MediaWindows64.arc";
		wstring fontFromClient = root + L"\\Minecraft.Client\\Common\\res\\font\\Mojangles_7.png";
		wstring fontLocal = root + L"\\Common\\res\\font\\Mojangles_7.png";

		if (IsFilePath(mediaFromClient) || IsFilePath(fontFromClient))
		{
			wstring clientRoot = root + L"\\Minecraft.Client";
			if (!SetCurrentDirectoryW(clientRoot.c_str()))
			{
				char pathA[MAX_PATH * 2] = { 0 };
				wcstombs(pathA, clientRoot.c_str(), sizeof(pathA) - 1);
				StandaloneLog("ConfigureStandaloneWorkingDirectory: failed to set cwd to '%s' (err=%lu)", pathA, GetLastError());
			}
			else
			{
				char pathA[MAX_PATH * 2] = { 0 };
				wcstombs(pathA, clientRoot.c_str(), sizeof(pathA) - 1);
				StandaloneLog("ConfigureStandaloneWorkingDirectory: cwd set to '%s'", pathA);
			}
			return;
		}
		if (IsFilePath(mediaLocal) || IsFilePath(fontLocal))
		{
			if (!SetCurrentDirectoryW(root.c_str()))
			{
				char pathA[MAX_PATH * 2] = { 0 };
				wcstombs(pathA, root.c_str(), sizeof(pathA) - 1);
				StandaloneLog("ConfigureStandaloneWorkingDirectory: failed to set cwd to '%s' (err=%lu)", pathA, GetLastError());
			}
			else
			{
				char pathA[MAX_PATH * 2] = { 0 };
				wcstombs(pathA, root.c_str(), sizeof(pathA) - 1);
				StandaloneLog("ConfigureStandaloneWorkingDirectory: cwd set to '%s'", pathA);
			}
			return;
		}
		root += L"\\..";
	}

	char exeDirA[MAX_PATH * 2] = { 0 };
	wcstombs(exeDirA, exeDir.c_str(), sizeof(exeDirA) - 1);
	StandaloneLog("ConfigureStandaloneWorkingDirectory: no media/font root found from '%s'", exeDirA);
}

struct Win64LaunchConfig
{
	bool dedicated;
	bool flatWorld;
	bool disableSaving;
	unsigned char maxPlayers;
	int port;
	string worldName;
	string bindAddress;
	string serverName;
	bool whitelistEnabled;
};

static string TrimAscii(const string &in)
{
	size_t start = 0;
	while (start < in.size() && isspace((unsigned char)in[start]))
	{
		++start;
	}
	size_t end = in.size();
	while (end > start && isspace((unsigned char)in[end - 1]))
	{
		--end;
	}
	return in.substr(start, end - start);
}

static bool ParseBoolValue(const string &value, bool defaultValue)
{
	const string v = TrimAscii(value);
	if (_stricmp(v.c_str(), "1") == 0 || _stricmp(v.c_str(), "true") == 0 || _stricmp(v.c_str(), "yes") == 0 || _stricmp(v.c_str(), "on") == 0)
	{
		return true;
	}
	if (_stricmp(v.c_str(), "0") == 0 || _stricmp(v.c_str(), "false") == 0 || _stricmp(v.c_str(), "no") == 0 || _stricmp(v.c_str(), "off") == 0)
	{
		return false;
	}
	return defaultValue;
}

static bool LoadDedicatedServerProperties(Win64LaunchConfig &cfg)
{
	const char *fileName = "dedicated-server.properties";
	std::ifstream in(fileName);
	if (!in.good())
	{
		std::ofstream out(fileName, std::ios::out | std::ios::trunc);
		if (out.good())
		{
			out << "# LCE Dedicated Server Properties\n";
			out << "dedicated=true\n";
			out << "server-name=" << cfg.serverName << "\n";
			out << "world-name=" << cfg.worldName << "\n";
			out << "bind-address=" << cfg.bindAddress << "\n";
			out << "server-port=" << cfg.port << "\n";
			out << "max-players=" << (int)cfg.maxPlayers << "\n";
			out << "level-type=" << (cfg.flatWorld ? "flat" : "normal") << "\n";
			out << "save-world=" << (cfg.disableSaving ? "false" : "true") << "\n";
			out << "whitelist=" << (cfg.whitelistEnabled ? "true" : "false") << "\n";
		}
		return false;
	}

	std::map<std::string, std::string> kv;
	std::string line;
	while (std::getline(in, line))
	{
		line = TrimAscii(line);
		if (line.empty() || line[0] == '#' || line[0] == ';')
		{
			continue;
		}
		const size_t eq = line.find('=');
		if (eq == std::string::npos)
		{
			continue;
		}
		std::string key = TrimAscii(line.substr(0, eq));
		std::string value = TrimAscii(line.substr(eq + 1));
		kv[key] = value;
	}

	if (kv.find("dedicated") != kv.end()) cfg.dedicated = ParseBoolValue(kv["dedicated"], cfg.dedicated);
	if (kv.find("server-name") != kv.end() && !kv["server-name"].empty()) cfg.serverName = kv["server-name"];
	if (kv.find("world-name") != kv.end() && !kv["world-name"].empty()) cfg.worldName = kv["world-name"];
	if (kv.find("bind-address") != kv.end() && !kv["bind-address"].empty()) cfg.bindAddress = kv["bind-address"];
	if (kv.find("server-port") != kv.end())
	{
		const int p = atoi(kv["server-port"].c_str());
		if (p > 0 && p <= 65535) cfg.port = p;
	}
	if (kv.find("max-players") != kv.end())
	{
		const int mp = atoi(kv["max-players"].c_str());
		if (mp >= 1 && mp <= MINECRAFT_NET_MAX_PLAYERS) cfg.maxPlayers = (unsigned char)mp;
	}
	if (kv.find("level-type") != kv.end())
	{
		cfg.flatWorld = (_stricmp(kv["level-type"].c_str(), "flat") == 0);
	}
	if (kv.find("save-world") != kv.end())
	{
		const bool saveWorld = ParseBoolValue(kv["save-world"], !cfg.disableSaving);
		cfg.disableSaving = !saveWorld;
	}
	if (kv.find("whitelist") != kv.end())
	{
		cfg.whitelistEnabled = ParseBoolValue(kv["whitelist"], cfg.whitelistEnabled);
	}

	return true;
}

static vector<string> TokenizeCommandLine(const char *cmdLine)
{
	vector<string> tokens;
	if (cmdLine == NULL || cmdLine[0] == 0)
	{
		return tokens;
	}

	string current;
	bool inQuotes = false;
	for (const char *p = cmdLine; *p != 0; ++p)
	{
		const char ch = *p;
		if (ch == '"')
		{
			inQuotes = !inQuotes;
			continue;
		}

		if (!inQuotes && isspace((unsigned char)ch))
		{
			if (!current.empty())
			{
				tokens.push_back(current);
				current.clear();
			}
		}
		else
		{
			current.push_back(ch);
		}
	}

	if (!current.empty())
	{
		tokens.push_back(current);
	}
	return tokens;
}

static bool HasCommandToken(const vector<string> &tokens, const char *flag)
{
	for (size_t i = 0; i < tokens.size(); ++i)
	{
		if (_stricmp(tokens[i].c_str(), flag) == 0)
		{
			return true;
		}
	}
	return false;
}

static bool GetCommandValue(const vector<string> &tokens, const char *flag, string &value)
{
	for (size_t i = 0; i < tokens.size(); ++i)
	{
		if (_stricmp(tokens[i].c_str(), flag) == 0)
		{
			if ((i + 1) < tokens.size())
			{
				value = tokens[i + 1];
				return true;
			}
			return false;
		}

		const size_t flagLen = strlen(flag);
		if (_strnicmp(tokens[i].c_str(), flag, flagLen) == 0 && tokens[i].size() > (flagLen + 1) && tokens[i][flagLen] == '=')
		{
			value = tokens[i].substr(flagLen + 1);
			return true;
		}
	}
	return false;
}

static bool StartDedicatedServer(const Win64LaunchConfig &launchConfig)
{
	Minecraft *pMinecraft = Minecraft::GetInstance();
	if (pMinecraft == NULL)
	{
		StandaloneLog("Dedicated: Minecraft instance is null");
		return false;
	}

	app.DebugPrintf("Dedicated: starting with world=\"%s\" flat=%d disableSaving=%d maxPlayers=%u bind=%s port=%d\n",
		launchConfig.worldName.c_str(),
		launchConfig.flatWorld ? 1 : 0,
		launchConfig.disableSaving ? 1 : 0,
		(unsigned int)launchConfig.maxPlayers,
		g_Win64MultiplayerIP,
		launchConfig.port);

	StorageManager.SetSaveDisabled(launchConfig.disableSaving);
	app.SetGameHostOption(eGameHostOption_DisableSaving, launchConfig.disableSaving ? 1 : 0);

	app.setLevelGenerationOptions(NULL);
	app.ReleaseSaveThumbnail();
	ProfileManager.SetLockedProfile(0);
	ProfileManager.SetPrimaryPad(0);
	pMinecraft->user->name = g_Win64UsernameW;
	app.ApplyGameSettingsChanged(0);

	MinecraftServer::resetFlags();
	app.SetTutorialMode(false);
	app.SetCorruptSaveDeleted(false);
	app.ClearTerrainFeaturePosition();

	wstring worldNameW = convStringToWstring(launchConfig.worldName);
	StorageManager.ResetSaveData();
	StorageManager.SetSaveTitle(worldNameW.c_str());

	NetworkGameInitData *param = new NetworkGameInitData();
	ZeroMemory(param, sizeof(NetworkGameInitData));
	param->seed = 0;
	param->saveData = NULL;

	app.SetGameHostOption(eGameHostOption_Difficulty, pMinecraft->options->difficulty);
	app.SetGameHostOption(eGameHostOption_FriendsOfFriends, 1);
	app.SetGameHostOption(eGameHostOption_Gamertags, 1);
	app.SetGameHostOption(eGameHostOption_BedrockFog, 1);
	app.SetGameHostOption(eGameHostOption_GameType, 0);
	app.SetGameHostOption(eGameHostOption_LevelType, launchConfig.flatWorld ? 1 : 0);
	app.SetGameHostOption(eGameHostOption_Structures, launchConfig.flatWorld ? 0 : 1);
	app.SetGameHostOption(eGameHostOption_BonusChest, 0);
	app.SetGameHostOption(eGameHostOption_PvP, 1);
	app.SetGameHostOption(eGameHostOption_TrustPlayers, 1);
	app.SetGameHostOption(eGameHostOption_FireSpreads, 1);
	app.SetGameHostOption(eGameHostOption_TNT, 1);
	app.SetGameHostOption(eGameHostOption_HostCanFly, 1);
	app.SetGameHostOption(eGameHostOption_HostCanChangeHunger, 1);
	app.SetGameHostOption(eGameHostOption_HostCanBeInvisible, 1);
	param->settings = app.GetGameHostOption(eGameHostOption_All);

	unsigned char publicSlots = launchConfig.maxPlayers;
	if (publicSlots == 0 || publicSlots > MINECRAFT_NET_MAX_PLAYERS)
	{
		publicSlots = MINECRAFT_NET_MAX_PLAYERS;
	}

	g_NetworkManager.SetPrivateGame(false);
	g_NetworkManager.HostGame(0, true, false, publicSlots, 0);
	// Keep a host network-player object for server-side queue/system bookkeeping.
	// The dedicated path now suppresses slot 0 from active gameplay player lists.
	g_NetworkManager.FakeLocalPlayerJoined();

	app.SetAutosaveTimerTime();

	C4JThread *thread = new C4JThread(&CGameNetworkManager::RunNetworkGameThreadProc, (LPVOID)param, "RunNetworkGame");
	thread->Run();

	const DWORD waitStart = GetTickCount();
	while (!app.GetGameStarted())
	{
		g_NetworkManager.DoWork();
		app.HandleXuiActions();
		Sleep(10);

		if (MinecraftServer::serverHalted())
		{
			StandaloneLog("Dedicated: server halted during startup");
			return false;
		}

		if ((GetTickCount() - waitStart) > 45000)
		{
			StandaloneLog("Dedicated: startup timed out");
			return false;
		}
	}

	g_dedicatedServerStartTick = GetTickCount();
	MinecraftServer *serverInstance = MinecraftServer::getInstance();
	if (serverInstance != NULL && serverInstance->getPlayers() != NULL)
	{
		serverInstance->getPlayers()->setWhitelistEnabled(launchConfig.whitelistEnabled);
		app.DebugPrintf("Dedicated: whitelist %s\n", launchConfig.whitelistEnabled ? "enabled" : "disabled");
	}
	DedicatedGuiUpdateStatus();
	StandaloneLog("Dedicated: server is online");
	return true;
}

void DefineActions(void)
{
	// The app needs to define the actions required, and the possible mappings for these

	// Split into Menu actions, and in-game actions

	InputManager.SetGameJoypadMaps(MAP_STYLE_0,ACTION_MENU_A,							_360_JOY_BUTTON_A);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,ACTION_MENU_B,							_360_JOY_BUTTON_B);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,ACTION_MENU_X,							_360_JOY_BUTTON_X);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,ACTION_MENU_Y,							_360_JOY_BUTTON_Y);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,ACTION_MENU_OK,							_360_JOY_BUTTON_A);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,ACTION_MENU_CANCEL,						_360_JOY_BUTTON_B);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,ACTION_MENU_UP,							_360_JOY_BUTTON_DPAD_UP | _360_JOY_BUTTON_LSTICK_UP);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,ACTION_MENU_DOWN,						_360_JOY_BUTTON_DPAD_DOWN | _360_JOY_BUTTON_LSTICK_DOWN);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,ACTION_MENU_LEFT,						_360_JOY_BUTTON_DPAD_LEFT | _360_JOY_BUTTON_LSTICK_LEFT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,ACTION_MENU_RIGHT,						_360_JOY_BUTTON_DPAD_RIGHT | _360_JOY_BUTTON_LSTICK_RIGHT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,ACTION_MENU_PAGEUP,						_360_JOY_BUTTON_LT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,ACTION_MENU_PAGEDOWN,					_360_JOY_BUTTON_RT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,ACTION_MENU_RIGHT_SCROLL,				_360_JOY_BUTTON_RB);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,ACTION_MENU_LEFT_SCROLL,					_360_JOY_BUTTON_LB);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,ACTION_MENU_PAUSEMENU,					_360_JOY_BUTTON_START);

	InputManager.SetGameJoypadMaps(MAP_STYLE_0,ACTION_MENU_STICK_PRESS,					_360_JOY_BUTTON_LTHUMB);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,ACTION_MENU_OTHER_STICK_PRESS,			_360_JOY_BUTTON_RTHUMB);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,ACTION_MENU_OTHER_STICK_UP,				_360_JOY_BUTTON_RSTICK_UP);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,ACTION_MENU_OTHER_STICK_DOWN,			_360_JOY_BUTTON_RSTICK_DOWN);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,ACTION_MENU_OTHER_STICK_LEFT,			_360_JOY_BUTTON_RSTICK_LEFT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,ACTION_MENU_OTHER_STICK_RIGHT,			_360_JOY_BUTTON_RSTICK_RIGHT);

	InputManager.SetGameJoypadMaps(MAP_STYLE_0,MINECRAFT_ACTION_JUMP,					_360_JOY_BUTTON_A);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,MINECRAFT_ACTION_FORWARD,				_360_JOY_BUTTON_LSTICK_UP);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,MINECRAFT_ACTION_BACKWARD,				_360_JOY_BUTTON_LSTICK_DOWN);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,MINECRAFT_ACTION_LEFT,					_360_JOY_BUTTON_LSTICK_LEFT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,MINECRAFT_ACTION_RIGHT,					_360_JOY_BUTTON_LSTICK_RIGHT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,MINECRAFT_ACTION_LOOK_LEFT,				_360_JOY_BUTTON_RSTICK_LEFT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,MINECRAFT_ACTION_LOOK_RIGHT,				_360_JOY_BUTTON_RSTICK_RIGHT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,MINECRAFT_ACTION_LOOK_UP,				_360_JOY_BUTTON_RSTICK_UP);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,MINECRAFT_ACTION_LOOK_DOWN,				_360_JOY_BUTTON_RSTICK_DOWN);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,MINECRAFT_ACTION_USE,					_360_JOY_BUTTON_LT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,MINECRAFT_ACTION_ACTION,					_360_JOY_BUTTON_RT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,MINECRAFT_ACTION_RIGHT_SCROLL,			_360_JOY_BUTTON_RB);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,MINECRAFT_ACTION_LEFT_SCROLL,			_360_JOY_BUTTON_LB);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,MINECRAFT_ACTION_INVENTORY,				_360_JOY_BUTTON_Y);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,MINECRAFT_ACTION_PAUSEMENU,				_360_JOY_BUTTON_START);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,MINECRAFT_ACTION_DROP,					_360_JOY_BUTTON_B);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,MINECRAFT_ACTION_SNEAK_TOGGLE,			_360_JOY_BUTTON_RTHUMB);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,MINECRAFT_ACTION_CRAFTING,				_360_JOY_BUTTON_X);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,MINECRAFT_ACTION_RENDER_THIRD_PERSON,	_360_JOY_BUTTON_LTHUMB);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,MINECRAFT_ACTION_GAME_INFO,				_360_JOY_BUTTON_BACK);

	InputManager.SetGameJoypadMaps(MAP_STYLE_0,MINECRAFT_ACTION_DPAD_LEFT,				_360_JOY_BUTTON_DPAD_LEFT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,MINECRAFT_ACTION_DPAD_RIGHT,				_360_JOY_BUTTON_DPAD_RIGHT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,MINECRAFT_ACTION_DPAD_UP,				_360_JOY_BUTTON_DPAD_UP);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,MINECRAFT_ACTION_DPAD_DOWN,				_360_JOY_BUTTON_DPAD_DOWN);

	InputManager.SetGameJoypadMaps(MAP_STYLE_1,ACTION_MENU_A,							_360_JOY_BUTTON_A);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,ACTION_MENU_B,							_360_JOY_BUTTON_B);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,ACTION_MENU_X,							_360_JOY_BUTTON_X);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,ACTION_MENU_Y,							_360_JOY_BUTTON_Y);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,ACTION_MENU_OK,							_360_JOY_BUTTON_A);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,ACTION_MENU_CANCEL,						_360_JOY_BUTTON_B);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,ACTION_MENU_UP,							_360_JOY_BUTTON_DPAD_UP | _360_JOY_BUTTON_LSTICK_UP);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,ACTION_MENU_DOWN,						_360_JOY_BUTTON_DPAD_DOWN | _360_JOY_BUTTON_LSTICK_DOWN);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,ACTION_MENU_LEFT,						_360_JOY_BUTTON_DPAD_LEFT | _360_JOY_BUTTON_LSTICK_LEFT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,ACTION_MENU_RIGHT,						_360_JOY_BUTTON_DPAD_RIGHT | _360_JOY_BUTTON_LSTICK_RIGHT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,ACTION_MENU_PAGEUP,						_360_JOY_BUTTON_LB);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,ACTION_MENU_PAGEDOWN,					_360_JOY_BUTTON_RT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,ACTION_MENU_RIGHT_SCROLL,				_360_JOY_BUTTON_RB);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,ACTION_MENU_LEFT_SCROLL,					_360_JOY_BUTTON_LB);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,ACTION_MENU_PAUSEMENU,					_360_JOY_BUTTON_START);

	InputManager.SetGameJoypadMaps(MAP_STYLE_1,ACTION_MENU_STICK_PRESS,					_360_JOY_BUTTON_LTHUMB);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,ACTION_MENU_OTHER_STICK_PRESS,			_360_JOY_BUTTON_RTHUMB);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,ACTION_MENU_OTHER_STICK_UP,				_360_JOY_BUTTON_RSTICK_UP);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,ACTION_MENU_OTHER_STICK_DOWN,			_360_JOY_BUTTON_RSTICK_DOWN);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,ACTION_MENU_OTHER_STICK_LEFT,			_360_JOY_BUTTON_RSTICK_LEFT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,ACTION_MENU_OTHER_STICK_RIGHT,			_360_JOY_BUTTON_RSTICK_RIGHT);

	InputManager.SetGameJoypadMaps(MAP_STYLE_1,MINECRAFT_ACTION_JUMP,					_360_JOY_BUTTON_RB);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,MINECRAFT_ACTION_FORWARD,				_360_JOY_BUTTON_LSTICK_UP);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,MINECRAFT_ACTION_BACKWARD,				_360_JOY_BUTTON_LSTICK_DOWN);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,MINECRAFT_ACTION_LEFT,					_360_JOY_BUTTON_LSTICK_LEFT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,MINECRAFT_ACTION_RIGHT,					_360_JOY_BUTTON_LSTICK_RIGHT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,MINECRAFT_ACTION_LOOK_LEFT,				_360_JOY_BUTTON_RSTICK_LEFT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,MINECRAFT_ACTION_LOOK_RIGHT,				_360_JOY_BUTTON_RSTICK_RIGHT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,MINECRAFT_ACTION_LOOK_UP,				_360_JOY_BUTTON_RSTICK_UP);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,MINECRAFT_ACTION_LOOK_DOWN,				_360_JOY_BUTTON_RSTICK_DOWN);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,MINECRAFT_ACTION_USE,					_360_JOY_BUTTON_RT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,MINECRAFT_ACTION_ACTION,					_360_JOY_BUTTON_LT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,MINECRAFT_ACTION_RIGHT_SCROLL,			_360_JOY_BUTTON_DPAD_RIGHT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,MINECRAFT_ACTION_LEFT_SCROLL,			_360_JOY_BUTTON_DPAD_LEFT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,MINECRAFT_ACTION_INVENTORY,				_360_JOY_BUTTON_Y);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,MINECRAFT_ACTION_PAUSEMENU,				_360_JOY_BUTTON_START);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,MINECRAFT_ACTION_DROP,					_360_JOY_BUTTON_B);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,MINECRAFT_ACTION_SNEAK_TOGGLE,			_360_JOY_BUTTON_LTHUMB);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,MINECRAFT_ACTION_CRAFTING,				_360_JOY_BUTTON_X);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,MINECRAFT_ACTION_RENDER_THIRD_PERSON,	_360_JOY_BUTTON_RTHUMB);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,MINECRAFT_ACTION_GAME_INFO,				_360_JOY_BUTTON_BACK);
	
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,MINECRAFT_ACTION_DPAD_LEFT,				_360_JOY_BUTTON_DPAD_LEFT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,MINECRAFT_ACTION_DPAD_RIGHT,				_360_JOY_BUTTON_DPAD_RIGHT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,MINECRAFT_ACTION_DPAD_UP,				_360_JOY_BUTTON_DPAD_UP);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,MINECRAFT_ACTION_DPAD_DOWN,				_360_JOY_BUTTON_DPAD_DOWN);	

	InputManager.SetGameJoypadMaps(MAP_STYLE_2,ACTION_MENU_A,							_360_JOY_BUTTON_A);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,ACTION_MENU_B,							_360_JOY_BUTTON_B);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,ACTION_MENU_X,							_360_JOY_BUTTON_X);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,ACTION_MENU_Y,							_360_JOY_BUTTON_Y);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,ACTION_MENU_OK,							_360_JOY_BUTTON_A);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,ACTION_MENU_CANCEL,						_360_JOY_BUTTON_B);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,ACTION_MENU_UP,							_360_JOY_BUTTON_DPAD_UP | _360_JOY_BUTTON_LSTICK_UP);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,ACTION_MENU_DOWN,						_360_JOY_BUTTON_DPAD_DOWN | _360_JOY_BUTTON_LSTICK_DOWN);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,ACTION_MENU_LEFT,						_360_JOY_BUTTON_DPAD_LEFT | _360_JOY_BUTTON_LSTICK_LEFT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,ACTION_MENU_RIGHT,						_360_JOY_BUTTON_DPAD_RIGHT | _360_JOY_BUTTON_LSTICK_RIGHT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,ACTION_MENU_PAGEUP,						_360_JOY_BUTTON_DPAD_UP | _360_JOY_BUTTON_LB);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,ACTION_MENU_PAGEDOWN,					_360_JOY_BUTTON_RT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,ACTION_MENU_RIGHT_SCROLL,				_360_JOY_BUTTON_RB);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,ACTION_MENU_LEFT_SCROLL,					_360_JOY_BUTTON_LB);

	InputManager.SetGameJoypadMaps(MAP_STYLE_2,MINECRAFT_ACTION_JUMP,					_360_JOY_BUTTON_LT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,MINECRAFT_ACTION_FORWARD,				_360_JOY_BUTTON_LSTICK_UP);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,MINECRAFT_ACTION_BACKWARD,				_360_JOY_BUTTON_LSTICK_DOWN);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,MINECRAFT_ACTION_LEFT,					_360_JOY_BUTTON_LSTICK_LEFT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,MINECRAFT_ACTION_RIGHT,					_360_JOY_BUTTON_LSTICK_RIGHT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,MINECRAFT_ACTION_LOOK_LEFT,				_360_JOY_BUTTON_RSTICK_LEFT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,MINECRAFT_ACTION_LOOK_RIGHT,				_360_JOY_BUTTON_RSTICK_RIGHT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,MINECRAFT_ACTION_LOOK_UP,				_360_JOY_BUTTON_RSTICK_UP);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,MINECRAFT_ACTION_LOOK_DOWN,				_360_JOY_BUTTON_RSTICK_DOWN);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,MINECRAFT_ACTION_USE,					_360_JOY_BUTTON_RT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,MINECRAFT_ACTION_ACTION,					_360_JOY_BUTTON_A);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,MINECRAFT_ACTION_RIGHT_SCROLL,			_360_JOY_BUTTON_DPAD_RIGHT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,MINECRAFT_ACTION_LEFT_SCROLL,			_360_JOY_BUTTON_DPAD_LEFT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,MINECRAFT_ACTION_INVENTORY,				_360_JOY_BUTTON_Y);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,MINECRAFT_ACTION_PAUSEMENU,				_360_JOY_BUTTON_START);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,MINECRAFT_ACTION_DROP,					_360_JOY_BUTTON_B);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,MINECRAFT_ACTION_SNEAK_TOGGLE,			_360_JOY_BUTTON_LB);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,MINECRAFT_ACTION_CRAFTING,				_360_JOY_BUTTON_X);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,MINECRAFT_ACTION_RENDER_THIRD_PERSON,	_360_JOY_BUTTON_LTHUMB);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,MINECRAFT_ACTION_GAME_INFO,				_360_JOY_BUTTON_BACK);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,ACTION_MENU_PAUSEMENU,					_360_JOY_BUTTON_START);

	InputManager.SetGameJoypadMaps(MAP_STYLE_2,ACTION_MENU_STICK_PRESS,					_360_JOY_BUTTON_LTHUMB);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,ACTION_MENU_OTHER_STICK_PRESS,			_360_JOY_BUTTON_RTHUMB);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,ACTION_MENU_OTHER_STICK_UP,				_360_JOY_BUTTON_RSTICK_UP);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,ACTION_MENU_OTHER_STICK_DOWN,			_360_JOY_BUTTON_RSTICK_DOWN);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,ACTION_MENU_OTHER_STICK_LEFT,			_360_JOY_BUTTON_RSTICK_LEFT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,ACTION_MENU_OTHER_STICK_RIGHT,			_360_JOY_BUTTON_RSTICK_RIGHT);
	
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,MINECRAFT_ACTION_DPAD_LEFT,				_360_JOY_BUTTON_DPAD_LEFT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,MINECRAFT_ACTION_DPAD_RIGHT,				_360_JOY_BUTTON_DPAD_RIGHT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,MINECRAFT_ACTION_DPAD_UP,				_360_JOY_BUTTON_DPAD_UP);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,MINECRAFT_ACTION_DPAD_DOWN,				_360_JOY_BUTTON_DPAD_DOWN);	
}

#if 0
HRESULT InitD3D( IDirect3DDevice9 **ppDevice,
				D3DPRESENT_PARAMETERS *pd3dPP )
{
	IDirect3D9 *pD3D;

	pD3D = Direct3DCreate9( D3D_SDK_VERSION );

	// Set up the structure used to create the D3DDevice
	// Using a permanent 1280x720 backbuffer now no matter what the actual video resolution.right Have also disabled letterboxing,
	// which would letterbox a 1280x720 output if it detected a 4:3 video source - we're doing an anamorphic squash in this
	// mode so don't need this functionality.

	ZeroMemory( pd3dPP, sizeof(D3DPRESENT_PARAMETERS) );
	XVIDEO_MODE VideoMode;
	XGetVideoMode( &VideoMode );
	g_bWidescreen = VideoMode.fIsWideScreen;
	pd3dPP->BackBufferWidth        = 1280;
	pd3dPP->BackBufferHeight       = 720;
	pd3dPP->BackBufferFormat       = D3DFMT_A8R8G8B8;
	pd3dPP->BackBufferCount        = 1;
	pd3dPP->EnableAutoDepthStencil = TRUE;
	pd3dPP->AutoDepthStencilFormat = D3DFMT_D24S8;
	pd3dPP->SwapEffect             = D3DSWAPEFFECT_DISCARD;
	pd3dPP->PresentationInterval   = D3DPRESENT_INTERVAL_ONE;
	//pd3dPP->Flags				   = D3DPRESENTFLAG_NO_LETTERBOX;
	//ERR[D3D]: Can't set D3DPRESENTFLAG_NO_LETTERBOX when wide-screen is enabled
	//	in the launcher/dashboard.
	if(g_bWidescreen)
		pd3dPP->Flags=0;
	else
		pd3dPP->Flags				   = D3DPRESENTFLAG_NO_LETTERBOX;

	// Create the device.
	return pD3D->CreateDevice(
		0,
		D3DDEVTYPE_HAL,
		NULL,
		D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_BUFFER_2_FRAMES,
		pd3dPP,
		ppDevice );
}
#endif
//#define MEMORY_TRACKING

#ifdef MEMORY_TRACKING
void ResetMem();
void DumpMem();
void MemPixStuff();
#else
void MemSect(int sect)
{
}
#endif

HINSTANCE               g_hInst = NULL;
HWND                    g_hWnd = NULL;

static bool g_isFullscreen = false;
static RECT g_windowedRect = {};
static LONG g_windowedStyle = 0;

static void ForwardPrintableKeyToTextEdit(Minecraft *mc, WPARAM wParam, LPARAM lParam)
{
	if (mc == NULL || mc->screen == NULL || dynamic_cast<TextEditScreen *>(mc->screen) == NULL)
	{
		return;
	}

	// Ignore ctrl/alt combos so gameplay shortcuts do not inject text.
	if ((GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_MENU) & 0x8000))
	{
		return;
	}

	BYTE keyState[256];
	if (!GetKeyboardState(keyState))
	{
		return;
	}

	const UINT vkCode = (UINT)wParam;
	const UINT scanCode = (UINT)((lParam >> 16) & 0xFF);
	wchar_t chars[4] = {};
	const int produced = ToUnicode(vkCode, scanCode, keyState, chars, 4, 0);
	if (produced <= 0)
	{
		return;
	}

	for (int i = 0; i < produced; ++i)
	{
		if (chars[i] >= 32)
		{
			mc->screen->HandleKeyPressed(chars[i], 0);
		}
	}
}

void ToggleFullscreen()
{
	if (!g_hWnd) return;

	if (!g_isFullscreen)
	{
		g_windowedStyle = GetWindowLong(g_hWnd, GWL_STYLE);
		GetWindowRect(g_hWnd, &g_windowedRect);

		HMONITOR hMon = MonitorFromWindow(g_hWnd, MONITOR_DEFAULTTONEAREST);
		MONITORINFO mi = { sizeof(mi) };
		GetMonitorInfo(hMon, &mi);

		SetWindowLong(g_hWnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
		SetWindowPos(g_hWnd, HWND_TOP,
			mi.rcMonitor.left, mi.rcMonitor.top,
			mi.rcMonitor.right - mi.rcMonitor.left,
			mi.rcMonitor.bottom - mi.rcMonitor.top,
			SWP_FRAMECHANGED | SWP_NOOWNERZORDER);

		g_isFullscreen = true;
	}
	else
	{
		SetWindowLong(g_hWnd, GWL_STYLE, g_windowedStyle);
		SetWindowPos(g_hWnd, HWND_NOTOPMOST,
			g_windowedRect.left, g_windowedRect.top,
			g_windowedRect.right - g_windowedRect.left,
			g_windowedRect.bottom - g_windowedRect.top,
			SWP_FRAMECHANGED | SWP_NOOWNERZORDER);

		g_isFullscreen = false;
	}

	if (g_KBMInput.IsWindowFocused())
		g_KBMInput.SetWindowFocused(true);
}
D3D_DRIVER_TYPE         g_driverType = D3D_DRIVER_TYPE_NULL;
D3D_FEATURE_LEVEL       g_featureLevel = D3D_FEATURE_LEVEL_11_0;
ID3D11Device*           g_pd3dDevice = NULL;
ID3D11DeviceContext*    g_pImmediateContext = NULL;
IDXGISwapChain*         g_pSwapChain = NULL;

static WORD g_originalGammaRamp[3][256];
static bool g_gammaRampSaved = false;

void Windows64_UpdateGamma(unsigned short usGamma)
{
	if (!g_hWnd) return;

	HDC hdc = GetDC(g_hWnd);
	if (!hdc) return;

	if (!g_gammaRampSaved)
	{
		GetDeviceGammaRamp(hdc, g_originalGammaRamp);
		g_gammaRampSaved = true;
	}

	float gamma = (float)usGamma / 32768.0f;
	if (gamma < 0.01f) gamma = 0.01f;
	if (gamma > 1.0f) gamma = 1.0f;

	float invGamma = 1.0f / (0.5f + gamma * 0.5f);

	WORD ramp[3][256];
	for (int i = 0; i < 256; i++)
	{
		float normalized = (float)i / 255.0f;
		float corrected = powf(normalized, invGamma);
		WORD val = (WORD)(corrected * 65535.0f + 0.5f);
		ramp[0][i] = val;
		ramp[1][i] = val;
		ramp[2][i] = val;
	}

	SetDeviceGammaRamp(hdc, ramp);
	ReleaseDC(g_hWnd, hdc);
}

void Windows64_RestoreGamma()
{
	if (!g_gammaRampSaved || !g_hWnd) return;
	HDC hdc = GetDC(g_hWnd);
	if (!hdc) return;
	SetDeviceGammaRamp(hdc, g_originalGammaRamp);
	ReleaseDC(g_hWnd, hdc);
}
ID3D11RenderTargetView* g_pRenderTargetView = NULL;
ID3D11DepthStencilView* g_pDepthStencilView = NULL;
ID3D11Texture2D*		g_pDepthStencilBuffer = NULL;

//
//  FUNCTION: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  PURPOSE:  Processes messages for the main window.
//
//  WM_COMMAND	- process the application menu
//  WM_PAINT	- Paint the main window
//  WM_DESTROY	- post a quit message and return
//
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	int wmId, wmEvent;
	PAINTSTRUCT ps;
	HDC hdc;

	switch (message)
	{
	case WM_COMMAND:
		wmId    = LOWORD(wParam);
		wmEvent = HIWORD(wParam);
		// Parse the menu selections:
		switch (wmId)
		{
		case ID_DEDICATED_REFRESH_BUTTON:
			DedicatedGuiUpdateStatus();
			break;
		case ID_DEDICATED_COPY_BUTTON:
			DedicatedGuiCopyConnectInfo(hWnd);
			break;
		case ID_DEDICATED_SAVE_TOGGLE_BUTTON:
		{
			const bool disableSaving = app.GetGameHostOption(eGameHostOption_DisableSaving) == 0;
			StorageManager.SetSaveDisabled(disableSaving);
			app.SetGameHostOption(eGameHostOption_DisableSaving, disableSaving ? 1 : 0);
			DedicatedGuiUpdateStatus();
			StandaloneLog("Dedicated GUI: world saving %s", disableSaving ? "disabled" : "enabled");
			break;
		}
		case ID_DEDICATED_WHITELIST_TOGGLE_BUTTON:
		{
			MinecraftServer *server = MinecraftServer::getInstance();
			if (server != NULL && server->getPlayers() != NULL)
			{
				const bool enabled = !server->getPlayers()->isWhitelistEnabled();
				server->getPlayers()->setWhitelistEnabled(enabled);
				StandaloneLog("Dedicated GUI: whitelist %s", enabled ? "enabled" : "disabled");
			}
			DedicatedGuiUpdateStatus();
			break;
		}
		case ID_DEDICATED_KICKALL_BUTTON:
			DedicatedGuiKickAllPlayers();
			DedicatedGuiUpdateStatus();
			break;
		case ID_DEDICATED_STOP_BUTTON:
			StandaloneLog("Dedicated: stop requested from GUI");
			DestroyWindow(hWnd);
			break;

		case IDM_EXIT:
			DestroyWindow(hWnd);
			break;

		default:
			return DefWindowProc(hWnd, message, wParam, lParam);
		}
		break;
	case WM_PAINT:
		hdc = BeginPaint(hWnd, &ps);
		if (g_hDedicatedBgBrush != NULL)
		{
			RECT rc;
			GetClientRect(hWnd, &rc);
			FillRect(hdc, &rc, g_hDedicatedBgBrush);
		}
		EndPaint(hWnd, &ps);
		break;
	case WM_ERASEBKGND:
		return 1;
	case WM_SIZE:
		DedicatedGuiLayout(hWnd);
		break;
	case WM_TIMER:
		if (wParam == ID_DEDICATED_TIMER)
		{
			DedicatedGuiUpdateStatus();
		}
		break;
	case WM_APP_DEDICATED_APPEND_LOG:
	{
		char *line = (char *)lParam;
		if (line != NULL)
		{
			DedicatedGuiAppendLogNow(line);
			delete[] line;
		}
		break;
	}
	case WM_DESTROY:
		KillTimer(hWnd, ID_DEDICATED_TIMER);
		if (g_hDedicatedBgBrush != NULL) { DeleteObject(g_hDedicatedBgBrush); g_hDedicatedBgBrush = NULL; }
		if (g_hDedicatedPanelBrush != NULL) { DeleteObject(g_hDedicatedPanelBrush); g_hDedicatedPanelBrush = NULL; }
		if (g_hDedicatedLogBrush != NULL) { DeleteObject(g_hDedicatedLogBrush); g_hDedicatedLogBrush = NULL; }
		if (g_hDedicatedFont != NULL) { DeleteObject(g_hDedicatedFont); g_hDedicatedFont = NULL; }
		PostQuitMessage(0);
		break;
	case WM_CTLCOLORSTATIC:
	{
		HDC controlDc = (HDC)wParam;
		HWND controlWnd = (HWND)lParam;
		if (controlWnd == g_hDedicatedLog)
		{
			SetTextColor(controlDc, DEDICATED_MUTED_TEXT_COLOR);
			SetBkColor(controlDc, DEDICATED_LOG_BG_COLOR);
			return (LRESULT)(g_hDedicatedLogBrush != NULL ? g_hDedicatedLogBrush : GetStockObject(BLACK_BRUSH));
		}
		SetTextColor(controlDc, DEDICATED_TEXT_COLOR);
		SetBkMode(controlDc, TRANSPARENT);
		return (LRESULT)(g_hDedicatedBgBrush != NULL ? g_hDedicatedBgBrush : GetStockObject(BLACK_BRUSH));
	}
	case WM_CTLCOLOREDIT:
	{
		HDC controlDc = (HDC)wParam;
		SetTextColor(controlDc, DEDICATED_MUTED_TEXT_COLOR);
		SetBkColor(controlDc, DEDICATED_LOG_BG_COLOR);
		return (LRESULT)(g_hDedicatedLogBrush != NULL ? g_hDedicatedLogBrush : GetStockObject(BLACK_BRUSH));
	}
	case WM_CTLCOLORBTN:
	{
		HDC controlDc = (HDC)wParam;
		SetTextColor(controlDc, DEDICATED_TEXT_COLOR);
		SetBkColor(controlDc, DEDICATED_PANEL_COLOR);
		return (LRESULT)(g_hDedicatedPanelBrush != NULL ? g_hDedicatedPanelBrush : GetStockObject(GRAY_BRUSH));
	}

	case WM_KILLFOCUS:
		g_KBMInput.ClearAllState();
		g_KBMInput.SetWindowFocused(false);
		if (g_KBMInput.IsMouseGrabbed())
			g_KBMInput.SetMouseGrabbed(false);
		break;

	case WM_SETFOCUS:
		g_KBMInput.SetWindowFocused(true);
		break;

	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
	{
		const int rawVk = (int)wParam;
		int vk = (int)wParam;
		if (vk == VK_F11)
		{
			ToggleFullscreen();
			break;
		}
		if (vk == VK_SHIFT)
			vk = (MapVirtualKey((lParam >> 16) & 0xFF, MAPVK_VSC_TO_VK_EX) == VK_RSHIFT) ? VK_RSHIFT : VK_LSHIFT;
		else if (vk == VK_CONTROL)
			vk = (lParam & (1 << 24)) ? VK_RCONTROL : VK_LCONTROL;
		else if (vk == VK_MENU)
			vk = (lParam & (1 << 24)) ? VK_RMENU : VK_LMENU;
		g_KBMInput.OnKeyDown(vk);

		// The old Keyboard.next() event pump is disabled on this platform build.
		// Forward key controls directly to chat so Enter/Backspace/Escape work.
		Minecraft *mc = Minecraft::GetInstance();
		if (mc != NULL && mc->screen != NULL &&
			(dynamic_cast<ChatScreen *>(mc->screen) != NULL || dynamic_cast<TextEditScreen *>(mc->screen) != NULL))
		{
			if (vk == VK_RETURN)
			{
				mc->screen->HandleKeyPressed(0, Keyboard::KEY_RETURN);
			}
			else if (vk == VK_BACK)
			{
				mc->screen->HandleKeyPressed(0, Keyboard::KEY_BACK);
			}
			else if (vk == VK_ESCAPE)
			{
				mc->screen->HandleKeyPressed(0, Keyboard::KEY_ESCAPE);
			}
			else if (vk == VK_UP)
			{
				mc->screen->HandleKeyPressed(0, Keyboard::KEY_UP);
			}
			else if (vk == VK_DOWN)
			{
				mc->screen->HandleKeyPressed(0, Keyboard::KEY_DOWN);
			}

			ForwardPrintableKeyToTextEdit(mc, (WPARAM)rawVk, lParam);
		}
		break;
	}
	case WM_KEYUP:
	case WM_SYSKEYUP:
	{
		int vk = (int)wParam;
		if (vk == VK_SHIFT)
			vk = (MapVirtualKey((lParam >> 16) & 0xFF, MAPVK_VSC_TO_VK_EX) == VK_RSHIFT) ? VK_RSHIFT : VK_LSHIFT;
		else if (vk == VK_CONTROL)
			vk = (lParam & (1 << 24)) ? VK_RCONTROL : VK_LCONTROL;
		else if (vk == VK_MENU)
			vk = (lParam & (1 << 24)) ? VK_RMENU : VK_LMENU;
		g_KBMInput.OnKeyUp(vk);
		break;
	}

	case WM_LBUTTONDOWN:
		g_KBMInput.OnMouseButtonDown(KeyboardMouseInput::MOUSE_LEFT);
		break;
	case WM_LBUTTONUP:
		g_KBMInput.OnMouseButtonUp(KeyboardMouseInput::MOUSE_LEFT);
		break;
	case WM_RBUTTONDOWN:
		g_KBMInput.OnMouseButtonDown(KeyboardMouseInput::MOUSE_RIGHT);
		break;
	case WM_RBUTTONUP:
		g_KBMInput.OnMouseButtonUp(KeyboardMouseInput::MOUSE_RIGHT);
		break;
	case WM_MBUTTONDOWN:
		g_KBMInput.OnMouseButtonDown(KeyboardMouseInput::MOUSE_MIDDLE);
		break;
	case WM_MBUTTONUP:
		g_KBMInput.OnMouseButtonUp(KeyboardMouseInput::MOUSE_MIDDLE);
		break;

	case WM_MOUSEMOVE:
		g_KBMInput.OnMouseMove(LOWORD(lParam), HIWORD(lParam));
		break;

	case WM_MOUSEWHEEL:
		g_KBMInput.OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));
		break;

	case WM_CHAR:
	{
		// Route printable text to chat input.
		// Sign editing receives text from WM_KEYDOWN+ToUnicode to avoid missing WM_CHAR cases.
		Minecraft *mc = Minecraft::GetInstance();
		if (mc != NULL && mc->screen != NULL &&
			(dynamic_cast<ChatScreen *>(mc->screen) != NULL))
		{
			wchar_t ch = (wchar_t)wParam;
			if (ch >= 32)
			{
				mc->screen->HandleKeyPressed(ch, 0);
			}
			return 0;
		}
		break;
	}

	case WM_INPUT:
		{
			UINT dwSize = 0;
			GetRawInputData((HRAWINPUT)lParam, RID_INPUT, NULL, &dwSize, sizeof(RAWINPUTHEADER));
			if (dwSize > 0 && dwSize <= 256)
			{
				BYTE rawBuffer[256];
				if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, rawBuffer, &dwSize, sizeof(RAWINPUTHEADER)) == dwSize)
				{
					RAWINPUT* raw = (RAWINPUT*)rawBuffer;
					if (raw->header.dwType == RIM_TYPEMOUSE)
					{
						g_KBMInput.OnRawMouseDelta(raw->data.mouse.lLastX, raw->data.mouse.lLastY);
					}
				}
			}
		}
		break;

	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}

//
//  FUNCTION: MyRegisterClass()
//
//  PURPOSE: Registers the window class.
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
	WNDCLASSEX wcex;

	wcex.cbSize = sizeof(WNDCLASSEX);

	wcex.style			= CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc	= WndProc;
	wcex.cbClsExtra		= 0;
	wcex.cbWndExtra		= 0;
	wcex.hInstance		= hInstance;
	wcex.hIcon			= LoadIcon(hInstance, "Minecraft");
	wcex.hCursor		= LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground	= (HBRUSH)(COLOR_WINDOW+1);
	wcex.lpszMenuName	= "Minecraft";
	wcex.lpszClassName	= "MinecraftClass";
	wcex.hIconSm		= LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

	return RegisterClassEx(&wcex);
}

//
//   FUNCTION: InitInstance(HINSTANCE, int)
//
//   PURPOSE: Saves instance handle and creates main window
//
//   COMMENTS:
//
//        In this function, we save the instance handle in a global variable and
//        create and display the main program window.
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
	g_hInst = hInstance; // Store instance handle in our global variable

	RECT wr = {0, 0, g_iScreenWidth, g_iScreenHeight};    // set the size, but not the position
	AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);    // adjust the size

	g_hWnd = CreateWindow(	"MinecraftClass",
		"Minecraft",
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		0,
		wr.right - wr.left,    // width of the window
		wr.bottom - wr.top,    // height of the window
		NULL,
		NULL,
		hInstance,
		NULL);

	if (!g_hWnd)
	{
		return FALSE;
	}

	ShowWindow(g_hWnd, nCmdShow);
	UpdateWindow(g_hWnd);

	return TRUE;
}

// 4J Stu - These functions are referenced from the Windows Input library
void ClearGlobalText()
{
	// clear the global text
	memset(chGlobalText,0,256);
	memset(ui16GlobalText,0,512);
}

uint16_t *GetGlobalText()
{
	//copy the ch text to ui16
	char * pchBuffer=(char *)ui16GlobalText;
		for(int i=0;i<256;i++)
		{
			pchBuffer[i*2]=chGlobalText[i];
		}
	return ui16GlobalText;
}
void SeedEditBox()
{
	DialogBox(hMyInst, MAKEINTRESOURCE(IDD_SEED),
		g_hWnd, reinterpret_cast<DLGPROC>(DlgProc));
}


//---------------------------------------------------------------------------
LRESULT CALLBACK DlgProc(HWND hWndDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	switch(Msg)
	{
	case WM_INITDIALOG:
		return TRUE;

	case WM_COMMAND:
		switch(wParam)
		{
		case IDOK:
			// Set the text
			GetDlgItemText(hWndDlg,IDC_EDIT,chGlobalText,256);
			EndDialog(hWndDlg, 0);
			return TRUE;
		}
		break;
	}

	return FALSE;
}

//--------------------------------------------------------------------------------------
// Create Direct3D device and swap chain
//--------------------------------------------------------------------------------------
HRESULT InitDevice()
{
	HRESULT hr = S_OK;

	RECT rc;
	GetClientRect( g_hWnd, &rc );
	UINT width = rc.right - rc.left;
	UINT height = rc.bottom - rc.top;
//app.DebugPrintf("width: %d, height: %d\n", width, height);
	width = g_iScreenWidth;
	height = g_iScreenHeight;
app.DebugPrintf("width: %d, height: %d\n", width, height);

	UINT createDeviceFlags = 0;
#ifdef _DEBUG
	//createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	D3D_DRIVER_TYPE driverTypes[] =
	{
		D3D_DRIVER_TYPE_HARDWARE,
		D3D_DRIVER_TYPE_WARP,
		D3D_DRIVER_TYPE_REFERENCE,
	};
	UINT numDriverTypes = ARRAYSIZE( driverTypes );

	D3D_FEATURE_LEVEL featureLevels[] =
	{
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_1,
		D3D_FEATURE_LEVEL_10_0,
	};
	UINT numFeatureLevels = ARRAYSIZE( featureLevels );

	DXGI_SWAP_CHAIN_DESC sd;
	ZeroMemory( &sd, sizeof( sd ) );
	sd.BufferCount = 1;
	sd.BufferDesc.Width = width;
	sd.BufferDesc.Height = height;
	sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sd.BufferDesc.RefreshRate.Numerator = 60;
	sd.BufferDesc.RefreshRate.Denominator = 1;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.OutputWindow = g_hWnd;
	sd.SampleDesc.Count = 1;
	sd.SampleDesc.Quality = 0;
	sd.Windowed = TRUE;

	for( UINT driverTypeIndex = 0; driverTypeIndex < numDriverTypes; driverTypeIndex++ )
	{
		g_driverType = driverTypes[driverTypeIndex];
		hr = D3D11CreateDeviceAndSwapChain( NULL, g_driverType, NULL, createDeviceFlags, featureLevels, numFeatureLevels,
			D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &g_featureLevel, &g_pImmediateContext );
		if( HRESULT_SUCCEEDED( hr ) )
			break;
	}
	if( FAILED( hr ) )
		return hr;

	// Create a render target view
	ID3D11Texture2D* pBackBuffer = NULL;
	hr = g_pSwapChain->GetBuffer( 0, __uuidof( ID3D11Texture2D ), ( LPVOID* )&pBackBuffer );
	if( FAILED( hr ) )
		return hr;

	// Create a depth stencil buffer
	D3D11_TEXTURE2D_DESC descDepth;

	descDepth.Width = width;
	descDepth.Height = height;
	descDepth.MipLevels = 1;
	descDepth.ArraySize = 1;
	descDepth.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	descDepth.SampleDesc.Count = 1;
	descDepth.SampleDesc.Quality = 0;
	descDepth.Usage = D3D11_USAGE_DEFAULT;
	descDepth.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	descDepth.CPUAccessFlags = 0;
	descDepth.MiscFlags = 0;
	hr = g_pd3dDevice->CreateTexture2D(&descDepth, NULL, &g_pDepthStencilBuffer);

	D3D11_DEPTH_STENCIL_VIEW_DESC descDSView;
	descDSView.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	descDSView.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	descDSView.Texture2D.MipSlice = 0;

	hr = g_pd3dDevice->CreateDepthStencilView(g_pDepthStencilBuffer, &descDSView, &g_pDepthStencilView);

	hr = g_pd3dDevice->CreateRenderTargetView( pBackBuffer, NULL, &g_pRenderTargetView );
	pBackBuffer->Release();
	if( FAILED( hr ) )
		return hr;

	g_pImmediateContext->OMSetRenderTargets( 1, &g_pRenderTargetView, g_pDepthStencilView );

	// Setup the viewport
	D3D11_VIEWPORT vp;
	vp.Width = (FLOAT)width;
	vp.Height = (FLOAT)height;
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;
	vp.TopLeftX = 0;
	vp.TopLeftY = 0;
	g_pImmediateContext->RSSetViewports( 1, &vp );

	RenderManager.Initialise(g_pd3dDevice, g_pSwapChain);

	return S_OK;
}

//--------------------------------------------------------------------------------------
// Render the frame
//--------------------------------------------------------------------------------------
void Render()
{
	// Just clear the backbuffer
	float ClearColor[4] = { 0.0f, 0.125f, 0.3f, 1.0f }; //red,green,blue,alpha

	g_pImmediateContext->ClearRenderTargetView( g_pRenderTargetView, ClearColor );
	g_pSwapChain->Present( 0, 0 );
}

//--------------------------------------------------------------------------------------
// Clean up the objects we've created
//--------------------------------------------------------------------------------------
void CleanupDevice()
{
	extern void Windows64_RestoreGamma();
	Windows64_RestoreGamma();

	if( g_pImmediateContext ) g_pImmediateContext->ClearState();

	if( g_pRenderTargetView ) g_pRenderTargetView->Release();
	if( g_pSwapChain ) g_pSwapChain->Release();
	if( g_pImmediateContext ) g_pImmediateContext->Release();
	if( g_pd3dDevice ) g_pd3dDevice->Release();
}



int APIENTRY _tWinMain(_In_ HINSTANCE hInstance,
					   _In_opt_ HINSTANCE hPrevInstance,
					   _In_ LPTSTR    lpCmdLine,
					   _In_ int       nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

	SetUnhandledExceptionFilter(StandaloneUnhandledExceptionFilter);
	StandaloneLog("==== Standalone startup begin ====");

	ConfigureStandaloneWorkingDirectory();
	wchar_t cwd[MAX_PATH];
	if (GetCurrentDirectoryW(MAX_PATH, cwd))
	{
		char cwdA[MAX_PATH * 2];
		ZeroMemory(cwdA, sizeof(cwdA));
		wcstombs(cwdA, cwd, sizeof(cwdA) - 1);
		StandaloneLog("Current directory: %s", cwdA);
	}

	Win64LaunchConfig launchConfig;
	launchConfig.dedicated = false;
	launchConfig.flatWorld = true;
	launchConfig.disableSaving = true;
	launchConfig.maxPlayers = MINECRAFT_NET_MAX_PLAYERS;
	launchConfig.port = WIN64_NET_DEFAULT_PORT;
	launchConfig.worldName = "LCE Dedicated Server";
	launchConfig.bindAddress = "0.0.0.0";
	launchConfig.serverName = "LCE Dedicated";
	launchConfig.whitelistEnabled = false;

	LoadDedicatedServerProperties(launchConfig);

	if(lpCmdLine)
	{
		if(lpCmdLine[0] == '1')
		{
			g_iScreenWidth = 1280;
			g_iScreenHeight = 720;
		}
		else if(lpCmdLine[0] == '2')
		{
			g_iScreenWidth = 640;
			g_iScreenHeight = 480;
		}
		else if(lpCmdLine[0] == '3')
		{
			// Vita
			g_iScreenWidth = 720;
			g_iScreenHeight = 408;

			// Vita native
			//g_iScreenWidth = 960;
			//g_iScreenHeight = 544;
		}
	}

	vector<string> cmdTokens = TokenizeCommandLine(lpCmdLine);

	launchConfig.dedicated = HasCommandToken(cmdTokens, "-dedicated");
	g_Win64DedicatedServerMode = launchConfig.dedicated;
	if (HasCommandToken(cmdTokens, "-normal"))
	{
		launchConfig.flatWorld = false;
	}
	if (HasCommandToken(cmdTokens, "-flat"))
	{
		launchConfig.flatWorld = true;
	}
	if (HasCommandToken(cmdTokens, "-save"))
	{
		launchConfig.disableSaving = false;
	}
	if (HasCommandToken(cmdTokens, "-nosave"))
	{
		launchConfig.disableSaving = true;
	}

	string value;
	if (GetCommandValue(cmdTokens, "-name", value) && !value.empty())
	{
		strncpy_s(g_Win64Username, 17, value.c_str(), _TRUNCATE);
	}
	if (GetCommandValue(cmdTokens, "-servername", value) && !value.empty())
	{
		launchConfig.serverName = value;
	}
	if (GetCommandValue(cmdTokens, "-world", value) && !value.empty())
	{
		launchConfig.worldName = value;
	}
	if (GetCommandValue(cmdTokens, "-port", value) && !value.empty())
	{
		int port = atoi(value.c_str());
		if (port > 0 && port <= 65535)
		{
			launchConfig.port = port;
		}
	}
	if (GetCommandValue(cmdTokens, "-bind", value) && !value.empty())
	{
		launchConfig.bindAddress = value;
	}
	if (GetCommandValue(cmdTokens, "-maxplayers", value) && !value.empty())
	{
		int maxPlayers = atoi(value.c_str());
		if (maxPlayers >= 1 && maxPlayers <= MINECRAFT_NET_MAX_PLAYERS)
		{
			launchConfig.maxPlayers = (unsigned char)maxPlayers;
		}
	}
	if (HasCommandToken(cmdTokens, "-whitelist"))
	{
		launchConfig.whitelistEnabled = true;
	}

	strncpy_s(g_Win64MultiplayerIP, sizeof(g_Win64MultiplayerIP), launchConfig.bindAddress.c_str(), _TRUNCATE);
	g_Win64MultiplayerPort = launchConfig.port;
	g_Win64MultiplayerMaxPlayers = (int)launchConfig.maxPlayers;

	if (launchConfig.dedicated)
	{
		g_dedicatedGuiEnabled = true;
		g_dedicatedPort = launchConfig.port;
		g_dedicatedMaxPlayers = (unsigned int)launchConfig.maxPlayers;
		g_dedicatedWorldName = convStringToWstring(launchConfig.worldName);
		g_dedicatedBindAddress = convStringToWstring(g_Win64MultiplayerIP);
	}

	StandaloneLog("Launch config: dedicated=%d, bind=%s, port=%d, maxPlayers=%u, flat=%d, disableSaving=%d, world=\"%s\"",
		launchConfig.dedicated ? 1 : 0,
		g_Win64MultiplayerIP,
		launchConfig.port,
		(unsigned int)launchConfig.maxPlayers,
		launchConfig.flatWorld ? 1 : 0,
		launchConfig.disableSaving ? 1 : 0,
		launchConfig.worldName.c_str());

	if (g_Win64Username[0] == 0)
	{
		if (!launchConfig.serverName.empty())
		{
			strncpy_s(g_Win64Username, 17, launchConfig.serverName.c_str(), _TRUNCATE);
		}
		else
		{
		DWORD sz = 17;
		if (!GetUserNameA(g_Win64Username, &sz))
			strncpy_s(g_Win64Username, 17, "Player", _TRUNCATE);
		}
		g_Win64Username[16] = 0;
	}

	MultiByteToWideChar(CP_ACP, 0, g_Win64Username, -1, g_Win64UsernameW, 17);

	// Initialize global strings
	MyRegisterClass(hInstance);

	// Perform application initialization:
	StandaloneLog("Calling InitInstance");
	if (!InitInstance (hInstance, nCmdShow))
	{
		StandaloneLog("InitInstance failed");
		return FALSE;
	}
	StandaloneLog("InitInstance succeeded");

	hMyInst=hInstance;

	if (launchConfig.dedicated)
	{
		SetWindowPos(g_hWnd, NULL, CW_USEDEFAULT, CW_USEDEFAULT, 900, 620, SWP_NOZORDER | SWP_NOMOVE);
		DedicatedGuiCreateControls(g_hWnd);
		StandaloneLog("Dedicated mode enabled (GUI active)");
	}

	StandaloneLog("Calling InitDevice");
	if( FAILED( InitDevice() ) )
	{
		StandaloneLog("InitDevice failed");
		CleanupDevice();
		return 0;
	}
	StandaloneLog("InitDevice succeeded");

#if 0
	// Main message loop
	MSG msg = {0};
	while( WM_QUIT != msg.message )
	{
		if( PeekMessage( &msg, NULL, 0, 0, PM_REMOVE ) )
		{
			TranslateMessage( &msg );
			DispatchMessage( &msg );
		}
		else
		{
			Render();
		}
	}

	return (int) msg.wParam;
#endif

	static bool bTrialTimerDisplayed=true;

#ifdef MEMORY_TRACKING
	ResetMem();
	MEMORYSTATUS memStat;
	GlobalMemoryStatus(&memStat);
	printf("RESETMEM start: Avail. phys %d\n",memStat.dwAvailPhys/(1024*1024));
#endif

#if 0
	// Initialize D3D
	hr = InitD3D( &pDevice, &d3dpp );
	g_pD3DDevice = pDevice;
	if( FAILED(hr) )
	{
		app.DebugPrintf
			( "Failed initializing D3D.\n" );
		return -1;
	}

	// Initialize the application, assuming sharing of the d3d interface.
	hr = app.InitShared( pDevice, &d3dpp,
		XuiPNGTextureLoader );

	if ( FAILED(hr) )
	{
		app.DebugPrintf
			( "Failed initializing application.\n" );

		return -1;
	}

#endif
	StandaloneLog("Calling app.loadMediaArchive");
	app.loadMediaArchive();
	StandaloneLog("Finished app.loadMediaArchive");

	RenderManager.Initialise(g_pd3dDevice, g_pSwapChain);
	
	StandaloneLog("Calling app.loadStringTable");
	app.loadStringTable();
	StandaloneLog("Finished app.loadStringTable");
	ui.init(g_pd3dDevice,g_pImmediateContext,g_pRenderTargetView,g_pDepthStencilView,g_iScreenWidth,g_iScreenHeight);
	StandaloneLog("Finished ui.init");

	////////////////
	// Initialise //
	////////////////

	// Set the number of possible joypad layouts that the user can switch between, and the number of actions
	InputManager.Initialise(1,3,MINECRAFT_ACTION_MAX, ACTION_MAX_MENU);

	// Set the default joypad action mappings for Minecraft
	DefineActions();
	InputManager.SetJoypadMapVal(0,0);
	InputManager.SetKeyRepeatRate(0.3f,0.2f);

	g_KBMInput.Init();

	// Initialise the profile manager with the game Title ID, Offer ID, a profile version number, and the number of profile values and settings
	ProfileManager.Initialise(TITLEID_MINECRAFT,
		app.m_dwOfferID,
		PROFILE_VERSION_10,
		NUM_PROFILE_VALUES,
		NUM_PROFILE_SETTINGS,
		dwProfileSettingsA,
		app.GAME_DEFINED_PROFILE_DATA_BYTES*XUSER_MAX_COUNT,
		&app.uiGameDefinedDataChangedBitmask
		);
#if 0
	// register the awards
	ProfileManager.RegisterAward(eAward_TakingInventory,	ACHIEVEMENT_01, eAwardType_Achievement);
	ProfileManager.RegisterAward(eAward_GettingWood,		ACHIEVEMENT_02, eAwardType_Achievement);
	ProfileManager.RegisterAward(eAward_Benchmarking,		ACHIEVEMENT_03, eAwardType_Achievement);
	ProfileManager.RegisterAward(eAward_TimeToMine,			ACHIEVEMENT_04, eAwardType_Achievement);
	ProfileManager.RegisterAward(eAward_HotTopic,			ACHIEVEMENT_05, eAwardType_Achievement);
	ProfileManager.RegisterAward(eAward_AquireHardware,		ACHIEVEMENT_06, eAwardType_Achievement);
	ProfileManager.RegisterAward(eAward_TimeToFarm,			ACHIEVEMENT_07, eAwardType_Achievement);
	ProfileManager.RegisterAward(eAward_BakeBread,			ACHIEVEMENT_08, eAwardType_Achievement);
	ProfileManager.RegisterAward(eAward_TheLie,				ACHIEVEMENT_09, eAwardType_Achievement);
	ProfileManager.RegisterAward(eAward_GettingAnUpgrade,	ACHIEVEMENT_10, eAwardType_Achievement);
	ProfileManager.RegisterAward(eAward_DeliciousFish,		ACHIEVEMENT_11, eAwardType_Achievement);
	ProfileManager.RegisterAward(eAward_OnARail,			ACHIEVEMENT_12, eAwardType_Achievement);
	ProfileManager.RegisterAward(eAward_TimeToStrike,		ACHIEVEMENT_13, eAwardType_Achievement);
	ProfileManager.RegisterAward(eAward_MonsterHunter,		ACHIEVEMENT_14, eAwardType_Achievement);
	ProfileManager.RegisterAward(eAward_CowTipper,			ACHIEVEMENT_15, eAwardType_Achievement);
	ProfileManager.RegisterAward(eAward_WhenPigsFly,		ACHIEVEMENT_16, eAwardType_Achievement);
	ProfileManager.RegisterAward(eAward_LeaderOfThePack,	ACHIEVEMENT_17, eAwardType_Achievement);
	ProfileManager.RegisterAward(eAward_MOARTools,			ACHIEVEMENT_18, eAwardType_Achievement);
	ProfileManager.RegisterAward(eAward_DispenseWithThis,	ACHIEVEMENT_19, eAwardType_Achievement);
	ProfileManager.RegisterAward(eAward_InToTheNether,		ACHIEVEMENT_20, eAwardType_Achievement);

	ProfileManager.RegisterAward(eAward_mine100Blocks,		GAMER_PICTURE_GAMERPIC1,			eAwardType_GamerPic,false,app.GetStringTable(),IDS_AWARD_TITLE,IDS_AWARD_GAMERPIC1,IDS_CONFIRM_OK);
	ProfileManager.RegisterAward(eAward_kill10Creepers,		GAMER_PICTURE_GAMERPIC2,			eAwardType_GamerPic,false,app.GetStringTable(),IDS_AWARD_TITLE,IDS_AWARD_GAMERPIC2,IDS_CONFIRM_OK);

	ProfileManager.RegisterAward(eAward_eatPorkChop,		AVATARASSETAWARD_PORKCHOP_TSHIRT,	eAwardType_AvatarItem,false,app.GetStringTable(),IDS_AWARD_TITLE,IDS_AWARD_AVATAR1,IDS_CONFIRM_OK);
	ProfileManager.RegisterAward(eAward_play100Days,		AVATARASSETAWARD_WATCH,				eAwardType_AvatarItem,false,app.GetStringTable(),IDS_AWARD_TITLE,IDS_AWARD_AVATAR2,IDS_CONFIRM_OK);
	ProfileManager.RegisterAward(eAward_arrowKillCreeper,	AVATARASSETAWARD_CAP,				eAwardType_AvatarItem,false,app.GetStringTable(),IDS_AWARD_TITLE,IDS_AWARD_AVATAR3,IDS_CONFIRM_OK);

	ProfileManager.RegisterAward(eAward_socialPost,			0,									eAwardType_Theme,false,app.GetStringTable(),IDS_AWARD_TITLE,IDS_AWARD_THEME,IDS_CONFIRM_OK,THEME_NAME,THEME_FILESIZE);

	//Rich Presence init - number of presences, number of contexts
	ProfileManager.RichPresenceInit(4,1);
	ProfileManager.RegisterRichPresenceContext(CONTEXT_GAME_STATE);

	// initialise the storage manager with a default save display name, a Minimum save size, and a callback for displaying the saving message
	StorageManager.Init(app.GetString(IDS_DEFAULT_SAVENAME),"savegame.dat",FIFTY_ONE_MB,&CConsoleMinecraftApp::DisplaySavingMessage,(LPVOID)&app);
	// Set up the global title storage path
	StorageManager.StoreTMSPathName();

	// set a function to be called when there's a sign in change, so we can exit a level if the primary player signs out
	ProfileManager.SetSignInChangeCallback(&CConsoleMinecraftApp::SignInChangeCallback,(LPVOID)&app);

	// set a function to be called when the ethernet is disconnected, so we can back out if required
	ProfileManager.SetNotificationsCallback(&CConsoleMinecraftApp::NotificationsCallback,(LPVOID)&app);

#endif

	// Ensure the GameHDD save directory exists at runtime (the 4J_Storage lib expects it)
	{
		wchar_t exePath[MAX_PATH];
		if (GetModuleFileNameW(NULL, exePath, MAX_PATH))
		{
			wstring exeDir(exePath);
			size_t lastSlash = exeDir.find_last_of(L"\\/");
			if (lastSlash != wstring::npos)
				exeDir = exeDir.substr(0, lastSlash);
			wstring gameHDDPath = exeDir + L"\\Windows64\\GameHDD";
			CreateDirectoryW((exeDir + L"\\Windows64").c_str(), NULL);
			CreateDirectoryW(gameHDDPath.c_str(), NULL);
		}
	}

	// Set a callback for the default player options to be set - when there is no profile data for the player
	ProfileManager.SetDefaultOptionsCallback(&CConsoleMinecraftApp::DefaultOptionsCallback,(LPVOID)&app);
#if 0
	// Set a callback to deal with old profile versions needing updated to new versions
	ProfileManager.SetOldProfileVersionCallback(&CConsoleMinecraftApp::OldProfileVersionCallback,(LPVOID)&app);

	// Set a callback for when there is a read error on profile data
	ProfileManager.SetProfileReadErrorCallback(&CConsoleMinecraftApp::ProfileReadErrorCallback,(LPVOID)&app);

#endif
	// QNet needs to be setup after profile manager, as we do not want its Notify listener to handle
	// XN_SYS_SIGNINCHANGED notifications. This does mean that we need to have a callback in the
	// ProfileManager for XN_LIVE_INVITE_ACCEPTED for QNet.
	g_NetworkManager.Initialise();
	g_networkManagerReady = true;

	for (int i = 0; i < MINECRAFT_NET_MAX_PLAYERS; i++)
	{
		IQNet::m_player[i].m_smallId = (BYTE)i;
		IQNet::m_player[i].m_isRemote = false;
		IQNet::m_player[i].m_isHostPlayer = (i == 0);
		swprintf_s(IQNet::m_player[i].m_gamertag, 32, L"Player%d", i);
	}
	extern wchar_t g_Win64UsernameW[17];
	wcscpy_s(IQNet::m_player[0].m_gamertag, 32, g_Win64UsernameW);

	WinsockNetLayer::Initialize();



	// 4J-PB moved further down
	//app.InitGameSettings();

	// debug switch to trial version
	ProfileManager.SetDebugFullOverride(true);

#if 0
	//ProfileManager.AddDLC(2);
	StorageManager.SetDLCPackageRoot("DLCDrive");
	StorageManager.RegisterMarketplaceCountsCallback(&CConsoleMinecraftApp::MarketplaceCountsCallback,(LPVOID)&app);
	// Kinect !

	if(XNuiGetHardwareStatus()!=0)
	{
		// If the Kinect Sensor is not physically connected, this function returns 0.
		NuiInitialize(NUI_INITIALIZE_FLAG_USES_HIGH_QUALITY_COLOR | NUI_INITIALIZE_FLAG_USES_DEPTH |
			NUI_INITIALIZE_FLAG_EXTRAPOLATE_FLOOR_PLANE | NUI_INITIALIZE_FLAG_USES_FITNESS | NUI_INITIALIZE_FLAG_NUI_GUIDE_DISABLED | NUI_INITIALIZE_FLAG_SUPPRESS_AUTOMATIC_UI,NUI_INITIALIZE_DEFAULT_HARDWARE_THREAD );
	}

	// Sentient !
	hr = TelemetryManager->Init();

#endif
	// Initialise TLS for tesselator, for this main thread
	Tesselator::CreateNewThreadStorage(1024*1024);
	// Initialise TLS for AABB and Vec3 pools, for this main thread
	AABB::CreateNewThreadStorage();
	Vec3::CreateNewThreadStorage();
	IntCache::CreateNewThreadStorage();
	Compression::CreateNewThreadStorage();
	OldChunkStorage::CreateNewThreadStorage();
	Level::enableLightingCache();
	Tile::CreateNewThreadStorage();

	Minecraft::main();
	Minecraft *pMinecraft=Minecraft::GetInstance();

	app.InitGameSettings();

#if 0
	//bool bDisplayPauseMenu=false;

	// set the default gamma level
	float fVal=50.0f*327.68f;
	RenderManager.UpdateGamma((unsigned short)fVal);

	// load any skins
	//app.AddSkinsToMemoryTextureFiles();

	// set the achievement text for a trial achievement, now we have the string table loaded
	ProfileManager.SetTrialTextStringTable(app.GetStringTable(),IDS_CONFIRM_OK, IDS_CONFIRM_CANCEL);
	ProfileManager.SetTrialAwardText(eAwardType_Achievement,IDS_UNLOCK_TITLE,IDS_UNLOCK_ACHIEVEMENT_TEXT);
	ProfileManager.SetTrialAwardText(eAwardType_GamerPic,IDS_UNLOCK_TITLE,IDS_UNLOCK_GAMERPIC_TEXT);
	ProfileManager.SetTrialAwardText(eAwardType_AvatarItem,IDS_UNLOCK_TITLE,IDS_UNLOCK_AVATAR_TEXT);
	ProfileManager.SetTrialAwardText(eAwardType_Theme,IDS_UNLOCK_TITLE,IDS_UNLOCK_THEME_TEXT);
	ProfileManager.SetUpsellCallback(&app.UpsellReturnedCallback,&app);

	// Set up a debug character press sequence
#ifndef _FINAL_BUILD
	app.SetDebugSequence("LRLRYYY");
#endif

	// Initialise the social networking manager.
	CSocialManager::Instance()->Initialise();

	// Update the base scene quick selects now that the minecraft class exists
	//CXuiSceneBase::UpdateScreenSettings(0);
#endif
	app.InitialiseTips();
#if 0

	DWORD initData=0;

#ifndef _FINAL_BUILD
#ifndef _DEBUG
#pragma message(__LOC__"Need to define the _FINAL_BUILD before submission")
#endif
#endif

	// Set the default sound levels
	pMinecraft->options->set(Options::Option::MUSIC,1.0f);
	pMinecraft->options->set(Options::Option::SOUND,1.0f);

	app.NavigateToScene(XUSER_INDEX_ANY,eUIScene_Intro,&initData);
#endif

	// Set the default sound levels
	pMinecraft->options->set(Options::Option::MUSIC,1.0f);
	pMinecraft->options->set(Options::Option::SOUND,1.0f);

	if (launchConfig.dedicated)
	{
		pMinecraft->noRender = true;
		pMinecraft->options->set(Options::Option::MUSIC, 0.0f);
		pMinecraft->options->set(Options::Option::SOUND, 0.0f);

		if (!StartDedicatedServer(launchConfig))
		{
			StandaloneLog("Dedicated startup failed");
			g_NetworkManager.Terminate();
			g_networkManagerReady = false;
			CleanupDevice();
			return -1;
		}
	}

	//app.TemporaryCreateGameStart();

	//Sleep(10000);
#if 0
	// Intro loop ?
	while(app.IntroRunning())
	{
		ProfileManager.Tick();
		// Tick XUI
		app.RunFrame();

		// 4J : WESTY : Added to ensure we always have clear background for intro.
		RenderManager.SetClearColour(D3DCOLOR_RGBA(0,0,0,255));
		RenderManager.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		// Render XUI
		hr = app.Render();

		// Present the frame.
		RenderManager.Present();

		// Update XUI Timers
		hr = XuiTimersRun();
	}
#endif
	MSG msg = {0};
	while( WM_QUIT != msg.message )
	{
		g_KBMInput.Tick();

		while( PeekMessage( &msg, NULL, 0, 0, PM_REMOVE ) )
		{
			TranslateMessage( &msg );
			DispatchMessage( &msg );
			if (msg.message == WM_QUIT) break;
		}
		if (msg.message == WM_QUIT) break;

		if (launchConfig.dedicated)
		{
			app.UpdateTime();
			StorageManager.Tick();
			g_NetworkManager.DoWork();
			if (app.GetGameStarted())
			{
				pMinecraft->tickAllConnections();
			}
			app.HandleXuiActions();
			Sleep(10);
			continue;
		}

		RenderManager.StartFrame();
#if 0
		if(pMinecraft->soundEngine->isStreamingWavebankReady() &&
			!pMinecraft->soundEngine->isPlayingStreamingGameMusic() &&
			!pMinecraft->soundEngine->isPlayingStreamingCDMusic() )
		{
			// play some music in the menus
			pMinecraft->soundEngine->playStreaming(L"", 0, 0, 0, 0, 0, false);
		}
#endif

		// 		static bool bPlay=false;
		// 		if(bPlay)
		// 		{
		// 			bPlay=false;
		// 			app.audio.PlaySound();
		// 		}

		app.UpdateTime();
		PIXBeginNamedEvent(0,"Input manager tick");
		InputManager.Tick();

		// detect input mode
		if (InputManager.IsPadConnected(0))
		{
			bool controllerUsed = InputManager.ButtonPressed(0) ||
				InputManager.GetJoypadStick_LX(0, false) != 0.0f ||
				InputManager.GetJoypadStick_LY(0, false) != 0.0f ||
				InputManager.GetJoypadStick_RX(0, false) != 0.0f ||
				InputManager.GetJoypadStick_RY(0, false) != 0.0f;

			if (controllerUsed)
				g_KBMInput.SetKBMActive(false);
			else if (g_KBMInput.HasAnyInput())
				g_KBMInput.SetKBMActive(true);
		}
		else
		{
			g_KBMInput.SetKBMActive(true);
		}


		PIXEndNamedEvent();
		PIXBeginNamedEvent(0,"Profile manager tick");
		//		ProfileManager.Tick();
		PIXEndNamedEvent();
		PIXBeginNamedEvent(0,"Storage manager tick");
		StorageManager.Tick();
		PIXEndNamedEvent();
		PIXBeginNamedEvent(0,"Render manager tick");
		RenderManager.Tick();
		PIXEndNamedEvent();

		// Tick the social networking manager.
		PIXBeginNamedEvent(0,"Social network manager tick");
		//		CSocialManager::Instance()->Tick();
		PIXEndNamedEvent();

		// Tick sentient.
		PIXBeginNamedEvent(0,"Sentient tick");
		MemSect(37);
		//		SentientManager.Tick();
		MemSect(0);
		PIXEndNamedEvent();

		PIXBeginNamedEvent(0,"Network manager do work #1");
		g_NetworkManager.DoWork();
		PIXEndNamedEvent();

		//		LeaderboardManager::Instance()->Tick();
		// Render game graphics.
		if(app.GetGameStarted())
		{
			pMinecraft->run_middle();
			app.SetAppPaused( g_NetworkManager.IsLocalGame() && g_NetworkManager.GetPlayerCount() == 1 && ui.IsPauseMenuDisplayed(ProfileManager.GetPrimaryPad()) );
		}
		else
		{
			MemSect(28);
			pMinecraft->soundEngine->tick(NULL, 0.0f);
			MemSect(0);
			pMinecraft->textures->tick(true,false);
			IntCache::Reset();
			if( app.GetReallyChangingSessionType() )
			{
				pMinecraft->tickAllConnections();		// Added to stop timing out when we are waiting after converting to an offline game
			}
		}

		pMinecraft->soundEngine->playMusicTick();

#ifdef MEMORY_TRACKING
		static bool bResetMemTrack = false;
		static bool bDumpMemTrack = false;

		MemPixStuff();

		if( bResetMemTrack )
		{
			ResetMem();
			MEMORYSTATUS memStat;
			GlobalMemoryStatus(&memStat);
			printf("RESETMEM: Avail. phys %d\n",memStat.dwAvailPhys/(1024*1024));
			bResetMemTrack = false;
		}

		if( bDumpMemTrack )
		{
			DumpMem();
			bDumpMemTrack = false;
			MEMORYSTATUS memStat;
			GlobalMemoryStatus(&memStat);
			printf("DUMPMEM: Avail. phys %d\n",memStat.dwAvailPhys/(1024*1024));
			printf("Renderer used: %d\n",RenderManager.CBuffSize(-1));
		}
#endif
#if 0
		static bool bDumpTextureUsage = false;
		if( bDumpTextureUsage )
		{
			RenderManager.TextureGetStats();
			bDumpTextureUsage = false;
		}
#endif
		ui.tick();
		ui.render();
#if 0
		app.HandleButtonPresses();

		// store the minecraft renderstates, and re-set them after the xui render
		GetRenderAndSamplerStates(pDevice,RenderStateA,SamplerStateA);

		// Tick XUI
		PIXBeginNamedEvent(0,"Xui running");
		app.RunFrame();
		PIXEndNamedEvent();

		// Render XUI

		PIXBeginNamedEvent(0,"XUI render");
		MemSect(7);
		hr = app.Render();
		MemSect(0);
		GetRenderAndSamplerStates(pDevice,RenderStateA2,SamplerStateA2);
		PIXEndNamedEvent();

		for(int i=0;i<8;i++)
		{
			if(RenderStateA2[i]!=RenderStateA[i])
			{
				//printf("Reseting RenderStateA[%d] after a XUI render\n",i);
				pDevice->SetRenderState(RenderStateModes[i],RenderStateA[i]);
			}
		}
		for(int i=0;i<5;i++)
		{
			if(SamplerStateA2[i]!=SamplerStateA[i])
			{
				//printf("Reseting SamplerStateA[%d] after a XUI render\n",i);
				pDevice->SetSamplerState(0,SamplerStateModes[i],SamplerStateA[i]);
			}
		}

		RenderManager.Set_matrixDirty();
#endif
		// Present the frame.
		RenderManager.Present();

		ui.CheckMenuDisplayed();
#if 0
		PIXBeginNamedEvent(0,"Profile load check");
		// has the game defined profile data been changed (by a profile load)
		if(app.uiGameDefinedDataChangedBitmask!=0)
		{
			void *pData;
			for(int i=0;i<XUSER_MAX_COUNT;i++)
			{
				if(app.uiGameDefinedDataChangedBitmask&(1<<i))
				{\
				// It has - game needs to update its values with the data from the profile
				pData=ProfileManager.GetGameDefinedProfileData(i);
				// reset the changed flag
				app.ClearGameSettingsChangedFlag(i);
				app.DebugPrintf("***  - APPLYING GAME SETTINGS CHANGE for pad %d\n",i);
				app.ApplyGameSettingsChanged(i);

#ifdef _DEBUG_MENUS_ENABLED
				if(app.DebugSettingsOn())
				{
					app.ActionDebugMask(i);
				}
				else
				{
					// force debug mask off
					app.ActionDebugMask(i,true);
				}
#endif
				// clear the stats first - there could have beena signout and sign back in in the menus
				// need to clear the player stats - can't assume it'll be done in setlevel - we may not be in the game
				pMinecraft->stats[ i ]->clear();
				pMinecraft->stats[i]->parse(pData);
				}
			}

			// Check to see if we can post to social networks.
			CSocialManager::Instance()->RefreshPostingCapability();

			// clear the flag
			app.uiGameDefinedDataChangedBitmask=0;

			// Check if any profile write are needed
			app.CheckGameSettingsChanged();
		}
		PIXEndNamedEvent();
		app.TickDLCOffersRetrieved();
		app.TickTMSPPFilesRetrieved();

		PIXBeginNamedEvent(0,"Network manager do work #2");
		g_NetworkManager.DoWork();
		PIXEndNamedEvent();

		PIXBeginNamedEvent(0,"Misc extra xui");
		// Update XUI Timers
		hr = XuiTimersRun();

#endif
		// Any threading type things to deal with from the xui side?
		app.HandleXuiActions();

#if 0
		PIXEndNamedEvent();
#endif

		// 4J-PB - Update the trial timer display if we are in the trial version
		if(!ProfileManager.IsFullVersion())
		{
			// display the trial timer
			if(app.GetGameStarted())
			{
				// 4J-PB - if the game is paused, add the elapsed time to the trial timer count so it doesn't tick down
				if(app.IsAppPaused())
				{
					app.UpdateTrialPausedTimer();
				}
				ui.UpdateTrialTimer(ProfileManager.GetPrimaryPad());
			}
		}
		else
		{
			// need to turn off the trial timer if it was on , and we've unlocked the full version
			if(bTrialTimerDisplayed)
			{
				ui.ShowTrialTimer(false);
				bTrialTimerDisplayed=false;
			}
		}

		// Fix for #7318 - Title crashes after short soak in the leaderboards menu
		// A memory leak was caused because the icon renderer kept creating new Vec3's because the pool wasn't reset
		Vec3::resetPool();
	}

	// Free resources, unregister custom classes, and exit.
	//	app.Uninit();
	if (launchConfig.dedicated && g_NetworkManager.IsInSession())
	{
		g_NetworkManager.LeaveGame(false);
	}
	g_NetworkManager.Terminate();
	g_networkManagerReady = false;

	if (g_pd3dDevice)
	{
		g_pd3dDevice->Release();
	}

	return (int)msg.wParam;
}

#ifdef MEMORY_TRACKING

int totalAllocGen = 0;
unordered_map<int,int> allocCounts;
bool trackEnable = false;
bool trackStarted = false;
volatile size_t sizeCheckMin = 1160;
volatile size_t sizeCheckMax = 1160;
volatile int sectCheck = 48;
CRITICAL_SECTION memCS;
DWORD tlsIdx;

LPVOID XMemAlloc(SIZE_T dwSize, DWORD dwAllocAttributes)
{
	if( !trackStarted )
	{
		void *p = XMemAllocDefault(dwSize,dwAllocAttributes);
		size_t realSize = XMemSizeDefault(p, dwAllocAttributes);
		totalAllocGen += realSize;
		return p;
	}

	EnterCriticalSection(&memCS);

	void *p=XMemAllocDefault(dwSize + 16,dwAllocAttributes);
	size_t realSize = XMemSizeDefault(p,dwAllocAttributes) - 16;

	if( trackEnable )
	{
#if 1
		int sect = ((int) TlsGetValue(tlsIdx)) & 0x3f;
		*(((unsigned char *)p)+realSize) = sect;

		if( ( realSize >= sizeCheckMin ) && ( realSize <= sizeCheckMax ) && ( ( sect == sectCheck ) || ( sectCheck == -1 ) ) )
		{
			app.DebugPrintf("Found one\n");
		}
#endif

		if( p )
		{
			totalAllocGen += realSize;
			trackEnable = false;
			int key = ( sect << 26 ) | realSize;
			int oldCount = allocCounts[key];
			allocCounts[key] = oldCount + 1;

			trackEnable = true;
		}
	}

	LeaveCriticalSection(&memCS);

	return p;
}

void* operator new (size_t size)
{
	return (unsigned char *)XMemAlloc(size,MAKE_XALLOC_ATTRIBUTES(0,FALSE,TRUE,FALSE,0,XALLOC_PHYSICAL_ALIGNMENT_DEFAULT,XALLOC_MEMPROTECT_READWRITE,FALSE,XALLOC_MEMTYPE_HEAP));
}

void operator delete (void *p)
{
	XMemFree(p,MAKE_XALLOC_ATTRIBUTES(0,FALSE,TRUE,FALSE,0,XALLOC_PHYSICAL_ALIGNMENT_DEFAULT,XALLOC_MEMPROTECT_READWRITE,FALSE,XALLOC_MEMTYPE_HEAP));
}

void WINAPI XMemFree(PVOID pAddress, DWORD dwAllocAttributes)
{
	bool special = false;
	if( dwAllocAttributes == 0 )
	{
		dwAllocAttributes = MAKE_XALLOC_ATTRIBUTES(0,FALSE,TRUE,FALSE,0,XALLOC_PHYSICAL_ALIGNMENT_DEFAULT,XALLOC_MEMPROTECT_READWRITE,FALSE,XALLOC_MEMTYPE_HEAP);
		special = true;
	}
	if(!trackStarted )
	{
		size_t realSize = XMemSizeDefault(pAddress, dwAllocAttributes);
		XMemFreeDefault(pAddress, dwAllocAttributes);
		totalAllocGen -= realSize;
		return;
	}
	EnterCriticalSection(&memCS);
	if( pAddress )
	{
		size_t realSize = XMemSizeDefault(pAddress, dwAllocAttributes) - 16;

		if(trackEnable)
		{
			int sect = *(((unsigned char *)pAddress)+realSize);
			totalAllocGen -= realSize;
			trackEnable = false;
			int key = ( sect << 26 ) | realSize;
			int oldCount = allocCounts[key];
			allocCounts[key] = oldCount - 1;
			trackEnable = true;
		}
		XMemFreeDefault(pAddress, dwAllocAttributes);
	}
	LeaveCriticalSection(&memCS);
}

SIZE_T WINAPI XMemSize(
	PVOID pAddress,
	DWORD dwAllocAttributes
	)
{
	if( trackStarted )
	{
		return XMemSizeDefault(pAddress, dwAllocAttributes) - 16;
	}
	else
	{
		return XMemSizeDefault(pAddress, dwAllocAttributes);
	}
}

void DumpMem()
{
	int totalLeak = 0;
	for(AUTO_VAR(it, allocCounts.begin()); it != allocCounts.end(); it++ )
	{
		if(it->second > 0 )
		{
			app.DebugPrintf("%d %d %d %d\n",( it->first >> 26 ) & 0x3f,it->first & 0x03ffffff, it->second, (it->first & 0x03ffffff) * it->second);
			totalLeak += ( it->first & 0x03ffffff ) * it->second;
		}
	}
	app.DebugPrintf("Total %d\n",totalLeak);
}

void ResetMem()
{
	if( !trackStarted )
	{
		trackEnable = true;
		trackStarted = true;
		totalAllocGen = 0;
		InitializeCriticalSection(&memCS);
		tlsIdx = TlsAlloc();
	}
	EnterCriticalSection(&memCS);
	trackEnable = false;
	allocCounts.clear();
	trackEnable = true;
	LeaveCriticalSection(&memCS);
}

void MemSect(int section)
{
	unsigned int value = (unsigned int)TlsGetValue(tlsIdx);
	if( section == 0 ) // pop
	{
		value = (value >> 6) & 0x03ffffff;
	}
	else
	{
		value = (value << 6) | section;
	}
	TlsSetValue(tlsIdx, (LPVOID)value);
}

void MemPixStuff()
{
	const int MAX_SECT = 46;

	int totals[MAX_SECT] = {0};

	for(AUTO_VAR(it, allocCounts.begin()); it != allocCounts.end(); it++ )
	{
		if(it->second > 0 )
		{
			int sect = ( it->first >> 26 ) & 0x3f;
			int bytes = it->first & 0x03ffffff;
			totals[sect] += bytes * it->second;
		}
	}

	unsigned int allSectsTotal = 0;
	for( int i = 0; i < MAX_SECT; i++ )
	{
		allSectsTotal += totals[i];
		PIXAddNamedCounter(((float)totals[i])/1024.0f,"MemSect%d",i);
	}

	PIXAddNamedCounter(((float)allSectsTotal)/(4096.0f),"MemSect total pages");
}

#endif
