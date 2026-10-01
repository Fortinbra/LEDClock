#include "portal.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "lwip/udp.h"

namespace {

constexpr ip4_addr_t kPortalAddress = IPADDR4_INIT_BYTES(192, 168, 4, 1);
constexpr size_t kRequestCapacity = 1024;
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
<div class="field full"><label for="password">Password</label><div class="password"><input id="password" name="password" type="password" autocomplete="current-password"><button class="secondary icon" id="toggle" type="button" aria-label="Show password" aria-pressed="false">&#128065;</button></div></div>
<div class="field"><label for="timezone">Time zone</label><select id="timezone" name="timezone"><option value="UTC">UTC</option><option value="America/New_York">Eastern Time</option><option value="America/Chicago">Central Time</option><option value="America/Denver">Mountain Time</option><option value="America/Los_Angeles">Pacific Time</option></select></div>
<div class="field"><label for="zip">ZIP code <span class="note">(future weather)</span></label><input id="zip" name="zip" inputmode="numeric" maxlength="10" autocomplete="postal-code"></div>
<div class="field"><label for="theme">Appearance</label><select id="theme" name="theme"><option value="system">System default</option><option value="light">Light</option><option value="dark">Dark</option><option value="future" disabled>More themes later</option></select></div>
</div><div class="actions"><div id="status" class="status" role="status" aria-live="polite"></div><button type="submit">Save and connect</button></div><p class="note">Your password is used only to connect this device. It is not shown after saving.</p></form></section>
</main><script>
const root=document.documentElement,theme=document.getElementById('theme'),status=document.getElementById('status'),ssid=document.getElementById('ssid');
function applyTheme(value){root.dataset.theme=value==='system'?'':value;localStorage.setItem('ledclock-theme',value)}
const saved=localStorage.getItem('ledclock-theme')||'system';theme.value=saved;applyTheme(saved);theme.addEventListener('change',()=>applyTheme(theme.value));
document.getElementById('toggle').addEventListener('click',event=>{const field=document.getElementById('password'),visible=field.type==='text';field.type=visible?'password':'text';event.currentTarget.setAttribute('aria-pressed',String(!visible));event.currentTarget.setAttribute('aria-label',visible?'Show password':'Hide password')});
async function scan(){status.className='status';status.textContent='Scanning for nearby networks...';ssid.innerHTML='<option>Scanning...</option>';try{const response=await fetch('/api/scan');const data=await response.json();ssid.innerHTML='';(data.networks||[]).forEach(network=>{const option=document.createElement('option');option.value=network.ssid;option.textContent=network.ssid+(network.rssi?'  ('+network.rssi+' dBm)':'');ssid.appendChild(option)});const manual=document.createElement('option');manual.value='';manual.textContent=(data.networks||[]).length?'Other network...':'Enter network manually...';ssid.appendChild(manual);status.textContent=data.message||'Choose a network.'}catch(error){ssid.innerHTML='<option value="">Enter network manually...</option>';status.className='status error';status.textContent='Scan unavailable. Enter the network name manually.'}}
document.getElementById('rescan').addEventListener('click',scan);document.getElementById('settings').addEventListener('submit',async event=>{event.preventDefault();status.className='status';status.textContent='Checking connection...';const payload=Object.fromEntries(new FormData(event.currentTarget));try{const response=await fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(payload)});if(!response.ok)throw new Error();status.className='status ok';status.textContent='Settings accepted. The device is connecting.'}catch(error){status.className='status error';status.textContent='Unable to submit settings. Please try again.'}});scan();
</script></body></html>)HTML";

struct HttpClient {
    tcp_pcb* pcb = nullptr;
    char request[kRequestCapacity]{};
    size_t request_length = 0;
    const char* response_body = nullptr;
    size_t response_length = 0;
    size_t response_offset = 0;
    char response_header[192]{};
    size_t response_header_length = 0;
    size_t header_offset = 0;
};

HttpClient http_client;
tcp_pcb* http_listener = nullptr;
udp_pcb* dns_listener = nullptr;

const char kScanResponse[] = R"JSON({"networks":[],"message":"Wi-Fi scan support is being connected to the radio."})JSON";
const char kSettingsResponse[] = R"JSON({"ok":false,"message":"Settings storage and station connection are not enabled in this build."})JSON";

void close_http_client() {
    if (http_client.pcb == nullptr) {
        return;
    }

    tcp_arg(http_client.pcb, nullptr);
    tcp_recv(http_client.pcb, nullptr);
    tcp_sent(http_client.pcb, nullptr);
    tcp_poll(http_client.pcb, nullptr, 0);
    tcp_close(http_client.pcb);
    http_client = {};
}

