#include "config.h"
#include "javascript.hh"
#include "jsengine.hh"
#include "jsstorage.hh"
#include "html_common.hh"
#include "html.hh"
#include "dw/textblock.hh"
#include "prefs.h"
#include "capi.h"
#include "web.hh"
#include "history.h"
#include "nav.h"
#include "timeout.hh"
#include "uicmd.hh"
#include "msg.h"
#include "cookies.h"
#include <map>
#include <deque>
#include <string>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <set>
#include <cmath>
#include <chrono>

#ifdef ENABLE_JAVASCRIPT
namespace {
std::string resolve(const std::string &url, const DilloUrl *base)
{
   DilloUrl *u = a_Url_new(url.c_str(), URL_STR(base));
   std::string result = u ? URL_STR(u) : "";
   a_Url_free(u); return result;
}
std::string origin(const std::string &url)
{
   DilloUrl *u = a_Url_new(url.c_str(), NULL);
   if (!u) return "";
   std::string scheme = URL_SCHEME(u), host = URL_HOST(u);
   int port = URL_PORT(u);
   std::string result = scheme + "://" + host;
   if (port && !((scheme == "http" && port == 80) || (scheme == "https" && port == 443)))
      result += ":" + std::to_string(port);
   a_Url_free(u); return result;
}
std::string headerValue(const std::string &headers, const std::string &name)
{
   size_t pos = 0;
   while (pos < headers.size()) {
      size_t end = headers.find('\n', pos);
      std::string line = headers.substr(pos, end == std::string::npos ? end : end - pos);
      if (line.size() > name.size() && line[name.size()] == ':' &&
          !dStrnAsciiCasecmp(line.c_str(), name.c_str(), name.size())) {
         size_t start = line.find_first_not_of(" \t", name.size() + 1);
         if (start == std::string::npos) return "";
         size_t last = line.find_last_not_of(" \t\r");
         return line.substr(start, last - start + 1);
      }
      if (end == std::string::npos) break;
      pos = end + 1;
   }
   return "";
}
std::string lower(std::string s)
{
   for (size_t i = 0; i < s.size(); ++i)
      s[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
   return s;
}
std::set<std::string> headerList(const std::string &value)
{
   std::set<std::string> result;
   size_t pos = 0;
   while (pos < value.size()) {
      size_t end = value.find(',', pos);
      std::string item = value.substr(pos, end == std::string::npos ? end : end - pos);
      size_t start = item.find_first_not_of(" \t\r\n"), last = item.find_last_not_of(" \t\r\n");
      if (start != std::string::npos) result.insert(lower(item.substr(start, last - start + 1)));
      if (end == std::string::npos) break;
      pos = end + 1;
   }
   return result;
}
bool corsAllowed(const std::string &headers, const std::string &site, bool credentials)
{
   std::string allow = headerValue(headers, "Access-Control-Allow-Origin");
   return (allow == site || (allow == "*" && !credentials)) &&
      (!credentials || headerValue(headers, "Access-Control-Allow-Credentials") == "true");
}
bool token(const std::string &name)
{
   if (name.empty()) return false;
   for (size_t i = 0; i < name.size(); ++i) {
      unsigned char c = name[i];
      if (!c || !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || strchr("!#$%&'*+-.^_`|~", c))) return false;
   }
   return true;
}
bool forbiddenHeader(const std::string &key)
{
   static const char *names[] = {"accept-charset", "accept-encoding", "access-control-request-headers",
      "access-control-request-method", "connection", "content-length", "cookie", "cookie2", "date",
      "dnt", "expect", "host", "keep-alive", "origin", "referer", "te", "trailer",
      "transfer-encoding", "upgrade", "user-agent", "via"};
   for (size_t i = 0; i < sizeof(names)/sizeof(names[0]); ++i)
      if (key == names[i]) return true;
   return key.compare(0, 6, "proxy-") == 0 || key.compare(0, 4, "sec-") == 0;
}
bool simpleHeader(const std::string &key, const std::string &value)
{
   if (value.size() > 128) return false;
   if (key == "accept" || key == "content-type") {
      for (size_t i = 0; i < value.size(); ++i) {
         unsigned char c = value[i];
         if ((c < 32 && c != 9) || c == 127 || strchr("\"():<>?@[\\]{}", c)) return false;
      }
      if (key == "accept") return true;
      std::string mime = lower(value.substr(0, value.find(';')));
      return mime == "application/x-www-form-urlencoded" || mime == "multipart/form-data" || mime == "text/plain";
   }
   if (key != "accept-language" && key != "content-language") return false;
   return value.find_first_not_of("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz *,-.;=") == std::string::npos;
}
std::string exposedHeaders(const std::string &headers, bool sameOrigin, bool credentials)
{
   std::string result;
   std::set<std::string> expose = headerList(headerValue(headers, "Access-Control-Expose-Headers"));
   size_t pos = 0;
   while (pos < headers.size()) {
      size_t end = headers.find('\n', pos);
      std::string line = headers.substr(pos, end == std::string::npos ? end : end - pos);
      size_t colon = line.find(':');
      if (colon != std::string::npos && token(line.substr(0, colon))) {
         std::string key = lower(line.substr(0, colon));
         bool safe = key == "cache-control" || key == "content-language" ||
            key == "content-length" || key == "content-type" || key == "expires" ||
            key == "last-modified" || key == "pragma";
         if (key != "set-cookie" && key != "set-cookie2" &&
             (sameOrigin || safe || (!credentials && expose.count("*")) || expose.count(key))) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            result += line + "\r\n";
         }
      }
      if (end == std::string::npos) break;
      pos = end + 1;
   }
   return result;
}
}

