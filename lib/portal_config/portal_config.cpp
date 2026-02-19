#include "portal_config.h"

#include <WiFi.h>
#include <SD.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>

#include "sdcard.h"
#include "config_manager.h"
#include "wifi_manager.h"
#include "buzzer_manager.h"
#include "sync_manager.h"

static DNSServer dns;
static AsyncWebServer server(80);

PortalConfig Portal;

static bool g_active = false;
static bool g_apMode = false;
static bool g_serverStarted = false;
static String g_sid;

static bool isLoggedIn(AsyncWebServerRequest *request)
{
    if (!request->hasHeader("Cookie"))
        return false;
    String cookie = request->header("Cookie");
    return cookie.indexOf("SID=" + g_sid) >= 0;
}

static void redirectTo(AsyncWebServerRequest *request, const String &to)
{
    request->redirect(to);
}

static void sendFile(AsyncWebServerRequest *request, const char *path, const char *contentType)
{
    if (!SD.exists(path))
    {
        request->send(404, "text/plain", "File not found");
        return;
    }
    request->send(SD, path, contentType);
}

static void streamDownload(AsyncWebServerRequest *request, const char *path, const char *downloadName)
{
    if (!SD.exists(path))
    {
        request->send(404, "text/plain", "File not found");
        return;
    }

    AsyncWebServerResponse *response = request->beginResponse(SD, path, "application/octet-stream", true);
    response->addHeader("Content-Disposition", String("attachment; filename=\"") + downloadName + "\"");
    request->send(response);
}

static String getIpString()
{
    if (WiFi.getMode() == WIFI_AP)
        return WiFi.softAPIP().toString();
    return WiFi.localIP().toString();
}

