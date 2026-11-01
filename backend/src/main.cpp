#include "crow_all.h"
#include <nlohmann/json.hpp>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>

#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

class WinHttpHandle {
public:
    explicit WinHttpHandle(HINTERNET handle = nullptr) : handle_(handle) {}
    ~WinHttpHandle() { if (handle_) WinHttpCloseHandle(handle_); }
    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;
    HINTERNET get() const { return handle_; }

private:
    HINTERNET handle_;
};

std::wstring widenAscii(const std::string& text)
{
    return std::wstring(text.begin(), text.end());
}

std::string fetchHttpsJson(const wchar_t* host, const std::string& pathText)
{
    const auto path = widenAscii(pathText);
    WinHttpHandle session(WinHttpOpen(L"WeatherScope/1.0",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session.get()) throw std::runtime_error("Could not start the HTTPS client.");
    WinHttpSetTimeouts(session.get(), 5000, 5000, 10000, 10000);

    WinHttpHandle connection(WinHttpConnect(session.get(), host,
        INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!connection.get()) throw std::runtime_error("Could not connect to the weather service.");

    WinHttpHandle request(WinHttpOpenRequest(connection.get(), L"GET", path.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE));
    if (!request.get()) throw std::runtime_error("Could not prepare the weather request.");

    if (!WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.get(), nullptr)) {
        throw std::runtime_error("The weather service could not be reached.");
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX) ||
        status < 200 || status >= 300) {
        throw std::runtime_error("The weather service returned an error.");
    }

    std::string body;
    DWORD available = 0;
    while (true) {
        if (!WinHttpQueryDataAvailable(request.get(), &available)) {
            throw std::runtime_error("Could not read the weather service response.");
        }
        if (available == 0) break;
        const auto oldSize = body.size();
        body.resize(oldSize + available);
        DWORD received = 0;
        if (!WinHttpReadData(request.get(), body.data() + oldSize, available, &received)) {
            throw std::runtime_error("Could not read the weather response.");
        }
        body.resize(oldSize + received);
    }

    return nlohmann::json::parse(body).dump();
}

std::string fetchCurrentWeather(double latitude, double longitude)
{
    std::ostringstream query;
    query << std::fixed << std::setprecision(6)
          << "/v1/forecast?latitude=" << latitude
          << "&longitude=" << longitude
          << "&current=temperature_2m,relative_humidity_2m,apparent_temperature,is_day,precipitation,weather_code,wind_speed_10m"
          << "&timezone=auto";
    return fetchHttpsJson(L"api.open-meteo.com", query.str());
}

std::string encodeQueryValue(const std::string& value)
{
    constexpr char hex[] = "0123456789ABCDEF";
    std::string encoded;
    for (const unsigned char byte : value) {
        const bool unreserved = (byte >= 'a' && byte <= 'z') ||
            (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9') ||
            byte == '-' || byte == '_' || byte == '.' || byte == '~';
        if (unreserved) {
            encoded.push_back(static_cast<char>(byte));
        } else {
            encoded.push_back('%');
            encoded.push_back(hex[byte >> 4]);
            encoded.push_back(hex[byte & 0x0F]);
        }
    }
    return encoded;
}

std::string searchLocations(const std::string& name)
{
    const auto body = fetchHttpsJson(L"geocoding-api.open-meteo.com",
        "/v1/search?name=" + encodeQueryValue(name) + "&count=8&language=en");
    const auto upstream = nlohmann::json::parse(body);
    nlohmann::json results = nlohmann::json::array();
    if (upstream.contains("results") && upstream["results"].is_array()) {
        for (const auto& item : upstream["results"]) {
            nlohmann::json place = {
                {"name", item.value("name", "")},
                {"latitude", item.value("latitude", 0.0)},
                {"longitude", item.value("longitude", 0.0)},
                {"country", item.value("country", "")},
                {"admin1", item.value("admin1", "")},
                {"timezone", item.value("timezone", "")}
            };
            results.push_back(std::move(place));
        }
    }
    return nlohmann::json{{"results", results}}.dump();
}

bool parseCoordinate(const char* value, double min, double max, double& result)
{
    if (!value) return false;
    try {
        std::size_t parsed = 0;
        result = std::stod(value, &parsed);
        return parsed == std::string(value).size() && std::isfinite(result) &&
               result >= min && result <= max;
    } catch (...) {
        return false;
    }
}

} // namespace

