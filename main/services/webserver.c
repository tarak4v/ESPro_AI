/**
 * @file webserver.c
 * @brief HTTP config portal — full configuration UI served from ESP32.
 */

#include "services/webserver.h"
#include "services/config_manager.h"
#include "services/wifi_manager.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "webserver";
static httpd_handle_t s_server = NULL;

/* ────────────────────────────────────────────────────────────────────
 *  HTML page — embedded as a string constant.
 *  Responsive single-page config UI with pastel styling.
 * ──────────────────────────────────────────────────────────────────── */
static const char CONFIG_PAGE[] =
    "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>ESPro AI Config</title>"
    "<style>"
    "*{box-sizing:border-box;margin:0;padding:0}"
    "body{font-family:'Segoe UI',system-ui,sans-serif;background:#1a1a2e;color:#e0e0e0;padding:16px;max-width:720px;margin:0 auto}"
    "h1{text-align:center;color:#4CAF50;margin:12px 0;font-size:1.4em}"
    "h2{font-size:1.1em;color:#aaa;border-bottom:1px solid #333;padding-bottom:6px;margin:16px 0 10px}"
    ".card{background:#262640;border-radius:10px;padding:14px;margin:10px 0}"
    "label{display:block;font-size:.85em;color:#999;margin:8px 0 3px}"
    "input[type=text],input[type=password],select{width:100%;padding:8px 10px;border:1px solid #444;border-radius:6px;"
    "background:#1a1a2e;color:#e0e0e0;font-size:.9em;outline:none}"
    "input:focus,select:focus{border-color:#4CAF50}"
    "button,.btn{display:inline-block;padding:8px 18px;border:none;border-radius:6px;cursor:pointer;"
    "font-size:.9em;margin:4px 2px;color:#fff;background:#4CAF50}"
    "button:hover,.btn:hover{opacity:.85}"
    ".btn-scan{background:#2196F3}"
    ".btn-danger{background:#e53935}"
    ".btn-ota{background:#FF9800}"
    ".wifi-list{max-height:200px;overflow-y:auto;margin:8px 0}"
    ".wifi-item{padding:6px 10px;background:#1a1a2e;border-radius:4px;margin:3px 0;cursor:pointer;display:flex;justify-content:space-between}"
    ".wifi-item:hover{background:#333}"
    ".rssi{color:#888;font-size:.8em}"
    ".status{text-align:center;padding:6px;border-radius:6px;margin:8px 0;font-size:.85em}"
    ".status-ok{background:#1b5e20;color:#a5d6a7}"
    ".status-err{background:#b71c1c;color:#ef9a9a}"
    ".toggle{display:flex;align-items:center;gap:8px;margin:6px 0}"
    ".toggle input{width:auto}"
    ".ver{text-align:center;color:#555;font-size:.75em;margin-top:12px}"
    "@media(max-width:480px){body{padding:8px}}"
    "</style></head><body>"
    "<h1>&#x2699; ESPro AI Configuration</h1>"
    "<div id='msg'></div>"

    /* WiFi Section */
    "<div class='card'>"
    "<h2>WiFi</h2>"
    "<button class='btn-scan' onclick='scanWifi()'>Scan Networks</button>"
    "<div id='wifi-list' class='wifi-list'></div>"
    "<label>SSID</label><input type='text' id='wifi_ssid' placeholder='Network name'>"
    "<div class='toggle'><input type='checkbox' id='wifi_hidden'><label for='wifi_hidden'>Hidden SSID</label></div>"
    "<label>Password</label><input type='password' id='wifi_pass' placeholder='WiFi password'>"
    "<button onclick='connectWifi()'>Connect</button>"
    "<div id='wifi-status' style='margin-top:6px;font-size:.85em;color:#888'></div>"
    "</div>"

    /* Device Section */
    "<div class='card'>"
    "<h2>Device</h2>"
    "<label>Device Name</label><input type='text' id='device_name' maxlength='31'>"
    "</div>"

    /* AI Providers Section */
    "<div class='card'>"
    "<h2>AI Configuration</h2>"
    "<label>STT Provider</label>"
    "<select id='stt_provider'>"
    "<option value='0'>Groq</option>"
    "<option value='1'>OpenAI</option>"
    "<option value='2'>Claude</option>"
    "<option value='3'>Gemini</option>"
    "<option value='4'>HuggingFace</option>"
    "</select>"
    "<label>LLM Provider</label>"
    "<select id='llm_provider'>"
    "<option value='0'>Groq</option>"
    "<option value='1'>OpenAI</option>"
    "<option value='2'>Claude</option>"
    "<option value='3'>Gemini</option>"
    "<option value='4'>HuggingFace</option>"
    "</select>"
    "<label>Groq API Key</label><input type='password' id='api_key_groq' placeholder='gsk_...'>"
    "<label>OpenAI API Key</label><input type='password' id='api_key_openai' placeholder='sk-...'>"
    "<label>Claude API Key</label><input type='password' id='api_key_claude' placeholder='sk-ant-...'>"
    "<label>Gemini API Key</label><input type='password' id='api_key_gemini' placeholder='AI...'>"
    "<label>HuggingFace Token</label><input type='password' id='api_key_huggingface' placeholder='hf_...'>"
    "</div>"

    /* Theme Section */
    "<div class='card'>"
    "<h2>Theme</h2>"
    "<select id='theme_id'>"
    "<option value='0'>Dark</option>"
    "<option value='1'>Light</option>"
    "<option value='2'>Autumn</option>"
    "<option value='3'>Spring</option>"
    "<option value='4'>Monsoon</option>"
    "</select>"
    "</div>"

    /* OTA Section */
    "<div class='card'>"
    "<h2>OTA Update</h2>"
    "<label>Firmware URL</label><input type='text' id='ota_url' placeholder='https://...'>"
    "<div class='toggle'><input type='checkbox' id='ota_auto_check'><label for='ota_auto_check'>Auto-check on boot</label></div>"
    "<button class='btn-ota' onclick='triggerOta()'>Update Firmware</button>"
    "<div id='ota-status'></div>"
    "</div>"

    /* Actions */
    "<div class='card' style='text-align:center'>"
    "<button onclick='saveConfig()'>Save All Settings</button> "
    "<button class='btn-danger' onclick='factoryReset()'>Factory Reset</button>"
    "</div>"

    "<div class='ver' id='fw-ver'></div>"

    "<script>"
    "function $(id){return document.getElementById(id)}"
    "function msg(t,ok){$('msg').innerHTML='<div class=\"status '+(ok?'status-ok':'status-err')+'\">'+t+'</div>';setTimeout(()=>{$('msg').innerHTML=''},4000)}"

    "function loadConfig(){"
    "fetch('/api/config').then(r=>r.json()).then(c=>{"
    "$('wifi_ssid').value=c.wifi_ssid||'';"
    "$('wifi_pass').value=c.wifi_pass||'';"
    "$('wifi_hidden').checked=!!c.wifi_hidden;"
    "$('device_name').value=c.device_name||'';"
    "$('stt_provider').value=c.stt_provider||0;"
    "$('llm_provider').value=c.llm_provider||0;"
    "$('api_key_groq').value=c.api_key_groq||'';"
    "$('api_key_openai').value=c.api_key_openai||'';"
    "$('api_key_claude').value=c.api_key_claude||'';"
    "$('api_key_gemini').value=c.api_key_gemini||'';"
    "$('api_key_huggingface').value=c.api_key_huggingface||'';"
    "$('theme_id').value=c.theme_id||0;"
    "$('ota_url').value=c.ota_url||'';"
    "$('ota_auto_check').checked=!!c.ota_auto_check;"
    "$('wifi-status').textContent=c.sta_ip?'Connected: '+c.sta_ip:'Not connected';"
    "$('fw-ver').textContent='FW: '+c.fw_version;"
    "}).catch(e=>msg('Load failed: '+e,false))}"

    "function saveConfig(){"
    "var d={"
    "wifi_ssid:$('wifi_ssid').value,"
    "wifi_pass:$('wifi_pass').value,"
    "wifi_hidden:$('wifi_hidden').checked?1:0,"
    "device_name:$('device_name').value,"
    "stt_provider:parseInt($('stt_provider').value),"
    "llm_provider:parseInt($('llm_provider').value),"
    "api_key_groq:$('api_key_groq').value,"
    "api_key_openai:$('api_key_openai').value,"
    "api_key_claude:$('api_key_claude').value,"
    "api_key_gemini:$('api_key_gemini').value,"
    "api_key_huggingface:$('api_key_huggingface').value,"
    "theme_id:parseInt($('theme_id').value),"
    "ota_url:$('ota_url').value,"
    "ota_auto_check:$('ota_auto_check').checked?1:0"
    "};"
    "fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(d)})"
    ".then(r=>r.json()).then(r=>{msg(r.status==='ok'?'Settings saved!':'Error: '+r.error,r.status==='ok')})"
    ".catch(e=>msg('Save failed: '+e,false))}"

    "function scanWifi(){"
    "$('wifi-list').innerHTML='<div style=\"color:#888\">Scanning...</div>';"
    "fetch('/api/scan').then(r=>r.json()).then(r=>{"
    "var h='';"
    "if(r.networks&&r.networks.length){"
    "r.networks.forEach(n=>{"
    "h+='<div class=\"wifi-item\" onclick=\"$(\\x27wifi_ssid\\x27).value=\\x27'+n.ssid.replace(/'/g,\"\\\\'\")+'\\x27\">';"
    "h+='<span>'+n.ssid+(n.auth>0?' &#x1F512;':'')+'</span>';"
    "h+='<span class=\"rssi\">'+n.rssi+' dBm</span></div>';"
    "});}else{h='<div style=\"color:#888\">No networks found</div>';}"
    "$('wifi-list').innerHTML=h;"
    "}).catch(e=>{$('wifi-list').innerHTML='<div style=\"color:#e53935\">Scan error</div>'})}"

    "function connectWifi(){"
    "var d={ssid:$('wifi_ssid').value,pass:$('wifi_pass').value,hidden:$('wifi_hidden').checked?1:0};"
    "fetch('/api/wifi',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(d)})"
    ".then(r=>r.json()).then(r=>msg(r.status==='ok'?'Connecting to '+d.ssid+'...':'Error: '+r.error,r.status==='ok'))"
    ".catch(e=>msg('Connect failed: '+e,false))}"

    "function triggerOta(){"
    "if(!confirm('Update firmware now? Device will restart.'))return;"
    "msg('Starting OTA update...',true);"
    "fetch('/api/ota',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({url:$('ota_url').value})})"
    ".then(r=>r.json()).then(r=>{msg(r.status==='ok'?'OTA started, rebooting...':'OTA error: '+r.error,r.status==='ok')})"
    ".catch(e=>msg('OTA failed: '+e,false))}"

    "function factoryReset(){"
    "if(!confirm('Reset ALL settings to factory defaults?'))return;"
    "fetch('/api/reset',{method:'POST'}).then(r=>r.json()).then(r=>{"
    "msg('Factory reset done. Rebooting...',true);setTimeout(()=>location.reload(),3000)"
    "}).catch(e=>msg('Reset failed: '+e,false))}"

    "loadConfig();"
    "</script></body></html>";

