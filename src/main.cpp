#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <shlobj.h>

int sprintKey = 0;
int forwardKey = 0;
extern "C" volatile bool forwardPressed;
volatile bool forwardPressed = false;
bool sprintHeld = false;


HANDLE hEvent = NULL; 


extern "C" {
    bool asm_load_forward_pressed();
    void asm_store_forward_pressed(bool val);
    bool asm_exchange_forward_pressed(bool val);
    void asm_tight_pause();
}

// i dont know 
__asm__(
    ".section .text\n"
    ".globl asm_load_forward_pressed\n"
    "asm_load_forward_pressed:\n"
    "    movb forwardPressed(%rip), %al\n"
    "    ret\n"
    "\n"
    ".globl asm_store_forward_pressed\n"
    "asm_store_forward_pressed:\n"
    "    movb %cl, forwardPressed(%rip)\n"
    "    ret\n"
    "\n"
    ".globl asm_exchange_forward_pressed\n"
    "asm_exchange_forward_pressed:\n"
    "    movb %cl, %al\n"
    "    xchgb %al, forwardPressed(%rip)\n"
    "    ret\n"
    "\n"
    ".globl asm_tight_pause\n"
    "asm_tight_pause:\n"
    "    mov $100000, %eax\n"
    "1:  pause\n"
    "    dec %eax\n"
    "    jnz 1b\n"
    "    ret\n"
);

void GetKeyNameWin32(int vkCode, char* outName, int outSize) {
	if (vkCode == VK_LCONTROL || vkCode == VK_RCONTROL || vkCode == 17) { strcpy(outName, "Control"); return; }
	if (vkCode == VK_LSHIFT || vkCode == VK_RSHIFT || vkCode == 16) { strcpy(outName, "Shift"); return; }
	if (vkCode == VK_LMENU || vkCode == VK_RMENU || vkCode == 18) { strcpy(outName, "Alt"); return; }
	
	UINT scanCode = MapVirtualKeyA(vkCode, MAPVK_VK_TO_VSC);
	if (!GetKeyNameTextA(scanCode << 16, outName, outSize)) {
		sprintf(outName, "Unknown (%d)", vkCode);
	}
}

bool IsMinecraftFocused() {
	static HWND cachedHwnd = nullptr;
	static bool cachedResult = false;
	const HWND hwnd = GetForegroundWindow();
	if (!hwnd) return false;
	if (hwnd == cachedHwnd) return cachedResult;
	cachedHwnd = hwnd;
	cachedResult = false;
	char className[256];
	if (!GetClassNameA(hwnd, className, sizeof(className))) return false;
	if (strcmp(className, "Bedrock") != 0 && strcmp(className, "ApplicationFrameWindow") != 0 && strcmp(className, "Windows.UI.Core.CoreWindow") != 0) return false; // no better option
	char title[256];
	if (!GetWindowTextA(hwnd, title, sizeof(title))) return false;
	if (strcmp(title, "Minecraft") != 0) return false;
	cachedResult = true;
	return true;
}

bool IsCursorHidden() {
	CURSORINFO cursor = { sizeof(cursor) };
	if (GetCursorInfo(&cursor)) return cursor.flags == 0;
	return false;
}

void SendKey(int key, bool down) {
	INPUT ip;
	ip.type = INPUT_KEYBOARD;
	ip.ki.wVk = (WORD)key;
	ip.ki.wScan = 0;
	ip.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
	ip.ki.time = 0;
	ip.ki.dwExtraInfo = 0;
	SendInput(1, &ip, sizeof(INPUT));
}

