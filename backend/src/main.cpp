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
          << "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max"
          << "&forecast_days=7"
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
    .forecast{margin-top:22px;padding:22px;border:1px solid #334365;border-radius:20px;background:#121d37}.forecast h2{margin:0 0 17px;font-size:20px}.forecast-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(92px,1fr));gap:10px}.day-card{padding:13px 8px;text-align:center;border:1px solid #2b3b5c;border-radius:14px;background:#0f1930}.day-name{font-size:13px;color:#b8c4df}.day-icon{font-size:26px;margin:8px 0}.day-temp{font-size:14px;font-weight:650;white-space:nowrap}.day-rain{font-size:11px;color:#9aabce;margin-top:7px}
    .visualizer{margin-top:22px;padding:20px;border:1px solid #334365;border-radius:22px;background:#121d37;overflow:hidden}.visualizer-head{display:flex;justify-content:space-between;align-items:center;gap:12px;margin-bottom:14px}.visualizer h2{font-size:20px;margin:3px 0}.viz-kicker{font-size:11px;text-transform:uppercase;letter-spacing:.14em;color:#91a8d6}.viz-wind{font-size:13px;color:#b8c4df}
    .weather-scene{height:270px;position:relative;overflow:hidden;border-radius:17px;perspective:900px;background:linear-gradient(160deg,#63b9f3,#d6efff 76%);transition:background 1s ease}.weather-scene[data-weather="cloudy"]{background:linear-gradient(160deg,#7594b4,#d5e0eb 78%)}.weather-scene[data-weather="fog"]{background:linear-gradient(160deg,#8295a6,#d6e0e4 78%)}.weather-scene[data-weather="rain"]{background:linear-gradient(160deg,#385776,#9caebd 85%)}.weather-scene[data-weather="snow"]{background:linear-gradient(160deg,#6a8baa,#e7f3fb 85%)}.weather-scene[data-weather="storm"]{background:linear-gradient(160deg,#151b31,#505b78 82%)}.weather-scene[data-time="night"]{background:linear-gradient(160deg,#111936,#30466d 82%)}
    .scene-stars{position:absolute;inset:0;opacity:0;transition:opacity .7s;background-image:radial-gradient(#fff 1px,transparent 1.5px),radial-gradient(#b9d8ff 1px,transparent 1.5px);background-size:67px 61px,101px 87px;background-position:8px 10px,28px 35px}.weather-scene[data-time="night"] .scene-stars{opacity:.8}
    .scene-sun,.scene-moon{position:absolute;z-index:2;right:16%;top:24px;width:67px;height:67px;border-radius:50%;background:radial-gradient(circle at 32% 28%,#fff9d4,#ffd56c 58%,#ff9c50);box-shadow:0 0 27px 10px #ffe3a577,0 0 80px 28px #ffe3a544;transform:translateZ(40px);transition:opacity .5s}.scene-moon{opacity:0;background:radial-gradient(circle at 33% 27%,#fff,#c5d5f7 70%,#8da3cc);box-shadow:0 0 40px 12px #aec9ff55}.weather-scene[data-time="night"] .scene-sun{opacity:0}.weather-scene[data-time="night"] .scene-moon{opacity:1}
    .scene-orb{position:absolute;left:50%;top:46%;width:min(47vw,330px);height:190px;border:1px solid #ffffff88;border-radius:50%;transform:translate(-50%,-50%) rotateX(64deg) rotateZ(-12deg);box-shadow:inset 0 0 34px #ffffff44,0 24px 35px #172a4c44;animation:orb-drift 8s ease-in-out infinite}.scene-orb::before,.scene-orb::after{content:"";position:absolute;inset:16px 31px;border:1px solid #ffffff66;border-radius:50%;transform:rotateY(62deg)}.scene-orb::after{transform:rotateY(-62deg)}
    .scene-horizon{position:absolute;left:-10%;right:-10%;height:104px;bottom:-37px;border-radius:50% 50% 0 0;background:linear-gradient(180deg,#316e78,#153747);transform:rotateX(5deg);box-shadow:0 -9px 32px #d8ffff55}.scene-horizon::before{content:"";position:absolute;left:13%;right:12%;top:-20px;height:40px;border-radius:50%;background:#57979a;filter:blur(1px)}
    .scene-cloud{position:absolute;z-index:3;width:106px;height:30px;border-radius:40px;background:linear-gradient(180deg,#fff,#d5e3ef);filter:drop-shadow(0 12px 8px #30476733);transform:translateZ(50px);animation:cloud-drift 7s ease-in-out infinite}.scene-cloud::before,.scene-cloud::after{content:"";position:absolute;bottom:9px;border-radius:50%;background:inherit}.scene-cloud::before{width:48px;height:48px;left:17px}.scene-cloud::after{width:37px;height:37px;left:49px}.cloud-a{left:16%;top:91px}.cloud-b{right:18%;top:119px;transform:translateZ(10px) scale(.72);animation-delay:-3s}.weather-scene[data-weather="clear"] .scene-cloud{opacity:0}.weather-scene[data-weather="rain"] .scene-cloud,.weather-scene[data-weather="storm"] .scene-cloud{background:linear-gradient(180deg,#9cabb9,#4c5e72)}.weather-scene[data-weather="fog"] .scene-cloud{filter:blur(3px);opacity:.68}
    .scene-particles{position:absolute;z-index:4;inset:0;pointer-events:none}.scene-particle{position:absolute;left:var(--x);top:-18px;opacity:0}.scene-particle.rain{width:3px;height:19px;border-radius:4px;background:linear-gradient(#d5f4ff,#52afff);animation:rain-fall var(--duration) linear var(--delay) infinite}.scene-particle.snow{width:7px;height:7px;border-radius:50%;background:#fff;box-shadow:0 0 8px #fff;animation:snow-fall var(--duration) linear var(--delay) infinite}.weather-scene[data-weather="rain"] .scene-particle,.weather-scene[data-weather="storm"] .scene-particle,.weather-scene[data-weather="snow"] .scene-particle{opacity:.9}
    .scene-lightning{position:absolute;z-index:4;left:51%;top:86px;width:18px;height:63px;background:#fff6b5;clip-path:polygon(47% 0,100% 0,62% 43%,100% 43%,18% 100%,40% 53%,0 53%);filter:drop-shadow(0 0 13px #fff);opacity:0}.weather-scene[data-weather="storm"] .scene-lightning{animation:lightning-flash 4s steps(1,end) infinite}
    .scene-caption{display:flex;justify-content:space-between;gap:12px;margin:12px 3px 0;color:#bdcbe8;font-size:13px}.scene-caption strong{color:white;font-size:15px}
    @keyframes orb-drift{0%,100%{margin-top:0}50%{margin-top:-8px}}@keyframes cloud-drift{0%,100%{translate:-5px 0}50%{translate:8px -5px}}@keyframes rain-fall{0%{transform:translateY(-10px);opacity:0}12%{opacity:.9}100%{transform:translateY(310px);opacity:0}}@keyframes snow-fall{0%{transform:translate(0,-10px);opacity:0}15%{opacity:.9}100%{transform:translate(20px,310px);opacity:0}}@keyframes lightning-flash{0%,44%,48%,100%{opacity:0}45%,47%{opacity:1}}
    @media(prefers-reduced-motion:reduce){.scene-orb,.scene-cloud,.scene-particle,.scene-lightning{animation:none!important}.scene-particle{display:none}}
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
    <section class="visualizer" id="visualizer" aria-label="Animated 3D weather scene" hidden>
      <div class="visualizer-head"><div><div class="viz-kicker">Live atmosphere</div><h2 id="scene-title">Clear skies</h2></div><div class="viz-wind" id="scene-wind">Wind 0 km/h</div></div>
      <div class="weather-scene" id="weather-scene" data-weather="clear" data-time="day" role="img" aria-label="Animated weather scene">
        <div class="scene-stars"></div><div class="scene-sun"></div><div class="scene-moon"></div>
        <div class="scene-orb"></div><div class="scene-cloud cloud-a"></div><div class="scene-cloud cloud-b"></div>
        <div class="scene-lightning"></div><div class="scene-particles" id="scene-particles"></div><div class="scene-horizon"></div>
      </div>
      <div class="scene-caption"><span id="scene-caption">The sky is clear.</span><strong id="scene-temperature">—°</strong></div>
    </section>
    <section class="forecast" id="forecast-card" aria-label="Seven day forecast" hidden>
      <h2>7-day forecast</h2>
      <div class="forecast-grid" id="forecast-days"></div>
    </section>
    <footer>Current conditions from Open-Meteo</footer>
  </main>
  <script>
    const form = document.querySelector('#weather-form');
    const button = document.querySelector('#search-button');
    const status = document.querySelector('#status');
    const card = document.querySelector('#weather-card');
    const forecastCard = document.querySelector('#forecast-card');
    const forecastDays = document.querySelector('#forecast-days');
    const placeResults = document.querySelector('#place-results');
    const visualizer = document.querySelector('#visualizer');
    const weatherScene = document.querySelector('#weather-scene');
    const sceneParticles = document.querySelector('#scene-particles');
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
    const sceneType = code => {
      if (code >= 95) return 'storm';
      if (code >= 71 && code <= 86) return 'snow';
      if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return 'rain';
      if (code === 45 || code === 48) return 'fog';
      if (code > 0) return 'cloudy';
      return 'clear';
    };
    const sceneWords = {clear:'Clear skies',cloudy:'Clouds drifting',fog:'Misty conditions',rain:'Rain in motion',snow:'Snowfall',storm:'Thunderstorm'};
    function updateScene(current, description) {
      const type = sceneType(current.weather_code);
      weatherScene.dataset.weather = type;
      weatherScene.dataset.time = current.is_day ? 'day' : 'night';
      weatherScene.setAttribute('aria-label', `${sceneWords[type]}, ${current.is_day ? 'daytime' : 'night'}`);
      document.querySelector('#scene-title').textContent = sceneWords[type];
      document.querySelector('#scene-caption').textContent = description;
      document.querySelector('#scene-temperature').textContent = `${Math.round(current.temperature_2m)}°`;
      document.querySelector('#scene-wind').textContent = `Wind ${current.wind_speed_10m} km/h`;
      sceneParticles.replaceChildren();
      const particleType = type === 'snow' ? 'snow' : (type === 'rain' || type === 'storm' ? 'rain' : '');
      if (particleType) {
        for (let index = 0; index < 24; index++) {
          const particle = document.createElement('span');
          particle.className = `scene-particle ${particleType}`;
          particle.style.setProperty('--x', `${(index * 41 + 7) % 100}%`);
          particle.style.setProperty('--delay', `${-((index * 13) % 30) / 10}s`);
          particle.style.setProperty('--duration', `${2.4 + (index % 5) * .35}s`);
          sceneParticles.append(particle);
        }
      }
      visualizer.hidden = false;
    }
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
        updateScene(current, description);
        document.querySelector('#feels-like').textContent = `${Math.round(current.apparent_temperature)} ${units.apparent_temperature}`;
        document.querySelector('#humidity').textContent = `${current.relative_humidity_2m}${units.relative_humidity_2m}`;
        document.querySelector('#wind').textContent = `${current.wind_speed_10m} ${units.wind_speed_10m}`;
        document.querySelector('#precipitation').textContent = `${current.precipitation} ${units.precipitation}`;
        forecastDays.replaceChildren();
        const daily = data.daily;
        const dailyUnits = data.daily_units;
        daily.time.forEach((date, index) => {
          const [description, icon] = conditions(daily.weather_code[index]);
          const day = document.createElement('article');
          day.className = 'day-card';
          const name = document.createElement('div');
          name.className = 'day-name';
          name.textContent = new Date(`${date}T12:00:00`).toLocaleDateString(undefined, {weekday:'short'});
          const symbol = document.createElement('div');
          symbol.className = 'day-icon';
          symbol.textContent = icon;
          symbol.title = description;
          const temperatures = document.createElement('div');
          temperatures.className = 'day-temp';
          temperatures.textContent = `${Math.round(daily.temperature_2m_max[index])}° / ${Math.round(daily.temperature_2m_min[index])}°`;
          const rain = document.createElement('div');
          rain.className = 'day-rain';
          rain.textContent = `Rain ${daily.precipitation_probability_max[index] ?? '—'}${dailyUnits.precipitation_probability_max}`;
          day.append(name, symbol, temperatures, rain);
          forecastDays.append(day);
        });
        card.hidden = false;
        forecastCard.hidden = false;
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

