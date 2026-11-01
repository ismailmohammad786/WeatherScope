#define NOMINMAX
#include <windows.h>
#include <winhttp.h>

#include <filesystem>
#include <string>
#include <vector>

namespace {

std::filesystem::path applicationFolder()
{
    std::vector<wchar_t> path(32768);
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) return {};
    return std::filesystem::path(std::wstring(path.data(), length)).parent_path();
}

std::wstring localAppData()
{
    std::vector<wchar_t> value(32768);
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", value.data(), static_cast<DWORD>(value.size()));
    if (length == 0 || length >= value.size()) return {};
    return std::wstring(value.data(), length);
}

std::filesystem::path findEdge()
{
    const std::wstring x86 = [] {
        std::vector<wchar_t> value(32768);
        const DWORD length = GetEnvironmentVariableW(L"ProgramFiles(x86)", value.data(), static_cast<DWORD>(value.size()));
        return (length > 0 && length < value.size()) ? std::wstring(value.data(), length) : std::wstring{};
    }();
    const std::wstring programFiles = [] {
        std::vector<wchar_t> value(32768);
        const DWORD length = GetEnvironmentVariableW(L"ProgramFiles", value.data(), static_cast<DWORD>(value.size()));
        return (length > 0 && length < value.size()) ? std::wstring(value.data(), length) : std::wstring{};
    }();

    std::vector<std::filesystem::path> candidates;
    if (!x86.empty()) candidates.emplace_back(std::filesystem::path(x86) / L"Microsoft" / L"Edge" / L"Application" / L"msedge.exe");
    if (!programFiles.empty()) candidates.emplace_back(std::filesystem::path(programFiles) / L"Microsoft" / L"Edge" / L"Application" / L"msedge.exe");
    if (!localAppData().empty()) candidates.emplace_back(std::filesystem::path(localAppData()) / L"Microsoft" / L"Edge" / L"Application" / L"msedge.exe");
    for (const auto& candidate : candidates) {
        if (std::filesystem::exists(candidate)) return candidate;
    }
    return {};
}

bool healthEndpointReady()
{
    HINTERNET session = WinHttpOpen(L"WeatherScope Desktop/1.0",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return false;
    WinHttpSetTimeouts(session, 300, 300, 500, 500);

    HINTERNET connection = WinHttpConnect(session, L"127.0.0.1", 8080, 0);
    if (!connection) {
        WinHttpCloseHandle(session);
        return false;
    }

    HINTERNET request = WinHttpOpenRequest(connection, L"GET", L"/health", nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    bool ready = false;
    if (request && WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(request, nullptr)) {
        DWORD status = 0;
        DWORD statusSize = sizeof(status);
        ready = WinHttpQueryHeaders(request,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX) && status == 200;
    }

    if (request) WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return ready;
}

bool waitForServer()
{
    for (int attempt = 0; attempt < 40; ++attempt) {
        if (healthEndpointReady()) return true;
        Sleep(250);
    }
    return false;
}

void showError(const std::wstring& message)
{
    MessageBoxW(nullptr, message.c_str(), L"WeatherScope", MB_OK | MB_ICONERROR);
}

void stopBackend(PROCESS_INFORMATION& process)
{
    if (!process.hProcess) return;
    if (WaitForSingleObject(process.hProcess, 0) == WAIT_TIMEOUT) {
        TerminateProcess(process.hProcess, 0);
        WaitForSingleObject(process.hProcess, 2000);
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    process = {};
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    const auto folder = applicationFolder();
    if (folder.empty()) {
        showError(L"WeatherScope could not locate its application folder.");
        return 1;
    }

    const auto backendPath = folder / L"weather_backend.exe";
    if (!std::filesystem::exists(backendPath)) {
        showError(L"WeatherScope could not find weather_backend.exe next to the app. Rebuild or reinstall WeatherScope.");
        return 1;
    }

    PROCESS_INFORMATION backend{};
    bool startedBackend = false;
    if (!healthEndpointReady()) {
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE;
        std::wstring backendCommand = L"\"" + backendPath.wstring() + L"\"";
        if (!CreateProcessW(backendPath.c_str(), backendCommand.data(), nullptr, nullptr, FALSE,
                CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP, nullptr, folder.c_str(), &startup, &backend)) {
            showError(L"WeatherScope could not start its weather service. Check that port 8080 is free, then try again.");
            return 1;
        }
        startedBackend = true;
        if (!waitForServer()) {
            stopBackend(backend);
            showError(L"WeatherScope's local service did not start. Another app may be using port 8080.");
            return 1;
        }
    }

    const auto edgePath = findEdge();
    if (edgePath.empty()) {
        if (startedBackend) stopBackend(backend);
        showError(L"Microsoft Edge could not be found. Install or repair Microsoft Edge, then try again.");
        return 1;
    }

    std::filesystem::path profilePath;
    const std::wstring appData = localAppData();
    if (!appData.empty()) {
        profilePath = std::filesystem::path(appData) / L"WeatherScope" / L"EdgeProfile";
    } else {
        profilePath = folder / L"EdgeProfile";
    }
    std::error_code filesystemError;
    std::filesystem::create_directories(profilePath, filesystemError);
    if (filesystemError) {
        if (startedBackend) stopBackend(backend);
        showError(L"WeatherScope could not prepare its app window profile.");
        return 1;
    }

    std::wstring edgeCommand = L"\"" + edgePath.wstring() +
        L"\" --app=\"http://127.0.0.1:8080/\" --user-data-dir=\"" +
        profilePath.wstring() + L"\" --no-first-run --no-default-browser-check --disable-background-mode";
    STARTUPINFOW edgeStartup{};
    edgeStartup.cb = sizeof(edgeStartup);
    PROCESS_INFORMATION edge{};
    if (!CreateProcessW(edgePath.c_str(), edgeCommand.data(), nullptr, nullptr, FALSE,
            0, nullptr, folder.c_str(), &edgeStartup, &edge)) {
        if (startedBackend) stopBackend(backend);
        showError(L"WeatherScope could not open its desktop window in Microsoft Edge.");
        return 1;
    }

    CloseHandle(edge.hThread);
    WaitForSingleObject(edge.hProcess, INFINITE);
    CloseHandle(edge.hProcess);
    if (startedBackend) stopBackend(backend);
    return 0;
}

