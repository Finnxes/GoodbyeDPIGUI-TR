#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <winsvc.h>
#include <tlhelp32.h>
#include "resource.h"

#define BTN_START 1
#define BTN_STOP 2
#define CMB_METHOD 3
#define BTN_INSTALL 4
#define BTN_REMOVE 5
#define IDI_ICON1 101

// Başa eklenecek fonksiyon prototipi
BOOL CheckServiceStatus(void);

// Global değişkenler
HWND hCombo, hStatus, hInstall, hRemove, hStartBtn, hStopBtn;
BOOL isRunning = FALSE;
PROCESS_INFORMATION processInfo;
BOOL isServiceRunning = FALSE;

// Metod komutları - Unicode için L öneki kullanıyoruz
const wchar_t* methods[] = {
    L"Standart Metod (DNS Değişikliği Gerekli)",
    L"Alternatif Metod 1 (DNS Değişikliği Gerekli)",
    L"Alternatif Metod 2 (-5)",
    L"Alternatif Metod 3 (TTL 3 + DNS)",
    L"Alternatif Metod 4 (-5 + DNS)",
    L"Alternatif Metod 5 (-9 + DNS)",
    L"Alternatif Metod 6 (-9)",
    L"Türk Telekom Metodu (-4 + TTL 5)"
};

const char* commands[] = {
    "goodbyedpi.exe -5 --set-ttl 5 --dns-addr 77.88.8.8 --dns-port 1253 --dnsv6-addr 2a02:6b8::feed:0ff --dnsv6-port 1253",
    "goodbyedpi.exe --set-ttl 3",
    "goodbyedpi.exe -5",
    "goodbyedpi.exe --set-ttl 3 --dns-addr 77.88.8.8 --dns-port 1253 --dnsv6-addr 2a02:6b8::feed:0ff --dnsv6-port 1253",
    "goodbyedpi.exe -5 --dns-addr 77.88.8.8 --dns-port 1253 --dnsv6-addr 2a02:6b8::feed:0ff --dnsv6-port 1253",
    "goodbyedpi.exe -9 --dns-addr 77.88.8.8 --dns-port 1253 --dnsv6-addr 2a02:6b8::feed:0ff --dnsv6-port 1253",
    "goodbyedpi.exe -9",
    "goodbyedpi.exe -4 --set-ttl 5"
};

// Servis kurulum komutları
const char* install_commands[] = {
    "sc create \"GoodbyeDPI\" binPath= \"%s\\x86_64\\%s\" start= \"auto\"",
    "sc description \"GoodbyeDPI\" \"Turkiye icin DNS zorlamasini kaldirir.\"",
    "sc start \"GoodbyeDPI\""
};

BOOL IsElevated() {
    BOOL fRet = FALSE;
    HANDLE hToken = NULL;
    if(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        TOKEN_ELEVATION Elevation;
        DWORD cbSize = sizeof(TOKEN_ELEVATION);
        if(GetTokenInformation(hToken, TokenElevation, &Elevation, sizeof(Elevation), &cbSize)) {
            fRet = Elevation.TokenIsElevated;
        }
    }
    if(hToken) {
        CloseHandle(hToken);
    }
    return fRet;
}

void StartGoodbyeDPI(HWND hwnd) {
    int methodIndex = SendMessage(hCombo, CB_GETCURSEL, 0, 0);
    if(methodIndex < 0) methodIndex = 0;

    wchar_t currentDir[MAX_PATH];
    GetCurrentDirectoryW(MAX_PATH, currentDir);

    wchar_t cmdLine[512];
    swprintf(cmdLine, 512, L"%ls\\x86_64\\%hs", currentDir, commands[methodIndex]);

    STARTUPINFOW startupInfo = { sizeof(startupInfo) };
    if(CreateProcessW(NULL, cmdLine, NULL, NULL, FALSE, 
        CREATE_NO_WINDOW, NULL, NULL, &startupInfo, &processInfo)) {
        isRunning = TRUE;
        SetWindowTextW(hStatus, L"Durum: Çalışıyor");
        EnableWindow(hStartBtn, FALSE);
        EnableWindow(hStopBtn, TRUE);
    } else {
        MessageBoxW(hwnd, L"GoodbyeDPI başlatılamadı!", L"Hata", MB_ICONERROR);
    }
}

