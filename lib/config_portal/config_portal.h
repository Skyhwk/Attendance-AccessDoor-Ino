#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <SD.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>

#include "sdcard.h"
#include "config_manager.h"
#include "wifi_manager.h"

class ConfigPortal
{
public:
    bool isActive() const { return _active; }
    bool isApMode() const { return _apMode; }

    bool beginLan()
    {
        if (_serverStarted)
            return true;

        Serial.println("Starting Config Portal (LAN)...");
        startServer();
        _active = true;
        _apMode = false;
        return true;
    }

    bool begin(uint8_t buttonPin, uint32_t holdMs = 10000)
    {
        pinMode(buttonPin, INPUT_PULLUP);

        uint32_t start = millis();
        while (digitalRead(buttonPin) == LOW)
        {
            if (millis() - start >= holdMs)
            {
                startAP();
                startServer();
                _active = true;
                _apMode = true;
                return true;
            }
            delay(10);
        }

        return false;
    }

    void loop()
    {
        if (!_active)
            return;

        if (_apMode)
            _dns.processNextRequest();
    }

private:
    void startAP()
    {
        SDCard_init();
        if (Config.load())
            Config.setDefaultIfInvalid();

        auto &cfg = Config.get();
        String name = (cfg.iddev[0] == '\0') ? String("device intilab") : (String("device ") + String(cfg.iddev));

        WiFi.mode(WIFI_AP);
        WiFi.softAP(name.c_str());
        delay(200);
        _dns.start(53, "*", WiFi.softAPIP());
        _apMode = true;
    }

    bool isLoggedIn(AsyncWebServerRequest *request)
    {
        if (!request->hasHeader("Cookie"))
            return false;

        String cookie = request->header("Cookie");
        return cookie.indexOf("SID=" + _sid) >= 0;
    }

    void redirect(AsyncWebServerRequest *request, const String &to)
    {
        request->redirect(to);
    }

    String htmlHeader(const String &title)
    {
        String h;
        h.reserve(1024);
        h += "<!doctype html><html><head><meta charset='utf-8'>";
        h += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
        h += "<title>" + title + "</title>";
        h += "<style>";
        h += "body{font-family:Arial,Helvetica,sans-serif;background:#0b1220;color:#e5e7eb;margin:0}";
        h += ".wrap{max-width:520px;margin:0 auto;padding:16px}";
        h += ".card{background:#111827;border:1px solid #1f2937;border-radius:12px;padding:16px;margin:12px 0}";
        h += ".h1{font-size:18px;margin:0 0 10px 0}";
        h += ".row{display:flex;justify-content:space-between;gap:12px;margin:8px 0}";
        h += ".label{color:#9ca3af}";
        h += "input,select{width:100%;padding:10px;border-radius:10px;border:1px solid #243041;background:#0b1220;color:#e5e7eb}";
        h += ".btn{display:block;width:100%;padding:11px;border-radius:10px;border:1px solid #243041;background:#1f2937;color:#e5e7eb;text-decoration:none;text-align:center;margin:10px 0;cursor:pointer}";
        h += ".btnP{background:#2563eb;border-color:#1d4ed8}";
        h += ".btnD{background:#111827}";
        h += ".err{color:#fca5a5}";
        h += "</style></head><body><div class='wrap'>";
        return h;
    }

    String htmlFooter()
    {
        return "</div></body></html>";
    }

    String getLocalIpString()
    {
        if (WiFi.getMode() == WIFI_AP)
            return WiFi.softAPIP().toString();
        return WiFi.localIP().toString();
    }

    String homePage()
    {
        auto &cfg = Config.get();
        String id = (cfg.iddev[0] == '\0') ? String("-") : String(cfg.iddev);
        String ip = getLocalIpString();
        String st = Wifi.isConnected() ? String("Online") : String("Offline");

        String h = htmlHeader("Home");
        h += "<div class='card'><div class='h1'>Home</div>";
        h += "<div class='row'><div class='label'>Device ID</div><div>" + id + "</div></div>";
        h += "<div class='row'><div class='label'>IP</div><div>" + ip + "</div></div>";
        h += "<div class='row'><div class='label'>Status</div><div>" + st + "</div></div>";
        h += "</div>";

        h += "<a class='btn btnP' href='/setting'>Setting</a>";
        h += "<a class='btn btnD' href='/download/log'>Download Log</a>";
        h += "<a class='btn btnD' href='/download/akses'>Download Akses</a>";
        h += "<a class='btn' href='/reset'>Reset</a>";
        h += "<a class='btn' href='/logout'>Logout</a>";
        h += htmlFooter();
        return h;
    }

