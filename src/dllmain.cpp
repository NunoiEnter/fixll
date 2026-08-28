#include <stdio.h>
#include <windows.h>
#include <winver.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <shlwapi.h>
#pragma comment(lib, "Shlwapi.lib")

#include "detours/detours.h"

typedef HMODULE(WINAPI* t_LoadLibraryW)(LPCWSTR);
t_LoadLibraryW o_LoadLibraryW = LoadLibraryW;

HMODULE WINAPI h_LoadLibraryW(LPCWSTR lpLibFileName)
{
	auto pResult = o_LoadLibraryW(lpLibFileName);
	if (pResult != NULL) {

		bool bIsFound = false;

		MODULEINFO mInfo;
		HANDLE hProcess = GetCurrentProcess();
		GetModuleInformation(hProcess, pResult, &mInfo, sizeof(mInfo));

		auto pAddr = (PBYTE)pResult;
		for (size_t i = 0; i < mInfo.SizeOfImage - 20; i++) {
			if (memcmp(pAddr + i, L"bootStrap", 20) == 0) {
				DetourTransactionBegin();
				DetourDetach(&o_LoadLibraryW, h_LoadLibraryW);
				DetourTransactionCommit();
				bIsFound = true;
				break;
			}
		}

		if (bIsFound) {
			for (size_t i = 0; i < mInfo.SizeOfImage - 16; i++) {
				if (memcmp(pAddr + i, "\x74\x04\xB0\x01\x5D\xC3\x32\xC0\x5D\xC3", 10) == 0) {
					WriteProcessMemory(hProcess, pAddr + i, "\x74\x00", 2, NULL);
					continue;
				}
				if (memcmp(pAddr + i, "\x84\xDB\x74\x19\xB8\x17\xFC\xFF\xFF\x8B\x4D\xF4", 12) == 0) {
					WriteProcessMemory(hProcess, pAddr + i, "\xB3\x01", 2, NULL);
					continue;
				}
			}
		}
	}
	return pResult;
}

static void logline(const char* s)
{
	HANDLE h = CreateFileW(L"Z:\\home\\moni\\dllload.log", FILE_APPEND_DATA,
		FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, 0, NULL);
	if (h != INVALID_HANDLE_VALUE) {
		DWORD w; WriteFile(h, s, (DWORD)strlen(s), &w, NULL);
		CloseHandle(h);
	}
}

BOOL APIENTRY DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID lpReserved)
{
	if (dwReason == DLL_PROCESS_ATTACH)
	{
		logline("DllMain ATTACH\n");
		HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
		if (snap != INVALID_HANDLE_VALUE) {
			MODULEENTRY32 me; me.dwSize = sizeof(me);
			if (Module32First(snap, &me)) {
				do {
					char buf[512];
					int len = (int)wcslen(me.szModule);
					int has = 0;
					for (int i = 0; i < len - 6; i++) {
						if ((me.szModule[i]=='v'||me.szModule[i]=='V') &&
							(me.szModule[i+1]=='e'||me.szModule[i+1]=='E') &&
							(me.szModule[i+2]=='r'||me.szModule[i+2]=='R') &&
							(me.szModule[i+3]=='s'||me.szModule[i+3]=='S') &&
							(me.szModule[i+4]=='i'||me.szModule[i+4]=='I') &&
							(me.szModule[i+5]=='o'||me.szModule[i+5]=='O')) { has = 1; break; }
					}
					if (has) {
						snprintf(buf, sizeof(buf), "MOD %p %ls\n", (void*)me.modBaseAddr, me.szModule);
						logline(buf);
					}
				} while (Module32Next(snap, &me));
			}
			CloseHandle(snap);
		}
		DetourTransactionBegin();
		DetourAttach(&o_LoadLibraryW, h_LoadLibraryW);
		DetourTransactionCommit();
	}
	return TRUE;
}