int main()
{
    // Create a simple Crow application
    crow::SimpleApp app;

    // Serve a small responsive page that uses the weather endpoint below.
    CROW_ROUTE(app, "/")([] {
        crow::response response(R"HTML(<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta name="theme-color" content="#101a35">
  <title>WeatherScope</title>
  <style>
    :root{color-scheme:dark;font-family:Inter,Segoe UI,Arial,sans-serif;background:#0b1224;color:#f4f7ff}
    *{box-sizing:border-box}body{margin:0;min-height:100vh;background:radial-gradient(ellipse at 50% -15%,#344b83 0,transparent 55%),#0b1224;padding:42px 18px}
    main{max-width:820px;margin:0 auto}.brand{font-size:14px;letter-spacing:.18em;text-transform:uppercase;color:#aab9df;font-weight:700}
    h1{font-size:clamp(34px,7vw,58px);margin:10px 0 8px;letter-spacing:-.045em}.intro{margin:0;color:#b8c4df;font-size:17px}
    .search{margin:30px 0 18px;padding:18px;border:1px solid #334365;border-radius:18px;background:#121d37;display:grid;grid-template-columns:1fr 1fr auto;gap:12px;align-items:end}
    label{display:grid;gap:7px;color:#b8c4df;font-size:13px}input{width:100%;padding:12px 13px;border:1px solid #425273;border-radius:10px;background:#0c152b;color:white;font:inherit}
    button{border:0;border-radius:10px;background:#a9e2ff;color:#10213b;font:inherit;font-size:15px;font-weight:700;padding:13px 20px;cursor:pointer}button:hover{background:#c6edff}button:disabled{opacity:.6;cursor:wait}
    .hint{grid-column:1/-1;margin:0;color:#8998ba;font-size:12px}.status{min-height:24px;color:#b9c8e7;font-size:14px;margin:14px 2px}
    .results{display:grid;gap:9px;margin:14px 0}.place-option{width:100%;text-align:left;border:1px solid #334365;background:#121d37;color:#eef3ff;padding:14px 16px}.place-option:hover{background:#1b2b4d;color:white}
    .weather{border:1px solid #334365;border-radius:24px;overflow:hidden;background:linear-gradient(135deg,#1c2c51,#101a32 72%);box-shadow:0 22px 70px #0005}
    .hero{padding:30px;display:flex;align-items:center;justify-content:space-between;gap:15px}.place{color:#b8c4df;font-size:14px}.temperature{font-size:clamp(60px,13vw,100px);font-weight:650;letter-spacing:-.07em;line-height:1;margin:10px 0}.condition{font-size:19px;color:#e4ecff}.weather-icon{font-size:72px;filter:drop-shadow(0 10px 18px #0004)}
    .details{display:grid;grid-template-columns:repeat(4,1fr);border-top:1px solid #334365}.detail{padding:20px 22px;border-right:1px solid #334365}.detail:last-child{border-right:0}.detail-name{font-size:12px;color:#9aabce}.detail-value{margin-top:8px;font-size:20px;font-weight:650}
    footer{margin-top:16px;color:#7f90b4;font-size:12px;text-align:center} [hidden]{display:none!important}
    @media(max-width:600px){body{padding:28px 14px}.search{grid-template-columns:1fr 1fr}.search button{grid-column:1/-1}.hero{padding:23px}.details{grid-template-columns:1fr 1fr}.detail:nth-child(2){border-right:0}.detail:nth-child(-n+2){border-bottom:1px solid #334365}}
  </style>
</head>
<body>
  <main>
    <div class="brand">WeatherScope</div>
    <h1>Weather, in focus.</h1>
    <p class="intro">A clear look at current conditions for any coordinates.</p>
    <form class="search" id="weather-form">
      <label style="grid-column:1/3">City or place<input id="city" type="search" value="Delhi" placeholder="Try London, Toronto, or Mumbai" minlength="2" required></label>
      <button id="search-button" type="submit">Find place</button>
      <p class="hint">Type a place name, then choose the matching city or region.</p>
    </form>
    <div class="results" id="place-results" aria-label="Matching places" hidden></div>
    <p class="status" id="status" role="status" aria-live="polite">Loading current weather…</p>
    <section class="weather" id="weather-card" aria-label="Current weather" hidden>
      <div class="hero">
        <div><div class="place" id="place">Current conditions</div><div class="temperature"><span id="temperature">—</span><span style="font-size:.48em;vertical-align:top">°</span></div><div class="condition" id="condition">—</div></div>
        <div class="weather-icon" id="weather-icon" aria-hidden="true">🌤️</div>
      </div>
      <div class="details">
        <div class="detail"><div class="detail-name">Feels like</div><div class="detail-value" id="feels-like">—</div></div>
        <div class="detail"><div class="detail-name">Humidity</div><div class="detail-value" id="humidity">—</div></div>
        <div class="detail"><div class="detail-name">Wind</div><div class="detail-value" id="wind">—</div></div>
        <div class="detail"><div class="detail-name">Precipitation</div><div class="detail-value" id="precipitation">—</div></div>
      </div>
    </section>
    <footer>Current conditions from Open-Meteo</footer>
  </main>
  <script>
    const form = document.querySelector('#weather-form');
    const button = document.querySelector('#search-button');
    const status = document.querySelector('#status');
    const card = document.querySelector('#weather-card');
    const placeResults = document.querySelector('#place-results');
    const conditions = code => {
      if (code === 0) return ['Clear sky', '☀️'];
      if (code === 1) return ['Mostly clear', '🌤️'];
      if (code === 2) return ['Partly cloudy', '⛅'];
      if (code === 3) return ['Overcast', '☁️'];
      if (code === 45 || code === 48) return ['Foggy', '🌫️'];
      if (code >= 51 && code <= 57) return ['Drizzle', '🌦️'];
      if (code >= 61 && code <= 67) return ['Rain', '🌧️'];
      if (code >= 71 && code <= 77) return ['Snow', '❄️'];
      if (code >= 80 && code <= 82) return ['Rain showers', '🌦️'];
      if (code >= 85 && code <= 86) return ['Snow showers', '🌨️'];
      if (code >= 95) return ['Thunderstorm', '⛈️'];
      return ['Current conditions', '🌡️'];
    };
    async function loadWeather(place) {
      button.disabled = true;
      status.textContent = `Getting weather for ${place.name}…`;
      try {
        const response = await fetch(`/weather?latitude=${encodeURIComponent(place.latitude)}&longitude=${encodeURIComponent(place.longitude)}`);
        const data = await response.json();
        if (!response.ok) throw new Error(data.error || 'Weather could not be loaded.');
        const current = data.current;
        const units = data.current_units;
        const [description, icon] = conditions(current.weather_code);
        const region = [place.admin1, place.country].filter(Boolean).join(', ');
        document.querySelector('#place').textContent = `${place.name}${region ? `, ${region}` : ''} · ${current.time.replace('T', ' ')}`;
        document.querySelector('#temperature').textContent = Math.round(current.temperature_2m);
        document.querySelector('#condition').textContent = description;
        document.querySelector('#weather-icon').textContent = current.is_day ? icon : '🌙';
        document.querySelector('#feels-like').textContent = `${Math.round(current.apparent_temperature)} ${units.apparent_temperature}`;
        document.querySelector('#humidity').textContent = `${current.relative_humidity_2m}${units.relative_humidity_2m}`;
        document.querySelector('#wind').textContent = `${current.wind_speed_10m} ${units.wind_speed_10m}`;
        document.querySelector('#precipitation').textContent = `${current.precipitation} ${units.precipitation}`;
        card.hidden = false;
        placeResults.hidden = true;
        status.textContent = `Weather updated for ${place.name}.`;
      } catch (error) {
        status.textContent = error.message;
      } finally {
        button.disabled = false;
      }
    }

    form.addEventListener('submit', async event => {
      event.preventDefault();
      button.disabled = true;
      placeResults.replaceChildren();
      placeResults.hidden = true;
      status.textContent = 'Searching for places…';
      try {
        const city = document.querySelector('#city').value.trim();
        const response = await fetch(`/geocode?name=${encodeURIComponent(city)}`);
        const data = await response.json();
        if (!response.ok) throw new Error(data.error || 'Place search failed.');
        if (!data.results.length) {
          status.textContent = 'No matching places found. Try another spelling or add a country.';
          return;
        }
        for (const place of data.results) {
          const option = document.createElement('button');
          option.type = 'button';
          option.className = 'place-option';
          option.textContent = [place.name, place.admin1, place.country].filter(Boolean).join(', ');
          option.addEventListener('click', () => loadWeather(place));
          placeResults.append(option);
        }
        placeResults.hidden = false;
        status.textContent = 'Choose the place you meant.';
      } catch (error) {
        status.textContent = error.message;
      } finally {
        button.disabled = false;
      }
    });
    loadWeather({name:'Delhi', admin1:'Delhi', country:'India', latitude:28.576448, longitude:77.18678});
  </script>
</body>
</html>)HTML");
        response.set_header("Content-Type", "text/html; charset=utf-8");
        return response;
    });

    // Health‑check endpoint returns a JSON object
    CROW_ROUTE(app, "/health")
    ([](){
        nlohmann::json j;
        j["status"] = "ok";
        return crow::response{j.dump()};
    });

    // Turn a place name into selectable coordinates for the weather endpoint.
    CROW_ROUTE(app, "/geocode")([](const crow::request& request) {
        const char* name = request.url_params.get("name");
        if (!name || std::string(name).size() < 2 || std::string(name).size() > 100) {
            crow::response response(400, nlohmann::json{
                {"error", "Enter a place name with at least 2 characters."}
            }.dump());
            response.set_header("Content-Type", "application/json; charset=utf-8");
            return response;
        }

        try {
            crow::response response(200, searchLocations(name));
            response.set_header("Content-Type", "application/json; charset=utf-8");
            return response;
        } catch (const std::exception&) {
            crow::response response(502, nlohmann::json{
                {"error", "Could not search for places. Please try again."}
            }.dump());
            response.set_header("Content-Type", "application/json; charset=utf-8");
            return response;
        }
    });

    // Fetch current conditions for a coordinate pair from Open-Meteo.
    CROW_ROUTE(app, "/weather")
    ([](const crow::request& request) {
        double latitude = 0.0;
        double longitude = 0.0;
        if (!parseCoordinate(request.url_params.get("latitude"), -90.0, 90.0, latitude) ||
            !parseCoordinate(request.url_params.get("longitude"), -180.0, 180.0, longitude)) {
            crow::response response(400, nlohmann::json{
                {"error", "Provide valid latitude (-90 to 90) and longitude (-180 to 180)."}
            }.dump());
            response.set_header("Content-Type", "application/json; charset=utf-8");
            return response;
        }

        try {
            crow::response response(200, fetchCurrentWeather(latitude, longitude));
            response.set_header("Content-Type", "application/json; charset=utf-8");
            return response;
        } catch (const std::exception&) {
            crow::response response(502, nlohmann::json{
                {"error", "Could not retrieve current weather. Please try again."}
            }.dump());
            response.set_header("Content-Type", "application/json; charset=utf-8");
            return response;
        }
    });

    // Run the server on localhost:8080 (default Crow port)
    app.port(8080).multithreaded().run();
    return 0;
}