class JavascriptDocument : public JsEngine::Host {
public:
   struct Request {
      JavascriptDocument *document;
      JsEngine::Script script;
      int id, key, status;
      int redirects;
      bool isScript, done, preflight, approved, failed;
      int credentials;
      size_t received;
      std::map<std::string, std::string> requestHeaders;
      std::set<std::string> unsafeHeaders;
      std::string url, body, headers, method, requestBody;
      Request(): document(NULL), id(0), key(0), status(0), redirects(0),
         isScript(false), done(false), preflight(false), approved(false), failed(false),
         credentials(1), received(0), method("GET") {}
   };
   struct Timer { JavascriptDocument *document; int id; };
   struct ImageEvent { int node; std::string type; unsigned width, height; };
   BrowserWindow *bw;
   DilloHtml *view;
   DilloUrl *url;
   JsEngine engine;
   std::map<int, Timer *> timers;
   std::vector<Request *> requests;
   std::deque<Request *> deferred, ordered;
   std::deque<ImageEvent> imageEvents;
   size_t offset;
   int rootKey;
   bool eof, ready, domReady, renderQueued, dirty, stopped, navigating, rendering, rendered;
   size_t received;
   int eventDepth;
   int focusedControl;
   std::chrono::steady_clock::time_point renderNotBefore;
   Request *blocking;
   DilloUrl *navigation;
   bool replaceNavigation;
   int historyOffset;