/* ------------------------------------------------------------------ */
/* Real implementation of the version APIs (no forwarding to builtin) */
/* ------------------------------------------------------------------ */

static HRSRC FindVerResource(HMODULE h)
{
	HRSRC hrsrc = FindResourceExW(h, RT_VERSION, MAKEINTRESOURCE(VS_VERSION_INFO),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL));
	if (!hrsrc)
		hrsrc = FindResourceExW(h, RT_VERSION, MAKEINTRESOURCE(VS_VERSION_INFO),
			MAKELANGID(LANG_ENGLISH, SUBLANG_DEFAULT));
	if (!hrsrc)
		hrsrc = FindResourceW(h, RT_VERSION, MAKEINTRESOURCE(VS_VERSION_INFO));
	return hrsrc;
}

extern "C" DWORD WINAPI ExpFunc008(LPCWSTR lpFileName, LPDWORD lpdwHandle)
{
	logline("ExpFunc008 called\n");
	if (!lpFileName) return 0;
	HMODULE h = LoadLibraryExW(lpFileName, NULL, LOAD_LIBRARY_AS_DATAFILE);
	if (!h) return 0;
	HRSRC hrsrc = FindVerResource(h);
	if (!hrsrc) return 0;
	DWORD sz = SizeofResource(h, hrsrc);
	if (lpdwHandle) *lpdwHandle = (DWORD)h; /* keep module alive (leak ok) */
	return sz;
}

extern "C" BOOL WINAPI ExpFunc009(LPCWSTR lpFileName, DWORD dwHandle, DWORD dwLen, LPVOID lpData)
{
	HMODULE h = (HMODULE)dwHandle;
	if (!h) {
		if (!lpFileName) return FALSE;
		h = LoadLibraryExW(lpFileName, NULL, LOAD_LIBRARY_AS_DATAFILE);
	}
	if (!h) return FALSE;
	HRSRC hrsrc = FindVerResource(h);
	if (!hrsrc) return FALSE;
	DWORD sz = SizeofResource(h, hrsrc);
	if (dwLen < sz) return FALSE;
	HGLOBAL hg = LoadResource(h, hrsrc);
	if (!hg) return FALSE;
	void* p = LockResource(hg);
	if (!p) return FALSE;
	memcpy(lpData, p, sz);
	return TRUE;
}

static const void* Align4(const void* p)
{
	uintptr_t v = (uintptr_t)p;
	return (const void*)((v + 3) & ~(uintptr_t)3);
}

