#include "webserver.h"
#include "datalog.h"
#include "../ui/ui.h"          // APP_NAME, APP_AUTHOR, HMI_FW_VERSION
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <SD.h>
#include <string.h>

static WebServer s_srv(80);
static bool      s_running = false;
static char      s_ip[20] = "-";

static const char* LOG_DIR = "/logs";

// ===========================================================================
//  PAGE
//
//  Deliberately one self-contained HTML string with inline CSS: no CDN, no
//  external assets. The phone connecting to this AP has NO internet, so
//  anything not served from here would simply fail to load.
//
//  Styled to match the instrument's dark theme so it reads as part of the
//  same product.
// ===========================================================================
static const char PAGE_HEAD[] PROGMEM =
"<!DOCTYPE html><html><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>" APP_NAME " - Data</title><style>"
"*{box-sizing:border-box}"
"body{margin:0;padding:16px;background:#0E1217;color:#fff;"
"font-family:-apple-system,Segoe UI,Roboto,sans-serif}"
"h1{font-size:20px;margin:0 0 4px;letter-spacing:2px}"
".sub{color:#8A93A3;font-size:13px;margin-bottom:16px}"
".card{background:#171D29;border-radius:12px;padding:14px;margin-bottom:12px}"
".bar{height:8px;background:#2A3240;border-radius:4px;overflow:hidden;margin:8px 0}"
".bar>i{display:block;height:100%;background:#2E8FD6}"
"table{width:100%;border-collapse:collapse}"
"td{padding:12px 6px;border-bottom:1px solid #2A3240;font-size:15px}"
"tr:last-child td{border-bottom:none}"
".sz{color:#8A93A3;font-size:13px;white-space:nowrap;text-align:right}"
"a.dl{display:inline-block;background:#2E8FD6;color:#fff;text-decoration:none;"
"padding:9px 16px;border-radius:8px;font-size:14px;font-weight:600}"
".tabs{display:flex;gap:8px;margin-bottom:12px}"
".tabs a{flex:1;text-align:center;padding:10px;border-radius:8px;"
"background:#1F2736;color:#8A93A3;text-decoration:none;font-size:14px}"
".tabs a.on{background:#2E8FD6;color:#fff;font-weight:600}"
".danger{width:100%;padding:14px;border:2px solid #E05A4E;background:#241314;"
"color:#E05A4E;border-radius:10px;font-size:15px;font-weight:600}"
".empty{color:#5A6273;text-align:center;padding:24px 0;font-size:14px}"
"</style></head><body>";

static String human_size(size_t b) {
    char s[24];
    if (b < 1024)              snprintf(s, sizeof(s), "%u B", (unsigned)b);
    else if (b < 1024UL * 1024) snprintf(s, sizeof(s), "%.1f KB", b / 1024.0);
    else                        snprintf(s, sizeof(s), "%.1f MB", b / (1024.0 * 1024.0));
    return String(s);
}