   explicit JavascriptDocument(DilloHtml *v): bw(v->bw), view(v),
      url(a_Url_dup(v->page_url)), engine(this, URL_STR(v->page_url)), offset(0),
      rootKey(0), eof(false), ready(false), domReady(false), renderQueued(false), dirty(false), stopped(false),
      navigating(false), rendering(false), rendered(false), received(0), eventDepth(0), focusedControl(0),
      blocking(NULL), navigation(NULL), replaceNavigation(false), historyOffset(0) {}
   ~JavascriptDocument() {
      stop();
      for (size_t i = 0; i < requests.size(); ++i) delete requests[i];
      a_Url_free(url);
      a_Url_free(navigation);
   }
   void stop() {
      if (stopped) return;
      stopped = true; engine.stop();
      a_Timeout_actually_remove(tick, this);
      a_Timeout_actually_remove(navigateNow, this);
      a_Timeout_actually_remove(historyNow, this);
      a_Timeout_actually_remove(flushStorage, this); JavascriptStorage::flush();
      for (std::map<int, Timer *>::iterator i = timers.begin(); i != timers.end(); ++i) {
         a_Timeout_actually_remove(timerNow, i->second); delete i->second;
      }
      timers.clear();
      for (size_t i = 0; i < requests.size(); ++i) if (requests[i]->key) {
         a_Timeout_actually_remove(requestTimeout, requests[i]);
         a_Capi_stop_client(requests[i]->key, 0);
         a_Bw_remove_client(bw, requests[i]->key);
         requests[i]->key = 0;
      }
   }
   void log(const std::string &message) { MSG_WARN("JavaScript [%s] %s\n", URL_STR(url), message.c_str()); }
   void title(const std::string &text) {
      a_UIcmd_set_page_title(bw, text.c_str()); a_History_set_title_by_url(url, text.c_str());
   }
   void imageEvent(int node, const char *type, unsigned width, unsigned height) {
      if (stopped || !node || imageEvents.size() >= 10000) return;
      ImageEvent event = {node, type ? type : "error", width, height};
      imageEvents.push_back(event);
      wake();
   }
   void changed(int delayMilliseconds = 0) {
      dirty = true;
      if (delayMilliseconds > 0) {
         std::chrono::steady_clock::time_point deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(delayMilliseconds);
         if (renderNotBefore < deadline) renderNotBefore = deadline;
         wake(delayMilliseconds / 1000.0);
      } else {
         renderNotBefore = std::chrono::steady_clock::time_point();
         wake();
      }
   }
   void focusChanged(int node, bool focused) {
      if (focused) focusedControl = node;
      else if (focusedControl == node) focusedControl = 0;
   }
   void wake(double delay = 0) {
      if (!stopped && !renderQueued) { renderQueued = true; a_Timeout_add(delay, tick, this); }
   }
   void navigate(const std::string &target, bool replace) {
      if (stopped) return;
      std::string destination = resolve(target, url);
      DilloUrl *u = a_Url_new(destination.c_str(), NULL);
      queueNavigation(u, replace); a_Url_free(u);
   }
   void queueNavigation(DilloUrl *u, bool replace) {
      if (stopped) return;
      if (!u) return;
      const char *scheme = URL_SCHEME(u);
      if (dStrAsciiCasecmp(scheme, "http") && dStrAsciiCasecmp(scheme, "https") &&
          !(dStrAsciiCasecmp(URL_SCHEME(url), "file") == 0 && dStrAsciiCasecmp(scheme, "file") == 0)) {
         log("Blocked navigation to an unsupported scheme"); return;
      }
      a_Url_free(navigation); navigation = a_Url_dup(u);
      replaceNavigation = replace; navigating = true;
      a_Timeout_actually_remove(navigateNow, this); a_Timeout_add(0, navigateNow, this);
   }
   static void navigateNow(void *data) {
      JavascriptDocument *d = static_cast<JavascriptDocument *>(data);
      BrowserWindow *bw = d->bw;
      DilloUrl *destination = a_Url_dup(d->navigation);
      DilloUrl *requester = a_Url_dup(d->url);
      bool replace = d->replaceNavigation;
      d->stop();
      if (replace) a_Nav_replace(bw, destination, requester);
      else a_Nav_push(bw, destination, requester);
      a_Url_free(destination); a_Url_free(requester);
   }
   void timer(int id, int delay) {
      std::map<int, Timer *>::iterator it = timers.find(id);
      if (it != timers.end()) {
         a_Timeout_actually_remove(timerNow, it->second); delete it->second; timers.erase(it);
      }
      if (stopped || delay < 0 || timers.size() >= 10000) return;
      Timer *t = new Timer; t->document = this; t->id = id; timers[id] = t;
      a_Timeout_add(std::max(delay, 1) / 1000.0f, timerNow, t);
   }
   static void timerNow(void *data) {
      Timer *t = static_cast<Timer *>(data);
      JavascriptDocument *d = t->document; int id = t->id;
      d->timers.erase(id); delete t;
      if (!d->stopped && prefs.javascript_enabled) {
         d->engine.timeout(id); d->advance();
         if (d->engine.hasPendingJobs()) d->wake(.001);
      }
   }
   std::string userAgent() { return prefs.http_user_agent; }
   void setDocumentUrl(const DilloUrl *target) {
      DilloUrl *next = a_Url_dup(target);
      a_Url_free(url); url = next; engine.setUrl(URL_STR(url));
      a_UIcmd_set_location_text(bw, URL_STR(url));
      changed();
   }
   static void historyNow(void *data) {
      JavascriptDocument *d = static_cast<JavascriptDocument *>(data);
      int offset = d->historyOffset;
      if (d->stopped) return;
      if (!offset) a_Nav_reload(d->bw);
      else if (offset >= -a_Nav_stack_ptr(d->bw) &&
               offset < a_Nav_stack_size(d->bw) - a_Nav_stack_ptr(d->bw))
         a_Nav_jump(d->bw, offset, 0);
   }
   std::string control(int node, const std::string &property, const std::string *value) {
      if (property == "imagePreload" && value)
         return a_Html_preload_image(view, value->c_str(), node) ? "1" : "0";
      if (property == "historyLength") return std::to_string(a_Nav_stack_size(bw));
      if (property == "historyState") return std::to_string(a_Nav_stack_ptr(bw)) + "\n" + a_Nav_history_state(bw);
      if ((property == "historyPush" || property == "historyReplace") && value) {
         size_t end = value->find('\n');
         if (end == std::string::npos) return "security";
         DilloUrl *target = a_Url_new(value->substr(0, end).c_str(), URL_STR(url));
         if (!target || origin(URL_STR(target)) != origin(URL_STR(url))) {
            a_Url_free(target); return "security";
         }
         bool success = a_Nav_history_update(bw, target, value->substr(end+1).c_str(), property == "historyReplace");
         if (success) setDocumentUrl(target);
         a_Url_free(target); return success ? "" : "quota";
      }
      if (property == "historyGo" && value) {
         historyOffset = atoi(value->c_str());
         a_Timeout_actually_remove(historyNow, this); a_Timeout_add(0, historyNow, this); return "";
      }
      if (property == "viewport") {
         dw::core::Layout *layout = static_cast<dw::core::Layout *>(bw->render_layout);
         return std::to_string(layout->getWidthViewport()) + "," + std::to_string(layout->getHeightViewport()) + "," +
            std::to_string(layout->getScrollPosX()) + "," + std::to_string(layout->getScrollPosY());
      }
      if (property == "scrollMetrics") {
         if (dirty && !rendering && !eventDepth) { dirty = false; render(); }
         dw::core::Layout *layout = static_cast<dw::core::Layout *>(bw->render_layout);
         layout->flushLayout();
         dw::core::Allocation *allocation = view->rootWidget->getAllocation();
         return std::to_string(std::max(layout->getContentWidthViewport(), allocation->width)) + "," +
            std::to_string(std::max(layout->getContentHeightViewport(), allocation->ascent + allocation->descent));
      }
      if (property == "scroll" && value) {
         dw::core::Layout *layout = static_cast<dw::core::Layout *>(bw->render_layout);
         size_t comma = value->find(',');
         if (comma != std::string::npos)
            layout->scrollTo(dw::core::HPOS_LEFT, dw::core::VPOS_TOP,
               atoi(value->substr(0, comma).c_str()), atoi(value->substr(comma + 1).c_str()), 0, 0);
         return "";
      }
      if (property == "stylesheet" && value) {
         DilloUrl *sheet = a_Url_new(value->c_str(), URL_STR(url));
         std::string result = "\1";
         uint_t flags = sheet ? a_Cache_get_flags_with_redirection(sheet) : 0;
         if (flags & (CA_NotFound | CA_Aborted | CA_RedirectLoop)) result = "\3";
         if (sheet && !flags && a_Bw_stylesheet_requested(bw, sheet)) result = "\3";
         if (sheet && (a_Capi_get_flags_with_redirection(sheet) & CAPI_Completed)) {
            char *data = NULL; int size = 0;
            result = "\3";
            if (!(flags & (CA_NotFound | CA_Aborted | CA_RedirectLoop)) && a_Capi_get_buf(sheet, &data, &size)) {
               const DilloUrl *finalUrl = a_Cache_get_final_url(sheet);
               bool same = finalUrl && origin(URL_STR(finalUrl)) == origin(URL_STR(url)) &&
                  origin(URL_STR(sheet)) == origin(URL_STR(url));
               if (size >= 0 && size <= 8 * 1024 * 1024)
                  result = same ? "\2s\n" + std::string(data, size) : "\2c\n";
               a_Capi_unref_buf(sheet);
            }
         }
         a_Url_free(sheet); return result;
      }
      if (property == "selectionText") {
         dw::core::Layout *layout = static_cast<dw::core::Layout *>(bw->render_layout);
         if (value) { layout->clearSelection(); return ""; }
         char *text = layout->getSelectionText();
         std::string result = text ? text : ""; free(text); return result;
      }
      if (property == "geometry" || property == "computedStyle" || property == "scrollIntoView" ||
          property == "elementFromPoint") {
         // Event handlers still run on the old FLTK widget's stack. Replacing
         // it there would invalidate the native caller; defer that render.
         if (dirty && !rendering && !eventDepth) { dirty = false; render(); }
         dw::core::Layout *layout = static_cast<dw::core::Layout *>(bw->render_layout);
         layout->flushLayout();
         if (property == "elementFromPoint") {
            if (!value) return "0";
            double x, y;
            if (sscanf(value->c_str(), "%lf,%lf", &x, &y) != 2 ||
                !std::isfinite(x) || !std::isfinite(y) || x < 0 || y < 0 ||
                x >= layout->getContentWidthViewport() || y >= layout->getContentHeightViewport())
               return "0";
            return std::to_string(layout->getDomNodeAtPoint(
               (int)x + layout->getScrollPosX(), (int)y + layout->getScrollPosY()));
         }
         if (property == "computedStyle")
            return view->javascript_styles.count(node) ? view->javascript_styles[node] : "\1";
         dw::core::Allocation box;
         if (!view->rootWidget->getDomAllocation(node, &box)) return "";
         if (property == "scrollIntoView") {
            layout->scrollTo(dw::core::HPOS_INTO_VIEW, dw::core::VPOS_INTO_VIEW,
               box.x, box.y, box.width, box.ascent + box.descent);
            return "";
         }
         return std::to_string(box.x - layout->getScrollPosX()) + "," + std::to_string(box.y - layout->getScrollPosY()) + "," +
            std::to_string(box.width) + "," + std::to_string(box.ascent + box.descent);
      }
      if (property == "form-submit" && value) {
         size_t first = value->find('\n'), second = value->find('\n', first + 1);
         if (first != std::string::npos && second != std::string::npos) {
            std::string method = value->substr(0, first);
            DilloUrl *target = a_Url_new(value->substr(first + 1, second - first - 1).c_str(), NULL);
            if (target && method == "post") {
               Dstr *body = dStr_new(value->substr(second + 1).c_str());
               a_Url_set_data(target, &body);
               a_Url_set_flags(target, URL_FLAGS(target) | URL_Post | URL_E2EQuery);
            }
            if (method == "post" || method == "get") queueNavigation(target, false);
            a_Url_free(target);
         }
         return "";
      }
      if (property == "cookie") {
         if (value) a_Cookies_set_document(url, value->c_str());
         char *cookies = a_Cookies_get_document(url);
         std::string result = cookies ? cookies : ""; dFree(cookies); return result;
      }
      if (property == "localStorage" || property == "sessionStorage") {
         std::string site = origin(URL_STR(url));
         bool local = property == "localStorage";
         if (value) {
            if (!JavascriptStorage::set(site, bw, local, *value)) return "error";
            a_Timeout_actually_remove(flushStorage, this); a_Timeout_add(.25, flushStorage, this);
         }
         return JavascriptStorage::get(site, bw, local);
      }
      if (property == "value") {
         char *text = a_Html_control_text(view, node, value ? value->c_str() : NULL);
         std::string result = text ? text : "\1"; dFree(text); return result;
      }
      if (property == "focus" || property == "blur" || property == "selection") {
         bool focusRenderPending = renderNotBefore.time_since_epoch().count() &&
            std::chrono::steady_clock::now() < renderNotBefore;
         if (dirty && !rendering && !eventDepth && !focusRenderPending) {
            dirty = false;
            renderNotBefore = std::chrono::steady_clock::time_point();
            render();
         }
         static_cast<dw::core::Layout *>(bw->render_layout)->flushLayout();
         char *text = a_Html_control_selection(view, node, property.c_str(), value ? value->c_str() : NULL);
         std::string result = text ? text : "\1"; dFree(text); return result;
      }
      if (property == "checked" || property == "selected") {
         char *text = a_Html_control_state(view, node, property.c_str(), value ? value->c_str() : NULL);
         std::string result = text ? text : "\1"; dFree(text); return result;
      }
      if (property == "submit") {
         DilloUrl *destination = a_Html_control_submit(view, node);
         if (destination) { queueNavigation(destination, false); a_Url_free(destination); }
      }
      return "\1";
   }
   static void flushStorage(void *) { JavascriptStorage::flush(); }
   void abort(int id) {
      for (size_t i = 0; i < requests.size(); ++i) {
         Request *r = requests[i];
         if (!r->isScript && r->id == id) {
            a_Timeout_actually_remove(requestTimeout, r);
            if (r->key) a_Capi_stop_client(r->key, 0);
            r->key = 0; r->done = true; r->id = -2;
            r->body.clear(); r->headers.clear();
         }
      }
   }
   static void requestTimeout(void *data) {
      Request *r = static_cast<Request *>(data);
      r->failed = true; r->done = true; r->document->wake();
   }
   void request(int id, const std::string &target, const std::string &method,
                const std::string &body, const std::string &headers, int credentials) {
      Request *r = new Request;
      r->document = this; r->id = id; r->url = resolve(target, url);
      r->method = method; r->requestBody = body; r->credentials = credentials;
      requests.push_back(r);
      bool valid = token(method) && method != "CONNECT" && method != "TRACE" && method != "TRACK" &&
         headers.size() <= 65536 && body.size() <= 8 * 1024 * 1024;
      size_t pos = 0;
      while (valid && pos < headers.size()) {
         size_t end = headers.find("\r\n", pos);
         std::string line = headers.substr(pos, end == std::string::npos ? end : end - pos);
         size_t colon = line.find(':');
         if (colon == std::string::npos || !token(line.substr(0, colon)) ||
             line.find('\0') != std::string::npos || line.find('\r') != std::string::npos ||
             line.find('\n') != std::string::npos) { valid = false; break; }
         std::string key = lower(line.substr(0, colon)), value = line.substr(colon + 1);
         size_t first = value.find_first_not_of(" \t"), last = value.find_last_not_of(" \t");
         value = first == std::string::npos ? "" : value.substr(first, last - first + 1);
         if (!forbiddenHeader(key)) {
            r->requestHeaders[key] = value;
            if (!simpleHeader(key, value)) r->unsafeHeaders.insert(key);
         }
         if (end == std::string::npos) break;
         pos = end + 2;
      }
      if ((method == "GET" || method == "HEAD") && !body.empty()) valid = false;
      if (!valid) { r->failed = true; r->done = true; wake(); return; }
      open(r, a_Url_new(r->url.c_str(), NULL));
   }
   void open(Request *r, DilloUrl *u) {
      if (stopped || !u || requests.size() > 2048) {
         a_Url_free(u); r->done = true; r->failed = true; wake(); return;
      }
      const char *scheme = URL_SCHEME(u);
      bool local = !dStrAsciiCasecmp(URL_SCHEME(url), "file") && !dStrAsciiCasecmp(scheme, "file") && r->isScript;
      if ((dStrAsciiCasecmp(scheme, "http") && dStrAsciiCasecmp(scheme, "https") && !local) ||
          strchr(URL_AUTHORITY(u), '@')) {
         a_Url_free(u); r->done = true; r->failed = true; wake(); return;
      }
      bool cross = origin(r->url) != origin(URL_STR(url));
      r->preflight = !r->isScript && cross && !r->approved &&
         ((r->method != "GET" && r->method != "HEAD" && r->method != "POST") || !r->unsafeHeaders.empty());
      if (!r->preflight && !r->isScript && r->method != "GET" && r->method != "HEAD") {
         a_Url_set_flags(u, URL_FLAGS(u) | URL_Post);
         Dstr *data = dStr_sized_new(r->requestBody.size());
         dStr_append_l(data, r->requestBody.data(), r->requestBody.size());
         a_Url_set_data(u, &data);
      }
      a_Url_set_unique(u);
      DilloWeb *web = a_Web_new(bw, u, url);
      web->flags |= r->isScript ? WEB_Script : WEB_Request;
      web->omit_credentials = r->preflight || (!r->isScript && (r->credentials == 0 || (r->credentials == 1 && cross)));
      if (!r->isScript) {
         web->method = dStrdup(r->preflight ? "OPTIONS" : r->method.c_str());
         std::string extra;
         if (cross || (r->method != "GET" && r->method != "HEAD"))
            extra = "Origin: " + origin(URL_STR(url)) + "\r\n";
         if (r->preflight) {
            extra += "Access-Control-Request-Method: " + r->method + "\r\n";
            if (!r->unsafeHeaders.empty()) {
               extra += "Access-Control-Request-Headers: ";
               for (std::set<std::string>::iterator i = r->unsafeHeaders.begin(); i != r->unsafeHeaders.end(); ++i) {
                  if (i != r->unsafeHeaders.begin()) extra += ", ";
                  extra += *i;
               }
               extra += "\r\n";
            }
         } else for (std::map<std::string, std::string>::iterator i = r->requestHeaders.begin(); i != r->requestHeaders.end(); ++i) {
            if (i->first == "content-type" && (URL_FLAGS(u) & URL_Post)) web->content_type = dStrdup(i->second.c_str());
            else extra += i->first + ": " + i->second + "\r\n";
         }
         web->extra_headers = dStrdup(extra.c_str());
         if (!r->preflight && !web->content_type && (URL_FLAGS(u) & URL_Post))
            web->content_type = dStrdup("text/plain;charset=UTF-8");
      }
      r->key = a_Capi_open_url(web, resource, r);
      if (!r->key) { r->done = true; r->failed = true; wake(); }
      else a_Timeout_add(30, requestTimeout, r);
      a_Url_free(u);
   }
   static void resource(int op, CacheClient_t *client) {
      Request *r = static_cast<Request *>(client->CbData);
      if (r->failed) return;
      size_t size = client->BufSize > 0 ? static_cast<size_t>(client->BufSize) : 0;
      if (size > r->received) { r->document->received += size - r->received; r->received = size; }
      if (size > 8 * 1024 * 1024 || r->document->received > 64 * 1024 * 1024) {
         r->failed = true; r->done = true; r->document->wake(); return;
      }
      if (op == CA_Send) return;
      a_Timeout_actually_remove(requestTimeout, r);
      r->done = true;
      r->url = URL_STR(client->Url);
      if (op == CA_Close) {
         if (client->Buf && client->BufSize > 0)
            r->body.assign(static_cast<const char *>(client->Buf), client->BufSize);
         Dstr *headers = a_Cache_get_header_direct(client->Url);
         // The cache owns this header buffer.
         if (headers) r->headers.assign(headers->str, headers->len);
         r->status = 200;
         if (r->headers.compare(0, 5, "HTTP/") == 0) {
            size_t space = r->headers.find(' ');
            if (space != std::string::npos) r->status = atoi(r->headers.c_str() + space + 1);
         }
      }
      a_Bw_close_client(r->document->bw, client->Key); r->key = 0;
      r->document->wake();
   }
   void execute(const JsEngine::Script &script, const std::string &source, const std::string &filename) {
      engine.evaluate("globalThis.__currentScript=" + std::to_string(script.node) + ";__dillo.named()", "dillo:script");
      engine.evaluate(source, filename);
      engine.evaluate("globalThis.__currentScript=0", "dillo:script");
      if (script.node && !script.url.empty()) engine.event(script.node, "load");
   }
   Request *scriptRequest(const JsEngine::Script &script) {
      Request *r = new Request;
      r->document = this; r->id = -1; r->isScript = true; r->script = script;
      r->url = resolve(script.url, url); requests.push_back(r);
      open(r, a_Url_new(r->url.c_str(), NULL));
      return r;
   }
   void finishScript(Request *r) {
      // Mark before entering JavaScript: a callback may add another request.
      r->id = -2;
      std::string mime = lower(headerValue(r->headers, "Content-Type"));
      mime = mime.substr(0, mime.find(';'));
      bool javascript = mime == "text/javascript" || mime == "application/javascript" ||
         mime == "application/ecmascript" || mime == "text/ecmascript" || mime == "application/x-javascript";
      bool blockedMime = mime.compare(0, 6, "image/") == 0 || mime.compare(0, 6, "audio/") == 0 ||
         mime.compare(0, 6, "video/") == 0 || mime == "text/csv" ||
         (lower(headerValue(r->headers, "X-Content-Type-Options")) == "nosniff" && !javascript);
      if (r->status >= 200 && r->status < 300 && !blockedMime) execute(r->script, r->body, r->url);
      else engine.event(r->script.node, "error");
      r->body.clear(); r->headers.clear();
   }
   void advance() {
      if (stopped || navigating) return;
      if (engine.hasPendingJobs()) wake(.001);
      if (!engine.valid()) { ready = true; render(); stop(); return; }
      for (size_t i = 0; i < requests.size(); ++i) {
         Request *r = requests[i];
         if (!r->done || r->id == -2) continue;
         if (r->failed) {
            a_Timeout_actually_remove(requestTimeout, r);
            if (r->key) a_Capi_stop_client(r->key, 0);
            r->key = 0; r->status = 0;
         }
         bool cross = origin(r->url) != origin(URL_STR(url));
         if (!r->isScript && cross && !corsAllowed(r->headers, origin(URL_STR(url)), r->credentials == 2))
            r->status = 0;
         if (r->preflight) {
            std::set<std::string> methods = headerList(headerValue(r->headers, "Access-Control-Allow-Methods"));
            std::set<std::string> names = headerList(headerValue(r->headers, "Access-Control-Allow-Headers"));
            bool allow = r->status >= 200 && r->status < 300 &&
               (methods.count(lower(r->method)) || (r->credentials != 2 && methods.count("*")));
            for (std::set<std::string>::iterator n = r->unsafeHeaders.begin(); n != r->unsafeHeaders.end(); ++n)
               if (!names.count(*n) && !(r->credentials != 2 && *n != "authorization" && names.count("*"))) allow = false;
            r->preflight = false;
            if (allow) {
               r->approved = true; r->done = false; r->status = 0;
               r->headers.clear(); r->body.clear(); r->received = 0;
               open(r, a_Url_new(r->url.c_str(), NULL)); continue;
            }
            r->status = 0;
         }
         std::string location = headerValue(r->headers, "Location");
         if (r->status >= 300 && r->status < 400 && !location.empty()) {
            if (++r->redirects > 10) r->status = 0;
            else {
               DilloUrl *base = a_Url_new(r->url.c_str(), NULL);
               std::string destination = resolve(location, base); a_Url_free(base);
               DilloUrl *target = a_Url_new(destination.c_str(), NULL);
               if (r->status == 303 || ((r->status == 301 || r->status == 302) && r->method == "POST")) {
                  r->method = "GET"; r->requestBody.clear();
               }
               if (origin(destination) != origin(r->url)) {
                  r->requestHeaders.erase("authorization"); r->unsafeHeaders.erase("authorization");
               }
               r->approved = false; r->received = 0;
               r->url = destination; r->done = false; r->status = 0;
               r->headers.clear(); r->body.clear(); open(r, target); continue;
            }
         }
         if (r->isScript) {
            if (r->script.async) finishScript(r);
         } else {
            int status = r->status;
            engine.response(r->id, status, status ? r->body : "",
               status ? exposedHeaders(r->headers, !cross, r->credentials == 2) : "", r->url);
            r->id = -2; r->body.clear(); r->headers.clear();
         }
      }
      while (!ordered.empty() && ordered.front()->done && !navigating && !stopped) {
         Request *r = ordered.front(); ordered.pop_front(); finishScript(r);
      }
      if (blocking) {
         if (!blocking->done) return;
         int node = blocking->script.node;
         finishScript(blocking); blocking = NULL; engine.scriptStarted(node);
      }
      JsEngine::Script script;
      while (!navigating && !stopped && engine.nextScript(script)) {
         if (!script.url.empty()) {
            Request *r = scriptRequest(script);
            if (!script.async) {
               if (script.defer) deferred.push_back(r);
               else if (!script.parserInserted) ordered.push_back(r);
               else { blocking = r; return; }
            }
         } else execute(script, script.source, URL_STR(url));
         if (script.parserInserted) engine.scriptStarted(script.node);
      }
      if (engine.parsed() && !navigating && !stopped) {
         while (!deferred.empty()) {
            if (!deferred.front()->done) return;
            Request *r = deferred.front(); deferred.pop_front(); finishScript(r);
         }
         if (!domReady) { domReady = true; engine.interactive(); }
      }
   }
   void maybeLoaded() {
      if (ready || !domReady || !rendered || dirty || engine.hasPendingJobs() || bw->NumPendingStyleSheets) return;
      for (size_t i = 0; i < requests.size(); ++i)
         if (requests[i]->isScript && requests[i]->id != -2) return;
      ready = true; engine.loaded();
   }
   static void tick(void *data) {
      JavascriptDocument *d = static_cast<JavascriptDocument *>(data);
      d->renderQueued = false;
      if (!prefs.javascript_enabled) d->stop();
      if (d->stopped) return;
      d->advance();
      d->engine.evaluate("__dillo.stylesheets()", "dillo:stylesheets");
      if (d->dirty) {
         std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
         if (now >= d->renderNotBefore) {
            d->dirty = false;
            d->renderNotBefore = std::chrono::steady_clock::time_point();
            d->render();
         } else {
            double remaining = std::chrono::duration<double>(d->renderNotBefore - now).count();
            d->wake(remaining);
         }
      }
      // Image-cache callbacks can arrive while a new HTML view is being
      // built. Defer their JS events until its render stack has unwound.
      while (!d->imageEvents.empty() && !d->stopped) {
         ImageEvent event = d->imageEvents.front();
         d->imageEvents.pop_front();
         ++d->eventDepth;
         d->engine.evaluate("__dillo.image(" + std::to_string(event.node) + ",\"" +
            event.type + "\"," + std::to_string(event.width) + "," +
            std::to_string(event.height) + ")", "dillo:image");
         --d->eventDepth;
      }
      d->maybeLoaded();
      if (d->engine.hasPendingJobs()) d->wake(.001);
      a_UIcmd_finish_loading(d->bw);
   }
   void render() {
      if (stopped || navigating || !view) return;
      renderNotBefore = std::chrono::steady_clock::time_point();
      // X11 clipboard conversion completes asynchronously and targets the
      // original FLTK control. Keep that receiver alive until FL_PASTE arrives.
      if (a_Html_controls_busy(view)) { dirty = true; wake(.01); return; }
      rendering = true;
      a_Html_sync_controls(view);
      int selectionStart = 0, selectionEnd = 0;
      int focusedNode = a_Html_control_focus(view, &selectionStart, &selectionEnd);
      std::string html = engine.serialize(true);
      if (html.empty()) { rendering = false; return; }
      // Image cache clients retain renderers owned by the previous widget tree.
      // Cancel them before replacing that tree; the new view reopens the images.
      a_Bw_stop_clients(bw, BW_Img);
      int x = 0, y = 0;
      a_UIcmd_get_scroll_xy(bw, &x, &y);
      DilloHtml *next = new DilloHtml(bw, url, "text/html; charset=utf-8");
      next->javascript = this; next->javascript_render = true;
      next->dw->setDomEventHandler(a_Javascript_ui_event, this);
      next->dw->setStyle(view->rootWidget->getStyle());
      view = next;
      dw::core::Layout *layout = static_cast<dw::core::Layout *>(bw->render_layout);
      layout->setWidget(next->dw);
      next->write(&html[0], html.size(), 1);
      next->finishParsing(0);
      a_Html_control_restore_focus(next, focusedNode, selectionStart, selectionEnd);
      layout->scrollTo(dw::core::HPOS_LEFT, dw::core::VPOS_TOP, x, y, 0, 0);
      rendering = false; rendered = true;
   }
};