extern "C" BOOL WINAPI ExpFunc017(LPVOID pBlock, LPCWSTR lpSubBlock, LPVOID* lplpBuffer, PUINT puLen)
{
	if (!pBlock || !lpSubBlock || !lplpBuffer || !puLen) return FALSE;
	*lplpBuffer = NULL; *puLen = 0;

	const WORD* root = (const WORD*)pBlock;
	WORD wLength = root[0];
	WORD wValueLength = root[1];
	const WCHAR* key = (const WCHAR*)(root + 3);
	if (wcsncmp(key, L"VS_VERSION_INFO", 15) != 0) return FALSE;

	const BYTE* valuePtr = (const BYTE*)Align4(key + wcslen(key) + 1);
	const VS_FIXEDFILEINFO* pffi = wValueLength > 0 ? (const VS_FIXEDFILEINFO*)valuePtr : NULL;

	LPCWSTR path = lpSubBlock;
	while (path[0] == L'\\') path++;
	if (path[0] == L'\0') {
		if (!pffi) return FALSE;
		*lplpBuffer = (LPVOID)pffi; *puLen = wValueLength; return TRUE;
	}

	const BYTE* childrenPtr = (const BYTE*)Align4(valuePtr + wValueLength);

	/* top-level component: StringFileInfo / VarFileInfo */
	WCHAR first[64];
	int i = 0;
	LPCWSTR p = path;
	while (*p && *p != L'\\' && i < 63) first[i++] = *p++;
	first[i] = L'\0';
	LPCWSTR rest = (*p == L'\\') ? p + 1 : p;

	const BYTE* top = NULL;
	const BYTE* c = childrenPtr;
	while (c < (const BYTE*)root + wLength && *(const WORD*)c > 0) {
		const WORD* cw = (const WORD*)c;
		WORD clen = cw[0];
		const WCHAR* ckey = (const WCHAR*)(cw + 3);
		if (_wcsicmp(ckey, first) == 0) { top = c; break; }
		c += clen;
	}
	if (!top) return FALSE;

	const WORD* topw = (const WORD*)top;
	WORD topVLen = topw[1];
	const WCHAR* topKey = (const WCHAR*)(topw + 3);
	const BYTE* topValue = (const BYTE*)Align4(topKey + wcslen(topKey) + 1);
	const BYTE* topChildren = (const BYTE*)Align4(topValue + topVLen);
	WORD topLen = topw[0];

	if (_wcsicmp(topKey, L"VarFileInfo") == 0) {
		const BYTE* vc = topChildren;
		while (vc < top + topLen && *(const WORD*)vc > 0) {
			const WORD* vw = (const WORD*)vc;
			WORD vlen = vw[0];
			const WCHAR* vkey = (const WCHAR*)(vw + 3);
			if (_wcsicmp(vkey, L"Translation") == 0) {
				WORD vvlen = vw[1];
				const BYTE* vvalue = (const BYTE*)Align4(vkey + wcslen(vkey) + 1);
				*lplpBuffer = (LPVOID)vvalue; *puLen = vvlen; return TRUE;
			}
			vc += vlen;
		}
		return FALSE;
	}

	if (_wcsicmp(topKey, L"StringFileInfo") == 0) {
		WCHAR lang[16];
		i = 0;
		const WCHAR* r = rest;
		while (*r && *r != L'\\' && i < 15) lang[i++] = *r++;
		lang[i] = L'\0';
		LPCWSTR valName = (*r == L'\\') ? r + 1 : L"";

		const BYTE* sc = topChildren;
		while (sc < top + topLen && *(const WORD*)sc > 0) {
			const WORD* sw = (const WORD*)sc;
			WORD slen = sw[0];
			const WCHAR* skey = (const WCHAR*)(sw + 3);
			if (_wcsicmp(skey, lang) == 0) {
				WORD svlen = sw[1];
				const BYTE* svalue = (const BYTE*)Align4(skey + wcslen(skey) + 1);
				const BYTE* schildren = (const BYTE*)Align4(svalue + svlen);
				const BYTE* st = schildren;
				while (st < sc + slen && *(const WORD*)st > 0) {
					const WORD* stw = (const WORD*)st;
					WORD stlen = stw[0];
					WORD stvlen = stw[1];
					const WCHAR* stkey = (const WCHAR*)(stw + 3);
					if (_wcsicmp(stkey, valName) == 0) {
						const BYTE* stvalue = (const BYTE*)Align4(stkey + wcslen(stkey) + 1);
						*lplpBuffer = (LPVOID)stvalue;
						*puLen = stvlen;
						return TRUE;
					}
					st += stlen;
				}
				return FALSE;
			}
			sc += slen;
		}
		return FALSE;
	}

	return FALSE;
}

/* ---- A (ANSI) wrappers ---- */

extern "C" DWORD WINAPI ExpFunc005(LPCSTR a, LPDWORD b)
{
	WCHAR w[MAX_PATH];
	if (!a) return 0;
	if (!MultiByteToWideChar(CP_ACP, 0, a, -1, w, MAX_PATH)) return 0;
	return ExpFunc008(w, b);
}

extern "C" BOOL WINAPI ExpFunc001(LPCSTR a, DWORD b, DWORD c, LPVOID d)
{
	WCHAR w[MAX_PATH];
	if (!a) return FALSE;
	if (!MultiByteToWideChar(CP_ACP, 0, a, -1, w, MAX_PATH)) return FALSE;
	return ExpFunc009(w, b, c, d);
}