/* ────────────────────────────────────────────────────────────────────
 *  REST API Handlers
 * ──────────────────────────────────────────────────────────────────── */

/* GET / — serve HTML page */
static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, CONFIG_PAGE, sizeof(CONFIG_PAGE) - 1);
    return ESP_OK;
}

/* GET /api/config — return all config as JSON */
static esp_err_t config_get_handler(httpd_req_t *req)
{
    const device_config_t *cfg = config_get();

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "wifi_ssid", cfg->wifi_ssid);
    cJSON_AddStringToObject(root, "wifi_pass", cfg->wifi_pass);
    cJSON_AddNumberToObject(root, "wifi_hidden", cfg->wifi_hidden ? 1 : 0);
    cJSON_AddStringToObject(root, "device_name", cfg->device_name);
    cJSON_AddNumberToObject(root, "stt_provider", (int)cfg->stt_provider);
    cJSON_AddNumberToObject(root, "llm_provider", (int)cfg->llm_provider);
    cJSON_AddStringToObject(root, "api_key_groq", cfg->api_key_groq);
    cJSON_AddStringToObject(root, "api_key_openai", cfg->api_key_openai);
    cJSON_AddStringToObject(root, "api_key_claude", cfg->api_key_claude);
    cJSON_AddStringToObject(root, "api_key_gemini", cfg->api_key_gemini);
    cJSON_AddStringToObject(root, "api_key_huggingface", cfg->api_key_huggingface);
    cJSON_AddNumberToObject(root, "theme_id", (int)cfg->theme_id);
    cJSON_AddStringToObject(root, "ota_url", cfg->ota_url);
    cJSON_AddNumberToObject(root, "ota_auto_check", cfg->ota_auto_check ? 1 : 0);
    cJSON_AddStringToObject(root, "sta_ip", wifi_get_sta_ip());
    cJSON_AddStringToObject(root, "ap_ip", wifi_get_ap_ip());

    /* Firmware version from app description */
    const esp_app_desc_t *app = esp_app_get_description();
    cJSON_AddStringToObject(root, "fw_version", app->version);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    free(json);
    return ESP_OK;
}