void send_available() {
    if (http_client.pcb == nullptr) {
        return;
    }

    while (http_client.header_offset < http_client.response_header_length ||
           http_client.response_offset < http_client.response_length) {
        const bool sending_header = http_client.header_offset < http_client.response_header_length;
        const char* source = sending_header ? http_client.response_header : http_client.response_body;
        size_t& offset = sending_header ? http_client.header_offset : http_client.response_offset;
        const size_t length = sending_header ? http_client.response_header_length : http_client.response_length;
        const size_t available = static_cast<size_t>(tcp_sndbuf(http_client.pcb));
        if (available == 0) {
            return;
        }
        const size_t count = std::min({kResponseChunk, available, length - offset});
        if (tcp_write(http_client.pcb, source + offset, static_cast<u16_t>(count), TCP_WRITE_FLAG_COPY) != ERR_OK) {
            return;
        }
        offset += count;
    }

    tcp_output(http_client.pcb);
}

void prepare_response(const char* status, const char* content_type, const char* body, size_t length) {
    http_client.response_body = body;
    http_client.response_length = length;
    http_client.response_offset = 0;
    http_client.header_offset = 0;
    http_client.response_header_length = static_cast<size_t>(std::snprintf(
        http_client.response_header,
        sizeof(http_client.response_header),
        "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %u\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n",
        status,
        content_type,
        static_cast<unsigned>(length)));
    send_available();
}

void request_complete() {
    http_client.request[http_client.request_length] = '\0';
    const bool is_scan = std::strncmp(http_client.request, "GET /api/scan", 13) == 0;
    const bool is_settings = std::strncmp(http_client.request, "POST /api/settings", 18) == 0;
    if (is_scan) {
        prepare_response("200 OK", "application/json", kScanResponse, sizeof(kScanResponse) - 1);
    } else if (is_settings) {
        prepare_response("501 Not Implemented", "application/json", kSettingsResponse, sizeof(kSettingsResponse) - 1);
    } else {
        prepare_response("200 OK", "text/html; charset=utf-8", kPortalPage, sizeof(kPortalPage) - 1);
    }
}

err_t http_sent(void*, tcp_pcb*, u16_t) {
    if (http_client.header_offset >= http_client.response_header_length &&
        http_client.response_offset >= http_client.response_length) {
        close_http_client();
    } else {
        send_available();
    }
    return ERR_OK;
}

err_t http_poll(void*, tcp_pcb*) {
    send_available();
    return ERR_OK;
}

void http_error(void*, err_t) {
    http_client = {};
}

err_t http_receive(void*, tcp_pcb* pcb, pbuf* packet, err_t error) {
    if (packet == nullptr) {
        close_http_client();
        return ERR_OK;
    }
    if (error != ERR_OK) {
        pbuf_free(packet);
        return error;
    }

    const size_t remaining = kRequestCapacity - 1 - http_client.request_length;
    const size_t copied = std::min(remaining, static_cast<size_t>(packet->tot_len));
    pbuf_copy_partial(packet, http_client.request + http_client.request_length, static_cast<u16_t>(copied), 0);
    http_client.request_length += copied;
    tcp_recved(pcb, packet->tot_len);
    pbuf_free(packet);

    http_client.request[http_client.request_length] = '\0';
    if (std::strstr(http_client.request, "\r\n\r\n") != nullptr || http_client.request_length == kRequestCapacity - 1) {
        request_complete();
    }
    return ERR_OK;
}

err_t http_accept(void*, tcp_pcb* new_pcb, err_t error) {
    if (error != ERR_OK) {
        return error;
    }
    if (http_client.pcb != nullptr) {
        tcp_abort(new_pcb);
        return ERR_ABRT;
    }

    http_client = {};
    http_client.pcb = new_pcb;
    tcp_arg(new_pcb, nullptr);
    tcp_recv(new_pcb, http_receive);
    tcp_sent(new_pcb, http_sent);
    tcp_poll(new_pcb, http_poll, 2);
    tcp_err(new_pcb, http_error);
    return ERR_OK;
}

void dns_receive(void*, udp_pcb* pcb, pbuf* packet, const ip_addr_t* address, u16_t port) {
    if (packet == nullptr || packet->tot_len < 12 || address == nullptr) {
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
    question_end += 5;

    uint8_t response[256]{};
    const size_t question_length = question_end - 12;
    const size_t response_length = 12 + question_length + 16;
    if (response_length > sizeof(response)) {
        return;
    }
    std::memcpy(response, query, 12 + question_length);
    response[2] = 0x81;
    response[3] = 0x80;
    response[4] = 0;
    response[5] = 1;
    response[6] = 0;
    response[7] = 1;
    size_t answer = 12 + question_length;
    response[answer++] = 0xc0;
    response[answer++] = 0x0c;
    response[answer++] = 0;
    response[answer++] = 1;
    response[answer++] = 0;
    response[answer++] = 1;
    response[answer++] = 0;
    response[answer++] = 0;
    response[answer++] = 0;
    response[answer++] = 30;
    response[answer++] = 0;
    response[answer++] = 4;
    response[answer++] = 192;
    response[answer++] = 168;
    response[answer++] = 4;
    response[answer] = 1;

    pbuf* reply = pbuf_alloc(PBUF_TRANSPORT, static_cast<u16_t>(response_length), PBUF_RAM);
    if (reply == nullptr) {
        return;
    }
    pbuf_take(reply, response, response_length);
    udp_sendto(pcb, reply, address, port);
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
    return true;
}

void poll() {
}

}  // namespace portal