static char g_qvA[2048];
extern "C" BOOL WINAPI ExpFunc016(LPVOID a, LPCSTR b, LPVOID* c, PUINT d)
{
	WCHAR wsub[512];
	if (b) { if (!MultiByteToWideChar(CP_ACP, 0, b, -1, wsub, 512)) return FALSE; }
	else wsub[0] = L'\0';

	LPVOID wb; UINT wl;
	if (!ExpFunc017(a, wsub, &wb, &wl)) return FALSE;

	if (b && _strnicmp(b, "\\StringFileInfo", 15) == 0) {
		int n = WideCharToMultiByte(CP_ACP, 0, (LPCWSTR)wb, -1, g_qvA, sizeof(g_qvA), NULL, NULL);
		if (!n) return FALSE;
		*c = g_qvA; *d = (UINT)n; return TRUE;
	}
	*c = wb; *d = wl; return TRUE;
}

/* ---- Ex variants (ignore flags) ---- */

extern "C" DWORD WINAPI ExpFunc007(DWORD, LPCWSTR a, LPDWORD b) { return ExpFunc008(a, b); }
extern "C" DWORD WINAPI ExpFunc006(DWORD, LPCSTR a, LPDWORD b) { return ExpFunc005(a, b); }
extern "C" BOOL WINAPI ExpFunc004(DWORD, LPCWSTR a, DWORD b, DWORD c, LPVOID d) { return ExpFunc009(a, b, c, d); }
extern "C" BOOL WINAPI ExpFunc003(DWORD, LPCSTR a, DWORD b, DWORD c, LPVOID d) { return ExpFunc001(a, b, c, d); }

/* ---- remaining: safe defaults ---- */

extern "C" BOOL WINAPI ExpFunc002(HANDLE) { return FALSE; }
// NOTE: forwarders must be extern "C" and the .def must be passed to the
// linker (not via -lversion, which aliases exports to import thunks/stubs).

// ===== English patch (r142645, Grok 4 + AutoWrap V1.20) =====
// Downloaded from https://files.catbox.moe/khms20.7z -> outer 7z ->
// "Latest version/_Grok 4 MTL + AutoWrap for v1_20.7z" (9.1MB).
// Inner contains: patch2.xp3 (215134088 B, English translation) and
// version.dll (94KB, a KirikiriTools XP3 loader, PE32, NO bootStrap
// bypass). Game is on official V1.20 (patch.txt proves it).
// Applied: copied inner patch2.xp3 over game's patch2.xp3 in BOTH
// /home/moni/Downloads/ll/limelight_lj and /home/moni/Documents/VN/limelight_lj.
// Originals backed up as patch2.xp3.bak. Kept our FuckBootStrap version.dll
// (does bootStrap bypass). KiriKiri auto-mounts *.xp3 so translation loads.
// If English does NOT show, also install the patch's version.dll (XP3 loader)
// — but back up our working version.dll first (Version.dll.bak-<ts> exists).
extern "C" DWORD WINAPI ExpFunc010(DWORD, LPCSTR, LPCSTR, LPCSTR, LPSTR, PUINT, LPSTR, PUINT) { return 0; }
extern "C" DWORD WINAPI ExpFunc011(DWORD, LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, PUINT, LPWSTR, PUINT) { return 0; }
extern "C" DWORD WINAPI ExpFunc012(DWORD, LPCSTR, LPCSTR, LPCSTR, LPCSTR, LPSTR, PUINT) { return 0; }
extern "C" DWORD WINAPI ExpFunc013(DWORD, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, PUINT) { return 0; }
extern "C" UINT WINAPI ExpFunc014(UINT, LPSTR, UINT) { return 0; }
extern "C" UINT WINAPI ExpFunc015(UINT, LPWSTR, UINT) { return 0; }