/* POST /api/config — set config values from JSON body */
static esp_err_t config_post_handler(httpd_req_t *req)
{
    int total = req->content_len;
    if (total <= 0 || total > 4096)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body");
        return ESP_FAIL;
    }

    char *buf = malloc(total + 1);
    if (!buf)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }

    int received = 0;
    while (received < total)
    {
        int ret = httpd_req_recv(req, buf + received, total - received);
        if (ret <= 0)
        {
            free(buf);
            return ESP_FAIL;
        }
        received += ret;
    }
    buf[total] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    /* Apply string fields */
    cJSON *item;
    const char *str_keys[] = {"wifi_ssid", "wifi_pass", "device_name",
                              "api_key_groq", "api_key_openai", "api_key_claude",
                              "api_key_gemini", "api_key_huggingface", "ota_url"};
    for (int i = 0; i < (int)(sizeof(str_keys) / sizeof(str_keys[0])); i++)
    {
        item = cJSON_GetObjectItem(root, str_keys[i]);
        if (cJSON_IsString(item))
        {
            config_set_str(str_keys[i], item->valuestring);
        }
    }

    /* Apply uint8 fields */
    const char *u8_keys[] = {"wifi_hidden", "stt_provider", "llm_provider", "theme_id", "ota_auto_check"};
    for (int i = 0; i < (int)(sizeof(u8_keys) / sizeof(u8_keys[0])); i++)
    {
        item = cJSON_GetObjectItem(root, u8_keys[i]);
        if (cJSON_IsNumber(item))
        {
            config_set_u8(u8_keys[i], (uint8_t)item->valueint);
        }
    }

    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