    String loginPage(bool error)
    {
        String h = htmlHeader("Login");
        h += "<div class='card'><div class='h1'>Login</div>";
        if (error)
            h += "<div class='err'>Login gagal</div>";
        h += "<form method='POST' action='/login'>";
        h += "<div style='margin:10px 0'><input name='user' placeholder='Username' value='admin'></div>";
        h += "<div style='margin:10px 0'><input name='pass' placeholder='Password' type='password'></div>";
        h += "<button class='btn btnP' type='submit'>Masuk</button>";
        h += "</form></div>";
        h += htmlFooter();
        return h;
    }

    String settingPage()
    {
        auto &cfg = Config.get();

        String h = htmlHeader("Setting");
        h += "<div class='card'><div class='h1'>Setting</div>";
        h += "<form method='POST' action='/save-config'>";

        h += "<div style='margin:10px 0'><div class='label'>SSID</div><input name='ssid' value='" + String(cfg.ssid) + "'></div>";
        h += "<div style='margin:10px 0'><div class='label'>Password</div><input name='password' type='password' value='" + String(cfg.password) + "'></div>";

        h += "<div style='margin:10px 0'><div class='label'>DHCP</div><select name='dhcp'>";
        h += String("<option value='true'") + (cfg.dhcp ? " selected" : "") + ">true</option>";
        h += String("<option value='false'") + (!cfg.dhcp ? " selected" : "") + ">false</option>";
        h += "</select></div>";

        h += "<div style='margin:10px 0'><div class='label'>IP</div><input name='ip' value='" + String(cfg.ip) + "'></div>";
        h += "<div style='margin:10px 0'><div class='label'>Gateway</div><input name='gateway' value='" + String(cfg.gateway) + "'></div>";
        h += "<div style='margin:10px 0'><div class='label'>Subnet</div><input name='subnet' value='" + String(cfg.subnet) + "'></div>";

        h += "<div style='margin:10px 0'><div class='label'>Host</div><input name='host' value='" + String(cfg.host) + "'></div>";
        h += "<div style='margin:10px 0'><div class='label'>Port</div><input name='port' value='" + String(cfg.port) + "'></div>";

        h += "<div style='margin:10px 0'><div class='label'>Topic Subscribe</div><input name='topic_subscribe' value='" + String(cfg.topic_subscribe) + "'></div>";
        h += "<div style='margin:10px 0'><div class='label'>Topic Publish</div><input name='topic_publish' value='" + String(cfg.topic_publish) + "'></div>";

        h += "<div style='margin:10px 0'><div class='label'>Device ID</div><input name='iddev' value='" + String(cfg.iddev) + "'></div>";
        h += "<div style='margin:10px 0'><div class='label'>Offset Day</div><input name='offsetday' value='" + String(cfg.offsetday) + "'></div>";

        h += "<div style='margin:10px 0'><div class='label'>Mode Device Data</div><select name='modeDeviceData'>";
        h += String("<option value='0'") + (cfg.modeDeviceData == MODE_ACCESS_DOOR ? " selected" : "") + ">Akses Pintu</option>";
        h += String("<option value='1'") + (cfg.modeDeviceData == MODE_ATTENDANCE ? " selected" : "") + ">Absensi</option>";
        h += "</select></div>";

        h += "<button class='btn btnP' type='submit'>Simpan</button>";
        h += "</form>";
        h += "<a class='btn' href='/home'>Kembali</a>";
        h += "</div>";
        h += htmlFooter();
        return h;
    }

    void streamFile(AsyncWebServerRequest *request, const char *path, const char *downloadName)
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

