#include <iostream>
#include <Windows.h>
#include <fstream>
#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;

int sprintKey = 0;
int forwardKey = 0;
std::atomic<bool> forwardPressed(false);
bool sprintHeld = false;

std::string GetKeyName(int vkCode) {
	if (vkCode == VK_LCONTROL || vkCode == VK_RCONTROL || vkCode == 17) return "Control";
	if (vkCode == VK_LSHIFT || vkCode == VK_RSHIFT || vkCode == 16) return "Shift";
	if (vkCode == VK_LMENU || vkCode == VK_RMENU || vkCode == 18) return "Alt";
	
	char name[64];
	UINT scanCode = MapVirtualKeyA(vkCode, MAPVK_VK_TO_VSC);
	if (GetKeyNameTextA(scanCode << 16, name, sizeof(name))) {
		return std::string(name);
	}
	return "Unknown (" + std::to_string(vkCode) + ")";
}

bool IsMinecraftFocused() {
	HWND hwnd = GetForegroundWindow();
	if (!hwnd) return false;

	char className[256];
	GetClassNameA(hwnd, className, sizeof(className));
	std::string clName = className;

	if (clName != "Bedrock" && clName != "ApplicationFrameWindow" && clName != "Windows.UI.Core.CoreWindow")
		return false;

	char title[256];
	GetWindowTextA(hwnd, title, sizeof(title));
	if (std::string(title) != "Minecraft") return false;

	return true;
}

bool IsCursorHidden() {
	CURSORINFO cursor = { sizeof(cursor) };
	if (GetCursorInfo(&cursor)) {
		return cursor.flags == 0;
	}
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

void SprintLoop() {
	while (true) {
		bool shouldSprint = false;

		if (forwardPressed.load()) {
			if (IsMinecraftFocused() && IsCursorHidden()) {
				shouldSprint = true;
			}
		}

		if (shouldSprint && !sprintHeld) {
			SendKey(sprintKey, true);
			sprintHeld = true;
		}
		else if (!shouldSprint && sprintHeld) {
			SendKey(sprintKey, false);
			sprintHeld = false;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
}

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
	if (nCode >= 0)
	{
		PKBDLLHOOKSTRUCT p = (PKBDLLHOOKSTRUCT)lParam;

		if (p->vkCode == (DWORD)forwardKey)
		{
			if (wParam == WM_KEYDOWN) forwardPressed.store(true);
			else if (wParam == WM_KEYUP) forwardPressed.store(false);
		}
	}
	return CallNextHookEx(NULL, nCode, wParam, lParam);
}

std::string GetEnv(const std::string& var) {
	char* val = std::getenv(var.c_str());
	return val ? std::string(val) : "";
}

std::string FindOptionsPath() {
	std::string localAppData = GetEnv("LOCALAPPDATA");
	if (!localAppData.empty()) {
		fs::path uwpPath = fs::path(localAppData) / "Packages" / "Microsoft.MinecraftUWP_8wekyb3d8bbwe" / "LocalState" / "games" / "com.mojang" / "minecraftpe" / "options.txt";
		if (fs::exists(uwpPath)) return uwpPath.string();
	}

	std::string appData = GetEnv("APPDATA");
	if (!appData.empty()) {
		fs::path bedrockUsersPath = fs::path(appData) / "Minecraft Bedrock" / "Users";
		if (fs::exists(bedrockUsersPath) && fs::is_directory(bedrockUsersPath)) {
			std::error_code ec;
			for (const auto& entry : fs::directory_iterator(bedrockUsersPath, ec)) {
				if (ec) break;
				if (entry.is_directory()) {
					fs::path potentialPath = entry.path() / "games" / "com.mojang" / "minecraftpe" / "options.txt";
					if (fs::exists(potentialPath)) {
						return potentialPath.string();
					}
				}
			}
		}
	}

	return "";
}

int main()
{
	std::string path = FindOptionsPath();
	
	bool foundForward = false;
	bool foundSprint = false;

	if (!path.empty()) {
		std::ifstream options(path);
		std::string line;

		if (options.is_open()) {
			while (getline(options, line))
			{
				if (line.find("keyboard_type_0_key.forward") != std::string::npos) {
					size_t sep = line.find(':');
					if (sep != std::string::npos) {
						std::string val = line.substr(sep + 1);
						if (!val.empty() && std::isdigit(val[0])) {
							forwardKey = std::atoi(val.c_str());
							foundForward = true;
						}
					}
				}
				if (line.find("keyboard_type_0_key.sprint") != std::string::npos) {
					size_t sep = line.find(':');
					if (sep != std::string::npos) {
						std::string val = line.substr(sep + 1);
						if (!val.empty() && std::isdigit(val[0])) {
							sprintKey = std::atoi(val.c_str());
							foundSprint = true;
						}
					}
				}
			}
			options.close();
		}
	}

	if (!foundForward || !foundSprint) {
		std::cout << "Could not auto-detect keys from options.txt.\n";
		std::cout << "Forward Key Code (Default 87 for 'W'): ";
		if (!(std::cin >> forwardKey)) forwardKey = 87;
		std::cout << "Sprint Key Code (Default 17 for 'Ctrl'): ";
		if (!(std::cin >> sprintKey)) sprintKey = 17;
	}

	if (forwardKey == 0) forwardKey = 87;
	if (sprintKey == 0) sprintKey = 17;

	std::cout << R"(
    _    _   _ _____ ___  ____  ____  ____  ___ _   _ _____ 
   / \  | | | |_   _/ _ \/ ___||  _ \|  _ \|_ _| \ | |_   _|
  / _ \ | | | | | || | | \___ \| |_) | |_) || ||  \| | | |  
 / ___ \| |_| | | || |_| |___) |  __/|  _ < | || |\  | | |  
/_/   \_\___/  |_| \___/|____/|_|    |_| \_\___|_| \_| |_|  

      ___  _   _ 
     / _ \| \ | |
    | | | |  \| |
    | |_| | |\  |
     \___/|_| \_|
)" << std::endl;

	std::cout << "---------------------------------------------------" << std::endl;
	std::cout << "[Config] Detected Forward: " << GetKeyName(forwardKey) << std::endl;
	std::cout << "[Config] Detected Sprint:  " << GetKeyName(sprintKey) << std::endl;
	std::cout << "---------------------------------------------------" << std::endl;
	std::cout << "Status: Running..." << std::endl;

	std::thread worker(SprintLoop);
	worker.detach();

	HHOOK hhkLowLevelKybd = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, 0, 0);

	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	UnhookWindowsHookEx(hhkLowLevelKybd);
	return 0;
}