void StopGoodbyeDPI() {
    // Önce servisi durdur
    SC_HANDLE schSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (schSCManager) {
        SC_HANDLE schService = OpenServiceW(schSCManager, L"GoodbyeDPI", SERVICE_STOP | SERVICE_QUERY_STATUS);
        if (schService) {
            SERVICE_STATUS_PROCESS ssp;
            DWORD dwBytesNeeded;

            if (QueryServiceStatusEx(
                    schService,
                    SC_STATUS_PROCESS_INFO,
                    (LPBYTE)&ssp,
                    sizeof(SERVICE_STATUS_PROCESS),
                    &dwBytesNeeded)) {

                if (ssp.dwCurrentState != SERVICE_STOPPED) {
                    SERVICE_STATUS ss;
                    ControlService(schService, SERVICE_CONTROL_STOP, &ss);
                    Sleep(1000); // Servisin durması için bekle
                }
            }
            CloseServiceHandle(schService);
        }
        CloseServiceHandle(schSCManager);
    }

    // Tüm goodbyedpi.exe process'lerini bul ve sonlandır
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe32;
        pe32.dwSize = sizeof(pe32);

        if (Process32FirstW(snapshot, &pe32)) {
            do {
                if (_wcsicmp(pe32.szExeFile, L"goodbyedpi.exe") == 0) {
                    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pe32.th32ProcessID);
                    if (hProcess != NULL) {
                        TerminateProcess(hProcess, 0);
                        CloseHandle(hProcess);
                    }
                }
            } while (Process32NextW(snapshot, &pe32));
        }
        CloseHandle(snapshot);
    }

    // Ek olarak taskkill komutunu da çalıştır
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    wchar_t cmd[] = L"taskkill /F /IM goodbyedpi.exe";
    
    CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 
        CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    
    WaitForSingleObject(pi.hProcess, 1000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    // Process handle'larını temizle
    if(processInfo.hProcess) {
        CloseHandle(processInfo.hProcess);
        processInfo.hProcess = NULL;
    }
    if(processInfo.hThread) {
        CloseHandle(processInfo.hThread);
        processInfo.hThread = NULL;
    }

    // Durumu güncelle
    isRunning = FALSE;
    isServiceRunning = FALSE;
}

void InstallService(HWND hwnd) {
    int methodIndex = SendMessage(hCombo, CB_GETCURSEL, 0, 0);
    if(methodIndex < 0) methodIndex = 0;

    // Doğrudan Windows API kullanarak servis kur
    SC_HANDLE schSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!schSCManager) {
        MessageBoxW(hwnd, L"Servis yöneticisine erişilemedi!", L"Hata", MB_ICONERROR);
        return;
    }

    // Önce var olan servisi kaldır
    SC_HANDLE schService = OpenServiceW(schSCManager, L"GoodbyeDPI", SERVICE_ALL_ACCESS);
    if (schService) {
        SERVICE_STATUS ssp;
        ControlService(schService, SERVICE_CONTROL_STOP, &ssp);
        DeleteService(schService);
        CloseServiceHandle(schService);
    }

    // Yeni servis yolu oluştur
    wchar_t currentDir[MAX_PATH];
    GetCurrentDirectoryW(MAX_PATH, currentDir);
    wchar_t servicePath[MAX_PATH * 2];
    swprintf(servicePath, MAX_PATH * 2, L"\"%ls\\x86_64\\goodbyedpi.exe\" %hs", 
        currentDir, commands[methodIndex] + strlen("goodbyedpi.exe"));

    // Yeni servisi oluştur
    schService = CreateServiceW(
        schSCManager,
        L"GoodbyeDPI",
        L"GoodbyeDPI",
        SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_AUTO_START,
        SERVICE_ERROR_NORMAL,
        servicePath,
        NULL,
        NULL,
        L"Tcpip\0Dnscache\0Nsi\0",
        NULL,
        NULL
    );

    if (!schService) {
        CloseServiceHandle(schSCManager);
        MessageBoxW(hwnd, L"Servis oluşturulamadı!", L"Hata", MB_ICONERROR);
        return;
    }

    // Servis açıklamasını ayarla
    SERVICE_DESCRIPTIONW sd = {L"Turkiye icin DNS zorlamasini kaldirir."};
    ChangeServiceConfig2W(schService, SERVICE_CONFIG_DESCRIPTION, &sd);

    // Servis kurtarma ayarlarını yap
    SERVICE_FAILURE_ACTIONS sfa = {0};
    SC_ACTION actions[3] = {
        {SC_ACTION_RESTART, 0},
        {SC_ACTION_RESTART, 0},
        {SC_ACTION_RESTART, 0}
    };
    sfa.cActions = 3;
    sfa.lpsaActions = actions;
    ChangeServiceConfig2W(schService, SERVICE_CONFIG_FAILURE_ACTIONS, &sfa);

    // Servisi başlat
    StartServiceW(schService, 0, NULL);

    CloseServiceHandle(schService);
    CloseServiceHandle(schSCManager);

    Sleep(1000);
    isServiceRunning = CheckServiceStatus();
    if (isServiceRunning) {
        SetWindowTextW(hStatus, L"Durum: Servis Çalışıyor");
        isRunning = TRUE;
        EnableWindow(hStartBtn, FALSE);
        EnableWindow(hStopBtn, TRUE);
        MessageBoxW(hwnd, L"Servis başarıyla kuruldu ve başlatıldı.\nBilgisayar yeniden başlatıldığında otomatik çalışacak.", 
            L"Bilgi", MB_ICONINFORMATION);
    } else {
        SetWindowTextW(hStatus, L"Durum: Servis Durduruldu");
        isRunning = FALSE;
        EnableWindow(hStartBtn, TRUE);
        EnableWindow(hStopBtn, FALSE);
        MessageBoxW(hwnd, L"Servis kuruldu fakat başlatılamadı!", L"Uyarı", MB_ICONWARNING);
    }
}