JavascriptDocument *a_Javascript_new(DilloHtml *view)
{
   if (!prefs.javascript_enabled || (URL_FLAGS(view->page_url) & URL_SpamSafe)) return NULL;
   const char *scheme = URL_SCHEME(view->page_url);
   if (dStrAsciiCasecmp(scheme, "http") && dStrAsciiCasecmp(scheme, "https") && dStrAsciiCasecmp(scheme, "file")) return NULL;
   JavascriptDocument *document = new JavascriptDocument(view);
   if (!document->engine.valid()) { delete document; return NULL; }
   return document;
}
void a_Javascript_delete(JavascriptDocument *document, DilloHtml *view)
{ if (document->view == view) delete document; }
void a_Javascript_callback(int op, CacheClient_t *client)
{
   JavascriptDocument *document = static_cast<JavascriptDocument *>(client->CbData);
   a_Javascript_write(document, static_cast<const char *>(client->Buf), client->BufSize, op != CA_Send, client->Key);
}
void a_Javascript_write(JavascriptDocument *d, const char *buf, size_t size, bool eof, int key)
{
   d->rootKey = key;
   if (d->stopped) { if (eof) a_Bw_close_client(d->bw, key); return; }
   if (size >= d->offset) { d->engine.append(buf + d->offset, size - d->offset); d->offset = size; }
   if (eof) { d->eof = true; d->engine.finish(); }
   d->advance();
   d->changed();
   if (eof) a_Bw_close_client(d->bw, key);
}
void a_Javascript_render(JavascriptDocument *d) { if (d) d->changed(); }
bool a_Javascript_event(JavascriptDocument *d, int node, const char *type)
{
   if (!d || d->stopped || d->rendering) return true;
   a_Html_sync_controls(d->view); ++d->eventDepth;
   bool result = d->engine.event(node, type); --d->eventDepth;
   if (d->engine.hasPendingJobs()) d->wake(.001);
   return result;
}
void a_Javascript_image_event(JavascriptDocument *d, int node,
                              const char *type, unsigned width, unsigned height)
{
   if (d) d->imageEvent(node, type, width, height);
}
bool a_Javascript_ui_event(void *data, int node, const char *type, const char *key, int code)
{
   JavascriptDocument *d = static_cast<JavascriptDocument *>(data);
   if (!d || d->stopped || d->rendering || !node) return true;
   a_Html_sync_controls(d->view);
   ++d->eventDepth;
   if (!strcmp(type, "click")) {
      int start = 0, end = 0;
      int focused = a_Html_control_focus(d->view, &start, &end);
      if (focused != d->focusedControl) {
         if (d->focusedControl) d->engine.event(d->focusedControl, "blur");
         if (focused) d->engine.event(focused, "focus");
      }
   }
   dw::core::DomMouseEvent nativeMouse;
   JsEngine::MouseEvent mouse;
   bool hasMouse = !strcmp(type, "click") &&
      static_cast<dw::core::Layout *>(d->bw->render_layout)->getDomMouseEvent(&nativeMouse);
   if (hasMouse) {
      mouse.clientX = nativeMouse.clientX; mouse.clientY = nativeMouse.clientY;
      mouse.pageX = nativeMouse.pageX; mouse.pageY = nativeMouse.pageY;
      mouse.screenX = nativeMouse.screenX; mouse.screenY = nativeMouse.screenY;
      mouse.button = nativeMouse.button; mouse.buttons = nativeMouse.buttons; mouse.detail = nativeMouse.detail;
      mouse.ctrlKey = nativeMouse.ctrlKey; mouse.shiftKey = nativeMouse.shiftKey;
      mouse.altKey = nativeMouse.altKey; mouse.metaKey = nativeMouse.metaKey;
   }
   bool result = d->engine.event(node, type, key, code, hasMouse ? &mouse : NULL); --d->eventDepth;
   if (d->engine.hasPendingJobs()) d->wake(.001);
   return result;
}
int a_Javascript_node(JavascriptDocument *d, const char *selector)
{ return d ? d->engine.find(selector) : 0; }
void a_Javascript_set_value(JavascriptDocument *d, int node, const char *value)
{ if (d) d->engine.setControlState(node, "value", value); }
void a_Javascript_set_state(JavascriptDocument *d, int node, const char *name, const char *value)
{ if (d) d->engine.setControlState(node, name, value); }
static JavascriptDocument *current(BrowserWindow *bw)
{
   DilloHtml *html = static_cast<DilloHtml *>(a_Bw_get_current_doc(bw));
   return html ? html->javascript : NULL;
}
int a_Javascript_history_traverse(BrowserWindow *bw, const DilloUrl *url)
{
   JavascriptDocument *d = current(bw);
   if (!d || d->stopped) return 0;
   std::string oldUrl = URL_STR(d->url);
   d->setDocumentUrl(url);
   d->engine.historyTraversal(oldUrl);
   if (d->engine.hasPendingJobs()) d->wake(.001);
   return 1;
}
void a_Javascript_disable_all()
{ for (int i = 0; i < a_Bw_num(); ++i) a_Javascript_stop(a_Bw_get(i)); }
void a_Javascript_forget_window(BrowserWindow *bw) { JavascriptStorage::forget(bw); }
void a_Javascript_stop(BrowserWindow *bw)
{ JavascriptDocument *d = current(bw); if (d) d->stop(); }
int a_Javascript_pending(BrowserWindow *bw)
{
   JavascriptDocument *d = current(bw);
   if (!d || d->stopped) return 0;
   if (!d->ready || d->renderQueued || d->blocking) return 1;
   for (size_t i = 0; i < d->requests.size(); ++i) if (d->requests[i]->key) return 1;
   return 0;
}
char *a_Javascript_dump(BrowserWindow *bw)
{ JavascriptDocument *d = current(bw); return d ? dStrdup(d->engine.serialize().c_str()) : NULL; }
char *a_Javascript_evaluate(BrowserWindow *bw, const char *expression)
{
   JavascriptDocument *d = current(bw);
   if (!d || d->stopped) return NULL;
   a_Html_sync_controls(d->view);
   std::string result = d->engine.stringValue(expression);
   d->engine.evaluate("", "dillo:inspect-jobs");
   d->advance();
   if (d->engine.hasPendingJobs()) d->wake(.001);
   return dStrdup(result.c_str());
}