    void startServer()
    {
        if (_serverStarted)
            return;

        Serial.println("Config Portal WebServer starting on port 80");

        _server.on("/", HTTP_GET, [this](AsyncWebServerRequest *request)
                   { redirect(request, "/home"); });

        _server.on("/login", HTTP_GET, [this](AsyncWebServerRequest *request)
                   { request->send(200, "text/html", loginPage(false)); });

        _server.on("/login", HTTP_POST, [this](AsyncWebServerRequest *request)
                   {
                       String user;
                       String pass;

                       if (request->hasParam("user", true))
                           user = request->getParam("user", true)->value();
                       if (request->hasParam("pass", true))
                           pass = request->getParam("pass", true)->value();

                       if (user == "admin" && pass == "78baLitni89")
                       {
                           _sid = String((uint32_t)esp_random(), HEX) + String((uint32_t)esp_random(), HEX);
                           AsyncWebServerResponse *res = request->beginResponse(302);
                           res->addHeader("Location", "/home");
                           res->addHeader("Set-Cookie", "SID=" + _sid + "; Path=/");
                           request->send(res);
                       }
                       else
                       {
                           request->send(200, "text/html", loginPage(true));
                       } });

        _server.on("/logout", HTTP_GET, [this](AsyncWebServerRequest *request)
                   {
                       AsyncWebServerResponse *res = request->beginResponse(302);
                       res->addHeader("Location", "/login");
                       res->addHeader("Set-Cookie", "SID=deleted; Path=/; Max-Age=0");
                       request->send(res); });

        _server.on("/home", HTTP_GET, [this](AsyncWebServerRequest *request)
                   {
                       if (!isLoggedIn(request))
                       {
                           redirect(request, "/login");
                           return;
                       }
                       request->send(200, "text/html", homePage()); });

        _server.on("/setting", HTTP_GET, [this](AsyncWebServerRequest *request)
                   {
                       if (!isLoggedIn(request))
                       {
                           redirect(request, "/login");
                           return;
                       }
                       request->send(200, "text/html", settingPage()); });

        _server.on("/save-config", HTTP_POST, [this](AsyncWebServerRequest *request)
                   {
                       if (!isLoggedIn(request))
                       {
                           redirect(request, "/login");
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
                       bool ok = Config.save();

                       if (!ok)
                       {
                           request->send(500, "text/plain", "Failed to save config");
                           return;
                       }

                       AsyncWebServerResponse *res = request->beginResponse(200, "text/html", "<meta http-equiv='refresh' content='2;url=/home'>Saved. Rebooting...");
                       request->send(res);
                       delay(300);
                       ESP.restart(); });

        _server.on("/download/log", HTTP_GET, [this](AsyncWebServerRequest *request)
                   {
                       if (!isLoggedIn(request))
                       {
                           redirect(request, "/login");
                           return;
                       }
                       streamFile(request, "/log.bin", "log.bin"); });

        _server.on("/download/akses", HTTP_GET, [this](AsyncWebServerRequest *request)
                   {
                       if (!isLoggedIn(request))
                       {
                           redirect(request, "/login");
                           return;
                       }
                       streamFile(request, "/access.bin", "access.bin"); });

        _server.on("/reset", HTTP_GET, [this](AsyncWebServerRequest *request)
                   {
                       if (!isLoggedIn(request))
                       {
                           redirect(request, "/login");
                           return;
                       }
                       request->send(200, "text/plain", "Rebooting...");
                       delay(300);
                       ESP.restart(); });

        _server.onNotFound([this](AsyncWebServerRequest *request)
                           {
                              if (request->url() == "/generate_204" || request->url() == "/fwlink")
                              {
                                  redirect(request, "/home");
                                  return;
                              }
                              redirect(request, "/home"); });

        _server.begin();
        _serverStarted = true;

        Serial.println("Config Portal WebServer started");
    }

    bool _active = false;
    bool _apMode = false;
    bool _serverStarted = false;
    String _sid;
    DNSServer _dns;
    AsyncWebServer _server{80};
};

inline ConfigPortal Portal;
