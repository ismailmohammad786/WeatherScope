const SHELL_CACHE = 'weatherscope-shell-v1';
const WEATHER_CACHE = 'weatherscope-weather-v1';
const SHELL_FILES = [
  './',
  './index.html',
  './style.css',
  './manifest.webmanifest',
  './icons/icon-192.png',
  './icons/icon-512.png'
];

self.addEventListener('install', event => {
  event.waitUntil(
    caches.open(SHELL_CACHE)
      .then(cache => cache.addAll(SHELL_FILES))
      .then(() => self.skipWaiting())
  );
});

self.addEventListener('activate', event => {
  event.waitUntil(
    caches.keys()
      .then(cacheNames => Promise.all(
        cacheNames
          .filter(name => ![SHELL_CACHE, WEATHER_CACHE].includes(name))
          .map(name => caches.delete(name))
      ))
      .then(() => self.clients.claim())
  );
});

self.addEventListener('fetch', event => {
  const request = event.request;
  if (request.method !== 'GET') {
    return;
  }

  const requestUrl = new URL(request.url);
  const appRoot = new URL('./', self.registration.scope);

  if (request.mode === 'navigate' && requestUrl.origin === appRoot.origin) {
    event.respondWith(
      fetch(request).then(response => {
        const updatedPage = response.clone();
        caches.open(SHELL_CACHE).then(cache => cache.put(appRoot, updatedPage));
        return response;
      }).catch(() => caches.match(appRoot))
    );
    return;
  }

  const isWeatherService =
    requestUrl.hostname === 'api.open-meteo.com' ||
    requestUrl.hostname === 'geocoding-api.open-meteo.com';

  if (!isWeatherService) {
    return;
  }

  event.respondWith(
    fetch(request)
      .then(response => {
        if (response.ok) {
          const savedResponse = response.clone();
          caches.open(WEATHER_CACHE)
            .then(cache => cache.put(request, savedResponse));
        }
        return response;
      })
      .catch(async () => {
        const savedResponse = await caches.match(request);
        return savedResponse || Response.error();
      })
  );
});

