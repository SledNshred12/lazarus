/* Lazarus offline cache.
   Keeps the whole game on the device so it opens with no internet at all.
   The cache name carries the game version, so every release makes a fresh cache
   and the old one is thrown away. The page itself is fetched network-first, so a
   new version is picked up as soon as there is a connection, and the cached copy
   is only used when the network is not there. */
var CACHE = 'lazarus-v126';   /* the patch rewrites this to the release version */
var CORE = ['./', './index.html', './manifest.webmanifest', './icon-192.png', './icon-512.png', './apple-touch-icon.png'];

self.addEventListener('install', function (e) {
  e.waitUntil(
    caches.open(CACHE)
      .then(function (c) { return c.addAll(CORE); })
      .catch(function () { })          /* one missing file must not stop the rest */
      .then(function () { return self.skipWaiting(); })
  );
});

self.addEventListener('activate', function (e) {
  e.waitUntil(
    caches.keys().then(function (ks) {
      return Promise.all(ks.map(function (k) { return k === CACHE ? null : caches.delete(k); }));
    }).then(function () { return self.clients.claim(); })
  );
});

self.addEventListener('fetch', function (e) {
  var r = e.request;
  if (r.method !== 'GET') return;
  var url;
  try { url = new URL(r.url); } catch (err) { return; }
  if (url.origin !== location.origin) return;         /* other people's servers are not ours to cache */

  var isPage = r.mode === 'navigate' || url.pathname === '/' ||
               url.pathname.slice(-1) === '/' || url.pathname.slice(-11) === '/index.html';

  if (isPage) {
    /* network first: always take a newer game when there is a connection */
    e.respondWith(
      fetch(r).then(function (res) {
        var copy = res.clone();
        caches.open(CACHE).then(function (c) { c.put('./index.html', copy); }).catch(function () { });
        return res;
      }).catch(function () {
        return caches.match('./index.html').then(function (m) { return m || caches.match('./'); });
      })
    );
    return;
  }

  /* everything else: straight from the device, refreshed quietly in the background */
  e.respondWith(
    caches.match(r).then(function (m) {
      if (m) {
        fetch(r).then(function (res) {
          caches.open(CACHE).then(function (c) { c.put(r, res); }).catch(function () { });
        }).catch(function () { });
        return m;
      }
      return fetch(r).then(function (res) {
        var copy = res.clone();
        caches.open(CACHE).then(function (c) { c.put(r, copy); }).catch(function () { });
        return res;
      });
    })
  );
});
