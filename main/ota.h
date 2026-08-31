#pragma once
// LAN push-OTA over HTTP. No-op (logs a warning) unless built with -DOTA_TOKEN.
// See ota.c for the endpoints (POST /update?key=..., GET /).
void ota_server_start(void);
