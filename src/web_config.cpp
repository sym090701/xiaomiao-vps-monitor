#include "web_config.h"

#include "api_client.h"
#include "config_store.h"

#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>

namespace {
WebServer server(80);
DNSServer dns;
AppConfig *activeConfig = nullptr;
bool routesRegistered = false;
bool serverStarted = false;
bool apActive = false;
bool rebootPending = false;
uint32_t rebootAt = 0;
String apSsid;
String apPassword;

String htmlEscape(const String &input) {
    String output;
    output.reserve(input.length() + 16);
    for (size_t i = 0; i < input.length(); i++) {
        switch (input[i]) {
            case '&': output += "&amp;"; break;
            case '<': output += "&lt;"; break;
            case '>': output += "&gt;"; break;
            case '"': output += "&quot;"; break;
            case '\'': output += "&#39;"; break;
            default: output += input[i]; break;
        }
    }
    return output;
}

String limitedArg(const char *name, size_t maximum) {
    String value = server.arg(name);
    value.trim();
    if (value.length() > maximum) value.remove(maximum);
    return value;
}

String configPage() {
    const bool isCf = activeConfig && activeConfig->backend == BackendType::CfServerMonitor;
    const String currentSsid = activeConfig ? htmlEscape(activeConfig->wifiSsid) : "";
    const String currentUrl = activeConfig ? htmlEscape(activeConfig->baseUrl) : "";
    const String currentIds = activeConfig ? htmlEscape(activeConfig->cfServerIds) : "";
    const unsigned refresh = activeConfig ? activeConfig->refreshSeconds : 15;
    const unsigned offline = activeConfig ? activeConfig->offlineSeconds : 90;
    const unsigned alertCpu = activeConfig ? activeConfig->cpuAlertPercent : 90;
    const unsigned alertMemory = activeConfig ? activeConfig->memoryAlertPercent : 90;
    const unsigned alertDisk = activeConfig ? activeConfig->diskAlertPercent : 90;
    const unsigned alertSeconds = activeConfig ? activeConfig->alertDurationSeconds : 60;

    String page;
    page.reserve(9000);
    page += F("<!doctype html><html lang='zh-CN'><head><meta charset='utf-8'>"
              "<meta name='viewport' content='width=device-width,initial-scale=1'>"
              "<title>小喵 VPS Monitor</title><style>"
              ":root{color-scheme:light dark;--bg:#f4f6f8;--panel:#fff;--text:#18212b;--muted:#5f6b78;--line:#d9dee5;--accent:#087e8b}"
              "@media(prefers-color-scheme:dark){:root{--bg:#101418;--panel:#1a2026;--text:#edf2f7;--muted:#a9b4bf;--line:#34404b;--accent:#32b8c6}}"
              "*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:15px system-ui,sans-serif}"
              "main{max-width:640px;margin:auto;padding:20px 16px 40px}h1{font-size:23px;margin:0 0 5px}p{color:var(--muted);line-height:1.5}"
              "section{background:var(--panel);border:1px solid var(--line);border-radius:8px;padding:18px;margin-top:16px}"
              "label{display:block;font-weight:650;margin:14px 0 6px}input,select{width:100%;height:42px;border:1px solid var(--line);border-radius:5px;padding:0 10px;background:var(--panel);color:var(--text);font:inherit}"
              ".row{display:grid;grid-template-columns:1fr 1fr;gap:12px}.hint{font-size:12px;color:var(--muted);margin-top:5px}"
              "button{width:100%;height:44px;margin-top:12px;border:0;border-radius:5px;background:var(--accent);color:white;font-weight:700;font-size:15px}.secondary{background:#52606d}"
              "pre{white-space:pre-wrap;background:var(--bg);border:1px solid var(--line);padding:10px;min-height:42px;color:var(--text)}"
              ".check{display:flex;align-items:center;gap:8px;font-weight:400}.check input{width:18px;height:18px}code{font-size:12px}"
              "@media(max-width:480px){.row{grid-template-columns:1fr}}"
              "</style></head><body><main><h1>小喵 VPS Monitor</h1>"
              "<p>保存后设备会重启。Token 和密码不会回显；留空会保留原值。</p>"
              "<form method='post' action='/save'><section><strong>网络</strong>"
              "<label for='ssid'>Wi-Fi 名称</label><input id='ssid' name='ssid' maxlength='63' required value='");
    page += currentSsid;
    page += F("'><label for='wifi_password'>Wi-Fi 密码</label>"
              "<input id='wifi_password' name='wifi_password' type='password' maxlength='64' autocomplete='new-password' placeholder='留空保持不变'>"
              "</section><section><strong>监控后端</strong><label for='backend'>类型</label><select id='backend' name='backend'>"
              "<option value='nezha'");
    if (!isCf) page += F(" selected");
    page += F(">哪吒探针</option><option value='cf'");
    if (isCf) page += F(" selected");
    page += F(">CF-Server-Monitor-Pro</option></select>"
              "<label for='url'>面板地址</label><input id='url' name='url' maxlength='240' required placeholder='https://status.example.com' value='");
    page += currentUrl;
    page += F("'><div class='hint'>填面板根地址，不要填具体 API 路径。</div>"
              "<label for='token'>Token / API_SECRET</label><input id='token' name='token' type='password' maxlength='256' autocomplete='new-password' placeholder='留空保持不变'>"
              "<div class='hint'>哪吒使用仅含 <code>nezha:inventory:read</code> 的 PAT；CF 私有面板填写 API_SECRET，公开面板可留空。</div>"
              "<label class='check'><input type='checkbox' name='clear_token' value='1'>清除已保存的 Token / API_SECRET</label>"
              "<label for='cf_ids'>CF 节点 ID</label><input id='cf_ids' name='cf_ids' maxlength='1024' placeholder='id1,id2,id3（公开首页可自动发现）' value='");
    page += currentIds;
    page += F("'><div class='hint'>CF 项目没有节点列表 JSON API。自动发现失败时，从详情页 URL 的 <code>?id=...</code> 复制，多个用逗号分隔。</div>"
              "<div class='row'><div><label for='refresh'>刷新秒数</label><input id='refresh' name='refresh' type='number' min='5' max='3600' value='");
    page += String(refresh);
    page += F("'></div><div><label for='offline'>离线阈值（秒）</label><input id='offline' name='offline' type='number' min='15' max='3600' value='");
    page += String(offline);
    page += F("'></div></div></section><section><strong>本地告警</strong>"
              "<div class='hint'>连续超限达到指定时长才告警；单项填 0 可关闭。</div>"
              "<div class='row'><div><label for='alert_cpu'>CPU (%)</label><input id='alert_cpu' name='alert_cpu' type='number' min='0' max='100' value='");
    page += String(alertCpu);
    page += F("'></div><div><label for='alert_mem'>内存 (%)</label><input id='alert_mem' name='alert_mem' type='number' min='0' max='100' value='");
    page += String(alertMemory);
    page += F("'></div><div><label for='alert_disk'>磁盘 (%)</label><input id='alert_disk' name='alert_disk' type='number' min='0' max='100' value='");
    page += String(alertDisk);
    page += F("'></div><div><label for='alert_sec'>持续秒数</label><input id='alert_sec' name='alert_sec' type='number' min='15' max='3600' value='");
    page += String(alertSeconds);
    page += F("'></div></div></section><button class='secondary' id='test' type='button'>测试当前连接</button>"
              "<pre id='test_result'>尚未测试</pre><button type='submit'>保存并重启</button></form>"
              "<p class='hint'>测试不会保存配置或重启，使用设备当前已连接的 Wi-Fi。设备端：总览用上下选择、A 进入、短按 B 刷新；详情用左右切节点、上下切页面、A 刷新、短按 B 返回。长按 B 重新配网，长按 A+B 重启。</p>"
              "<script>const f=document.querySelector('form'),b=document.querySelector('#test'),o=document.querySelector('#test_result');"
              "b.onclick=async()=>{b.disabled=true;o.textContent='测试中...';try{const r=await fetch('/test',{method:'POST',body:new FormData(f)});o.textContent=await r.text()}catch(e){o.textContent='FAIL '+e}b.disabled=false}</script>"
              "</main></body></html>");
    return page;
}

void handleSave() {
    if (!activeConfig) {
        server.send(500, "text/plain", "Configuration unavailable");
        return;
    }

    AppConfig updated = *activeConfig;
    updated.wifiSsid = limitedArg("ssid", 63);
    const String wifiPassword = limitedArg("wifi_password", 64);
    if (wifiPassword.length()) updated.wifiPassword = wifiPassword;
    updated.backend = server.arg("backend") == "cf"
                          ? BackendType::CfServerMonitor
                          : BackendType::Nezha;
    updated.baseUrl = limitedArg("url", 240);
    while (updated.baseUrl.endsWith("/")) updated.baseUrl.remove(updated.baseUrl.length() - 1);
    const String token = limitedArg("token", 256);
    if (server.hasArg("clear_token")) updated.token = "";
    else if (token.length()) updated.token = token;
    updated.cfServerIds = limitedArg("cf_ids", 1024);
    updated.refreshSeconds = constrain(server.arg("refresh").toInt(), 5, 3600);
    updated.offlineSeconds = constrain(server.arg("offline").toInt(), 15, 3600);
    updated.cpuAlertPercent = constrain(server.arg("alert_cpu").toInt(), 0, 100);
    updated.memoryAlertPercent = constrain(server.arg("alert_mem").toInt(), 0, 100);
    updated.diskAlertPercent = constrain(server.arg("alert_disk").toInt(), 0, 100);
    updated.alertDurationSeconds = constrain(server.arg("alert_sec").toInt(), 15, 3600);

    if (!updated.isComplete() ||
        (!updated.baseUrl.startsWith("https://") && !updated.baseUrl.startsWith("http://"))) {
        server.send(400, "text/plain; charset=utf-8", "Wi-Fi、面板地址不能为空，地址必须以 http:// 或 https:// 开头。");
        return;
    }
    if (!saveConfig(updated)) {
        server.send(500, "text/plain; charset=utf-8", "保存失败，请重试。");
        return;
    }

    *activeConfig = updated;
    server.send(200, "text/html; charset=utf-8",
                "<!doctype html><meta charset='utf-8'><meta name='viewport' content='width=device-width'><h2>保存成功</h2><p>设备正在重启，可以关闭本页。</p>");
    rebootPending = true;
    rebootAt = millis() + 1200;
}

void handleTest() {
    if (!activeConfig) {
        server.send(500, "text/plain; charset=utf-8", "FAIL configuration unavailable");
        return;
    }
    AppConfig candidate = *activeConfig;
    candidate.backend = server.arg("backend") == "cf"
                            ? BackendType::CfServerMonitor
                            : BackendType::Nezha;
    candidate.baseUrl = limitedArg("url", 240);
    while (candidate.baseUrl.endsWith("/")) candidate.baseUrl.remove(candidate.baseUrl.length() - 1);
    const String token = limitedArg("token", 256);
    if (server.hasArg("clear_token")) candidate.token = "";
    else if (token.length()) candidate.token = token;
    candidate.cfServerIds = limitedArg("cf_ids", 1024);

    ConnectionTestResult result = testServerConnection(candidate);
    String response;
    response.reserve(512);
    for (const String &stage : result.stages) response += stage + "\n";
    response += result.success ? "PASS connection test" : "FAIL connection test";
    server.send(result.success ? 200 : 422, "text/plain; charset=utf-8", response);
}

void registerRoutes() {
    if (routesRegistered) return;
    server.on("/", HTTP_GET, []() { server.send(200, "text/html; charset=utf-8", configPage()); });
    server.on("/save", HTTP_POST, handleSave);
    server.on("/test", HTTP_POST, handleTest);
    server.on("/generate_204", HTTP_ANY, []() { server.sendHeader("Location", "/", true); server.send(302, "text/plain", ""); });
    server.on("/hotspot-detect.html", HTTP_ANY, []() { server.sendHeader("Location", "/", true); server.send(302, "text/plain", ""); });
    server.onNotFound([]() { server.sendHeader("Location", "/", true); server.send(302, "text/plain", ""); });
    routesRegistered = true;
}

void deriveAccessPointCredentials() {
    const uint32_t suffix = static_cast<uint32_t>(ESP.getEfuseMac() & 0xFFFFFF);
    char suffixText[7];
    snprintf(suffixText, sizeof(suffixText), "%06X", suffix);
    apSsid = "XiaoMiao-VPS-" + String(suffixText);
    apPassword = "xm" + String(suffixText);
}
}  // namespace

void startWebConfig(AppConfig &config) {
    activeConfig = &config;
    registerRoutes();
    if (!serverStarted) {
        server.begin();
        serverStarted = true;
    }
}

void startConfigurationAccessPoint() {
    if (apActive) return;
    if (!apSsid.length()) deriveAccessPointCredentials();
    WiFi.mode(WIFI_AP_STA);
    if (!WiFi.softAP(apSsid.c_str(), apPassword.c_str())) return;
    dns.start(53, "*", WiFi.softAPIP());
    apActive = true;
}

void serviceWebConfig() {
    if (apActive) dns.processNextRequest();
    if (serverStarted) server.handleClient();
    if (rebootPending && static_cast<int32_t>(millis() - rebootAt) >= 0) ESP.restart();
}

bool configurationAccessPointActive() {
    return apActive;
}

String configurationAccessPointSsid() {
    if (!apSsid.length()) deriveAccessPointCredentials();
    return apSsid;
}

String configurationAccessPointPassword() {
    if (!apPassword.length()) deriveAccessPointCredentials();
    return apPassword;
}
