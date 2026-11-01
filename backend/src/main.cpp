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
          << "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,precipitation_sum,wind_speed_10m_max"
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
    main{max-width:1060px;margin:0 auto}.brand{font-size:14px;letter-spacing:.18em;text-transform:uppercase;color:#aab9df;font-weight:700}
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
    .visualizer,.charts{margin-top:22px;padding:20px;border:1px solid #334365;border-radius:22px;background:#121d37;overflow:hidden}.visualizer-head{display:flex;justify-content:space-between;align-items:center;gap:12px;margin-bottom:14px}.visualizer h2,.charts h2{font-size:20px;margin:3px 0}.viz-kicker{font-size:11px;text-transform:uppercase;letter-spacing:.14em;color:#91a8d6}.viz-wind{font-size:13px;color:#b8c4df}
    .globe-layout{display:grid;grid-template-columns:minmax(280px,1.15fr) minmax(190px,.85fr);align-items:center;gap:20px}.globe-wrap{position:relative;display:grid;place-items:center;min-height:390px;border-radius:18px;background:radial-gradient(ellipse at center,#243d67 0,#14223e 54%,#101a30 76%)}#globe-canvas{display:block;width:min(100%,440px);aspect-ratio:1;touch-action:none;cursor:grab}#globe-canvas:active{cursor:grabbing}.globe-tip{position:absolute;bottom:15px;left:0;right:0;text-align:center;color:#91a8d6;font-size:12px;pointer-events:none}.globe-info{padding:12px}.globe-info h3{font-size:25px;margin:8px 0}.globe-info p{color:#b8c4df;line-height:1.6}.globe-key{display:flex;align-items:center;gap:9px;margin-top:18px;color:#aab9df;font-size:13px}.globe-dot{width:10px;height:10px;border-radius:50%;background:#ffe37b;box-shadow:0 0 13px #ffe37b}
    .charts-grid{display:grid;grid-template-columns:1fr 1fr;gap:14px}.chart-card{padding:16px;border:1px solid #2b3b5c;border-radius:16px;background:#0f1930;min-width:0}.chart-card h3{font-size:15px;margin:0 0 10px}.chart-card svg{width:100%;height:auto;overflow:visible}.chart-grid-line{stroke:#34445f;stroke-width:1}.chart-axis-label{fill:#8ea0c3;font:11px Inter,Segoe UI,Arial,sans-serif}.chart-high{fill:none;stroke:#ffb86b;stroke-width:3;stroke-linecap:round;stroke-linejoin:round}.chart-low{fill:none;stroke:#78c8ff;stroke-width:3;stroke-linecap:round;stroke-linejoin:round}.chart-high-dot{fill:#ffcf8c}.chart-low-dot{fill:#9cddff}.rain-bar{fill:#6bbce8;opacity:.85}.chart-legend{display:flex;gap:16px;color:#aab9df;font-size:12px;margin-top:6px}.legend-high::before,.legend-low::before{content:"";display:inline-block;width:9px;height:9px;border-radius:50%;margin-right:6px;background:#ffb86b}.legend-low::before{background:#78c8ff}
    .download-actions{display:flex;gap:10px;flex-wrap:wrap;margin:15px 0 2px}.download-actions button{background:#23385d;color:#e9f0ff;border:1px solid #40577f;padding:10px 14px}.download-actions button:hover{background:#2e4977}
    @media(max-width:720px){.globe-layout{grid-template-columns:1fr}.globe-wrap{min-height:320px}.globe-info{padding:4px 8px}.charts-grid{grid-template-columns:1fr}}
    footer{margin-top:16px;color:#7f90b4;font-size:12px;text-align:center} [hidden]{display:none!important}
    @media(max-width:600px){body{padding:28px 14px}.search{grid-template-columns:1fr 1fr}.search button{grid-column:1/-1}.hero{padding:23px}.details{grid-template-columns:1fr 1fr}.detail:nth-child(2){border-right:0}.detail:nth-child(-n+2){border-bottom:1px solid #334365}.globe-wrap{min-height:275px}.charts,.visualizer{padding:15px}}
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
    <section class="visualizer" id="visualizer" aria-label="Interactive 3D globe" hidden>
      <div class="visualizer-head"><div><div class="viz-kicker">Explore the location</div><h2>WeatherScope globe</h2></div><div class="viz-wind" id="globe-coordinates">—</div></div>
      <div class="globe-layout">
        <div class="globe-wrap"><canvas id="globe-canvas" width="640" height="640" aria-label="Draggable 3D globe with selected location marker"></canvas><div class="globe-tip">Drag the globe to rotate · the gold marker is your selected place</div></div>
        <div class="globe-info"><div class="viz-kicker">Selected location</div><h3 id="globe-location">Delhi</h3><p id="globe-description">Current conditions appear on the globe as you explore.</p><div class="globe-key"><span class="globe-dot"></span>Selected city</div></div>
      </div>
    </section>
    <section class="forecast" id="forecast-card" aria-label="Seven day forecast" hidden>
      <h2>7-day forecast</h2>
      <div class="forecast-grid" id="forecast-days"></div>
    </section>
    <section class="charts" id="charts-card" aria-label="Seven day weather charts" hidden>
      <h2>7-day trends</h2>
      <div class="charts-grid">
        <div class="chart-card"><h3>Daily temperatures</h3><svg id="temperature-chart" viewBox="0 0 520 240" role="img" aria-label="High and low temperature chart"></svg><div class="chart-legend"><span class="legend-high">High</span><span class="legend-low">Low</span></div></div>
        <div class="chart-card"><h3>Chance of precipitation</h3><svg id="precipitation-chart" viewBox="0 0 520 240" role="img" aria-label="Daily precipitation chance chart"></svg></div>
      </div>
      <div class="download-actions"><button id="download-csv" type="button">Download CSV</button><button id="download-json" type="button">Download JSON</button></div>
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
    const chartsCard = document.querySelector('#charts-card');
    const globeCanvas = document.querySelector('#globe-canvas');
    const globeContext = globeCanvas.getContext('2d');
    let globeCenter = {latitude: 20, longitude: 0};
    let globePlace = {latitude: 20, longitude: 0};
    let savedForecast = null;
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
    const landPolygons = [
      [[-168,72],[-145,70],[-130,55],[-124,48],[-116,32],[-107,23],[-98,17],[-91,18],[-83,9],[-77,8],[-80,25],[-67,45],[-58,51],[-66,59],[-91,62],[-110,70],[-135,72]],
      [[-74,12],[-60,9],[-48,-1],[-36,-8],[-45,-24],[-53,-39],[-66,-56],[-73,-42],[-78,-15]],
      [[-52,82],[-25,80],[-20,67],[-38,59],[-52,63]],
      [[-12,36],[-8,50],[8,58],[25,70],[48,69],[65,58],[85,56],[105,72],[135,68],[160,60],[179,65],[179,8],[155,5],[140,20],[121,22],[110,5],[98,8],[80,8],[72,20],[56,25],[46,38],[30,35],[20,40],[10,37],[3,43],[-5,42]],
      [[-18,36],[3,38],[16,32],[34,30],[51,12],[42,-5],[34,-18],[18,-35],[10,-34],[0,-18],[-6,2],[-17,16]],
      [[69,24],[82,28],[96,22],[105,9],[100,-1],[91,6],[80,7],[73,14]],
      [[112,-11],[133,-10],[153,-25],[146,-39],[126,-43],[114,-33]],
      [[-180,-72],[-120,-75],[-60,-73],[0,-75],[60,-72],[120,-75],[180,-72],[180,-88],[-180,-88]]
    ];
    const toViewPoint = (longitude, latitude) => {
      const rad = Math.PI / 180;
      const phi = latitude * rad;
      const phi0 = globeCenter.latitude * rad;
      const delta = (longitude - globeCenter.longitude) * rad;
      return {
        x: Math.cos(phi) * Math.sin(delta),
        y: Math.cos(phi0) * Math.sin(phi) - Math.sin(phi0) * Math.cos(phi) * Math.cos(delta),
        z: Math.sin(phi0) * Math.sin(phi) + Math.cos(phi0) * Math.cos(phi) * Math.cos(delta)
      };
    };
    function clipPolygonAtHorizon(points) {
      const output = [];
      for (let index = 0; index < points.length; index++) {
        const current = points[index];
        const previous = points[(index + points.length - 1) % points.length];
        const currentInside = current.z >= 0;
        const previousInside = previous.z >= 0;
        if (currentInside !== previousInside) {
          const ratio = previous.z / (previous.z - current.z);
          const point = {
            x: previous.x + (current.x - previous.x) * ratio,
            y: previous.y + (current.y - previous.y) * ratio,
            z: 0
          };
          const length = Math.hypot(point.x, point.y) || 1;
          output.push({x: point.x / length, y: point.y / length, z: 0});
        }
        if (currentInside) output.push(current);
      }
      return output;
    }
    function drawGlobe() {
      const bounds = globeCanvas.getBoundingClientRect();
      if (!bounds.width || !globeContext) return;
      const dpr = window.devicePixelRatio || 1;
      globeCanvas.width = Math.round(bounds.width * dpr);
      globeCanvas.height = Math.round(bounds.width * dpr);
      globeContext.setTransform(dpr, 0, 0, dpr, 0, 0);
      const ctx = globeContext;
      const size = bounds.width;
      const cx = size / 2, cy = size / 2, radius = size * .425;
      ctx.clearRect(0, 0, size, size);
      const ocean = ctx.createRadialGradient(cx - radius * .34, cy - radius * .42, radius * .05, cx, cy, radius * 1.2);
      ocean.addColorStop(0, '#53c2d7'); ocean.addColorStop(.48, '#14759e'); ocean.addColorStop(1, '#071932');
      ctx.beginPath(); ctx.arc(cx, cy, radius, 0, Math.PI * 2); ctx.fillStyle = ocean; ctx.fill();
      ctx.save(); ctx.beginPath(); ctx.arc(cx, cy, radius, 0, Math.PI * 2); ctx.clip();
      const screen = point => ({x: cx + point.x * radius, y: cy - point.y * radius});
      ctx.fillStyle = '#78ba83'; ctx.strokeStyle = '#c4e5a3'; ctx.lineWidth = 1.1;
      for (const polygon of landPolygons) {
        const clipped = clipPolygonAtHorizon(polygon.map(([longitude, latitude]) => toViewPoint(longitude, latitude)));
        if (clipped.length < 3) continue;
        ctx.beginPath();
        clipped.forEach((point, index) => { const p = screen(point); index ? ctx.lineTo(p.x, p.y) : ctx.moveTo(p.x, p.y); });
        ctx.closePath(); ctx.fill(); ctx.stroke();
      }
      const drawGridLine = points => {
        ctx.beginPath(); let drawing = false;
        for (const point of points) {
          if (point.z < 0) { drawing = false; continue; }
          const p = screen(point);
          if (!drawing) { ctx.moveTo(p.x, p.y); drawing = true; } else ctx.lineTo(p.x, p.y);
        }
        ctx.stroke();
      };
      ctx.strokeStyle = '#d4f2ff45'; ctx.lineWidth = .8;
      for (let latitude = -75; latitude <= 75; latitude += 25) {
        const points = []; for (let longitude = -180; longitude <= 180; longitude += 4) points.push(toViewPoint(longitude, latitude));
        drawGridLine(points);
      }
      for (let longitude = -180; longitude < 180; longitude += 30) {
        const points = []; for (let latitude = -90; latitude <= 90; latitude += 3) points.push(toViewPoint(longitude, latitude));
        drawGridLine(points);
      }
      const marker = toViewPoint(globePlace.longitude, globePlace.latitude);
      if (marker.z >= 0) {
        const p = screen(marker);
        ctx.beginPath(); ctx.arc(p.x, p.y, 10, 0, Math.PI * 2); ctx.fillStyle = '#ffd76c55'; ctx.fill();
        ctx.beginPath(); ctx.arc(p.x, p.y, 5, 0, Math.PI * 2); ctx.fillStyle = '#ffe27a'; ctx.fill();
        ctx.strokeStyle = '#fff6d3'; ctx.lineWidth = 1.5; ctx.stroke();
      }
      const shade = ctx.createRadialGradient(cx - radius * .35, cy - radius * .4, radius * .35, cx, cy, radius * 1.08);
      shade.addColorStop(0, '#ffffff00'); shade.addColorStop(.75, '#00112d05'); shade.addColorStop(1, '#00112d99');
      ctx.fillStyle = shade; ctx.beginPath(); ctx.arc(cx, cy, radius, 0, Math.PI * 2); ctx.fill();
      ctx.restore();
      ctx.beginPath(); ctx.arc(cx, cy, radius, 0, Math.PI * 2); ctx.strokeStyle = '#b6e6ff99'; ctx.lineWidth = 1.5; ctx.stroke();
    }
    let globeDrag = null;
    globeCanvas.addEventListener('pointerdown', event => { globeDrag = {x:event.clientX,y:event.clientY}; globeCanvas.setPointerCapture(event.pointerId); });
    globeCanvas.addEventListener('pointermove', event => {
      if (!globeDrag) return;
      const bounds = globeCanvas.getBoundingClientRect();
      globeCenter.longitude -= (event.clientX - globeDrag.x) / bounds.width * 180;
      globeCenter.latitude = Math.max(-82, Math.min(82, globeCenter.latitude + (event.clientY - globeDrag.y) / bounds.height * 150));
      globeDrag = {x:event.clientX,y:event.clientY}; drawGlobe();
    });
    globeCanvas.addEventListener('pointerup', () => { globeDrag = null; });
    globeCanvas.addEventListener('pointercancel', () => { globeDrag = null; });
    window.addEventListener('resize', drawGlobe);
    function updateGlobe(place, current, description) {
      globePlace = {latitude:place.latitude,longitude:place.longitude};
      globeCenter = {latitude:place.latitude,longitude:place.longitude};
      document.querySelector('#globe-location').textContent = [place.name, place.admin1, place.country].filter(Boolean).join(', ');
      document.querySelector('#globe-description').textContent = `${description} · ${Math.round(current.temperature_2m)}°C · ${current.time.replace('T',' ')}`;
      document.querySelector('#globe-coordinates').textContent = `${Number(place.latitude).toFixed(2)}°, ${Number(place.longitude).toFixed(2)}°`;
      visualizer.hidden = false;
      requestAnimationFrame(drawGlobe);
    }
    const svgElement = (name, attributes, label) => {
      const element = document.createElementNS('http://www.w3.org/2000/svg', name);
      for (const [key,value] of Object.entries(attributes || {})) element.setAttribute(key, value);
      if (label !== undefined) element.textContent = label;
      return element;
    };
    function drawTemperatureChart(daily, units) {
      const svg = document.querySelector('#temperature-chart');
      svg.replaceChildren();
      const left=48,right=504,top=24,bottom=188;
      const values=[...daily.temperature_2m_max,...daily.temperature_2m_min].filter(Number.isFinite);
      const min=Math.floor((Math.min(...values)-2)/5)*5;
      const max=Math.ceil((Math.max(...values)+2)/5)*5;
      const xAt=i=>left+(right-left)*i/(daily.time.length-1);
      const yAt=value=>bottom-(value-min)/(max-min)*(bottom-top);
      for(let tick=min;tick<=max;tick+=Math.max(5,Math.ceil((max-min)/4/5)*5)){
        const y=yAt(tick);svg.append(svgElement('line',{x1:left,y1:y,x2:right,y2:y,class:'chart-grid-line'}));
        svg.append(svgElement('text',{x:left-8,y:y+4,'text-anchor':'end',class:'chart-axis-label'},`${tick}°`));
      }
      const high=daily.temperature_2m_max.map((value,i)=>`${xAt(i)},${yAt(value)}`).join(' ');
      const low=daily.temperature_2m_min.map((value,i)=>`${xAt(i)},${yAt(value)}`).join(' ');
      svg.append(svgElement('polyline',{points:high,class:'chart-high'}),svgElement('polyline',{points:low,class:'chart-low'}));
      daily.time.forEach((date,index)=>{
        svg.append(svgElement('circle',{cx:xAt(index),cy:yAt(daily.temperature_2m_max[index]),r:4,class:'chart-high-dot'}));
        svg.append(svgElement('circle',{cx:xAt(index),cy:yAt(daily.temperature_2m_min[index]),r:4,class:'chart-low-dot'}));
        svg.append(svgElement('text',{x:xAt(index),y:218,'text-anchor':'middle',class:'chart-axis-label'},new Date(`${date}T12:00:00`).toLocaleDateString(undefined,{weekday:'short'})));
      });
      svg.setAttribute('aria-label',`Seven day temperatures in ${units.temperature_2m_max}`);
    }
    function drawPrecipitationChart(daily) {
      const svg=document.querySelector('#precipitation-chart');svg.replaceChildren();
      const left=48,right=504,top=24,bottom=188;
      const xAt=i=>left+(right-left)*(i+.5)/daily.time.length;
      for(let tick=0;tick<=100;tick+=25){
        const y=bottom-tick/100*(bottom-top);svg.append(svgElement('line',{x1:left,y1:y,x2:right,y2:y,class:'chart-grid-line'}));
        svg.append(svgElement('text',{x:left-8,y:y+4,'text-anchor':'end',class:'chart-axis-label'},`${tick}%`));
      }
      daily.time.forEach((date,index)=>{
        const probability=daily.precipitation_probability_max[index] ?? 0;
        const barHeight=probability/100*(bottom-top);
        svg.append(svgElement('rect',{x:xAt(index)-18,y:bottom-barHeight,width:36,height:barHeight,rx:6,class:'rain-bar'}));
        svg.append(svgElement('text',{x:xAt(index),y:218,'text-anchor':'middle',class:'chart-axis-label'},new Date(`${date}T12:00:00`).toLocaleDateString(undefined,{weekday:'short'})));
      });
      svg.setAttribute('aria-label','Seven day maximum chance of precipitation by day');
    }
    function downloadForecast(format) {
      if(!savedForecast)return;
      const {location,timezone,generatedAt,units,daily}=savedForecast;
      const rows=daily.time.map((date,index)=>({
        date,weather_code:daily.weather_code[index],
        temperature_2m_max:daily.temperature_2m_max[index],temperature_2m_min:daily.temperature_2m_min[index],
        precipitation_probability_max:daily.precipitation_probability_max[index],precipitation_sum:daily.precipitation_sum[index],
        wind_speed_10m_max:daily.wind_speed_10m_max[index]
      }));
      let content,mime,extension;
      if(format==='csv'){
        const headers=['city','region','country','timezone','date','weather_code',`high (${units.temperature_2m_max})`,`low (${units.temperature_2m_min})`,`precipitation chance (${units.precipitation_probability_max})`,`precipitation (${units.precipitation_sum})`,`max wind (${units.wind_speed_10m_max})`];
        const quote=value=>`"${String(value ?? '').replaceAll('"','""')}"`;
        content=[headers,...rows.map(row=>[location.name,location.admin1,location.country,timezone,row.date,row.weather_code,row.temperature_2m_max,row.temperature_2m_min,row.precipitation_probability_max,row.precipitation_sum,row.wind_speed_10m_max])].map(row=>row.map(quote).join(',')).join('\r\n');
        mime='text/csv;charset=utf-8';extension='csv';
      }else{
        content=JSON.stringify({location,timezone,generated_at:generatedAt,units,forecast:rows},null,2);mime='application/json';extension='json';
      }
      const blob=new Blob([content],{type:mime});const url=URL.createObjectURL(blob);const link=document.createElement('a');
      const safeName=location.name.toLowerCase().replace(/[^a-z0-9]+/g,'-').replace(/^-|-$/g,'')||'location';
      link.href=url;link.download=`weatherscope-${safeName}-7-day.${extension}`;document.body.append(link);link.click();link.remove();setTimeout(()=>URL.revokeObjectURL(url),1000);
    }
    document.querySelector('#download-csv').addEventListener('click',()=>downloadForecast('csv'));
    document.querySelector('#download-json').addEventListener('click',()=>downloadForecast('json'));
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
        updateGlobe(place, current, description);
        document.querySelector('#feels-like').textContent = `${Math.round(current.apparent_temperature)} ${units.apparent_temperature}`;
        document.querySelector('#humidity').textContent = `${current.relative_humidity_2m}${units.relative_humidity_2m}`;
        document.querySelector('#wind').textContent = `${current.wind_speed_10m} ${units.wind_speed_10m}`;
        document.querySelector('#precipitation').textContent = `${current.precipitation} ${units.precipitation}`;
        forecastDays.replaceChildren();
        const daily = data.daily;
        const dailyUnits = data.daily_units;
        savedForecast={location:{name:place.name,admin1:place.admin1||'',country:place.country||'',latitude:place.latitude,longitude:place.longitude},timezone:data.timezone,generatedAt:current.time,units:dailyUnits,daily};
        drawTemperatureChart(daily,dailyUnits);
        drawPrecipitationChart(daily);
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
        chartsCard.hidden = false;
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