DWORD WINAPI SprintLoop(LPVOID lpParam) {
	while (true) {
		WaitForSingleObject(hEvent, INFINITE);
		while (asm_load_forward_pressed()) {
			bool shouldSprint = (IsMinecraftFocused() && IsCursorHidden());
			if (shouldSprint != sprintHeld) {
				SendKey(sprintKey, shouldSprint);
				sprintHeld = shouldSprint;
			}
			asm_tight_pause();
		}
		if (sprintHeld) {
			SendKey(sprintKey, false);
			sprintHeld = false;
		}
		ResetEvent(hEvent);
	}
	return 0;
}

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
	if (nCode >= 0) {
		PKBDLLHOOKSTRUCT p = (PKBDLLHOOKSTRUCT)lParam;
		if (p->vkCode == (DWORD)forwardKey) {
			if (wParam == WM_KEYDOWN) {
				if (!asm_exchange_forward_pressed(true)) SetEvent(hEvent);
			} else if (wParam == WM_KEYUP) {
				asm_store_forward_pressed(false);
			}
		}
	}
	return CallNextHookEx(NULL, nCode, wParam, lParam);
}

bool BuildPath(char* out, int outSize, const char* a, const char* b) {
	int written = snprintf(out, outSize, "%s\\%s", a, b);
	return (written > 0 && written < outSize);
}

bool ContainsCaseInsensitive(const char* text, const char* needle) {
	if (!text || !needle || !needle[0]) return false;
	const int nLen = (int)strlen(needle);
	for (const char* p = text; *p; ++p) {
		int i = 0;
		while (i < nLen && p[i] &&
			tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) {
			++i;
		}
		if (i == nLen) return true;
	}
	return false;
}
// contender ?
void TryOptionsCandidate(
	const char* candidatePath,
	FILETIME* newestWriteTime,
	char* bestPath,
	int bestPathSize,
	bool* foundAny
) {
	WIN32_FILE_ATTRIBUTE_DATA attrs;
	if (!GetFileAttributesExA(candidatePath, GetFileExInfoStandard, &attrs)) return;
	if (attrs.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return;
	if (!*foundAny || CompareFileTime(&attrs.ftLastWriteTime, newestWriteTime) > 0) {
		*newestWriteTime = attrs.ftLastWriteTime;
		snprintf(bestPath, bestPathSize, "%s", candidatePath);
		*foundAny = true;
	}
}

void ScanOptionsRecursive(
	const char* root,
	int depth,
	FILETIME* newestWriteTime,
	char* bestPath,
	int bestPathSize,
	bool* foundAny
) {
	if (!root || !root[0] || depth > 10) return;
	char search[MAX_PATH];
	if (!BuildPath(search, MAX_PATH, root, "*")) return;
	WIN32_FIND_DATAA fd;
	HANDLE hFind = FindFirstFileA(search, &fd);
	if (hFind == INVALID_HANDLE_VALUE) return;

	do {
		if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
		char fullPath[MAX_PATH];
		if (!BuildPath(fullPath, MAX_PATH, root, fd.cFileName)) continue;

		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
			bool minecraftContext = ContainsCaseInsensitive(root, "minecraft")
				|| ContainsCaseInsensitive(root, "mojang")
				|| ContainsCaseInsensitive(fd.cFileName, "minecraft")
				|| ContainsCaseInsensitive(fd.cFileName, "mojang");
			if (depth < 2 || minecraftContext) {
				ScanOptionsRecursive(fullPath, depth + 1, newestWriteTime, bestPath, bestPathSize, foundAny);
			}
			continue;
		}

		if (_stricmp(fd.cFileName, "options.txt") == 0 &&
			ContainsCaseInsensitive(fullPath, "minecraftpe")) {
			TryOptionsCandidate(fullPath, newestWriteTime, bestPath, bestPathSize, foundAny);
		}
	} while (FindNextFileA(hFind, &fd));

	FindClose(hFind);
}