/* GET /api/scan — trigger WiFi scan and return results */
static esp_err_t scan_get_handler(httpd_req_t *req)
{
    /* Start scan and wait briefly for results */
    wifi_scan_start();
    vTaskDelay(pdMS_TO_TICKS(3000)); /* Wait for scan to complete */

    wifi_scan_entry_t entries[WIFI_SCAN_MAX_AP];
    uint16_t count = wifi_scan_get_results(entries, WIFI_SCAN_MAX_AP);

    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(root, "networks");
    for (uint16_t i = 0; i < count; i++)
    {
        cJSON *net = cJSON_CreateObject();
        cJSON_AddStringToObject(net, "ssid", entries[i].ssid);
        cJSON_AddNumberToObject(net, "rssi", entries[i].rssi);
        cJSON_AddNumberToObject(net, "auth", entries[i].authmode);
        cJSON_AddItemToArray(arr, net);
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    free(json);
    return ESP_OK;
}

/* POST /api/wifi — connect to a specific network */
static esp_err_t wifi_post_handler(httpd_req_t *req)
{
    int total = req->content_len;
    if (total <= 0 || total > 512)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body");
        return ESP_FAIL;
    }

    char *buf = malloc(total + 1);
    if (!buf)
        return ESP_FAIL;

    int received = 0;
    while (received < total)
    {
        int ret = httpd_req_recv(req, buf + received, total - received);
        if (ret <= 0)
        {
            free(buf);
            return ESP_FAIL;
        }
        received += ret;
    }
    buf[total] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *ssid_j = cJSON_GetObjectItem(root, "ssid");
    cJSON *pass_j = cJSON_GetObjectItem(root, "pass");
    cJSON *hide_j = cJSON_GetObjectItem(root, "hidden");

    if (!cJSON_IsString(ssid_j) || strlen(ssid_j->valuestring) == 0)
    {
        cJSON_Delete(root);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"status\":\"error\",\"error\":\"SSID required\"}");
        return ESP_OK;
    }

    const char *pass = cJSON_IsString(pass_j) ? pass_j->valuestring : "";
    bool hidden = cJSON_IsNumber(hide_j) && hide_j->valueint != 0;

    wifi_connect_to(ssid_j->valuestring, pass, hidden);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