static void serverRoutes()
{
    server.on("/portal.css", HTTP_GET, [](AsyncWebServerRequest *request)
              { sendFile(request, "/assets/portal.css", "text/css"); });

    server.on("/portal.js", HTTP_GET, [](AsyncWebServerRequest *request)
              { sendFile(request, "/assets/portal.js", "application/javascript"); });

    server.on("/login", HTTP_GET, [](AsyncWebServerRequest *request)
              { sendFile(request, "/assets/portal_login.html", "text/html"); });

    server.on("/home", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                  if (!isLoggedIn(request))
                  {
                      redirectTo(request, "/login");
                      return;
                  }
                  sendFile(request, "/assets/portal_home.html", "text/html"); });

    server.on("/setting", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                  if (!isLoggedIn(request))
                  {
                      redirectTo(request, "/login");
                      return;
                  }
                  sendFile(request, "/assets/portal_setting.html", "text/html"); });

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
              { redirectTo(request, "/home"); });

    server.on("/api/me", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                  if (!isLoggedIn(request))
                  {
                      request->send(401, "application/json", "{\"ok\":false}");
                      return;
                  }
                  request->send(200, "application/json", "{\"ok\":true}"); });

    server.on("/api/login", HTTP_POST, [](AsyncWebServerRequest *request)
              {
                  String user;
                  String pass;

                  if (request->hasParam("user", true))
                      user = request->getParam("user", true)->value();
                  if (request->hasParam("pass", true))
                      pass = request->getParam("pass", true)->value();

                  if (user == "admin" && pass == "78baLitni89")
                  {
                      g_sid = String((uint32_t)esp_random(), HEX) + String((uint32_t)esp_random(), HEX);
                      AsyncWebServerResponse *res = request->beginResponse(200, "application/json", "{\"ok\":true}");
                      res->addHeader("Set-Cookie", "SID=" + g_sid + "; Path=/");
                      request->send(res);
                      return;
                  }

                  request->send(403, "application/json", "{\"ok\":false}"); });

    server.on("/api/logout", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                  AsyncWebServerResponse *res = request->beginResponse(200, "application/json", "{\"ok\":true}");
                  res->addHeader("Set-Cookie", "SID=deleted; Path=/; Max-Age=0");
                  request->send(res); });

    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                  if (!isLoggedIn(request))
                  {
                      request->send(401, "application/json", "{\"ok\":false}");
                      return;
                  }

                  auto &cfg = Config.get();
                  String id = (cfg.iddev[0] == '\0') ? String("") : String(cfg.iddev);
                  String ip = getIpString();
                  bool online = Wifi.isConnected();
                  uint32_t pending = Sync.getPendingCount();

                  String out = "{\"ok\":true";
                  out += ",\"iddev\":" + String("\"") + id + "\"";
                  out += ",\"ip\":" + String("\"") + ip + "\"";
                  out += ",\"online\":" + String(online ? "true" : "false");
                  out += ",\"offlinePending\":" + String(pending);
                  out += "}";

                  request->send(200, "application/json", out); });

    server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                  if (!isLoggedIn(request))
                  {
                      request->send(401, "application/json", "{\"ok\":false}");
                      return;
                  }

                  auto &cfg = Config.get();

                  String out = "{";
                  out += "\"ssid\":\"" + String(cfg.ssid) + "\",";
                  out += "\"password\":\"" + String(cfg.password) + "\",";
                  out += "\"dhcp\":\"" + String(cfg.dhcp ? "true" : "false") + "\",";
                  out += "\"ip\":\"" + String(cfg.ip) + "\",";
                  out += "\"gateway\":\"" + String(cfg.gateway) + "\",";
                  out += "\"subnet\":\"" + String(cfg.subnet) + "\",";
                  out += "\"host\":\"" + String(cfg.host) + "\",";
                  out += "\"port\":\"" + String(cfg.port) + "\",";
                  out += "\"topic_subscribe\":\"" + String(cfg.topic_subscribe) + "\",";
                  out += "\"topic_publish\":\"" + String(cfg.topic_publish) + "\",";
                  out += "\"iddev\":\"" + String(cfg.iddev) + "\",";
                  out += "\"offsetday\":\"" + String(cfg.offsetday) + "\",";
                  out += "\"modeDeviceData\":\"" + String((int)cfg.modeDeviceData) + "\"";
                  out += "}";

                  request->send(200, "application/json", out); });

    server.on("/api/config", HTTP_POST, [](AsyncWebServerRequest *request)
              {
                  if (!isLoggedIn(request))
                  {
                      request->send(401, "application/json", "{\"ok\":false}");
                      return;
                  }

                  auto &cfg = Config.get();

                  if (request->hasParam("ssid", true))
                      strlcpy(cfg.ssid, request->getParam("ssid", true)->value().c_str(), sizeof(cfg.ssid));
                  if (request->hasParam("password", true))
                      strlcpy(cfg.password, request->getParam("password", true)->value().c_str(), sizeof(cfg.password));
                  if (request->hasParam("dhcp", true))
                      cfg.dhcp = (request->getParam("dhcp", true)->value() == "true");
                  if (request->hasParam("ip", true))
                      strlcpy(cfg.ip, request->getParam("ip", true)->value().c_str(), sizeof(cfg.ip));
                  if (request->hasParam("gateway", true))
                      strlcpy(cfg.gateway, request->getParam("gateway", true)->value().c_str(), sizeof(cfg.gateway));
                  if (request->hasParam("subnet", true))
                      strlcpy(cfg.subnet, request->getParam("subnet", true)->value().c_str(), sizeof(cfg.subnet));
                  if (request->hasParam("host", true))
                      strlcpy(cfg.host, request->getParam("host", true)->value().c_str(), sizeof(cfg.host));
                  if (request->hasParam("port", true))
                      cfg.port = request->getParam("port", true)->value().toInt();
                  if (request->hasParam("topic_subscribe", true))
                      strlcpy(cfg.topic_subscribe, request->getParam("topic_subscribe", true)->value().c_str(), sizeof(cfg.topic_subscribe));
                  if (request->hasParam("topic_publish", true))
                      strlcpy(cfg.topic_publish, request->getParam("topic_publish", true)->value().c_str(), sizeof(cfg.topic_publish));
                  if (request->hasParam("iddev", true))
                      strlcpy(cfg.iddev, request->getParam("iddev", true)->value().c_str(), sizeof(cfg.iddev));
                  if (request->hasParam("offsetday", true))
                      cfg.offsetday = request->getParam("offsetday", true)->value().toInt();
                  if (request->hasParam("modeDeviceData", true))
                      cfg.modeDeviceData = (DeviceModeData)request->getParam("modeDeviceData", true)->value().toInt();

                  Config.setDefaultIfInvalid();
                  if (!Config.save())
                  {
                      request->send(500, "application/json", "{\"ok\":false}");
                      return;
                  }

                  request->send(200, "application/json", "{\"ok\":true}");
                  delay(1000);
                  Buzzer.reject();
                  delay(2000);
                  ESP.restart(); });

    server.on("/download/log", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                  if (!isLoggedIn(request))
                  {
                      redirectTo(request, "/login");
                      return;
                  }
                  streamDownload(request, "/log.bin", "log.bin"); });

    server.on("/download/akses", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                  if (!isLoggedIn(request))
                  {
                      redirectTo(request, "/login");
                      return;
                  }
                  streamDownload(request, "/access.bin", "access.bin"); });

    server.on("/download/offline", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                  if (!isLoggedIn(request))
                  {
                      redirectTo(request, "/login");
                      return;
                  }
                  streamDownload(request, "/offline_data.bin", "offline_data.bin"); });

    server.on("/api/sync/status", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                  if (!isLoggedIn(request))
                  {
                      request->send(401, "application/json", "{\"ok\":false}");
                      return;
                  }

                  uint32_t pending = Sync.getPendingCount();
                  
                  String out = "{\"ok\":true";
                  out += ",\"pending\":" + String(pending);
                  out += "}";

                  request->send(200, "application/json", out); });

    server.on("/api/sync/clear", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                  if (!isLoggedIn(request))
                  {
                      redirectTo(request, "/login");
                      return;
                  }
                  
                  bool ok = Sync.clearOfflineData();
                  
                  if (ok)
                  {
                      request->send(200, "application/json", "{\"ok\":true,\"message\":\"Offline data cleared\"}");
                  }
                  else
                  {
                      request->send(500, "application/json", "{\"ok\":false,\"message\":\"Failed to clear offline data\"}");
                  } });

    server.on("/reset", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                  if (!isLoggedIn(request))
                  {
                      redirectTo(request, "/login");
                      return;
                  }
                  SD.remove("/config.bin");
                  Buzzer.reset();
                  request->send(200, "text/plain", "Config cleared. Rebooting...");
                  delay(3000);
                  ESP.restart(); });

    server.onNotFound([](AsyncWebServerRequest *request)
                      {
                          if (request->url() == "/generate_204" || request->url() == "/fwlink")
                          {
                              redirectTo(request, "/home");
                              return;
                          }
                          redirectTo(request, "/home"); });
}