void ScanKnownBedrockUsers(
	const char* roamingPath,
	FILETIME* newestWriteTime,
	char* bestPath,
	int bestPathSize,
	bool* foundAny
) {
	char usersSearch[MAX_PATH];
	if (!BuildPath(usersSearch, MAX_PATH, roamingPath, "Minecraft Bedrock\\Users\\*")) return;
	WIN32_FIND_DATAA fd;
	HANDLE hFind = FindFirstFileA(usersSearch, &fd);
	if (hFind == INVALID_HANDLE_VALUE) return;
	do {
		if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 || fd.cFileName[0] == '.') continue;
		char candidate[MAX_PATH];
		int written = snprintf(
			candidate,
			MAX_PATH,
			"%s\\Minecraft Bedrock\\Users\\%s\\games\\com.mojang\\minecraftpe\\options.txt",
			roamingPath,
			fd.cFileName
		);
		if (written > 0 && written < MAX_PATH) {
			TryOptionsCandidate(candidate, newestWriteTime, bestPath, bestPathSize, foundAny);
		}
	} while (FindNextFileA(hFind, &fd));
	FindClose(hFind);
}
// FIND ME
void FindOptionsPath(char* outPath, int outSize) {
	if (!outPath || outSize <= 0) return;
	outPath[0] = '\0';

	bool foundAny = false;
	FILETIME newestWriteTime = {};
	char localPath[MAX_PATH] = {};
	char roamingPath[MAX_PATH] = {};
	char candidate[MAX_PATH];

	if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localPath))) {
		int written = snprintf(
			candidate,
			MAX_PATH,
			"%s\\Packages\\Microsoft.MinecraftUWP_8wekyb3d8bbwe\\LocalState\\games\\com.mojang\\minecraftpe\\options.txt",
			localPath
		);
		if (written > 0 && written < MAX_PATH) {
			TryOptionsCandidate(candidate, &newestWriteTime, outPath, outSize, &foundAny);
		}

		written = snprintf(
			candidate,
			MAX_PATH,
			"%s\\Packages\\Microsoft.MinecraftWindowsBeta_8wekyb3d8bbwe\\LocalState\\games\\com.mojang\\minecraftpe\\options.txt",
			localPath
		);
		if (written > 0 && written < MAX_PATH) {
			TryOptionsCandidate(candidate, &newestWriteTime, outPath, outSize, &foundAny);
		}
	}

	if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, roamingPath))) {
		ScanKnownBedrockUsers(roamingPath, &newestWriteTime, outPath, outSize, &foundAny);
	}

	// fallback
	if (roamingPath[0]) ScanOptionsRecursive(roamingPath, 0, &newestWriteTime, outPath, outSize, &foundAny);
	if (localPath[0]) ScanOptionsRecursive(localPath, 0, &newestWriteTime, outPath, outSize, &foundAny);

	if (!foundAny) outPath[0] = '\0';
}

int main() {
	SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
	hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	char path[MAX_PATH];
	FindOptionsPath(path, MAX_PATH);
	bool foundForward = false, foundSprint = false;
	if (path[0]) {
		printf("[Config] options.txt: %s\n", path);
		FILE* f = fopen(path, "r");
		if (f) {
			char line[512];
			while (fgets(line, sizeof(line), f)) {
				if (strstr(line, "keyboard_type_0_key.forward")) {
					char* sep = strchr(line, ':');
					if (sep) { forwardKey = atoi(sep + 1); if (forwardKey) foundForward = true; }
				}
				if (strstr(line, "keyboard_type_0_key.sprint")) {
					char* sep = strchr(line, ':');
					if (sep) { sprintKey = atoi(sep + 1); if (sprintKey) foundSprint = true; }
				}
			}
			fclose(f);
		}
	}
	if (!foundForward || !foundSprint) {
		printf("Manual keys: Forward (87=W): "); if (scanf("%d", &forwardKey) != 1) forwardKey = 87;
		printf("Sprint (17=Ctrl): "); if (scanf("%d", &sprintKey) != 1) sprintKey = 17;
	}
	if (!forwardKey) forwardKey = 87; if (!sprintKey) sprintKey = 17;
	
	char fName[64], sName[64];
	GetKeyNameWin32(forwardKey, fName, 64); GetKeyNameWin32(sprintKey, sName, 64);
	printf("---------------------------------------------------\n");
	printf("[Config] Detected Forward: %s\n", fName);
	printf("[Config] Detected Sprint:  %s\n", sName);
	printf("---------------------------------------------------\n");
	printf("Status: Running...\n");

	CreateThread(NULL, 0, SprintLoop, NULL, 0, NULL);
	HHOOK hhk = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, 0, 0);
	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
	UnhookWindowsHookEx(hhk);
	return 0;
}
