#include "portal.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

#include "lwip/ip.h"
#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "lwip/udp.h"
#include "pico/cyw43_arch.h"

namespace {

constexpr ip4_addr_t kPortalAddress = IPADDR4_INIT_BYTES(192, 168, 4, 1);
constexpr size_t kRequestCapacity = 2048;
constexpr size_t kMaxRequestBody = 512;
constexpr size_t kResponseChunk = 512;

const char kPortalPage[] = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>LEDClock setup</title>
<style>
:root{color-scheme:light dark;--bg:#f3f6f8;--panel:#fff;--text:#17212b;--muted:#63727d;--accent:#007c91;--accent-strong:#005f70;--border:#d7e0e5;--shadow:0 18px 45px #102a3d18}
@media(prefers-color-scheme:dark){:root{--bg:#10171c;--panel:#192329;--text:#eef4f6;--muted:#a9b9c0;--accent:#49d3dd;--accent-strong:#8beaf0;--border:#35464f;--shadow:0 18px 45px #00000055}}
:root[data-theme=light]{color-scheme:light;--bg:#f3f6f8;--panel:#fff;--text:#17212b;--muted:#63727d;--accent:#007c91;--accent-strong:#005f70;--border:#d7e0e5;--shadow:0 18px 45px #102a3d18}
:root[data-theme=dark]{color-scheme:dark;--bg:#10171c;--panel:#192329;--text:#eef4f6;--muted:#a9b9c0;--accent:#49d3dd;--accent-strong:#8beaf0;--border:#35464f;--shadow:0 18px 45px #00000055}
*{box-sizing:border-box}body{margin:0;background:linear-gradient(135deg,var(--bg),color-mix(in srgb,var(--bg) 78%,var(--accent)));color:var(--text);font:16px/1.5 ui-sans-serif,system-ui,sans-serif;min-height:100vh}main{max-width:720px;margin:auto;padding:32px 18px 48px}.brand{display:flex;align-items:center;gap:14px;margin:8px 0 30px}.mark{width:42px;height:42px;border-radius:12px;background:linear-gradient(135deg,#ef476f,#ffd166 45%,#06d6a0 70%,#118ab2);box-shadow:0 8px 24px #007c9140}.eyebrow{color:var(--accent-strong);font-size:12px;font-weight:800;letter-spacing:.12em;text-transform:uppercase}.brand h1{font-size:24px;line-height:1.1;margin:2px 0}.panel{background:var(--panel);border:1px solid var(--border);border-radius:18px;box-shadow:var(--shadow);padding:24px}.intro{margin:0 0 24px;color:var(--muted)}.grid{display:grid;grid-template-columns:1fr auto;gap:16px}.field{margin-bottom:18px}.field.full{grid-column:1/-1}label{display:block;font-size:13px;font-weight:700;margin-bottom:7px}input,select{width:100%;min-height:44px;border:1px solid var(--border);border-radius:10px;background:var(--bg);color:var(--text);padding:9px 12px;font:inherit}input:focus,select:focus{outline:3px solid color-mix(in srgb,var(--accent) 28%,transparent);border-color:var(--accent)}button{min-height:44px;border:0;border-radius:10px;padding:9px 16px;background:var(--accent);color:#06252c;font:700 15px inherit;cursor:pointer}button:hover{background:var(--accent-strong);color:#fff}button.secondary{background:transparent;color:var(--accent-strong);border:1px solid var(--border)}.actions{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-top:8px}.status{min-height:24px;color:var(--muted);font-size:14px}.status.error{color:#d84b4b}.status.ok{color:#14805e}.note{font-size:13px;color:var(--muted);margin:18px 0 0}.password{display:flex;gap:8px}.password input{flex:1}.icon{min-width:44px;padding:8px;font-size:19px}.hidden{display:none}@media(max-width:560px){main{padding:22px 12px 36px}.panel{padding:18px;border-radius:14px}.grid{grid-template-columns:1fr}.field.full{grid-column:auto}.actions{align-items:stretch;flex-direction:column}.actions button{width:100%}}
</style>
</head>
<body><main>
<header class="brand"><div class="mark" aria-hidden="true"></div><div><div class="eyebrow">Device setup</div><h1>LEDClock</h1></div></header>
<section class="panel"><p class="intro">Connect LEDClock to your Wi-Fi network and choose the regional settings used for local time. Weather settings can be added later.</p>
<form id="settings"><div class="grid">
<div class="field"><label for="ssid">Wi-Fi network</label><select id="ssid" name="ssid"><option value="">Scanning for networks...</option></select></div>
<div class="field" style="align-self:end"><button class="secondary" id="rescan" type="button">Rescan</button></div>
<div class="field full hidden" id="manual-field"><label for="manual-ssid">Network name</label><input id="manual-ssid" maxlength="32" autocomplete="off" autocapitalize="none" spellcheck="false"></div>
<div class="field full"><label for="password">Password</label><div class="password"><input id="password" name="password" type="password" autocomplete="current-password"><button class="secondary icon" id="toggle" type="button" aria-label="Show password" aria-pressed="false">&#128065;</button></div></div>
<div class="field"><label for="timezone">Time zone</label><select id="timezone" name="timezone"><option value="UTC">UTC</option><option value="America/New_York">Eastern Time</option><option value="America/Chicago">Central Time</option><option value="America/Denver">Mountain Time</option><option value="America/Los_Angeles">Pacific Time</option></select></div>
<div class="field"><label for="zip">ZIP code <span class="note">(future weather)</span></label><input id="zip" name="zip" inputmode="numeric" maxlength="10" autocomplete="postal-code"></div>
<div class="field"><label for="theme">Appearance</label><select id="theme" name="theme"><option value="system">System default</option><option value="light">Light</option><option value="dark">Dark</option><option value="future" disabled>More themes later</option></select></div>
<div class="field"><label for="ntp">Time server</label><select id="ntp" name="ntp"><option value="pool.ntp.org">NTP Pool (default)</option><option value="time.google.com">Google</option><option value="time.cloudflare.com">Cloudflare</option><option value="time.nist.gov">NIST</option><option value="time.windows.com">Microsoft</option><option value="time.apple.com">Apple</option></select></div>
</div><div class="actions"><div id="status" class="status" role="status" aria-live="polite"></div><button id="save" type="submit">Save and connect</button></div><p class="note">Your password is used only to connect this device. It is not shown after saving.</p></form></section>
</main><script>
const $=id=>document.getElementById(id);
const root=document.documentElement,theme=$('theme'),status=$('status'),ssid=$('ssid'),manualField=$('manual-field'),manualSsid=$('manual-ssid'),rescan=$('rescan'),password=$('password'),save=$('save');
let savedSsid='';
function setStatus(text,kind){status.className='status'+(kind?' '+kind:'');status.textContent=text}
function updateManual(){manualField.classList.toggle('hidden',ssid.value!=='')}ssid.addEventListener('change',updateManual);
function applyTheme(value){root.dataset.theme=value==='system'?'':value;localStorage.setItem('ledclock-theme',value)}
theme.value=localStorage.getItem('ledclock-theme')||'system';applyTheme(theme.value);theme.addEventListener('change',()=>applyTheme(theme.value));
$('toggle').addEventListener('click',event=>{const visible=password.type==='text';password.type=visible?'password':'text';event.currentTarget.setAttribute('aria-pressed',String(!visible));event.currentTarget.setAttribute('aria-label',visible?'Show password':'Hide password')});
async function loadSaved(){try{const data=await(await fetch('/api/settings')).json();if(data.ssid){savedSsid=data.ssid;manualSsid.value=data.ssid}if(data.timezone)$('timezone').value=data.timezone;if(data.zip)$('zip').value=data.zip;if(data.ntp)$('ntp').value=data.ntp;if(data.theme){theme.value=data.theme;applyTheme(data.theme)}if(data.hasPassword)password.placeholder='Leave blank to keep the saved password'}catch(error){}}
async function scan(){rescan.disabled=true;setStatus('Scanning for nearby networks. This takes a few seconds...');ssid.innerHTML='<option value="-">Scanning...</option>';updateManual();try{const data=await(await fetch('/api/scan')).json();const networks=data.networks||[];ssid.innerHTML='';networks.forEach(network=>{const option=document.createElement('option');option.value=network.ssid;option.textContent=network.ssid+(network.rssi?'  ('+network.rssi+' dBm)':'');ssid.appendChild(option)});const manual=document.createElement('option');manual.value='';manual.textContent=networks.length?'Other network...':'Enter network manually...';ssid.appendChild(manual);if(savedSsid)ssid.value=networks.some(network=>network.ssid===savedSsid)?savedSsid:'';setStatus(data.message||'Choose a network.')}catch(error){ssid.innerHTML='<option value="">Enter network manually...</option>';setStatus('Scan unavailable. Enter the network name manually.','error')}rescan.disabled=false;updateManual()}
const sleep=ms=>new Promise(resolve=>setTimeout(resolve,ms));
async function reachable(url){const controller=new AbortController(),timer=setTimeout(()=>controller.abort(),3000);try{await fetch(url,{mode:'no-cors',cache:'no-store',signal:controller.signal});return true}catch(error){return false}finally{clearTimeout(timer)}}
async function followTo(ip){const target='http://'+ip+'/',until=Date.now()+180000;while(Date.now()<until){if(await reachable(target+'api/status')){location.href=target;return}await sleep(2000)}setStatus('Could not reach LEDClock at '+target+'. Reconnect to your home Wi-Fi and open that address.','error')}
async function waitForResult(){const until=Date.now()+60000;while(Date.now()<until){await sleep(1000);try{const result=await(await fetch('/api/status',{cache:'no-store'})).json();if(result.state==='connected'){const moving=result.ip&&result.ip!==location.hostname;setStatus(result.message+(moving?' Opening http://'+result.ip+'/ once this device is back on that network...':''),'ok');if(moving)await followTo(result.ip);return}if(result.state==='failed'){setStatus(result.message,'error');return}setStatus(result.message||'Connecting...')}catch(error){setStatus('Waiting for LEDClock. Your device may briefly drop off the setup network.')}}setStatus('No answer from LEDClock. If the setup network has disappeared it connected successfully; otherwise try again.','error')}
rescan.addEventListener('click',scan);
$('settings').addEventListener('submit',async event=>{event.preventDefault();const payload=Object.fromEntries(new FormData(event.currentTarget));if(ssid.value==='')payload.ssid=manualSsid.value.trim();if(!payload.ssid||payload.ssid==='-'){setStatus('Choose or enter a network name.','error');return}save.disabled=true;setStatus('Sending settings...');try{const response=await fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(payload)});const data=await response.json().catch(()=>({}));if(response.ok){setStatus(data.message||'Connecting...');await waitForResult()}else{setStatus(data.message||'Unable to submit settings. Please try again.','error')}}catch(error){setStatus('Unable to submit settings. Please try again.','error')}save.disabled=false});
loadSaved().then(scan);
</script></body></html>)HTML";

constexpr size_t kMaxHttpClients = 4;
constexpr size_t kMaxScanNetworks = 20;

struct HttpClient {
    tcp_pcb* pcb = nullptr;
    char request[kRequestCapacity]{};
    size_t request_length = 0;
    bool request_handled = false;
    bool waiting_for_scan = false;
    const char* response_body = nullptr;
    size_t response_length = 0;
    size_t response_offset = 0;
    char response_header[256]{};
    size_t response_header_length = 0;
    size_t header_offset = 0;
    char body[384]{};
};

struct ScanNetwork {
    char ssid[32];
    uint8_t ssid_length;
    int16_t rssi;
    bool secure;
};

HttpClient http_clients[kMaxHttpClients]{};
tcp_pcb* http_listener = nullptr;

ScanNetwork scan_networks[kMaxScanNetworks]{};
size_t scan_count = 0;
bool scan_pending = false;
char scan_json[kMaxScanNetworks * 260 + 64];
size_t scan_json_length = 0;

const settings::Settings* saved_settings = nullptr;
settings::Settings submission{};
bool submission_ready = false;
portal::JoinState join_state = portal::JoinState::Idle;
const char* join_message = "";
char join_ip[16] = "";
udp_pcb* dns_listener = nullptr;
udp_pcb* dhcp_listener = nullptr;

constexpr uint8_t kPortalOctets[4] = {192, 168, 4, 1};
constexpr uint8_t kDhcpFirstHost = 16;
constexpr size_t kDhcpMaxLeases = 8;
constexpr size_t kDhcpOptionsOffset = 240;
constexpr size_t kDhcpReplyLength = 320;
constexpr uint32_t kDhcpLeaseSeconds = 24 * 60 * 60;
constexpr uint8_t kDhcpDiscover = 1;
constexpr uint8_t kDhcpOffer = 2;
constexpr uint8_t kDhcpRequest = 3;
constexpr uint8_t kDhcpAck = 5;
constexpr uint8_t kDhcpNak = 6;

struct DhcpLease {
    bool used;
    uint8_t mac[6];
};

DhcpLease dhcp_leases[kDhcpMaxLeases]{};
// Static to keep these off the small main stack (DHCP runs from cyw43_arch_poll()).
uint8_t dhcp_request[548];
uint8_t dhcp_reply[kDhcpReplyLength];

const char kScanFailedResponse[] = R"JSON({"networks":[],"message":"Wi-Fi scan could not start. Enter the network name manually."})JSON";
const char kPortalHost[] = "192.168.4.1";

void close_http_client(HttpClient& client) {
    if (client.pcb == nullptr) {
        return;
    }

    tcp_arg(client.pcb, nullptr);
    tcp_recv(client.pcb, nullptr);
    tcp_sent(client.pcb, nullptr);
    tcp_poll(client.pcb, nullptr, 0);
    tcp_err(client.pcb, nullptr);
    tcp_close(client.pcb);
    client = {};
}

void send_available(HttpClient& client) {
    if (client.pcb == nullptr) {
        return;
    }

    while (client.header_offset < client.response_header_length ||
           client.response_offset < client.response_length) {
        const bool sending_header = client.header_offset < client.response_header_length;
        const char* source = sending_header ? client.response_header : client.response_body;
        size_t& offset = sending_header ? client.header_offset : client.response_offset;
        const size_t length = sending_header ? client.response_header_length : client.response_length;
        const size_t available = static_cast<size_t>(tcp_sndbuf(client.pcb));
        if (available == 0) {
            break;
        }
        const size_t count = std::min({kResponseChunk, available, length - offset});
        if (tcp_write(client.pcb, source + offset, static_cast<u16_t>(count), TCP_WRITE_FLAG_COPY) != ERR_OK) {
            break;
        }
        offset += count;
    }

    tcp_output(client.pcb);
}

void prepare_response(HttpClient& client, const char* status, const char* content_type, const char* body,
                      size_t length, const char* extra_headers = "") {
    client.response_body = body;
    client.response_length = length;
    client.response_offset = 0;
    client.header_offset = 0;
    client.response_header_length = static_cast<size_t>(std::snprintf(
        client.response_header,
        sizeof(client.response_header),
        "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %u\r\n%sConnection: close\r\nCache-Control: no-store\r\n\r\n",
        status,
        content_type,
        static_cast<unsigned>(length),
        extra_headers));
    send_available(client);
}

const char* find_header_value(const char* request, const char* name) {
    const size_t name_length = std::strlen(name);
    for (const char* line = std::strstr(request, "\r\n"); line != nullptr; line = std::strstr(line + 2, "\r\n")) {
        const char* field = line + 2;
        if (field[0] == '\r') {
            return nullptr;
        }
        if (strncasecmp(field, name, name_length) == 0 && field[name_length] == ':') {
            const char* value = field + name_length + 1;
            while (*value == ' ') {
                ++value;
            }
            return value;
        }
    }
    return nullptr;
}

bool request_is_for_portal_host(const char* request) {
    const char* host = find_header_value(request, "host");
    if (host == nullptr) {
        return true;
    }
    const size_t length = sizeof(kPortalHost) - 1;
    return std::strncmp(host, kPortalHost, length) == 0 && (host[length] == '\r' || host[length] == ':');
}

bool request_is(const char* request, const char* method_and_path) {
    const size_t length = std::strlen(method_and_path);
    return std::strncmp(request, method_and_path, length) == 0 && (request[length] == ' ' || request[length] == '?');
}

struct JsonWriter {
    char* out;
    size_t capacity;
    size_t length = 0;

    void put(char c) {
        if (length < capacity) {
            out[length++] = c;
        }
    }
    void raw(const char* text) {
        while (*text != '\0') {
            put(*text++);
        }
    }
    void string(const char* text, size_t text_length) {
        put('"');
        for (size_t i = 0; i < text_length; ++i) {
            const auto c = static_cast<unsigned char>(text[i]);
            if (c == '"' || c == '\\') {
                put('\\');
                put(static_cast<char>(c));
            } else if (c < 0x20) {
                char escape[7];
                std::snprintf(escape, sizeof(escape), "\\u%04x", c);
                raw(escape);
            } else {
                put(static_cast<char>(c));
            }
        }
        put('"');
    }
    void string(const char* text) { string(text, std::strlen(text)); }
    void number(int value) {
        char digits[12];
        std::snprintf(digits, sizeof(digits), "%d", value);
        raw(digits);
    }
};

struct JsonReader {
    const char* position;
    const char* end;

    void skip_space() {
        while (position < end && (*position == ' ' || *position == '\t' || *position == '\r' || *position == '\n')) {
            ++position;
        }
    }
    bool consume(char expected) {
        skip_space();
        if (position < end && *position == expected) {
            ++position;
            return true;
        }
        return false;
    }
    // Decodes a JSON string into a NUL-terminated buffer; fails on bad syntax or if it does not fit.
    bool string(char* out, size_t capacity) {
        if (!consume('"')) {
            return false;
        }
        size_t length = 0;
        while (position < end) {
            const auto c = static_cast<unsigned char>(*position++);
            if (c == '"') {
                out[length] = '\0';
                return true;
            }
            if (c < 0x20) {
                return false;
            }
            uint32_t code = c;
            if (c == '\\') {
                if (position >= end) {
                    return false;
                }
                switch (*position++) {
                case '"': code = '"'; break;
                case '\\': code = '\\'; break;
                case '/': code = '/'; break;
                case 'b': code = '\b'; break;
                case 'f': code = '\f'; break;
                case 'n': code = '\n'; break;
                case 'r': code = '\r'; break;
                case 't': code = '\t'; break;
                case 'u':
                    if (end - position < 4) {
                        return false;
                    }
                    code = 0;
                    for (int i = 0; i < 4; ++i) {
                        const char h = *position++;
                        code <<= 4;
                        if (h >= '0' && h <= '9') {
                            code |= static_cast<uint32_t>(h - '0');
                        } else if (h >= 'a' && h <= 'f') {
                            code |= static_cast<uint32_t>(h - 'a' + 10);
                        } else if (h >= 'A' && h <= 'F') {
                            code |= static_cast<uint32_t>(h - 'A' + 10);
                        } else {
                            return false;
                        }
                    }
                    if (code == 0 || (code >= 0xd800 && code <= 0xdfff)) {
                        return false;
                    }
                    break;
                default:
                    return false;
                }
            }

            uint8_t bytes[3];
            size_t count = 1;
            if (c != '\\' || code < 0x80) {
                bytes[0] = static_cast<uint8_t>(code);
            } else if (code < 0x800) {
                bytes[0] = static_cast<uint8_t>(0xc0 | (code >> 6));
                bytes[1] = static_cast<uint8_t>(0x80 | (code & 0x3f));
                count = 2;
            } else {
                bytes[0] = static_cast<uint8_t>(0xe0 | (code >> 12));
                bytes[1] = static_cast<uint8_t>(0x80 | ((code >> 6) & 0x3f));
                bytes[2] = static_cast<uint8_t>(0x80 | (code & 0x3f));
                count = 3;
            }
            if (length + count >= capacity) {
                return false;
            }
            std::memcpy(out + length, bytes, count);
            length += count;
        }
        return false;
    }
};

// Accepts a flat JSON object of string values; returns nullptr on success or a user-facing error.
const char* parse_settings(const char* body, size_t length, settings::Settings& out) {
    constexpr const char* kInvalid = "Invalid request.";
    JsonReader reader{body, body + length};
    if (!reader.consume('{')) {
        return kInvalid;
    }
    if (reader.consume('}')) {
        return nullptr;
    }
    do {
        char key[16];
        if (!reader.string(key, sizeof(key)) || !reader.consume(':')) {
            return kInvalid;
        }
        char ignored[64];
        char* field = ignored;
        size_t capacity = sizeof(ignored);
        const char* error = kInvalid;
        if (std::strcmp(key, "ssid") == 0) {
            field = out.ssid;
            capacity = sizeof(out.ssid);
            error = "The network name must be 32 bytes or fewer.";
        } else if (std::strcmp(key, "password") == 0) {
            field = out.password;
            capacity = sizeof(out.password);
            error = "The Wi-Fi password must be 8 to 63 characters, or blank for an open network.";
        } else if (std::strcmp(key, "timezone") == 0) {
            field = out.timezone;
            capacity = sizeof(out.timezone);
            error = "Choose a supported time zone.";
        } else if (std::strcmp(key, "zip") == 0) {
            field = out.zip;
            capacity = sizeof(out.zip);
            error = "Enter a 5-digit ZIP code or ZIP+4, or leave it blank.";
        } else if (std::strcmp(key, "theme") == 0) {
            field = out.theme;
            capacity = sizeof(out.theme);
            error = "Choose a supported appearance.";
        } else if (std::strcmp(key, "ntp") == 0) {
            field = out.ntp_server;
            capacity = sizeof(out.ntp_server);
            error = "Choose a supported time server.";
        }
        if (!reader.string(field, capacity)) {
            return error;
        }
    } while (reader.consume(','));
    return reader.consume('}') ? nullptr : kInvalid;
}

void build_scan_json() {
    std::sort(scan_networks, scan_networks + scan_count,
              [](const ScanNetwork& a, const ScanNetwork& b) { return a.rssi > b.rssi; });

    JsonWriter json{scan_json, sizeof(scan_json)};
    json.raw("{\"networks\":[");
    for (size_t i = 0; i < scan_count; ++i) {
        if (i != 0) {
            json.put(',');
        }
        json.raw("{\"ssid\":");
        json.string(scan_networks[i].ssid, scan_networks[i].ssid_length);
        json.raw(",\"rssi\":");
        json.number(scan_networks[i].rssi);
        json.raw(scan_networks[i].secure ? ",\"secure\":true}" : ",\"secure\":false}");
    }
    json.raw("],\"message\":");
    json.string(scan_count == 0 ? "No networks found. Enter the network name manually." : "Choose a network.");
    json.put('}');
    scan_json_length = json.length;
}

int scan_result(void*, const cyw43_ev_scan_result_t* result) {
    if (result == nullptr || result->ssid_len == 0 || result->ssid_len > sizeof(result->ssid)) {
        return 0;
    }
    for (size_t i = 0; i < scan_count; ++i) {
        ScanNetwork& known = scan_networks[i];
        if (known.ssid_length == result->ssid_len && std::memcmp(known.ssid, result->ssid, result->ssid_len) == 0) {
            known.rssi = std::max(known.rssi, result->rssi);
            return 0;
        }
    }
    if (scan_count < kMaxScanNetworks) {
        ScanNetwork& added = scan_networks[scan_count++];
        std::memcpy(added.ssid, result->ssid, result->ssid_len);
        added.ssid_length = result->ssid_len;
        added.rssi = result->rssi;
        added.secure = result->auth_mode != CYW43_AUTH_OPEN;
    }
    return 0;
}

bool start_scan() {
    if (scan_pending) {
        return true;
    }
    scan_count = 0;
    cyw43_wifi_scan_options_t options{};
    const int result = cyw43_wifi_scan(&cyw43_state, &options, nullptr, scan_result);
    if (result != 0) {
        std::printf("Wi-Fi scan failed to start (%d)\n", result);
        return false;
    }
    scan_pending = true;
    return true;
}

void respond_json(HttpClient& client, const char* status, const JsonWriter& json) {
    prepare_response(client, status, "application/json", client.body, json.length);
}

void respond_message(HttpClient& client, const char* status, bool ok, const char* message) {
    JsonWriter json{client.body, sizeof(client.body)};
    json.raw(ok ? "{\"ok\":true,\"message\":" : "{\"ok\":false,\"message\":");
    json.string(message);
    json.put('}');
    respond_json(client, status, json);
}

void handle_get_settings(HttpClient& client) {
    JsonWriter json{client.body, sizeof(client.body)};
    json.put('{');
    if (saved_settings != nullptr) {
        json.raw("\"ssid\":");
        json.string(saved_settings->ssid);
        json.raw(",\"timezone\":");
        json.string(saved_settings->timezone);
        json.raw(",\"zip\":");
        json.string(saved_settings->zip);
        json.raw(",\"theme\":");
        json.string(saved_settings->theme);
        json.raw(",\"ntp\":");
        json.string(saved_settings->ntp_server);
        json.raw(saved_settings->password[0] != '\0' ? ",\"hasPassword\":true" : ",\"hasPassword\":false");
    }
    json.put('}');
    respond_json(client, "200 OK", json);
}

void handle_status(HttpClient& client) {
    static const char* const kStateNames[] = {"idle", "connecting", "connected", "failed"};
    JsonWriter json{client.body, sizeof(client.body)};
    json.raw("{\"state\":");
    json.string(kStateNames[static_cast<int>(join_state)]);
    json.raw(",\"message\":");
    json.string(join_message);
    json.raw(",\"ip\":");
    json.string(join_ip);
    json.put('}');
    respond_json(client, "200 OK", json);
}

void handle_post_settings(HttpClient& client, const char* body, size_t body_length) {
    // Requiring a JSON content type forces a CORS preflight, which blocks cross-site form posts.
    const char* content_type = find_header_value(client.request, "content-type");
    if (content_type == nullptr || strncasecmp(content_type, "application/json", 16) != 0) {
        respond_message(client, "415 Unsupported Media Type", false, "Settings must be sent as JSON.");
        return;
    }
    if (submission_ready || join_state == portal::JoinState::Connecting) {
        respond_message(client, "409 Conflict", false, "A connection attempt is already in progress.");
        return;
    }

    settings::Settings parsed{};
    std::strcpy(parsed.theme, "system");
    std::strcpy(parsed.ntp_server, settings::kDefaultNtpServer);
    const char* error = parse_settings(body, body_length, parsed);
    if (error == nullptr) {
        error = settings::validate(parsed);
    }
    if (error != nullptr) {
        respond_message(client, "400 Bad Request", false, error);
        return;
    }

    submission = parsed;
    submission_ready = true;
    join_state = portal::JoinState::Connecting;
    join_message = "Connecting to the network...";
    join_ip[0] = '\0';
    respond_message(client, "202 Accepted", true, join_message);
}

bool connection_is_on_setup_ap(const HttpClient& client) {
    return ip_2_ip4(&client.pcb->local_ip)->addr == kPortalAddress.addr;
}

void request_complete(HttpClient& client, const char* body, size_t body_length) {
    client.request_handled = true;
    if (connection_is_on_setup_ap(client) && !request_is_for_portal_host(client.request)) {
        prepare_response(client, "302 Found", "text/plain", "", 0, "Location: http://192.168.4.1/\r\n");
        return;
    }

    if (request_is(client.request, "GET /api/scan")) {
        if (start_scan()) {
            client.waiting_for_scan = true;
        } else {
            prepare_response(client, "200 OK", "application/json", kScanFailedResponse, sizeof(kScanFailedResponse) - 1);
        }
    } else if (request_is(client.request, "GET /api/settings")) {
        handle_get_settings(client);
    } else if (request_is(client.request, "POST /api/settings")) {
        handle_post_settings(client, body, body_length);
    } else if (request_is(client.request, "GET /api/status")) {
        handle_status(client);
    } else if (std::strncmp(client.request, "GET ", 4) == 0) {
        prepare_response(client, "200 OK", "text/html; charset=utf-8", kPortalPage, sizeof(kPortalPage) - 1);
    } else {
        respond_message(client, "405 Method Not Allowed", false, "Unsupported request.");
    }
}

err_t http_sent(void* arg, tcp_pcb*, u16_t) {
    HttpClient& client = *static_cast<HttpClient*>(arg);
    if (client.request_handled && !client.waiting_for_scan &&
        client.header_offset >= client.response_header_length &&
        client.response_offset >= client.response_length) {
        close_http_client(client);
    } else {
        send_available(client);
    }
    return ERR_OK;
}

err_t http_poll(void* arg, tcp_pcb*) {
    send_available(*static_cast<HttpClient*>(arg));
    return ERR_OK;
}

void http_error(void* arg, err_t) {
    *static_cast<HttpClient*>(arg) = {};
}

err_t http_receive(void* arg, tcp_pcb* pcb, pbuf* packet, err_t error) {
    HttpClient& client = *static_cast<HttpClient*>(arg);
    if (packet == nullptr) {
        close_http_client(client);
        return ERR_OK;
    }
    if (error != ERR_OK) {
        pbuf_free(packet);
        return error;
    }

    tcp_recved(pcb, packet->tot_len);
    if (client.request_handled) {
        pbuf_free(packet);
        return ERR_OK;
    }
    const size_t remaining = kRequestCapacity - 1 - client.request_length;
    const size_t copied = std::min(remaining, static_cast<size_t>(packet->tot_len));
    pbuf_copy_partial(packet, client.request + client.request_length, static_cast<u16_t>(copied), 0);
    client.request_length += copied;
    pbuf_free(packet);

    client.request[client.request_length] = '\0';
    const char* header_end = std::strstr(client.request, "\r\n\r\n");
    if (header_end == nullptr) {
        if (client.request_length == kRequestCapacity - 1) {
            client.request_handled = true;
            respond_message(client, "431 Request Header Fields Too Large", false, "Request too large.");
        }
        return ERR_OK;
    }

    const size_t body_offset = static_cast<size_t>(header_end + 4 - client.request);
    const char* length_value = find_header_value(client.request, "content-length");
    const unsigned long content_length = length_value == nullptr ? 0 : std::strtoul(length_value, nullptr, 10);
    if (content_length > kMaxRequestBody || body_offset + content_length > kRequestCapacity - 1) {
        client.request_handled = true;
        respond_message(client, "413 Content Too Large", false, "Request too large.");
        return ERR_OK;
    }
    if (client.request_length >= body_offset + content_length) {
        request_complete(client, client.request + body_offset, content_length);
    }
    return ERR_OK;
}

err_t http_accept(void*, tcp_pcb* new_pcb, err_t error) {
    if (error != ERR_OK) {
        return error;
    }
    HttpClient* client = nullptr;
    for (HttpClient& candidate : http_clients) {
        if (candidate.pcb == nullptr) {
            client = &candidate;
            break;
        }
    }
    if (client == nullptr) {
        tcp_abort(new_pcb);
        return ERR_ABRT;
    }

    *client = {};
    client->pcb = new_pcb;
    tcp_arg(new_pcb, client);
    tcp_recv(new_pcb, http_receive);
    tcp_sent(new_pcb, http_sent);
    tcp_poll(new_pcb, http_poll, 2);
    tcp_err(new_pcb, http_error);
    return ERR_OK;
}

bool from_setup_ap() {
    return ip_current_input_netif() == &cyw43_state.netif[CYW43_ITF_AP];
}

void dns_receive(void*, udp_pcb* pcb, pbuf* packet, const ip_addr_t* address, u16_t port) {
    if (packet == nullptr || packet->tot_len < 12 || address == nullptr || !from_setup_ap()) {
        if (packet != nullptr) {
            pbuf_free(packet);
        }
        return;
    }

    uint8_t query[256]{};
    const size_t query_length = std::min(static_cast<size_t>(packet->tot_len), sizeof(query));
    pbuf_copy_partial(packet, query, static_cast<u16_t>(query_length), 0);
    pbuf_free(packet);

    size_t question_end = 12;
    while (question_end < query_length && query[question_end] != 0) {
        const uint8_t label_length = query[question_end];
        if (label_length > 63 || question_end + label_length + 1 >= query_length) {
            return;
        }
        question_end += label_length + 1;
    }
    if (question_end + 5 > query_length) {
        return;
    }
    const bool is_a_query = query[question_end + 1] == 0 && query[question_end + 2] == 1;
    question_end += 5;

    uint8_t response[256]{};
    const size_t question_length = question_end - 12;
    const size_t response_length = 12 + question_length + (is_a_query ? 16 : 0);
    if (response_length > sizeof(response)) {
        return;
    }
    std::memcpy(response, query, 12 + question_length);
    response[2] = 0x81;
    response[3] = 0x80;
    response[4] = 0;
    response[5] = 1;
    response[6] = 0;
    response[7] = is_a_query ? 1 : 0;
    // The query's authority/additional records (e.g. EDNS) are not echoed back.
    response[8] = 0;
    response[9] = 0;
    response[10] = 0;
    response[11] = 0;
    if (is_a_query) {
        constexpr uint8_t kAnswer[16] = {0xc0, 0x0c, 0, 1, 0, 1, 0, 0, 0, 30, 0, 4, 192, 168, 4, 1};
        std::memcpy(response + 12 + question_length, kAnswer, sizeof(kAnswer));
    }

    pbuf* reply = pbuf_alloc(PBUF_TRANSPORT, static_cast<u16_t>(response_length), PBUF_RAM);
    if (reply == nullptr) {
        return;
    }
    pbuf_take(reply, response, response_length);
    udp_sendto(pcb, reply, address, port);
    pbuf_free(reply);
}

int dhcp_find_lease(const uint8_t* mac) {
    for (size_t i = 0; i < kDhcpMaxLeases; ++i) {
        if (dhcp_leases[i].used && std::memcmp(dhcp_leases[i].mac, mac, 6) == 0) {
            return static_cast<int>(i);
        }
    }
    for (size_t i = 0; i < kDhcpMaxLeases; ++i) {
        if (!dhcp_leases[i].used) {
            dhcp_leases[i].used = true;
            std::memcpy(dhcp_leases[i].mac, mac, 6);
            return static_cast<int>(i);
        }
    }
    return -1;
}

const uint8_t* dhcp_find_option(size_t length, uint8_t code, uint8_t& option_length) {
    size_t i = kDhcpOptionsOffset;
    while (i < length) {
        const uint8_t current = dhcp_request[i];
        if (current == 255) {
            break;
        }
        if (current == 0) {
            ++i;
            continue;
        }
        if (i + 1 >= length || i + 2 + dhcp_request[i + 1] > length) {
            break;
        }
        if (current == code) {
            option_length = dhcp_request[i + 1];
            return dhcp_request + i + 2;
        }
        i += 2 + dhcp_request[i + 1];
    }
    return nullptr;
}

void dhcp_receive(void*, udp_pcb* pcb, pbuf* packet, const ip_addr_t*, u16_t) {
    if (packet == nullptr) {
        return;
    }
    // Never answer DHCP on the home network.
    if (!from_setup_ap()) {
        pbuf_free(packet);
        return;
    }
    const size_t length = std::min(static_cast<size_t>(packet->tot_len), sizeof(dhcp_request));
    pbuf_copy_partial(packet, dhcp_request, static_cast<u16_t>(length), 0);
    pbuf_free(packet);

    constexpr uint8_t kMagic[4] = {99, 130, 83, 99};
    if (length < kDhcpOptionsOffset || dhcp_request[0] != 1 || dhcp_request[2] != 6 ||
        std::memcmp(dhcp_request + 236, kMagic, 4) != 0) {
        return;
    }

    uint8_t option_length = 0;
    const uint8_t* type = dhcp_find_option(length, 53, option_length);
    if (type == nullptr || option_length != 1 || (*type != kDhcpDiscover && *type != kDhcpRequest)) {
        return;
    }

    const int lease = dhcp_find_lease(dhcp_request + 28);
    const uint8_t* mac = dhcp_request + 28;
    std::printf("DHCP %s from %02x:%02x:%02x:%02x:%02x:%02x\n", *type == kDhcpDiscover ? "DISCOVER" : "REQUEST",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    if (lease < 0) {
        std::printf("DHCP no free lease\n");
        return;
    }
    const uint8_t client_octets[4] = {192, 168, 4, static_cast<uint8_t>(kDhcpFirstHost + lease)};

    uint8_t reply_type = kDhcpOffer;
    if (*type == kDhcpRequest) {
        reply_type = kDhcpAck;
        const uint8_t* requested = dhcp_find_option(length, 50, option_length);
        if (requested == nullptr && std::memcmp(dhcp_request + 12, "\0\0\0\0", 4) != 0) {
            requested = dhcp_request + 12;
            option_length = 4;
        }
        if (requested != nullptr && (option_length != 4 || std::memcmp(requested, client_octets, 4) != 0)) {
            reply_type = kDhcpNak;
        }
    }

    std::memset(dhcp_reply, 0, sizeof(dhcp_reply));
    dhcp_reply[0] = 2;
    dhcp_reply[1] = 1;
    dhcp_reply[2] = 6;
    std::memcpy(dhcp_reply + 4, dhcp_request + 4, 4);    // xid
    std::memcpy(dhcp_reply + 10, dhcp_request + 10, 2);  // flags
    if (reply_type != kDhcpNak) {
        std::memcpy(dhcp_reply + 16, client_octets, 4);
        std::memcpy(dhcp_reply + 20, kPortalOctets, 4);
    }
    std::memcpy(dhcp_reply + 24, dhcp_request + 24, 4);  // giaddr
    std::memcpy(dhcp_reply + 28, dhcp_request + 28, 16); // chaddr
    std::memcpy(dhcp_reply + 236, kMagic, 4);

    size_t o = kDhcpOptionsOffset;
    auto add_option = [&o](uint8_t code, const uint8_t* data, uint8_t data_length) {
        dhcp_reply[o++] = code;
        dhcp_reply[o++] = data_length;
        std::memcpy(dhcp_reply + o, data, data_length);
        o += data_length;
    };
    add_option(53, &reply_type, 1);
    add_option(54, kPortalOctets, 4);
    if (reply_type != kDhcpNak) {
        constexpr uint8_t kMask[4] = {255, 255, 255, 0};
        constexpr uint8_t kLease[4] = {
            static_cast<uint8_t>(kDhcpLeaseSeconds >> 24), static_cast<uint8_t>(kDhcpLeaseSeconds >> 16),
            static_cast<uint8_t>(kDhcpLeaseSeconds >> 8), static_cast<uint8_t>(kDhcpLeaseSeconds)};
        add_option(51, kLease, 4);
        add_option(1, kMask, 4);
        add_option(3, kPortalOctets, 4);
        add_option(6, kPortalOctets, 4);
        // RFC 8910 captive-portal URI, used by recent Android/iOS to open the setup page.
        constexpr char kCaptiveUri[] = "http://192.168.4.1/";
        add_option(114, reinterpret_cast<const uint8_t*>(kCaptiveUri), sizeof(kCaptiveUri) - 1);
    }
    dhcp_reply[o] = 255;

    pbuf* reply = pbuf_alloc(PBUF_TRANSPORT, static_cast<u16_t>(sizeof(dhcp_reply)), PBUF_RAM);
    if (reply == nullptr) {
        return;
    }
    pbuf_take(reply, dhcp_reply, sizeof(dhcp_reply));
    const err_t sent = udp_sendto_if(pcb, reply, IP_ADDR_BROADCAST, 68, &cyw43_state.netif[CYW43_ITF_AP]);
    std::printf("DHCP %s 192.168.4.%u (err %d)\n",
                reply_type == kDhcpOffer ? "OFFER" : reply_type == kDhcpAck ? "ACK" : "NAK",
                static_cast<unsigned>(client_octets[3]), static_cast<int>(sent));
    pbuf_free(reply);
}

}  // namespace

namespace portal {

bool init() {
    http_listener = tcp_new();
    if (http_listener == nullptr || tcp_bind(http_listener, IP_ADDR_ANY, 80) != ERR_OK) {
        return false;
    }
    http_listener = tcp_listen(http_listener);
    if (http_listener == nullptr) {
        return false;
    }
    tcp_accept(http_listener, http_accept);

    dns_listener = udp_new();
    if (dns_listener == nullptr || udp_bind(dns_listener, IP_ADDR_ANY, 53) != ERR_OK) {
        return false;
    }
    udp_recv(dns_listener, dns_receive, nullptr);

    dhcp_listener = udp_new();
    if (dhcp_listener == nullptr || udp_bind(dhcp_listener, IP_ADDR_ANY, 67) != ERR_OK) {
        return false;
    }
    udp_recv(dhcp_listener, dhcp_receive, nullptr);
    return true;
}

void poll() {
    if (!scan_pending || cyw43_wifi_scan_active(&cyw43_state)) {
        return;
    }
    scan_pending = false;
    build_scan_json();
    std::printf("Wi-Fi scan found %u networks\n", static_cast<unsigned>(scan_count));
    for (HttpClient& client : http_clients) {
        if (client.pcb != nullptr && client.waiting_for_scan) {
            client.waiting_for_scan = false;
            prepare_response(client, "200 OK", "application/json", scan_json, scan_json_length);
        }
    }
}

void set_saved(const settings::Settings* saved) {
    saved_settings = saved;
}

bool take_submission(settings::Settings& out) {
    if (!submission_ready) {
        return false;
    }
    out = submission;
    submission_ready = false;
    std::memset(submission.password, 0, sizeof(submission.password));
    return true;
}

void report_join(JoinState state, const char* message, const char* ip) {
    join_state = state;
    join_message = message;
    std::snprintf(join_ip, sizeof(join_ip), "%s", ip == nullptr ? "" : ip);
}

}  // namespace portal
