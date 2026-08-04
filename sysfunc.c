#include <windows.h>
#include <stdio.h>

int main() {
    HMODULE hModule = GetModuleHandle("kernel32.dll");
    if (hModule == NULL) {
        printf("knihovna nejde nacist\n");
        return 0;
    }

    FARPROC pWinExec = GetProcAddress(hModule, "WinExec");
    if (pWinExec == NULL) {
        printf("funkce nenalezena\n");
        return 0;
    }

    printf("adresa funkce WinExec: %p\n", pWinExec);
    
    return 0;
}
