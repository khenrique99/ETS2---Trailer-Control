// DLL vazia com o nome da DLL do TruckersMP (core_ets2mp.dll), para o autoteste
// conferir que o plugin desliga o backend de memoria quando ela esta no processo.
#include <windows.h>

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }
