#include <windows.h>
#include <wchar.h>
#include <array>
#include <string>

#if !defined(DEDICATED)

#define DLL_EXPORT extern "C" __declspec(dllexport)

DLL_EXPORT DWORD NvOptimusEnablement = 1;
DLL_EXPORT int AmdPowerXpressRequestHighPerformance = 1;

DLL_EXPORT bool BSecureAllowed(unsigned char *, int, int)
{
    return true;
}

DLL_EXPORT int CountFilesCompletedTrustCheck()
{
    return 0;
}

DLL_EXPORT int CountFilesNeedTrustCheck()
{
    return 0;
}

DLL_EXPORT int GetTotalFilesLoaded()
{
    return 0;
}

DLL_EXPORT int RuntimeCheck(int, int)
{
    return 0;
}

#endif

#if defined(DEDICATED)
#define LAUNCHER_LIB "dedicated"
#define SYMBOL_NAME "DedicatedMain"
typedef int (*LauncherMain_t)(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd);
#else
#define LAUNCHER_LIB "launcher"
#define SYMBOL_NAME "LauncherMain"
typedef int (*LauncherMain_t)(bool bSecure, HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd);
#endif

typedef void (*InstallGC_t)(bool dedicated);
typedef void (*PreInstallGC_t)(bool dedicated);

// Which GC dll to load. Defaults to "csgo_gc" (CS:GO/CS2, unchanged); the tf
// launcher target overrides this to "tf2_gc" (see launcher/CMakeLists.txt).
#if !defined(GC_MODULE_NAME)
#define GC_MODULE_NAME "csgo_gc"
#endif

#if defined(CS2_LAUNCHER)
// cs2.exe loads tier0.dll first (from game\bin\win64\), then engine2.dll, then calls
// Source2Main(hInstance, hPrevInstance, lpCmdLine, nShowCmd, exeDir, "csgo") in engine2.
// We replicate that flow with csgo_gc injected before Source2Main.
typedef int (*Source2Main_t)(HINSTANCE, HINSTANCE, LPSTR, int, const char *, const char *);
#endif

static void ErrorMessageBox(const wchar_t *format, ...)
{
    va_list ap;
    wchar_t buffer[4096];

    va_start(ap, format);
    _vsnwprintf_s(buffer, std::size(buffer), format, ap);
    va_end(ap);

    MessageBoxW(nullptr, buffer, L"csgo_gc", MB_OK | MB_ICONERROR);
}

static const wchar_t *LastErrorString()
{
    static wchar_t buffer[4096];

    buffer[0] = '\0';

    int error = GetLastError();

    int result = FormatMessageW(
        FORMAT_MESSAGE_FROM_SYSTEM
            | FORMAT_MESSAGE_IGNORE_INSERTS
            | FORMAT_MESSAGE_MAX_WIDTH_MASK,
        nullptr,
        error,
        0,
        buffer,
        std::size(buffer),
        nullptr);

    if (!result)
    {
        _snwprintf_s(buffer, std::size(buffer), L"Unknown error (%d)", error);
    }

    return buffer;
}

static void *LoadModuleAndFindSymbol(const wchar_t *abosoluteModulePath, const char *symbol)
{
    HMODULE module = LoadLibraryExW(abosoluteModulePath, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!module)
    {
        ErrorMessageBox(L"Could not load '%s':\n%s", abosoluteModulePath, LastErrorString());
        return nullptr;
    }

    void *function = GetProcAddress(module, symbol);
    if (!function)
    {
        ErrorMessageBox(L"Could not find '%S' from '%s':\n%s", symbol, abosoluteModulePath, LastErrorString());
        return nullptr;
    }

    return function;
}

#if defined(CS2_LAUNCHER)

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
    wchar_t baseDir[MAX_PATH];
    wchar_t modulePath[MAX_PATH];

    DWORD baseDirLength = GetModuleFileNameW(nullptr, baseDir, std::size(baseDir));
    if (!baseDirLength || baseDirLength == std::size(baseDir))
    {
        ErrorMessageBox(L"GetModuleFileName failed:\n%ls", LastErrorString());
        return 1;
    }

    // strip exe filename; we end up in game\bin\win64 alongside tier0.dll and engine2.dll
    wchar_t *slash = wcsrchr(baseDir, '\\');
    if (!slash)
    {
        slash = baseDir;
    }
    *slash = '\0';

    // exe is in game\bin\win64; set working directory to game root (two levels up)
    // so that csgo_gc's relative paths (csgo_gc/config.txt, csgo/steam.inf) resolve correctly
    {
        wchar_t gameRootRaw[MAX_PATH];
        wchar_t gameRoot[MAX_PATH];
        _snwprintf_s(gameRootRaw, std::size(gameRootRaw), L"%ls\\..\\..", baseDir);
        GetFullPathNameW(gameRootRaw, std::size(gameRoot), gameRoot, nullptr);
        SetCurrentDirectoryW(gameRoot);
    }

    // add our directory to PATH so dependent DLLs (vstdlib, etc.) are found
    {
        std::wstring replacePath;
        replacePath.reserve(2048);
        replacePath.append(baseDir);
        replacePath.append(L"\\;");
        const wchar_t *currentPath = _wgetenv(L"PATH");
        if (currentPath)
        {
            replacePath.append(currentPath);
        }
        _wputenv_s(L"PATH", replacePath.c_str());
    }

    // load tier0.dll and steam_api64.dll by absolute path before loading csgo_gc.dll.
    // tier0 must come first (engine dependency); steam_api64 must be pre-loaded so that
    // when csgo_gc's import table is resolved Wine binds the real Valve DLL rather than
    // its own built-in stub (which has SteamAPI_Init unimplemented).
    static const wchar_t *preloadDlls[] = { L"tier0.dll", L"steam_api64.dll" };
    for (const wchar_t *dll : preloadDlls)
    {
        _snwprintf_s(modulePath, std::size(modulePath), L"%ls\\%ls", baseDir, dll);
        if (!LoadLibraryExW(modulePath, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH))
        {
            ErrorMessageBox(L"Could not load '%ls':\n%ls", modulePath, LastErrorString());
            return 1;
        }
    }

    // csgo_gc.dll lives in game\csgo_gc\x64; go up two levels from game\bin\win64
    wchar_t gcPathRaw[MAX_PATH];
    wchar_t gcPathFull[MAX_PATH];
    _snwprintf_s(gcPathRaw, std::size(gcPathRaw), L"%ls\\..\\..\\csgo_gc\\" GC_LIB_DIR "\\csgo_gc" GC_LIB_EXTENSION, baseDir);
    GetFullPathNameW(gcPathRaw, std::size(gcPathFull), gcPathFull, nullptr);

    PreInstallGC_t PreInstallGC = (PreInstallGC_t)LoadModuleAndFindSymbol(gcPathFull, "PreInstallGC");
    if (!PreInstallGC)
    {
        return 1;
    }
    PreInstallGC(false);

    // engine2.dll lives next to this exe in game\bin\win64
    _snwprintf_s(modulePath, std::size(modulePath), L"%ls\\engine2.dll", baseDir);
    Source2Main_t Source2Main = (Source2Main_t)LoadModuleAndFindSymbol(modulePath, "Source2Main");
    if (!Source2Main)
    {
        return 1;
    }

    char baseDirA[MAX_PATH];
    WideCharToMultiByte(CP_ACP, 0, baseDir, -1, baseDirA, sizeof(baseDirA), nullptr, nullptr);

    return Source2Main(hInstance, hPrevInstance, lpCmdLine, nShowCmd, baseDirA, "csgo");
}

#else // CS:GO / dedicated launcher

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
    wchar_t baseDir[MAX_PATH];
    wchar_t modulePath[MAX_PATH];

    DWORD baseDirLength = GetModuleFileNameW(nullptr, baseDir, std::size(baseDir));
    if (!baseDirLength || baseDirLength == std::size(baseDir))
    {
        ErrorMessageBox(L"GetModuleFileName failed:\n%ls", LastErrorString());
        return 1;
    }

    // rip off exe from the path
    wchar_t *slash = wcsrchr(baseDir, '\\');
    if (!slash)
    {
        slash = baseDir; // what the fuck
    }

    *slash = '\0';

    // add bin dir to PATH
    {
        // allocate this on the heap
        std::wstring replacePath;
        replacePath.reserve(2048);

        replacePath.append(baseDir);
        replacePath.append(L"\\bin\\" GC_GAME_BIN_DIR "\\;");

        const wchar_t *currentPath = _wgetenv(L"PATH");
        if (currentPath)
        {
            replacePath.append(currentPath);
        }

        _wputenv_s(L"PATH", replacePath.c_str());
    }

    _snwprintf_s(modulePath, std::size(modulePath), L"%ls\\bin\\" GC_GAME_BIN_DIR "\\" LAUNCHER_LIB GC_LIB_SUFFIX GC_LIB_EXTENSION, baseDir);
    LauncherMain_t LauncherMain = (LauncherMain_t)LoadModuleAndFindSymbol(modulePath, SYMBOL_NAME);
    if (!LauncherMain)
    {
        // LoadModuleAndFindSymbol told us why
        return 1;
    }

    _snwprintf_s(modulePath, std::size(modulePath), L"%ls\\" GC_MODULE_NAME "\\" GC_LIB_DIR "\\"
                                                    GC_MODULE_NAME GC_LIB_EXTENSION,
        baseDir);
    InstallGC_t InstallGC = (InstallGC_t)LoadModuleAndFindSymbol(modulePath, "InstallGC");
    if (!InstallGC)
    {
        // LoadModuleAndFindSymbol told us why
        return 1;
    }

#if defined(DEDICATED)
    InstallGC(true);
#else
    InstallGC(false);
#endif

#if defined(DEDICATED)
    return LauncherMain(hInstance, hPrevInstance, lpCmdLine, nShowCmd);
#else
    return LauncherMain(true, hInstance, hPrevInstance, lpCmdLine, nShowCmd);
#endif
}

#endif // CS2_LAUNCHER
