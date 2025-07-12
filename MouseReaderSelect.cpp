#include <windows.h>
#include <stdio.h>
#include <vector>
#include <string>

#pragma comment(lib, "User32.lib")

struct MouseDevice {
    HANDLE handle;
    std::wstring name;
};

static HANDLE g_outputDevice = INVALID_HANDLE_VALUE;

std::vector<MouseDevice> EnumerateMice() {
    UINT deviceCount = 0;
    if (GetRawInputDeviceList(nullptr, &deviceCount, sizeof(RAWINPUTDEVICELIST)) != 0 || deviceCount == 0) {
        return {};
    }

    std::vector<RAWINPUTDEVICELIST> deviceList(deviceCount);
    deviceCount = GetRawInputDeviceList(deviceList.data(), &deviceCount, sizeof(RAWINPUTDEVICELIST));
    if (deviceCount == (UINT)-1) {
        return {};
    }

    std::vector<MouseDevice> result;
    for (UINT i = 0; i < deviceCount; ++i) {
        if (deviceList[i].dwType != RIM_TYPEMOUSE) {
            continue;
        }
        UINT nameSize = 0;
        GetRawInputDeviceInfo(deviceList[i].hDevice, RIDI_DEVICENAME, nullptr, &nameSize);
        std::wstring name;
        if (nameSize > 0) {
            name.resize(nameSize);
            if (GetRawInputDeviceInfo(deviceList[i].hDevice, RIDI_DEVICENAME, &name[0], &nameSize) != (UINT)-1) {
                if (!name.empty() && name.back() == L'\0') name.pop_back();
            } else {
                name.clear();
            }
        }
        if (name.empty()) {
            name = L"(nome sconosciuto)";
        }
        result.push_back({deviceList[i].hDevice, name});
    }
    return result;
}

static HANDLE g_selectedMouse = nullptr;
static LONG g_accumX = 0;
static LONG g_accumY = 0;
static DWORD g_lastPrint = 0;

void SendBytes() {
    if (g_outputDevice != INVALID_HANDLE_VALUE) {
        BYTE data[4] = {1, 2, 3, 4};
        DWORD written = 0;
        if (!WriteFile(g_outputDevice, data, sizeof(data), &written, nullptr)) {
            printf("Errore scrittura dispositivo (codice %lu)\n", GetLastError());
        }
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INPUT: {
        UINT size = 0;
        GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
        if (size > 0) {
            std::vector<BYTE> buffer(size);
            if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, buffer.data(), &size, sizeof(RAWINPUTHEADER)) == size) {
                RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(buffer.data());
                if (raw->header.dwType == RIM_TYPEMOUSE && raw->header.hDevice == g_selectedMouse) {
                    g_accumX += raw->data.mouse.lLastX;
                    g_accumY += raw->data.mouse.lLastY;
                    DWORD now = GetTickCount();
                    if (now - g_lastPrint >= 1000) {
                        printf("Delta X: %ld, Delta Y: %ld\n", g_accumX, g_accumY);
                        g_accumX = g_accumY = 0;
                        g_lastPrint = now;
                    }
                }
            }
        }
        return 0;
    }
    case WM_TIMER:
        SendBytes();
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int main() {
    std::vector<MouseDevice> mice = EnumerateMice();
    if (mice.empty()) {
        printf("Nessun mouse trovato.\n");
        return 0;
    }

    printf("Elenco dispositivi mouse trovati:\n");
    for (size_t i = 0; i < mice.size(); ++i) {
        printf("%zu: %ws\n", i, mice[i].name.c_str());
    }
    printf("Seleziona l'indice del mouse da monitorare: ");
    size_t index = 0;
    if (scanf("%zu", &index) != 1 || index >= mice.size()) {
        printf("Indice non valido.\n");
        return 0;
    }

    g_selectedMouse = mice[index].handle;

    std::wstring openPath = mice[index].name;
    if (openPath.rfind(L"\\??\\", 0) == 0) {
        openPath.replace(0, 4, L"\\\\.\\");
    }
    g_outputDevice = CreateFileW(openPath.c_str(), GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (g_outputDevice == INVALID_HANDLE_VALUE) {
        printf("Impossibile aprire il dispositivo per scrittura (errore %lu).\n",
               GetLastError());
    }

    const wchar_t CLASS_NAME[] = L"MouseReaderWindow";
    WNDCLASS wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = CLASS_NAME;
    RegisterClass(&wc);
    HWND hwnd = CreateWindowEx(0, CLASS_NAME, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);

    RAWINPUTDEVICE rid{};
    rid.usUsagePage = 0x01; // Generic Desktop Controls
    rid.usUsage = 0x02;     // Mouse
    rid.dwFlags = RIDEV_INPUTSINK;
    rid.hwndTarget = hwnd;

    if (!RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
        printf("Registrazione RawInput fallita.\n");
        return 0;
    }

    SetTimer(hwnd, 1, 1000, nullptr);

    g_lastPrint = GetTickCount();

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (g_outputDevice != INVALID_HANDLE_VALUE) {
        CloseHandle(g_outputDevice);
    }

    return 0;
}