bool PortalConfig::isActive() const { return g_active; }
bool PortalConfig::isApMode() const { return g_apMode; }

bool PortalConfig::beginApOnHold(uint8_t buttonPin, uint32_t holdMs)
{
    pinMode(buttonPin, INPUT_PULLUP);

    if (digitalRead(buttonPin) == LOW)
        Serial.println("[Portal] Hold detected, waiting...");

    uint32_t start = millis();
    while (digitalRead(buttonPin) == LOW)
    {
        if (millis() - start >= holdMs)
        {
            Serial.println("[Portal] Hold time reached, starting AP...");
            startAp();
            Serial.println("[Portal] Starting server...");
            startServer();
            g_active = true;
            g_apMode = true;
            Serial.println("[Portal] Active in AP mode");
            return true;
        }
        delay(10);
    }

    return false;
}

bool PortalConfig::beginLan()
{
    if (g_serverStarted)
        return true;

    startServer();
    g_active = true;
    g_apMode = false;
    return true;
}

void PortalConfig::loop()
{
    if (!g_active)
        return;
    if (g_apMode)
        dns.processNextRequest();
}

void PortalConfig::startAp()
{
    Serial.println("[Portal] startAp()");
    SDCard_init();
    if (Config.load())
        Config.setDefaultIfInvalid();

    auto &cfg = Config.get();
    String name = (cfg.iddev[0] == '\0') ? String("device intilab") : (String("device ") + String(cfg.iddev));

    WiFi.mode(WIFI_AP);
    WiFi.softAP(name.c_str());
    delay(200);
    dns.start(53, "*", WiFi.softAPIP());
    Serial.println("[Portal] AP started: " + name + " " + WiFi.softAPIP().toString());
    Buzzer.reset();
}

void PortalConfig::startServer()
{
    if (g_serverStarted)
        return;

    Serial.println("[Portal] startServer()");

    SDCard_init();

    serverRoutes();

    server.begin();
    g_serverStarted = true;
}