/* Forward declaration — implemented in ota_manager.c */
extern bool ota_start_update(const char *url);

/* POST /api/ota — trigger firmware update */
static esp_err_t ota_post_handler(httpd_req_t *req)
{
    int total = req->content_len;
    if (total <= 0 || total > 512)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body");
        return ESP_FAIL;
    }

    char *buf = malloc(total + 1);
    if (!buf)
        return ESP_FAIL;

    int received = 0;
    while (received < total)
    {
        int ret = httpd_req_recv(req, buf + received, total - received);
        if (ret <= 0)
        {
            free(buf);
            return ESP_FAIL;
        }
        received += ret;
    }
    buf[total] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);

    const char *url = NULL;
    if (root)
    {
        cJSON *url_j = cJSON_GetObjectItem(root, "url");
        if (cJSON_IsString(url_j) && strlen(url_j->valuestring) > 0)
        {
            url = url_j->valuestring;
        }
    }

    /* Fallback to config OTA URL */
    if (!url || strlen(url) == 0)
    {
        url = config_get()->ota_url;
    }

    if (!url || strlen(url) == 0)
    {
        if (root)
            cJSON_Delete(root);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"status\":\"error\",\"error\":\"No OTA URL configured\"}");
        return ESP_OK;
    }

    /* Save URL to config if provided */
    if (root)
    {
        cJSON *url_j = cJSON_GetObjectItem(root, "url");
        if (cJSON_IsString(url_j) && strlen(url_j->valuestring) > 0)
        {
            config_set_str("ota_url", url_j->valuestring);
        }
    }

    /* Start OTA in background — ota_start_update will handle reboot */
    bool started = ota_start_update(url);
    if (root)
        cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    if (started)
    {
        httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    }
    else
    {
        httpd_resp_sendstr(req, "{\"status\":\"error\",\"error\":\"OTA start failed\"}");
    }
    return ESP_OK;
}

/* POST /api/reset — factory reset */
static esp_err_t reset_post_handler(httpd_req_t *req)
{
    config_factory_reset();

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");

    /* Schedule reboot */
    ESP_LOGW(TAG, "Factory reset — rebooting in 1s");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();

    return ESP_OK; /* unreachable */
}

/* ────────────────────────────────────────────────────────────────────
 *  Server start / stop
 * ──────────────────────────────────────────────────────────────────── */
void webserver_start(void)
{
    if (s_server)
        return;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;
    config.stack_size = 8192;
    config.lru_purge_enable = true;

    if (httpd_start(&s_server, &config) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return;
    }

    /* Register URI handlers */
    const httpd_uri_t uris[] = {
        {.uri = "/", .method = HTTP_GET, .handler = root_get_handler},
        {.uri = "/api/config", .method = HTTP_GET, .handler = config_get_handler},
        {.uri = "/api/config", .method = HTTP_POST, .handler = config_post_handler},
        {.uri = "/api/scan", .method = HTTP_GET, .handler = scan_get_handler},
        {.uri = "/api/wifi", .method = HTTP_POST, .handler = wifi_post_handler},
        {.uri = "/api/ota", .method = HTTP_POST, .handler = ota_post_handler},
        {.uri = "/api/reset", .method = HTTP_POST, .handler = reset_post_handler},
    };

    for (int i = 0; i < (int)(sizeof(uris) / sizeof(uris[0])); i++)
    {
        httpd_register_uri_handler(s_server, &uris[i]);
    }

    ESP_LOGI(TAG, "Config portal running on http://%s/ and http://%s/",
             wifi_get_ap_ip(), wifi_get_sta_ip());
}

void webserver_stop(void)
{
    if (s_server)
    {
        httpd_stop(s_server);
        s_server = NULL;
        ESP_LOGI(TAG, "HTTP server stopped");
    }
}
