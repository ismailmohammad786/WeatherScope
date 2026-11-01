# WeatherScope

WeatherScope is a local weather dashboard built with a C++17 backend and an interactive 3D Earth. Search for a place to explore its current conditions, the next 24 hours, and a seven-day forecast.

## Features

- Search for cities and places around the world.
- See current temperature, feels-like temperature, humidity, wind, and precipitation.
- Explore an interactive, textured 3D Earth. Drag to rotate, scroll to zoom, use **Reset view** to center the selected city, and pause or resume the spin.
- View an hourly temperature graph and rain chances for the next 24 hours.
- Browse a seven-day forecast with daily high and low temperatures and precipitation chances.
- Compare and save up to three cities in this browser. Comparison cards include current conditions, a seven-day outlook, and a weather comfort estimate based on temperature, rain chance, and wind.
- Download the seven-day forecast as CSV or JSON.
- Open `/health` to check whether the backend is running.

## Build and run on Windows

These commands assume CMake and the MinGW UCRT64 compiler are installed through MSYS2 at `C:\msys64\ucrt64`.

1. Open PowerShell in the WeatherScope project folder (the folder containing this README).
2. Configure and build the backend:

   ```powershell
   & 'C:\msys64\ucrt64\bin\cmake.exe' -S backend -B backend\build -G Ninja
   & 'C:\msys64\ucrt64\bin\cmake.exe' --build backend\build --config Release
   ```

3. Start the server:

   ```powershell
   .\backend\build\weather_backend.exe
   ```

4. Open [http://127.0.0.1:8080/](http://127.0.0.1:8080/) in your browser. The health check is at [http://127.0.0.1:8080/health](http://127.0.0.1:8080/health).

The first configure can take a little longer because CMake downloads Crow, standalone Asio, and nlohmann/json. The backend uses Open-Meteo for forecast and geocoding data. The browser loads Three.js and Earth imagery from their respective public sources, so internet access is needed for live data and the detailed globe texture.

To stop the server, focus its terminal and press **Ctrl+C**.

## Backend endpoints

| Endpoint | Purpose |
| --- | --- |
| `GET /health` | Returns a small JSON health response. |
| `GET /weather?latitude=28.576448&longitude=77.18678` | Returns current, hourly, and daily forecast data for coordinates. |
| `GET /geocode?name=Delhi` | Finds matching places by name. |

## Data and credits

- Weather forecasts and place search: [Open-Meteo](https://open-meteo.com/).
- 3D rendering and Earth imagery: [Three.js](https://threejs.org/).
- City comparisons are stored in the browser that you use; they are not uploaded to a user account.


