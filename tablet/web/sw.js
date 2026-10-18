/* Zappo X as an app on the tablet: keeps the page itself, so the app opens
   even while Zappo X isn't running yet (it then connects as soon as it is).
   Everything else comes straight from Zappo X. */
const CACHE = "zappo-x-app-1";
const SHELL = ["/", "/manifest.webmanifest", "/icon-192.png", "/icon-512.png"];
self.addEventListener("install", (e) => {
  e.waitUntil(caches.open(CACHE).then((c) => c.addAll(SHELL)).catch(() => {}).then(() => self.skipWaiting()));
});
self.addEventListener("activate", (e) => {
  e.waitUntil(caches.keys().then((ks) => Promise.all(ks.filter((k) => k !== CACHE).map((k) => caches.delete(k)))).then(() => self.clients.claim()));
});
self.addEventListener("fetch", (e) => {
  const u = new URL(e.request.url);
  if (e.request.method !== "GET" || u.origin !== location.origin || u.pathname === "/ws") return;
  const key = u.pathname === "/index.html" ? "/" : u.pathname;
  e.respondWith(
    fetch(e.request)
      .then((r) => {
        if (r.ok && SHELL.includes(key)) { const c = r.clone(); caches.open(CACHE).then((cc) => cc.put(key, c)); }
        return r;
      })
      .catch(() => caches.match(key).then((r) => r || caches.match("/")))
  );
});