void RemoveService() {
    SC_HANDLE schSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (schSCManager == NULL) {
        MessageBoxW(NULL, L"Servis yöneticisine erişilemedi!", L"Hata", MB_ICONERROR);
        return;
    }

    SC_HANDLE schService = OpenServiceW(schSCManager, L"GoodbyeDPI", SERVICE_ALL_ACCESS);
    if (schService == NULL) {
        MessageBoxW(NULL, L"Servis bulunamadı!", L"Bilgi", MB_ICONINFORMATION);
        CloseServiceHandle(schSCManager);
        return;
    }

    // Servisi durdur
    SERVICE_STATUS_PROCESS ssp;
    ControlService(schService, SERVICE_CONTROL_STOP, (LPSERVICE_STATUS)&ssp);

    // Servisi sil
    if (DeleteService(schService)) {
        MessageBoxW(NULL, L"Servis kaldırıldı.", L"Bilgi", MB_ICONINFORMATION);
    } else {
        MessageBoxW(NULL, L"Servis kaldırılamadı!", L"Hata", MB_ICONERROR);
    }

    CloseServiceHandle(schService);
    CloseServiceHandle(schSCManager);
}

// Servis durumunu kontrol eden fonksiyon
BOOL CheckServiceStatus(void) {
    BOOL running = FALSE;
    SC_HANDLE schSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    
    if (schSCManager) {
        SC_HANDLE schService = OpenServiceW(schSCManager, L"GoodbyeDPI", SERVICE_QUERY_STATUS);
        if (schService) {
            SERVICE_STATUS_PROCESS ssStatus;
            DWORD dwBytesNeeded;
            
            if (QueryServiceStatusEx(
                    schService,
                    SC_STATUS_PROCESS_INFO,
                    (LPBYTE)&ssStatus,
                    sizeof(SERVICE_STATUS_PROCESS),
                    &dwBytesNeeded)) {
                
                running = (ssStatus.dwCurrentState == SERVICE_RUNNING);
            }
            CloseServiceHandle(schService);
        }
        CloseServiceHandle(schSCManager);
    }
    return running;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch(msg) {
        case WM_CREATE: {
            // Başlat butonu
            hStartBtn = CreateWindowW(L"BUTTON", L"Başlat", 
                WS_VISIBLE | WS_CHILD, 10, 10, 140, 30,
                hwnd, (HMENU)BTN_START, NULL, NULL);

            // Durdur butonu - her zaman aktif
            hStopBtn = CreateWindowW(L"BUTTON", L"Durdur", 
                WS_VISIBLE | WS_CHILD, 160, 10, 140, 30,
                hwnd, (HMENU)BTN_STOP, NULL, NULL);

            // Başlangıç durumu
            EnableWindow(hStartBtn, TRUE);
            EnableWindow(hStopBtn, TRUE);  // Her zaman aktif

            // Metod seçimi combo box
            hCombo = CreateWindowW(L"COMBOBOX", L"", 
                WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST, 
                10, 50, 300, 200, hwnd, (HMENU)CMB_METHOD, NULL, NULL);
            
            for(int i = 0; i < sizeof(methods)/sizeof(methods[0]); i++) {
                SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)methods[i]);
            }
            SendMessage(hCombo, CB_SETCURSEL, 0, 0);

            // Durum etiketi
            hStatus = CreateWindowW(L"STATIC", L"Durum: Durduruldu",
                WS_VISIBLE | WS_CHILD, 10, 90, 200, 20,
                hwnd, NULL, NULL, NULL);

            // Servis kurulum butonları
            hInstall = CreateWindowW(L"BUTTON", L"Servis Olarak Kur", 
                WS_VISIBLE | WS_CHILD, 10, 120, 140, 30,
                hwnd, (HMENU)BTN_INSTALL, NULL, NULL);

            hRemove = CreateWindowW(L"BUTTON", L"Servisi Kaldır", 
                WS_VISIBLE | WS_CHILD, 160, 120, 140, 30,
                hwnd, (HMENU)BTN_REMOVE, NULL, NULL);

            // Timer'ı başlat (her 1 saniyede bir kontrol)
            SetTimer(hwnd, 1, 1000, NULL);
            
            break;
        }
        
        case WM_COMMAND: {
            switch(LOWORD(wParam)) {
                case BTN_START:
                    StartGoodbyeDPI(hwnd);
                    isServiceRunning = CheckServiceStatus();
                    if(isServiceRunning) {
                        SetWindowTextW(hStatus, L"Durum: Çalışıyor");
                        isRunning = TRUE;
                        EnableWindow(hStartBtn, FALSE);
                    }
                    break;
                
                case BTN_STOP:
                    StopGoodbyeDPI();
                    Sleep(500); // Servisin durması için kısa bir bekleme
                    isServiceRunning = CheckServiceStatus();
                    if(!isServiceRunning) {
                        SetWindowTextW(hStatus, L"Durum: Durduruldu");
                        isRunning = FALSE;
                        EnableWindow(hStartBtn, TRUE);
                    } else {
                        // Eğer servis hala çalışıyorsa, zorla durdurmayı dene
                        SHELLEXECUTEINFOW sei = {0};
                        sei.cbSize = sizeof(SHELLEXECUTEINFOW);
                        sei.lpVerb = L"runas";
                        sei.lpFile = L"net";
                        sei.lpParameters = L"stop GoodbyeDPI";
                        sei.nShow = SW_HIDE;
                        sei.fMask = SEE_MASK_NOCLOSEPROCESS;
                        
                        if (ShellExecuteExW(&sei)) {
                            WaitForSingleObject(sei.hProcess, INFINITE);
                            CloseHandle(sei.hProcess);
                            
                            isServiceRunning = CheckServiceStatus();
                            if(!isServiceRunning) {
                                SetWindowTextW(hStatus, L"Durum: Durduruldu");
                                isRunning = FALSE;
                                EnableWindow(hStartBtn, TRUE);
                            }
                        }
                    }
                    break;
                
                case BTN_INSTALL:
                    InstallService(hwnd);
                    isServiceRunning = CheckServiceStatus();
                    if(isServiceRunning) {
                        SetWindowTextW(hStatus, L"Durum: Çalışıyor");
                        isRunning = TRUE;
                        EnableWindow(hStartBtn, FALSE);
                    } else {
                        SetWindowTextW(hStatus, L"Durum: Durduruldu");
                        isRunning = FALSE;
                        EnableWindow(hStartBtn, TRUE);
                    }
                    break;
                
                case BTN_REMOVE:
                    RemoveService();
                    isServiceRunning = FALSE;
                    isRunning = FALSE;
                    SetWindowTextW(hStatus, L"Durum: Durduruldu");
                    EnableWindow(hStartBtn, TRUE);
                    break;
            }
            break;
        }
        
        case WM_TIMER: {
            if (wParam == 1) {
                isServiceRunning = CheckServiceStatus();
                if (isServiceRunning) {
                    SetWindowTextW(hStatus, L"Durum: Servis Çalışıyor");
                    isRunning = TRUE;
                    EnableWindow(hStartBtn, FALSE);
                } else {
                    SetWindowTextW(hStatus, L"Durum: Servis Durduruldu");
                    isRunning = FALSE;
                    EnableWindow(hStartBtn, TRUE);
                }
            }
            break;
        }
        
        case WM_DESTROY:
            KillTimer(hwnd, 1);
            PostQuitMessage(0);
            break;
            
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
    LPWSTR lpCmdLine, int nCmdShow) {
    
    if(!IsElevated()) {
        MessageBoxW(NULL, L"Program yönetici olarak çalıştırılmalıdır!", 
            L"Hata", MB_ICONERROR);
        return 1;
    }

    const wchar_t CLASS_NAME[] = L"GoodbyeDPIWindow";
    
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    wc.hIcon = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDI_MYICON));
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    
    if (!RegisterClassW(&wc)) {
        MessageBoxW(NULL, L"Pencere sınıfı kaydedilemedi!", L"Hata", MB_ICONERROR);
        return 1;
    }
    
    HWND hwnd = CreateWindowW(
        CLASS_NAME,
        L"GoodbyeDPI GUI Türkiye",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 330, 200,
        NULL,
        NULL,
        hInstance,
        NULL
    );
    
    if(hwnd == NULL) {
        MessageBoxW(NULL, L"Pencere oluşturulamadı!", L"Hata", MB_ICONERROR);
        return 0;
    }

    // Pencere ikonunu ayarla
    HICON hIcon = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDI_MYICON));
    if(hIcon) {
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
    }
    
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);
    
    MSG msg = {0};
    while(GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    return 0;
}