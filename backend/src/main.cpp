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
    :root{color-scheme:dark;font-family:Inter,Segoe UI,Arial,sans-serif;background:#070d1a;color:#f5f8ff}
    *{box-sizing:border-box}body{margin:0;min-height:100vh;background:radial-gradient(ellipse at 78% -12%,#42639266 0,transparent 42%),radial-gradient(ellipse at 8% 15%,#18355880 0,transparent 34%),#070d1a;padding:32px 20px 60px}
    body.booting{overflow:hidden}.launch-screen{position:fixed;z-index:1000;inset:0;display:grid;place-items:center;overflow:hidden;background:radial-gradient(ellipse at 50% 43%,#183d61 0,#0b1b30 38%,#050a14 78%);color:#f4f8ff;transition:opacity .85s ease,visibility .85s ease}.launch-screen.is-hidden{opacity:0;visibility:hidden;pointer-events:none}.launch-stars,.launch-stars::before,.launch-stars::after{position:absolute;inset:-20%;content:"";background-image:radial-gradient(1px 1px at 12% 24%,#d8ecffb0 99%,transparent),radial-gradient(1px 1px at 78% 16%,#b8daff9c 99%,transparent),radial-gradient(1px 1px at 43% 68%,#fff9 99%,transparent),radial-gradient(1px 1px at 89% 73%,#b8daff9c 99%,transparent),radial-gradient(1px 1px at 26% 88%,#fff8 99%,transparent);background-size:340px 280px;animation:star-drift 34s linear infinite}.launch-stars::before{transform:translate(130px,50px) scale(1.3);opacity:.55}.launch-stars::after{transform:translate(-90px,100px) scale(.8);opacity:.5}@keyframes star-drift{to{transform:translateY(90px)}}.launch-content{position:relative;z-index:1;display:flex;flex-direction:column;align-items:center;width:min(92vw,650px);text-align:center;padding:30px 20px}.launch-brand{font-size:12px;font-weight:750;letter-spacing:.28em;text-transform:uppercase;color:#c4d9f7}.launch-brand span{color:#76e2dc}.launch-orbit{position:relative;width:min(57vw,310px);height:min(57vw,310px);margin:40px auto 23px;display:grid;place-items:center;perspective:800px}.launch-earth{position:relative;width:72%;aspect-ratio:1;border-radius:50%;overflow:hidden;background:radial-gradient(circle at 31% 25%,#9ce4ff 0,#328dc6 22%,#14507f 53%,#07182e 78%);box-shadow:inset -26px -10px 38px #020b1bea,inset 8px 5px 24px #b4edff77,0 0 30px #50c8ff80,0 0 100px #2584e566;animation:earth-turn 18s linear infinite}.launch-earth::before{position:absolute;inset:-10%;content:"";background-image:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 400 200'%3E%3Cg fill='%2391d59b' fill-opacity='.92'%3E%3Cpath d='M42 42l19-14 24 3 13 13-8 13 12 12-8 18-19 2-8 18-16-5-8-21-13-8 3-19-10-11zM103 103l17 5 12 17-2 26-13 23-12-16-1-22-10-15zM169 42l20-12 19 9 18-4 14 12-7 14 17 12-5 14-16 7-2 18-15 5-13-13-13 6-8-14-18-4-9-17 10-13-9-11zM218 111l22-9 22 9 9 16-13 12-5 22-17 9-16-14-4-23-13-10zM274 48l18-9 14 8 14-3 13 12-6 14 11 12-9 14-22-3-11-12-17 4-10-17z'/%3E%3C/g%3E%3C/svg%3E");background-size:200% 100%;filter:drop-shadow(0 0 3px #b5f1ad77);animation:land-turn 24s linear infinite}.launch-earth::after{position:absolute;inset:0;content:"";border-radius:50%;background:linear-gradient(112deg,transparent 35%,#8addff26 51%,transparent 68%),radial-gradient(ellipse at 22% 29%,#ffffff26,transparent 44%);mix-blend-mode:screen}@keyframes earth-turn{to{transform:rotate(360deg)}}@keyframes land-turn{to{background-position:200% 0}}.launch-atmosphere{position:absolute;inset:8%;border:1px solid #83dfff70;border-radius:50%;box-shadow:0 0 28px #52caff36,inset 0 0 24px #52caff2b}.launch-orbit-ring{position:absolute;inset:17% -9%;border:1px solid #9bdcff70;border-radius:50%;transform:rotateX(68deg) rotateZ(-24deg);box-shadow:0 0 16px #52bfff24;animation:ring-turn 12s linear infinite}.launch-orbit-ring::after{position:absolute;left:15%;top:13%;width:8px;height:8px;content:"";border-radius:50%;background:#d7f6ff;box-shadow:0 0 14px 5px #68d8ff}.launch-orbit-ring.ring-two{inset:22% -4%;transform:rotateX(72deg) rotateZ(52deg);border-color:#e5c88755;animation-direction:reverse}.launch-orbit-ring.ring-two::after{left:78%;top:22%;width:5px;height:5px;background:#ffe0a2;box-shadow:0 0 12px 4px #ffc86b}@keyframes ring-turn{to{rotate:360deg}}.launch-kicker{color:#70dcd5;font-size:10px;font-weight:750;letter-spacing:.22em;text-transform:uppercase}.launch-content h2{margin:12px 0 8px;font-size:clamp(27px,5vw,45px);font-weight:650;letter-spacing:-.045em}.launch-copy{margin:0;color:#9eb5d4;font-size:14px}.launch-progress{display:flex;align-items:center;gap:13px;width:min(100%,330px);margin-top:29px;text-align:left}.launch-progress-track{height:3px;flex:1;overflow:hidden;border-radius:9px;background:#243b57}.launch-progress-fill{height:100%;width:8%;border-radius:inherit;background:linear-gradient(90deg,#68d5d4,#a2e3ff);box-shadow:0 0 12px #6bdbe0;transition:width .55s ease}.launch-progress-value{width:36px;color:#c8dbf2;font-size:11px;font-variant-numeric:tabular-nums}.launch-stage{min-height:18px;margin:10px 0 0;color:#7892b7;font-size:11px;letter-spacing:.04em}.launch-skip{position:absolute;z-index:2;right:24px;bottom:22px;padding:9px 12px;border:1px solid #466283;border-radius:999px;background:#10243bb0;color:#bcd0ea;font-size:11px}.launch-skip:hover{background:#1d3959;color:#fff}@media(prefers-reduced-motion:reduce){.launch-stars,.launch-earth,.launch-earth::before,.launch-orbit-ring{animation-duration:60s}.launch-screen{transition-duration:.15s}}
    main{max-width:1160px;margin:0 auto}.brand-row{display:flex;align-items:center;justify-content:space-between;gap:16px}.brand{font-size:13px;letter-spacing:.2em;text-transform:uppercase;color:#c0d2f4;font-weight:750}.brand-pill{margin-left:9px;padding:4px 8px;border:1px solid #40678c;border-radius:999px;color:#7de2da;font-size:9px;letter-spacing:.14em;vertical-align:2px}.source-label{font-size:12px;color:#8fa5c8}
    h1{font-size:clamp(36px,6vw,64px);margin:16px 0 7px;letter-spacing:-.055em;line-height:1.02}.intro{margin:0;color:#aebed9;font-size:16px}
    .search{margin:25px 0 16px;padding:15px 17px;border:1px solid #334c6b;border-radius:17px;background:linear-gradient(120deg,#111e34eF,#10192dee);display:grid;grid-template-columns:1fr auto;gap:12px;align-items:end;box-shadow:0 15px 42px #0003}
    label{display:grid;gap:7px;color:#b8c4df;font-size:13px}input{width:100%;padding:12px 13px;border:1px solid #425273;border-radius:10px;background:#0c152b;color:white;font:inherit}
    button{border:0;border-radius:10px;background:#a9e2ff;color:#10213b;font:inherit;font-size:15px;font-weight:700;padding:13px 20px;cursor:pointer}button:hover{background:#c6edff}button:disabled{opacity:.6;cursor:wait}
    .hint{grid-column:1/-1;margin:0;color:#8998ba;font-size:12px}.status{min-height:24px;color:#b9c8e7;font-size:14px;margin:14px 2px}
    .results{display:grid;gap:9px;margin:14px 0}.place-option{width:100%;text-align:left;border:1px solid #334365;background:#121d37;color:#eef3ff;padding:14px 16px}.place-option:hover{background:#1b2b4d;color:white}
    .top-grid{display:grid;grid-template-columns:minmax(300px,.84fr) minmax(420px,1.16fr);gap:18px;align-items:stretch;margin-top:18px}.weather{border:1px solid #344a6c;border-radius:24px;overflow:hidden;background:linear-gradient(145deg,#1e3458,#10192e 78%);box-shadow:0 22px 70px #0005;margin:0;display:flex;flex-direction:column;justify-content:space-between}
    .hero{padding:30px;display:flex;align-items:center;justify-content:space-between;gap:15px}.place{color:#b8c4df;font-size:14px}.temperature{font-size:clamp(60px,13vw,100px);font-weight:650;letter-spacing:-.07em;line-height:1;margin:10px 0}.condition{font-size:19px;color:#e4ecff}.weather-icon{font-size:72px;filter:drop-shadow(0 10px 18px #0004)}
    .details{display:grid;grid-template-columns:repeat(4,1fr);border-top:1px solid #334365}.detail{padding:20px 22px;border-right:1px solid #334365}.detail:last-child{border-right:0}.detail-name{font-size:12px;color:#9aabce}.detail-value{margin-top:8px;font-size:20px;font-weight:650}
    .forecast{margin-top:22px;padding:22px;border:1px solid #334365;border-radius:20px;background:#121d37}.forecast h2{margin:0 0 17px;font-size:20px}.forecast-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(92px,1fr));gap:10px}.day-card{padding:13px 8px;text-align:center;border:1px solid #2b3b5c;border-radius:14px;background:#0f1930}.day-name{font-size:13px;color:#b8c4df}.day-icon{font-size:26px;margin:8px 0}.day-temp{font-size:14px;font-weight:650;white-space:nowrap}.day-rain{font-size:11px;color:#9aabce;margin-top:7px}
    .visualizer,.charts{padding:20px;border:1px solid #344a6c;border-radius:22px;background:linear-gradient(145deg,#111d33f5,#0e1729f5);overflow:hidden;box-shadow:0 20px 55px #0004}.visualizer{margin:0;min-width:0}.visualizer-head{display:flex;justify-content:space-between;align-items:center;gap:12px;margin-bottom:10px}.visualizer h2,.charts h2{font-size:20px;margin:3px 0}.viz-kicker{font-size:11px;text-transform:uppercase;letter-spacing:.14em;color:#94add8}.viz-wind{font-size:12px;color:#c5d4ee;padding:8px 10px;border:1px solid #344e71;border-radius:999px;background:#162640}
    .globe-layout{display:grid;grid-template-columns:minmax(0,1fr);align-items:center;gap:10px}.globe-wrap{position:relative;display:grid;place-items:center;min-height:330px;height:clamp(320px,37vw,450px);border:1px solid #30496a;border-radius:18px;background:radial-gradient(ellipse at 50% 48%,#24456d 0,#152843 47%,#0b1424 82%);overflow:hidden}.globe-wrap::after{content:"";position:absolute;inset:auto 15% 3% 15%;height:9%;border-radius:50%;background:#76c9ff30;filter:blur(22px);pointer-events:none}#globe-container{position:absolute;inset:0;touch-action:none}#globe-container canvas{display:block;width:100%;height:100%;outline:none}.globe-tip{position:absolute;z-index:2;bottom:13px;left:0;right:0;text-align:center;color:#a9bfdf;font-size:11px;pointer-events:none;text-shadow:0 1px 8px #06101f}.globe-loading,.globe-error{position:absolute;inset:0;display:grid;place-items:center;color:#d0e1f7;font-size:14px;text-align:center;padding:24px}.globe-info{display:flex;justify-content:space-between;align-items:center;gap:10px;padding:5px 3px 0}.globe-info h3{font-size:16px;margin:5px 0 0}.globe-info p{color:#b8c8e3;font-size:12px;line-height:1.45;margin:4px 0}.globe-key{display:flex;align-items:center;gap:8px;color:#aec3e3;font-size:11px;white-space:nowrap}.globe-dot{width:9px;height:9px;border-radius:50%;background:#ffe27a;box-shadow:0 0 13px #ffe27a}
    .charts-grid{display:grid;grid-template-columns:1fr 1fr;gap:14px}.chart-card{padding:16px;border:1px solid #2b3b5c;border-radius:16px;background:#0f1930;min-width:0}.chart-card h3{font-size:15px;margin:0 0 10px}.chart-card svg{width:100%;height:auto;overflow:visible}.chart-grid-line{stroke:#34445f;stroke-width:1}.chart-axis-label{fill:#8ea0c3;font:11px Inter,Segoe UI,Arial,sans-serif}.chart-high{fill:none;stroke:#ffb86b;stroke-width:3;stroke-linecap:round;stroke-linejoin:round}.chart-low{fill:none;stroke:#78c8ff;stroke-width:3;stroke-linecap:round;stroke-linejoin:round}.chart-high-dot{fill:#ffcf8c}.chart-low-dot{fill:#9cddff}.rain-bar{fill:#6bbce8;opacity:.85}.chart-legend{display:flex;gap:16px;color:#aab9df;font-size:12px;margin-top:6px}.legend-high::before,.legend-low::before{content:"";display:inline-block;width:9px;height:9px;border-radius:50%;margin-right:6px;background:#ffb86b}.legend-low::before{background:#78c8ff}
    .download-actions{display:flex;gap:10px;flex-wrap:wrap;margin:15px 0 2px}.download-actions button{background:#23385d;color:#e9f0ff;border:1px solid #40577f;padding:10px 14px}.download-actions button:hover{background:#2e4977}
    @media(max-width:820px){.top-grid{grid-template-columns:1fr}.globe-wrap{height:min(86vw,450px)}.charts-grid{grid-template-columns:1fr}}
    footer{margin-top:16px;color:#7f90b4;font-size:12px;text-align:center} [hidden]{display:none!important}
    @media(max-width:600px){body{padding:24px 12px 45px}.search{grid-template-columns:1fr}.search button{grid-column:1}.hero{padding:23px}.details{grid-template-columns:1fr 1fr}.detail:nth-child(2){border-right:0}.detail:nth-child(-n+2){border-bottom:1px solid #334365}.globe-info{align-items:flex-start;flex-direction:column}.charts,.visualizer{padding:15px}}
  </style>
  <script type="importmap">
    {"imports":{"three":"https://cdn.jsdelivr.net/npm/three@0.187.0/build/three.module.js","three/addons/":"https://cdn.jsdelivr.net/npm/three@0.187.0/examples/jsm/"}}
  </script>
</head>
<body>
  <section class="launch-screen" id="launch-screen" aria-label="WeatherScope is loading" aria-live="polite">
    <div class="launch-stars" aria-hidden="true"></div>
    <div class="launch-content">
      <div class="launch-brand">Weather<span>Scope</span> &nbsp; / &nbsp; PLANETARY WEATHER</div>
      <div class="launch-orbit" aria-hidden="true"><div class="launch-atmosphere"></div><div class="launch-earth"></div><div class="launch-orbit-ring"></div><div class="launch-orbit-ring ring-two"></div></div>
      <div class="launch-kicker">Your world, in motion</div>
      <h2>Mapping the atmosphere.</h2>
      <p class="launch-copy">Bringing your location and the next seven days into view.</p>
      <div class="launch-progress"><div class="launch-progress-track"><div class="launch-progress-fill" id="launch-progress-fill"></div></div><span class="launch-progress-value" id="launch-progress-value">08%</span></div>
      <p class="launch-stage" id="launch-stage">Synchronizing live conditions…</p>
    </div>
    <button class="launch-skip" id="launch-skip" type="button">Skip intro</button>
  </section>
  <main>
    <div class="brand-row"><div class="brand">WeatherScope<span class="brand-pill">LIVE</span></div><div class="source-label">Live weather · Global view</div></div>
    <h1>See weather from every angle.</h1>
    <p class="intro">Current conditions, a real 3D Earth, and your week ahead.</p>
    <form class="search" id="weather-form">
      <label>City or place<input id="city" type="search" value="Delhi" placeholder="Try London, Toronto, or Mumbai" minlength="2" required></label>
      <button id="search-button" type="submit">Find place</button>
      <p class="hint">Type a place name, then choose the matching city or region.</p>
    </form>
    <div class="results" id="place-results" aria-label="Matching places" hidden></div>
    <p class="status" id="status" role="status" aria-live="polite">Loading current weather…</p>
    <div class="top-grid">
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
        <div class="globe-wrap"><div id="globe-container" aria-label="Interactive 3D Earth with selected location marker"><div class="globe-loading" id="globe-loading">Preparing the 3D Earth…</div></div><div class="globe-tip">Drag to rotate · scroll to zoom · gold marker is your place</div></div>
        <div class="globe-info"><div class="viz-kicker">Selected location</div><h3 id="globe-location">Delhi</h3><p id="globe-description">Current conditions appear on the globe as you explore.</p><div class="globe-key"><span class="globe-dot"></span>Selected city</div></div>
      </div>
    </section>
    </div>
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
    <footer>Weather data by Open-Meteo · Detailed Earth imagery from the Three.js globe example</footer>
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
    const launchScreen = document.querySelector('#launch-screen');
    const launchFill = document.querySelector('#launch-progress-fill');
    const launchValue = document.querySelector('#launch-progress-value');
    const launchStage = document.querySelector('#launch-stage');
    document.body.classList.add('booting');
    let launchWeatherReady = false;
    let launchGlobeReady = false;
    let launchFinished = false;
    function setLaunchProgress(value, stage) {
      launchFill.style.width = `${value}%`;
      launchValue.textContent = `${String(value).padStart(2,'0')}%`;
      launchStage.textContent = stage;
    }
    function finishLaunch() {
      if (launchFinished || !launchWeatherReady || !launchGlobeReady) return;
      launchFinished = true;
      setLaunchProgress(100, 'Your forecast is ready.');
      setTimeout(() => {
        launchScreen.classList.add('is-hidden');
        document.body.classList.remove('booting');
      }, 520);
    }
    function markLaunchGlobeReady() {
      launchGlobeReady = true;
      setLaunchProgress(launchWeatherReady ? 92 : 68, launchWeatherReady ? 'Finishing the 3D Earth…' : 'Rendering your 3D Earth…');
      finishLaunch();
    }
    window.addEventListener('weatherscope-globe-ready', markLaunchGlobeReady, {once:true});
    window.addEventListener('weatherscope-weather-ready', () => {
      launchWeatherReady = true;
      setLaunchProgress(launchGlobeReady ? 92 : 48, launchGlobeReady ? 'Finishing the 3D Earth…' : 'Forecast received · preparing the globe…');
      finishLaunch();
    }, {once:true});
    document.querySelector('#launch-skip').addEventListener('click', () => {
      launchFinished = true;
      launchScreen.classList.add('is-hidden');
      document.body.classList.remove('booting');
    });
    window.setTimeout(() => {
      if (launchFinished) return;
      launchStage.textContent = 'Opening your dashboard…';
      launchScreen.classList.add('is-hidden');
      document.body.classList.remove('booting');
      launchFinished = true;
    }, 15000);
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
    let savedForecast = null;
    function updateGlobe(place, current, description) {
      const region = [place.admin1, place.country].filter(Boolean).join(', ');
      document.querySelector('#globe-location').textContent = [place.name, region].filter(Boolean).join(', ');
      document.querySelector('#globe-description').textContent = `${description} · ${Math.round(current.temperature_2m)}°C · ${current.time.replace('T',' ')}`;
      document.querySelector('#globe-coordinates').textContent = `${Number(place.latitude).toFixed(2)}°, ${Number(place.longitude).toFixed(2)}°`;
      window.weatherScopePlace = {latitude:Number(place.latitude), longitude:Number(place.longitude), name:place.name};
      window.dispatchEvent(new CustomEvent('weatherscope-place-change', {detail:window.weatherScopePlace}));
      visualizer.hidden = false;
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
        if (!window.weatherScopeInitialReady) {
          window.weatherScopeInitialReady = true;
          if (!launchWeatherReady) {
            launchWeatherReady = true;
            setLaunchProgress(launchGlobeReady ? 92 : 48, launchGlobeReady ? 'Finishing the 3D Earth…' : 'Forecast received · preparing the globe…');
            finishLaunch();
          }
          window.dispatchEvent(new Event('weatherscope-weather-ready'));
        }
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
  <script type="module">
    try {
      const THREE = await import('three');
      const { OrbitControls } = await import('three/addons/controls/OrbitControls.js');
      const host = document.querySelector('#globe-container');
      const loading = document.querySelector('#globe-loading');
      const scene = new THREE.Scene();
      const camera = new THREE.PerspectiveCamera(36, 1, 0.1, 30);
      camera.position.set(0, 0.12, 3.35);
      const renderer = new THREE.WebGLRenderer({alpha:true,antialias:true,powerPreference:'high-performance'});
      renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
      renderer.setSize(host.clientWidth, host.clientHeight, false);
      renderer.outputColorSpace = THREE.SRGBColorSpace;
      renderer.toneMapping = THREE.ACESFilmicToneMapping;
      renderer.toneMappingExposure = 1.15;
      host.appendChild(renderer.domElement);

      scene.add(new THREE.HemisphereLight(0xa9d5ff,0x10203b,1.7));
      const keyLight = new THREE.DirectionalLight(0xfff1d7,3.1); keyLight.position.set(-3,2.4,4); scene.add(keyLight);
      const fillLight = new THREE.DirectionalLight(0x4f9dff,1.1); fillLight.position.set(3,-1,-3); scene.add(fillLight);

      const stars = [];
      for(let i=0;i<900;i++){
        const y=1-2*(i+.5)/900, radius=Math.sqrt(1-y*y), angle=i*2.399963;
        stars.push(Math.cos(angle)*radius*8,y*8,Math.sin(angle)*radius*8);
      }
      const starGeometry = new THREE.BufferGeometry();
      starGeometry.setAttribute('position',new THREE.Float32BufferAttribute(stars,3));
      scene.add(new THREE.Points(starGeometry,new THREE.PointsMaterial({color:0xbfdcff,size:.018,sizeAttenuation:true,transparent:true,opacity:.8,depthWrite:false})));

      const earthGroup = new THREE.Group(); scene.add(earthGroup);
      const sphere = new THREE.SphereGeometry(1,96,64);
      const textureLoader = new THREE.TextureLoader(); textureLoader.setCrossOrigin('anonymous');
      const earthMaterial = new THREE.MeshPhongMaterial({color:0xffffff,specular:0x31506c,shininess:18});
      const earth = new THREE.Mesh(sphere,earthMaterial); earthGroup.add(earth);
      const earthTextureUrl = 'https://threejs.org/examples/textures/planets/earth_atmos_2048.jpg';
      textureLoader.load(earthTextureUrl, texture=>{
        texture.colorSpace=THREE.SRGBColorSpace; earthMaterial.map=texture; earthMaterial.needsUpdate=true;
        if(loading)loading.remove();
        window.dispatchEvent(new Event('weatherscope-globe-ready'));
      },undefined,()=>{if(loading)loading.textContent='Earth imagery did not load. Check the internet connection and refresh.';window.dispatchEvent(new Event('weatherscope-globe-ready'));});

      const cloudsMaterial = new THREE.MeshLambertMaterial({transparent:true,opacity:.34,depthWrite:false});
      const clouds = new THREE.Mesh(new THREE.SphereGeometry(1.009,72,48),cloudsMaterial); earthGroup.add(clouds);
      textureLoader.load('https://threejs.org/examples/textures/planets/earth_clouds_1024.png',texture=>{
        texture.colorSpace=THREE.SRGBColorSpace; cloudsMaterial.map=texture; cloudsMaterial.needsUpdate=true;
      },undefined,()=>{});
      const atmosphere = new THREE.Mesh(new THREE.SphereGeometry(1.045,72,48),new THREE.MeshBasicMaterial({color:0x55aaff,transparent:true,opacity:.13,side:THREE.BackSide,depthWrite:false}));
      earthGroup.add(atmosphere);

      const marker = new THREE.Group(); earthGroup.add(marker);
      const markerGold = new THREE.MeshBasicMaterial({color:0xffd878});
      const glowMaterial = new THREE.MeshBasicMaterial({color:0xffc85b,transparent:true,opacity:.25,depthWrite:false});
      const glow = new THREE.Mesh(new THREE.SphereGeometry(.044,16,12),glowMaterial); glow.position.y=.035; marker.add(glow);
      const stem = new THREE.Mesh(new THREE.CylinderGeometry(.003,.004,.075,8),markerGold); stem.position.y=.043; marker.add(stem);
      const pin = new THREE.Mesh(new THREE.SphereGeometry(.014,18,14),new THREE.MeshBasicMaterial({color:0xfff0ad})); pin.position.y=.087; marker.add(pin);
      const ring = new THREE.Mesh(new THREE.TorusGeometry(.025,.0035,8,32),new THREE.MeshBasicMaterial({color:0xffe28a})); ring.rotation.x=Math.PI/2; ring.position.y=.023; marker.add(ring);

      const controls = new OrbitControls(camera,renderer.domElement);
      controls.enableDamping=true; controls.dampingFactor=.055; controls.enablePan=false;
      controls.minDistance=2.35; controls.maxDistance=4.8; controls.autoRotate=true; controls.autoRotateSpeed=.28;
      controls.target.set(0,0,0); controls.update();
      function showPlace(place){
        if(!place)return;
        const latitude=Number(place.latitude)*Math.PI/180, longitude=Number(place.longitude)*Math.PI/180;
        const normal=new THREE.Vector3(Math.cos(latitude)*Math.cos(longitude),Math.sin(latitude),-Math.cos(latitude)*Math.sin(longitude)).normalize();
        marker.position.copy(normal.clone().multiplyScalar(1.004));
        marker.quaternion.setFromUnitVectors(new THREE.Vector3(0,1,0),normal);
        earthGroup.quaternion.setFromUnitVectors(normal,new THREE.Vector3(0,0,1));
        controls.reset(); controls.update();
      }
      window.addEventListener('weatherscope-place-change',event=>showPlace(event.detail));
      showPlace(window.weatherScopePlace);

      const resize = ()=>{
        const width=host.clientWidth,height=host.clientHeight;
        if(!width||!height)return;
        renderer.setSize(width,height,false);camera.aspect=width/height;camera.updateProjectionMatrix();
      };
      new ResizeObserver(resize).observe(host);
      function animate(){requestAnimationFrame(animate);controls.update();clouds.rotation.y+=.00012;renderer.render(scene,camera);}
      animate();
    } catch(error) {
      const loading=document.querySelector('#globe-loading');
      if(loading)loading.textContent='The 3D globe could not start in this browser. Refresh the page or use a browser with WebGL enabled.';
      window.dispatchEvent(new Event('weatherscope-globe-ready'));
    }
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

