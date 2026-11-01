# WeatherScope

WeatherScope is a weather app for Windows. Its desktop launcher opens a clean app window, starts the local C++ service in the background, and stops that service when the app window closes. Microsoft Edge supplies the window’s web display engine.

## What you can do

| Feature | What it does |
| --- | --- |
| Current weather | Shows temperature, feels-like temperature, humidity, wind, and precipitation for a selected place. |
| 3D Earth | Opens on the requested Asia-centered view, with Antarctica at the top. Drag to rotate, scroll to zoom, return to the Asia view, or resume slow rotation. |
| City focus | Search for a city to rotate and zoom the globe to its coordinates. The selected place is marked on the Earth. |
| City photo views | Loads photos related to the selected city from Wikimedia Commons. Each photo links to its file page and shows available creator and license information. |
| Hourly forecast | Shows the next 24 hours of temperatures and rain chances. |
| Seven-day forecast | Shows daily conditions, high and low temperatures, and rain chances. |
| Weather charts | Graphs hourly temperatures and rain chances, and seven-day temperature and precipitation trends. |
| Downloads | Saves the seven-day forecast as a CSV or JSON file. |
| City comparison | Saves up to three places in the app’s browser profile and compares current conditions and seven-day outlooks. It also shows a weather comfort estimate based on temperature, rain chance, and wind. |
| Loading intro | Shows the animated WeatherScope Earth while the initial weather and globe load. |

## How the app works

1. `WeatherScope.exe` checks whether the local weather service is already running. If needed, it starts `weather_backend.exe` without opening a terminal window.
2. The backend serves the WeatherScope page from `backend/web/index.html` and provides local weather and place-search endpoints.
3. The launcher opens that page in a separate Microsoft Edge app window. It uses a dedicated Edge profile so saved comparison cities stay with WeatherScope.
4. The backend requests weather and place-search data from Open-Meteo. The page requests city photos from Wikimedia Commons and loads Three.js and Earth imagery from their public sources.
5. When you close the WeatherScope window, the launcher stops the backend if it started it. A backend that was already running is left alone.

WeatherScope needs internet access for live forecasts, city search, the detailed Earth texture, and city photos. City comparisons are stored in the app’s local Edge profile; they are not uploaded to a WeatherScope account.

## Project files

```text
WeatherScope/
├── .github/workflows/backend.yml    Build check run on GitHub
├── backend/
│   ├── CMakeLists.txt               C++ build configuration
│   ├── src/
│   │   ├── desktop.cpp              Windows desktop launcher
│   │   ├── main.cpp                 C++ weather service and API routes
│   │   └── crow_all.h               Crow HTTP server header
│   └── web/
│       └── index.html               WeatherScope interface and browser logic
├── scripts/
│   ├── build-desktop.ps1            Builds the app and portable package
│   ├── install-windows.ps1          Adds Start menu and Desktop shortcuts
│   └── uninstall-windows.ps1        Removes the app and its shortcuts
├── .gitignore                      Excludes build and package outputs
└── README.md                       Project guide
```

## Build on Windows

The build script expects the MSYS2 UCRT64 tools at `C:\msys64\ucrt64` and Microsoft Edge installed. Required build tools are CMake, Ninja, and the MinGW UCRT64 C++ compiler. The first build downloads Crow, standalone Asio, and nlohmann/json through CMake, so it needs internet access.

Open PowerShell in the WeatherScope project folder and run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-desktop.ps1
```

The script builds the launcher and backend, then creates:

```text
dist/WeatherScope/                 Portable app folder
dist/WeatherScope-Windows.zip      Portable package to share
```

The portable folder includes the launcher, backend, web page, README, and install/uninstall scripts. The recipient does not need CMake or a C++ compiler, but does need Windows, Microsoft Edge, and internet access for live data and remote images.

## Run or install WeatherScope

To run the portable version, open `dist\WeatherScope` and double-click `WeatherScope.exe`.

To install it for your Windows account and create Start menu and Desktop shortcuts, run from the project folder:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\install-windows.ps1
```

The installer uses your account’s Local AppData folder and does not require administrator access. You can also run the included `install-windows.ps1` from inside the extracted portable folder.

To uninstall the installed app and its shortcuts:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\uninstall-windows.ps1
```

The uninstall script keeps WeatherScope’s separate Edge profile, including saved comparison cities.

## Run the backend in a browser

The backend can also be started directly for development. From the project folder, build it with CMake and then run the executable:

```powershell
& 'C:\msys64\ucrt64\bin\cmake.exe' -S backend -B backend\build -G Ninja
& 'C:\msys64\ucrt64\bin\cmake.exe' --build backend\build --config Release
.\backend\build\weather_backend.exe
```

Open [http://127.0.0.1:8080/](http://127.0.0.1:8080/) in a browser. Stop the server by focusing its terminal and pressing **Ctrl+C**.

## Backend endpoints

| Endpoint | Purpose |
| --- | --- |
| `GET /health` | Confirms the local service is running. |
| `GET /weather?latitude=28.576448&longitude=77.18678` | Returns current, hourly, and daily forecast data for coordinates. |
| `GET /geocode?name=Delhi` | Finds matching places by name. |

## Data sources and credits

- Forecasts and place search: [Open-Meteo](https://open-meteo.com/).
- 3D rendering and Earth imagery: [Three.js](https://threejs.org/).
- City photos and available creator/license details: [Wikimedia Commons](https://commons.wikimedia.org/). Photo cards link to the original file pages.

## Troubleshooting

| Problem | What to check |
| --- | --- |
| The app says port 8080 is busy | Close another WeatherScope server window or stop its terminal with **Ctrl+C**, then reopen the app. |
| WeatherScope cannot find Edge | Repair or install Microsoft Edge, then start WeatherScope again. |
| Weather or photos do not load | Check the internet connection. Forecasts and photos come from separate services. |
| The globe is blank | Use a recent Microsoft Edge version with WebGL enabled, then reopen WeatherScope. |
| Build tools are missing | Install CMake, Ninja, and the UCRT64 C++ compiler through MSYS2. The app user does not need these tools after the portable package is built. |