// ---------------------------------------------------------------------------
//  INDEX
//  ?sort=name (default) or ?sort=new
// ---------------------------------------------------------------------------
static void handle_root() {
    const bool by_new = (s_srv.arg("sort") == "new");

    String h;
    h.reserve(4096);
    h += FPSTR(PAGE_HEAD);
    h += "<h1>" APP_NAME "</h1>";

    if (!datalog_has_card()) {
        // Be explicit about the most likely cause rather than just "no card".
        h += "<div class='sub'>No SD card detected.</div>"
             "<div class='card'>Insert a microSD card formatted as <b>FAT32</b>. "
             "Cards of 32&nbsp;GB or larger usually ship as exFAT and must be "
             "reformatted before use.</div></body></html>";
        s_srv.send(200, "text/html", h);
        return;
    }

    // ---- storage summary ------------------------------------------------
    const uint32_t total = datalog_total_mb();
    const uint32_t freem = datalog_free_mb();
    const int      pct   = total ? (int)(100 - (freem * 100 / total)) : 0;

    h += "<div class='sub'>Test data - download before starting a new run</div>";
    h += "<div class='card'><b>Storage</b><div class='bar'><i style='width:";
    h += String(pct); h += "%'></i></div><div class='sub' style='margin:0'>";
    h += String(freem); h += " MB free of "; h += String(total); h += " MB</div></div>";

    // ---- sort tabs -------------------------------------------------------
    h += "<div class='tabs'><a href='/?sort=name' class='";
    h += (by_new ? "" : "on"); h += "'>By name</a>";
    h += "<a href='/?sort=new' class='";
    h += (by_new ? "on" : ""); h += "'>Newest first</a></div>";

    // ---- gather files ----------------------------------------------------
    // Collected into a fixed array first so they can be sorted; the SD
    // directory handle gives no ordering guarantee.
    static char  names[64][40];
    static size_t sizes[64];
    int n = 0;

    File dir = SD.open(LOG_DIR);
    if (dir) {
        for (File f = dir.openNextFile(); f && n < 64; f = dir.openNextFile()) {
            if (!f.isDirectory()) {
                const char* nm = f.name();
                const char* slash = strrchr(nm, '/');
                snprintf(names[n], sizeof(names[0]), "%s", slash ? slash + 1 : nm);
                sizes[n] = f.size();
                n++;
            }
            f.close();
        }
        dir.close();
    }

    // "Newest" = reverse directory order, the best proxy available without
    // timestamps (there is no RTC on this board).
    if (by_new) {
        for (int i = 0; i < n / 2; i++) {
            char tn[40]; size_t ts;
            snprintf(tn, sizeof(tn), "%s", names[i]); ts = sizes[i];
            snprintf(names[i], sizeof(names[0]), "%s", names[n - 1 - i]);
            sizes[i] = sizes[n - 1 - i];
            snprintf(names[n - 1 - i], sizeof(names[0]), "%s", tn);
            sizes[n - 1 - i] = ts;
        }
    } else {
        for (int i = 1; i < n; i++) {                 // insertion sort by name
            char kn[40]; snprintf(kn, sizeof(kn), "%s", names[i]);
            size_t ks = sizes[i];
            int j = i - 1;
            while (j >= 0 && strcasecmp(names[j], kn) > 0) {
                snprintf(names[j + 1], sizeof(names[0]), "%s", names[j]);
                sizes[j + 1] = sizes[j];
                j--;
            }
            snprintf(names[j + 1], sizeof(names[0]), "%s", kn);
            sizes[j + 1] = ks;
        }
    }

    // ---- file table ------------------------------------------------------
    h += "<div class='card'>";
    if (n == 0) {
        h += "<div class='empty'>No test files yet.<br>Run a test, then come back.</div>";
    } else {
        h += "<table>";
        for (int i = 0; i < n; i++) {
            h += "<tr><td>"; h += names[i];
            h += "</td><td class='sz'>"; h += human_size(sizes[i]);
            h += "</td><td style='text-align:right'><a class='dl' href='/dl?f=";
            h += names[i]; h += "'>Download</a></td></tr>";
        }
        h += "</table>";
    }
    h += "</div>";

    // ---- erase, behind a confirm ----------------------------------------
    h += "<div class='card'><form action='/erase' method='post' "
         "onsubmit=\"return confirm('Erase ALL test files? This cannot be undone.')\">"
         "<button class='danger' type='submit'>Erase all files</button></form></div>";

    h += "<div class='sub' style='text-align:center'>" APP_NAME " - " APP_AUTHOR
         " - HMI " HMI_FW_VERSION "</div>";
    h += "</body></html>";
    s_srv.send(200, "text/html", h);
}

// ---------------------------------------------------------------------------
//  DOWNLOAD
// ---------------------------------------------------------------------------
static void handle_download() {
    String f = s_srv.arg("f");

    // Path-traversal guard: the filename comes off the query string, so
    // "../../secret" must not be able to escape the log directory.
    if (f.length() == 0 || f.indexOf("..") >= 0 || f.indexOf('/') >= 0) {
        s_srv.send(400, "text/plain", "Bad filename");
        return;
    }

    String path = String(LOG_DIR) + "/" + f;
    File file = SD.open(path, FILE_READ);
    if (!file) { s_srv.send(404, "text/plain", "Not found"); return; }

    // text/csv + attachment so phones save it rather than rendering it.
    s_srv.sendHeader("Content-Disposition", "attachment; filename=\"" + f + "\"");
    s_srv.streamFile(file, "text/csv");
    file.close();
}

static void handle_erase() {
    const int n = datalog_erase_all();
    String h = FPSTR(PAGE_HEAD);
    h += "<h1>Erased</h1><div class='card'>";
    h += String(n); h += " file(s) removed.</div>"
         "<div class='card'><a class='dl' href='/'>Back</a></div></body></html>";
    s_srv.send(200, "text/html", h);
}

static void handle_404() { s_srv.send(404, "text/plain", "Not found"); }

// ===========================================================================
//  LIFECYCLE
// ===========================================================================
void web_start() {
    if (s_running) return;

    WiFi.mode(WIFI_AP);
    WiFi.softAP(web_ssid(), web_pass());
    delay(100);                              // let the AP settle before reading IP
    snprintf(s_ip, sizeof(s_ip), "%s", WiFi.softAPIP().toString().c_str());

    s_srv.on("/",      HTTP_GET,  handle_root);
    s_srv.on("/dl",    HTTP_GET,  handle_download);
    s_srv.on("/erase", HTTP_POST, handle_erase);
    s_srv.onNotFound(handle_404);
    s_srv.begin();

    s_running = true;
    Serial.printf("[web] AP '%s' up at %s\n", web_ssid(), s_ip);
}

void web_stop() {
    if (!s_running) return;
    s_srv.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);                     // give the memory bus back to the panel
    s_running = false;
    snprintf(s_ip, sizeof(s_ip), "-");
    Serial.println("[web] AP stopped");
}

bool        web_is_running()   { return s_running; }
const char* web_ip()           { return s_ip; }
const char* web_ssid()         { return WIFI_AP_SSID_DEFAULT; }
const char* web_pass()         { return WIFI_AP_PASS_DEFAULT; }
int         web_client_count() { return s_running ? WiFi.softAPgetStationNum() : 0; }

// Must be pumped from the LVGL task while the dialog is open.
void web_poll() { if (s_running) s_srv.handleClient(); }