#else
JavascriptDocument *a_Javascript_new(DilloHtml *) { return NULL; }
void a_Javascript_delete(JavascriptDocument *, DilloHtml *) {}
void a_Javascript_callback(int, CacheClient_t *) {}
void a_Javascript_write(JavascriptDocument *, const char *, size_t, bool, int) {}
void a_Javascript_render(JavascriptDocument *) {}
void a_Javascript_image_event(JavascriptDocument *, int, const char *, unsigned, unsigned) {}
bool a_Javascript_event(JavascriptDocument *, int, const char *) { return true; }
bool a_Javascript_ui_event(void *, int, const char *, const char *, int) { return true; }
int a_Javascript_node(JavascriptDocument *, const char *) { return 0; }
void a_Javascript_set_value(JavascriptDocument *, int, const char *) {}
void a_Javascript_set_state(JavascriptDocument *, int, const char *, const char *) {}
void a_Javascript_disable_all() {}
void a_Javascript_forget_window(BrowserWindow *bw) { JavascriptStorage::forget(bw); }
void a_Javascript_stop(BrowserWindow *) {}
int a_Javascript_history_traverse(BrowserWindow *, const DilloUrl *) { return 0; }
int a_Javascript_pending(BrowserWindow *) { return 0; }
char *a_Javascript_dump(BrowserWindow *) { return NULL; }
char *a_Javascript_evaluate(BrowserWindow *, const char *) { return NULL; }
#endif
