/* JavaScript and HTML5 document state, independent of the rendered view. */
#include "jsengine.hh"
#include "config.h"

#ifdef ENABLE_JAVASCRIPT
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#include "quickjs.h"
#include <lexbor/html/html.h>
#include <lexbor/css/css.h>
#include <lexbor/selectors/selectors.h>
#include <lexbor/url/url.h>
#include <map>
#include <set>
#include <deque>
#include <chrono>
#include <algorithm>
#include <cstring>
#include <cctype>

namespace {
const size_t MaxInput = 32 * 1024 * 1024;
const size_t MaxNodes = 200000;
const size_t MaxText = 32 * 1024 * 1024;
const char bootstrap[] =
#include "js-dom.inc"
;
const lxb_char_t *bytes(const std::string &s)
{ return reinterpret_cast<const lxb_char_t *>(s.data()); }
std::string str(const lxb_char_t *s, size_t len)
{ return s ? std::string(reinterpret_cast<const char *>(s), len) : std::string(); }
std::string jsString(JSContext *ctx, JSValueConst v)
{
   size_t size = 0;
   const char *s = JS_ToCStringLen(ctx, &size, v);
   std::string result = s ? std::string(s, size) : std::string();
   JS_FreeCString(ctx, s);
   return result;
}
lxb_status_t serializeCallback(const lxb_char_t *data, size_t len, void *ctx)
{
   static_cast<std::string *>(ctx)->append(reinterpret_cast<const char *>(data), len);
   return LXB_STATUS_OK;
}
}

struct JsEngine::Impl {
   Host *host;
   JSRuntime *runtime;
   JSContext *context;
   lxb_html_parser_t *parser;
   lxb_html_document_t *document;
   lxb_html_tokenizer_token_f treeCallback;
   void *treeContext;
   std::vector<lxb_dom_node_t *> nodes;
   std::map<lxb_dom_node_t *, int> handles;
   std::set<lxb_dom_node_t *> scheduled;
   std::map<lxb_dom_node_t *, bool> scriptAsync;
   std::map<int, std::map<std::string, std::string> > controlState;
   std::deque<Script> scripts;
   std::string url, input, writes;
   size_t inputTotal, textTotal;
   uint32_t revision;
   unsigned checkpointJobs;
   bool ended, eof, blocked, stopped, running;
   std::chrono::steady_clock::time_point deadline;

   Impl(Host *h, const std::string &u): host(h), runtime(NULL), context(NULL),
      parser(NULL), document(NULL), treeCallback(NULL), treeContext(NULL), url(u),
      inputTotal(0), textTotal(0), revision(0), checkpointJobs(0), ended(false), eof(false), blocked(false),
      stopped(false), running(false) { nodes.push_back(NULL); }

   ~Impl() {
      if (context) JS_FreeContext(context);
      if (runtime) JS_FreeRuntime(runtime);
      if (document) lxb_html_document_destroy(document);
      if (parser) lxb_html_parser_destroy(parser);
   }

   int id(lxb_dom_node_t *n) {
      if (!n) return 0;
      std::map<lxb_dom_node_t *, int>::iterator it = handles.find(n);
      if (it != handles.end()) return it->second;
      if (nodes.size() >= MaxNodes) { stopped = true; return 0; }
      int result = static_cast<int>(nodes.size());
      nodes.push_back(n); handles[n] = result;
      return result;
   }
   lxb_dom_node_t *node(int n) {
      return n > 0 && static_cast<size_t>(n) < nodes.size() ? nodes[n] : NULL;
   }
   std::string attr(lxb_dom_node_t *n, const std::string &name) {
      if (!n || n->type != LXB_DOM_NODE_TYPE_ELEMENT) return "";
      size_t len = 0;
      const lxb_char_t *value = lxb_dom_element_get_attribute(
         lxb_dom_interface_element(n), bytes(name), name.size(), &len);
      return str(value, len);
   }
   bool has(lxb_dom_node_t *n, const char *name) {
      return n && n->type == LXB_DOM_NODE_TYPE_ELEMENT &&
         lxb_dom_element_has_attribute(lxb_dom_interface_element(n),
            reinterpret_cast<const lxb_char_t *>(name), strlen(name));
   }
   std::string text(lxb_dom_node_t *n) {
      size_t len = 0;
      lxb_char_t *value = lxb_dom_node_text_content(n, &len);
      std::string result = str(value, len);
      lxb_dom_document_destroy_text(n->owner_document, value);
      return result;
   }
   bool connected(lxb_dom_node_t *n) {
      while (n && n->parent) n = n->parent;
      return n == lxb_dom_interface_node(document);
   }
   void discover(lxb_dom_node_t *n, bool parserInserted = false) {
      if (!n || !connected(n)) return;
      lxb_dom_node_t *root = n;
      std::vector<lxb_dom_node_t *> todo(1, n);
      while (!todo.empty()) {
         n = todo.back(); todo.pop_back();
      if (n->type == LXB_DOM_NODE_TYPE_ELEMENT && n->local_name == LXB_TAG_SCRIPT &&
          !scheduled.count(n)) {
         std::string type = attr(n, "type");
         std::transform(type.begin(), type.end(), type.begin(), ::tolower);
         if (type.empty() || type == "text/javascript" ||
             type == "application/javascript" || type == "application/ecmascript" ||
             type == "text/ecmascript") {
            Script s;
            s.node = id(n); s.source = text(n); s.url = attr(n, "src");
            if (s.source.empty() && s.url.empty()) continue;
            scheduled.insert(n);
            if (parserInserted && n == root && !scriptAsync.count(n)) scriptAsync[n] = false;
            s.parserInserted = parserInserted && n == root;
            s.async = has(n, "async") || (!s.parserInserted &&
               (!scriptAsync.count(n) || scriptAsync[n]));
            s.defer = s.parserInserted && has(n, "defer");
            scripts.push_back(s);
         }
      }
         for (lxb_dom_node_t *child = n->last_child; child; child = child->prev) todo.push_back(child);
      }
   }
   static lxb_html_token_t *token(lxb_html_tokenizer_t *tkz,
                                 lxb_html_token_t *t, void *data) {
      Impl *self = static_cast<Impl *>(data);
      lxb_dom_node_t *script = NULL;
      if (t->tag_id == LXB_TAG_SCRIPT && (t->type & LXB_HTML_TOKEN_TYPE_CLOSE))
         script = lxb_html_tree_current_node(self->parser->tree);
      lxb_html_token_t *result = self->treeCallback(tkz, t, self->treeContext);
      ++self->revision;
      if (script && script->local_name == LXB_TAG_SCRIPT) {
         self->discover(script, true);
         if (!self->scripts.empty()) self->blocked = true;
      }
      return result;
   }
   void pump() {
      if (stopped || ended || blocked) return;
      while (!input.empty() && !blocked && !stopped) {
         size_t size = input.find('>');
         size = size == std::string::npos ? input.size() : size + 1;
         size = std::min(size, static_cast<size_t>(8192));
         std::string part = input.substr(0, size);
         input.erase(0, size);
         if (lxb_html_parse_chunk_process(parser, bytes(part), size) != LXB_STATUS_OK) {
            stopped = true; host->log("HTML5 parser failed");
         }
         budget();
      }
      if (eof && input.empty() && !blocked && !ended) {
         ended = true;
         if (lxb_html_parse_chunk_end(parser) != LXB_STATUS_OK) {
            stopped = true; host->log("HTML5 parser could not finish");
         }
      }
   }
   static int interrupt(JSRuntime *, void *data) {
      Impl *self = static_cast<Impl *>(data);
      return self->stopped || (self->running &&
         std::chrono::steady_clock::now() >= self->deadline);
   }
   bool budget() {
      // Lexbor owns detached nodes too. Include its arenas, not only JS handles.
      lxb_dom_document_t *d = lxb_dom_interface_document(document);
      size_t allocated = 0;
      lexbor_mem_t *arenas[] = {d->mraw->mem, d->text->mem};
      for (size_t i = 0; i < 2; ++i)
         for (lexbor_mem_chunk_t *chunk = arenas[i]->chunk_first; chunk; chunk = chunk->next)
            allocated += chunk->size;
      if (allocated > 64 * 1024 * 1024 || nodes.size() >= MaxNodes) {
         stopped = true; host->log("DOM allocation limit exceeded"); return false;
      }
      return true;
   }
   void exception(const std::string &filename) {
      JSValue e = JS_GetException(context);
      std::string message = jsString(context, e);
      JSValue stack = JS_GetPropertyStr(context, e, "stack");
      if (!JS_IsUndefined(stack)) message += "\n" + jsString(context, stack);
      host->log(filename + ": " + message);
      JS_FreeValue(context, stack); JS_FreeValue(context, e);
   }
   bool eval(const std::string &source, const std::string &filename) {
      if (stopped || !context) return false;
      bool nested = running;
      if (!nested) deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
      running = true;
      JSValue result = JS_Eval(context, source.data(), source.size(),
                               filename.c_str(), JS_EVAL_TYPE_GLOBAL);
      bool ok = !JS_IsException(result);
      if (!ok) exception(filename);
      JS_FreeValue(context, result);
      // Native focus/control callbacks may synchronously enter JavaScript.
      // The outer task owns its deadline and the microtask checkpoint.
      if (nested) return ok;
      // Layout requested by a script can spend time in the native renderer.
      // Give the microtask checkpoint its own budget after that task returns.
      deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
      while (JS_IsJobPending(runtime) && !stopped) {
         // Yield long finite checkpoints to the GUI; retain a total-job cap
         // across yields so a self-replenishing queue cannot run forever.
         if (std::chrono::steady_clock::now() >= deadline) break;
         if (++checkpointJobs > 10000) {
            host->log(filename + ": microtask checkpoint exceeded its execution budget (" +
                      std::to_string(checkpointJobs) + " jobs)");
            stopped = true; ok = false; break;
         }
         JSContext *ctx = NULL;
         if (JS_ExecutePendingJob(runtime, &ctx) < 0) exception(filename);
      }
      if (!JS_IsJobPending(runtime)) checkpointJobs = 0;
      running = false;
      return ok;
   }
   static lxb_status_t match(lxb_dom_node_t *n, lxb_css_selector_specificity_t,
                             void *data) {
      static_cast<std::vector<lxb_dom_node_t *> *>(data)->push_back(n);
      return LXB_STATUS_OK;
   }
   bool query(lxb_dom_node_t *root, const std::string &selector, bool matches,
               std::vector<lxb_dom_node_t *> &result) {
      lxb_css_parser_t *css = lxb_css_parser_create();
      lxb_selectors_t *select = lxb_selectors_create();
      bool ok = css && select && lxb_css_parser_init(css, NULL) == LXB_STATUS_OK &&
         lxb_selectors_init(select) == LXB_STATUS_OK;
      if (ok) {
         lxb_css_selector_list_t *list = lxb_css_selectors_parse(css,
            bytes(selector), selector.size());
         ok = list && css->status == LXB_STATUS_OK;
         if (ok) {
            lxb_selectors_opt_set(select, LXB_SELECTORS_OPT_MATCH_FIRST);
            lxb_status_t status = matches ?
               lxb_selectors_match_node(select, root, list, match, &result) :
               lxb_selectors_find(select, root, list, match, &result);
            ok = status == LXB_STATUS_OK;
         }
         if (list) lxb_css_selector_list_destroy_memory(list);
      }
      if (select) lxb_selectors_destroy(select, true);
      if (css) lxb_css_parser_destroy(css, true);
      return ok;
   }
   std::string serialize(lxb_dom_node_t *n, bool deep) {
      std::string result;
      if (deep) lxb_html_serialize_deep_cb(n, serializeCallback, &result);
      else lxb_html_serialize_tree_cb(n, serializeCallback, &result);
      return result;
   }
   static JSValue parseUrl(JSContext *ctx, const std::string &input, const std::string &base,
                           const std::string &property, const std::string &value) {
      if (input.size() + base.size() + value.size() > 1024 * 1024)
         return JS_ThrowRangeError(ctx, "URL is too long");
      lxb_url_parser_t *parser = lxb_url_parser_create();
      if (!parser || lxb_url_parser_init(parser, NULL) != LXB_STATUS_OK) {
         if (parser) lxb_url_parser_destroy(parser, true);
         return JS_ThrowInternalError(ctx, "Cannot create URL parser");
      }
      lxb_url_t *baseUrl = base.empty() ? NULL : lxb_url_parse(parser, NULL, bytes(base), base.size());
      lxb_url_parser_clean(parser);
      lxb_url_t *url = (!base.empty() && !baseUrl) ? NULL : lxb_url_parse(parser, baseUrl, bytes(input), input.size());
      JSValue result = JS_UNDEFINED;
      if (!url) result = JS_ThrowTypeError(ctx, "Invalid URL");
      else {
         lxb_url_parser_clean(parser);
         if (property == "protocol") lxb_url_api_protocol_set(url, parser, bytes(value), value.size());
         else if (property == "username") lxb_url_api_username_set(url, bytes(value), value.size());
         else if (property == "password") lxb_url_api_password_set(url, bytes(value), value.size());
         else if (property == "host") lxb_url_api_host_set(url, parser, bytes(value), value.size());
         else if (property == "hostname") lxb_url_api_hostname_set(url, parser, bytes(value), value.size());
         else if (property == "port") lxb_url_api_port_set(url, parser, bytes(value), value.size());
         else if (property == "pathname") lxb_url_api_pathname_set(url, parser, bytes(value), value.size());
         else if (property == "search") lxb_url_api_search_set(url, parser, bytes(value), value.size());
         else if (property == "hash") lxb_url_api_hash_set(url, parser, bytes(value), value.size());
         std::string href, host, path, port;
         lxb_url_serialize(url, serializeCallback, &href, false);
         lxb_url_serialize_host(&url->host, serializeCallback, &host);
         lxb_url_serialize_path(&url->path, serializeCallback, &path);
         lxb_url_serialize_port(url, serializeCallback, &port);
         std::string scheme = str(url->scheme.name.data, url->scheme.name.length) + ":";
         std::string authority = host + (port.empty() ? "" : ":" + port);
         result = JS_NewObject(ctx);
         const char *names[] = {"href", "protocol", "host", "hostname", "port", "pathname", "search", "hash", "origin", "username", "password"};
         std::string values[] = {href, scheme, authority, host, port, path,
            url->query.length ? "?" + str(url->query.data, url->query.length) : "",
            url->fragment.length ? "#" + str(url->fragment.data, url->fragment.length) : "",
            (scheme == "http:" || scheme == "https:" || scheme == "ftp:") ? scheme + "//" + authority : "null",
            str(url->username.data, url->username.length), str(url->password.data, url->password.length)};
         for (size_t i = 0; i < sizeof(names)/sizeof(names[0]); ++i)
            JS_SetPropertyStr(ctx, result, names[i], JS_NewStringLen(ctx, values[i].data(), values[i].size()));
      }
      lxb_url_parser_memory_destroy(parser); lxb_url_parser_destroy(parser, true);
      return result;
   }
   static JSValue microtask(JSContext *ctx, int, JSValueConst *argv) {
      return JS_Call(ctx, argv[0], JS_UNDEFINED, 0, NULL);
   }
   static JSValue native(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv) {
      Impl *s = static_cast<Impl *>(JS_GetContextOpaque(ctx));
      if (!argc || s->stopped) return JS_UNDEFINED;
      std::string op = jsString(ctx, argv[0]);
      if (op == "microtask") {
         if (argc < 3 || !JS_IsFunction(ctx, argv[2]))
            return JS_ThrowTypeError(ctx, "Microtask callback must be a function");
         if (JS_EnqueueJob(ctx, microtask, 1, argv + 2) < 0) return JS_EXCEPTION;
         return JS_UNDEFINED;
      }
      if (op == "clone-serialize") {
         if (argc < 3) return JS_ThrowTypeError(ctx, "Missing clone value");
         size_t size = 0;
         uint8_t *data = JS_WriteObject(ctx, &size, argv[2], JS_WRITE_OBJ_REFERENCE);
         if (!data) return JS_EXCEPTION;
         if (size > 512 * 1024) {
            js_free(ctx, data);
            return JS_ThrowRangeError(ctx, "History state exceeds 512 KiB");
         }
         static const char hex[] = "0123456789abcdef";
         std::string encoded(size * 2, '0');
         for (size_t i = 0; i < size; ++i) {
            encoded[2*i] = hex[data[i] >> 4]; encoded[2*i+1] = hex[data[i] & 15];
         }
         js_free(ctx, data);
         return JS_NewStringLen(ctx, encoded.data(), encoded.size());
      }
      if (op == "clone-deserialize") {
         std::string encoded = argc > 2 ? jsString(ctx, argv[2]) : "";
         if (encoded.empty()) return JS_NULL;
         if (encoded.size() % 2 || encoded.size() > 1024 * 1024)
            return JS_ThrowTypeError(ctx, "Invalid cloned state");
         std::vector<uint8_t> data(encoded.size()/2);
         static const char hex[] = "0123456789abcdef";
         for (size_t i = 0; i < encoded.size(); ++i) {
            const char *digit = strchr(hex, encoded[i]);
            if (!encoded[i] || !digit) return JS_ThrowTypeError(ctx, "Invalid cloned state");
            data[i/2] = static_cast<uint8_t>((data[i/2] << 4) | (digit - hex));
         }
         // Deliberately exclude bytecode and shared buffers from both APIs.
         return JS_ReadObject(ctx, &data[0], data.size(), JS_READ_OBJ_REFERENCE);
      }
      int32_t handle = 0;
      if (argc > 1) JS_ToInt32(ctx, &handle, argv[1]);
      lxb_dom_node_t *n = s->node(handle);
      std::string key = argc > 2 ? jsString(ctx, argv[2]) : "";
      std::string value = argc > 3 ? jsString(ctx, argv[3]) : "";
      if (op == "urlparse") {
         std::string property = argc > 4 ? jsString(ctx, argv[4]) : "";
         std::string assigned = argc > 5 ? jsString(ctx, argv[5]) : "";
         return parseUrl(ctx, key, value, property, assigned);
      }
      if (op == "revision") return JS_NewUint32(ctx, s->revision);
      if (op == "log") { s->host->log(key); return JS_UNDEFINED; }
      if (op == "ua") return JS_NewString(ctx, s->host->userAgent().c_str());
      if (op == "url") return JS_NewString(ctx, s->url.c_str());
      if (op == "title") { s->host->title(key); return JS_UNDEFINED; }
      if (op == "navigate") { s->host->navigate(key, handle != 0); return JS_UNDEFINED; }
      if (op == "timer") {
         int32_t delay = 0; if (argc > 2) JS_ToInt32(ctx, &delay, argv[2]);
         s->host->timer(handle, delay); return JS_UNDEFINED;
      }
      if (op == "request") {
         std::string body = argc > 4 ? jsString(ctx, argv[4]) : "";
         std::string headers = argc > 5 ? jsString(ctx, argv[5]) : "";
         int32_t credentials = 1; if (argc > 6) JS_ToInt32(ctx, &credentials, argv[6]);
         s->host->request(handle, key, value, body, headers, credentials); return JS_UNDEFINED;
      }
      if (op == "abort") { s->host->abort(handle); return JS_UNDEFINED; }
      if (op == "write") {
         if (s->inputTotal + key.size() > MaxInput)
            return JS_ThrowRangeError(ctx, "Document input limit exceeded");
         s->writes += key; s->inputTotal += key.size(); return JS_UNDEFINED;
      }
      if (op == "create") {
         if (!s->budget()) return JS_ThrowRangeError(ctx, "Document allocation limit exceeded");
         if (s->nodes.size() >= MaxNodes || s->textTotal + key.size() > MaxText)
            return JS_ThrowRangeError(ctx, "Document allocation limit exceeded");
         lxb_dom_document_t *d = lxb_dom_interface_document(s->document);
         lxb_dom_node_t *created = NULL;
         if (handle == 1)
            created = lxb_dom_interface_node(lxb_dom_document_create_element(d, bytes(key), key.size(), NULL));
         else if (handle == 3)
            created = lxb_dom_interface_node(lxb_dom_document_create_text_node(d, bytes(key), key.size()));
         else if (handle == 8)
            created = lxb_dom_interface_node(lxb_dom_document_create_comment(d, bytes(key), key.size()));
         else if (handle == 11)
            created = lxb_dom_interface_node(lxb_dom_document_create_document_fragment(d));
         s->textTotal += key.size();
         if (!created) return JS_ThrowInternalError(ctx, "Cannot create DOM node");
         return JS_NewInt32(ctx, s->id(created));
      }
      if (!n) return JS_ThrowTypeError(ctx, "Invalid DOM node");
      if (op == "get") {
         if (key == "type") return JS_NewInt32(ctx, n->type);
         lxb_dom_node_t *related = NULL;
         if (key == "parent") related = n->parent;
         else if (key == "first") related = n->first_child;
         else if (key == "last") related = n->last_child;
         else if (key == "next") related = n->next;
         else if (key == "prev") related = n->prev;
         else if (key == "connected") return JS_NewBool(ctx, s->connected(n));
         else if (key == "name") {
            size_t len = 0;
            const lxb_char_t *name = lxb_dom_node_name(n, &len);
            return JS_NewStringLen(ctx, reinterpret_cast<const char *>(name), len);
         } else if (key == "text") {
            std::string text = s->text(n); return JS_NewStringLen(ctx, text.data(), text.size());
         } else if (key == "inner" || key == "outer") {
            std::string html = s->serialize(n, key == "inner");
            return JS_NewStringLen(ctx, html.data(), html.size());
         }
         return JS_NewInt32(ctx, s->id(related));
      }
      if (op == "children") {
         JSValue result = JS_NewArray(ctx); uint32_t i = 0;
         for (lxb_dom_node_t *c = n->first_child; c; c = c->next)
            JS_SetPropertyUint32(ctx, result, i++, JS_NewInt32(ctx, s->id(c)));
         return result;
      }
      if (op == "query" || op == "matches") {
         std::vector<lxb_dom_node_t *> found;
         if (!s->query(n, key, op == "matches", found))
            return JS_ThrowSyntaxError(ctx, "Invalid selector: %s", key.c_str());
         if (op == "matches") return JS_NewBool(ctx, !found.empty());
         JSValue result = JS_NewArray(ctx);
         for (size_t i = 0; i < found.size(); ++i)
            JS_SetPropertyUint32(ctx, result, i, JS_NewInt32(ctx, s->id(found[i])));
         return result;
      }
      if (op == "clone") {
         if (!s->budget()) return JS_ThrowRangeError(ctx, "Document allocation limit exceeded");
         lxb_dom_node_t *copy = lxb_dom_node_clone(n, key == "true");
         if (!copy) return JS_ThrowInternalError(ctx, "Cannot clone DOM node");
         if (s->scheduled.count(n)) s->scheduled.insert(copy);
         return JS_NewInt32(ctx, s->id(copy));
      }
      if (op == "attrs") {
         JSValue result = JS_NewArray(ctx); uint32_t i = 0;
         if (n->type == LXB_DOM_NODE_TYPE_ELEMENT)
            for (lxb_dom_attr_t *a = lxb_dom_interface_element(n)->first_attr; a; a = a->next) {
               size_t len = 0; const lxb_char_t *name = lxb_dom_attr_qualified_name(a, &len);
               JSValue pair = JS_NewArray(ctx);
               JS_SetPropertyUint32(ctx, pair, 0, JS_NewStringLen(ctx, reinterpret_cast<const char *>(name), len));
               const lxb_char_t *val = lxb_dom_attr_value(a, &len);
               JS_SetPropertyUint32(ctx, pair, 1, JS_NewStringLen(ctx, reinterpret_cast<const char *>(val), len));
               JS_SetPropertyUint32(ctx, result, i++, pair);
            }
         return result;
      }
      if (op == "attr") {
         if (n->type != LXB_DOM_NODE_TYPE_ELEMENT) return JS_NULL;
         size_t len = 0;
         const lxb_char_t *v = lxb_dom_element_get_attribute(lxb_dom_interface_element(n),
            bytes(key), key.size(), &len);
         return v ? JS_NewStringLen(ctx, reinterpret_cast<const char *>(v), len) : JS_NULL;
      }
      if (op == "control") {
         // A synchronous style/layout flush is host work. Charge JavaScript
         // execution against its own budget, excluding time inside Dw.
         std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
         std::string v = s->host->control(handle, key, argc > 3 ? &value : NULL);
         if (s->running) s->deadline += std::chrono::steady_clock::now() - started;
         return JS_NewStringLen(ctx, v.data(), v.size());
      }
      if (op == "scriptasync") {
         if (argc > 2) s->scriptAsync[n] = key == "true";
         return JS_NewBool(ctx, s->has(n, "async") ||
            !s->scriptAsync.count(n) || s->scriptAsync[n]);
      }
      if (op == "state") {
         if (argc > 3) {
            std::map<std::string, std::string> &state = s->controlState[handle];
            std::map<std::string, std::string>::iterator current = state.find(key);
            if (current == state.end() || current->second != value) {
               state[key] = value;
               ++s->revision;
               if (key == "focus")
                  s->host->focusChanged(handle, value == "true");
               s->host->changed(key == "focus" ? 150 : 0);
            }
         }
         return s->controlState[handle].count(key) ? JS_NewString(ctx, s->controlState[handle][key].c_str()) : JS_NULL;
      }
      if (key.size() + value.size() > MaxText - std::min(s->textTotal, MaxText))
         return JS_ThrowRangeError(ctx, "Document text limit exceeded");
      if (!s->budget()) return JS_ThrowRangeError(ctx, "Document allocation limit exceeded");
      // Detached node updates do not affect layout until they are inserted.
      // Avoid replacing the live view for each new Image/src assignment so
      // its in-flight image preload remains attached to this document.
      bool affectsDocument = s->connected(n);
      if (op == "setattr" || op == "removeattr") {
         if (n->type != LXB_DOM_NODE_TYPE_ELEMENT)
            return JS_ThrowTypeError(ctx, "Attributes require an element");
         if (op == "setattr") {
            if (!lxb_dom_element_set_attribute(lxb_dom_interface_element(n), bytes(key), key.size(), bytes(value), value.size()))
               return JS_ThrowInternalError(ctx, "Cannot set attribute");
         } else lxb_dom_element_remove_attribute(lxb_dom_interface_element(n), bytes(key), key.size());
         if (key == "src" && n->local_name == LXB_TAG_SCRIPT && s->ended)
            s->discover(n);
      } else if (op == "text" || op == "inner") {
         // Detach rather than destroy: JavaScript may still reference removed nodes.
         while (n->first_child) lxb_dom_node_remove(n->first_child);
         if (op == "text") {
            if (n->type == LXB_DOM_NODE_TYPE_TEXT || n->type == LXB_DOM_NODE_TYPE_COMMENT)
               lxb_dom_node_text_content_set(n, bytes(key), key.size());
            else if (!key.empty()) {
               lxb_dom_text_t *t = lxb_dom_document_create_text_node(n->owner_document, bytes(key), key.size());
               if (t) lxb_dom_node_insert_child(n, lxb_dom_interface_node(t));
            }
         } else {
            lxb_html_parser_t *p = lxb_html_parser_create();
            if (!p || lxb_html_parser_init(p) != LXB_STATUS_OK) {
               if (p) lxb_html_parser_destroy(p);
               return JS_ThrowInternalError(ctx, "Cannot create fragment parser");
            }
            lxb_dom_node_t *fragment = lxb_html_parse_fragment_by_tag_id(p, s->document,
               n->type == LXB_DOM_NODE_TYPE_ELEMENT ? n->local_name : static_cast<lxb_tag_id_t>(LXB_TAG_DIV),
               LXB_NS_HTML, bytes(key), key.size());
            if (fragment) while (fragment->first_child) {
               lxb_dom_node_t *c = fragment->first_child;
               lxb_dom_node_remove(c); lxb_dom_node_insert_child(n, c);
               // Scripts inserted through innerHTML are inert.
               std::vector<lxb_dom_node_t *> todo(1, c);
               while (!todo.empty()) {
                  lxb_dom_node_t *v = todo.back(); todo.pop_back();
                  if (v->local_name == LXB_TAG_SCRIPT) s->scheduled.insert(v);
                  for (lxb_dom_node_t *q = v->first_child; q; q = q->next) todo.push_back(q);
               }
            }
            lxb_html_parser_destroy(p);
         }
      } else if (op == "insert") {
         int32_t childId = 0, beforeId = 0;
         if (argc > 2) JS_ToInt32(ctx, &childId, argv[2]);
         if (argc > 3) JS_ToInt32(ctx, &beforeId, argv[3]);
         lxb_dom_node_t *child = s->node(childId), *before = s->node(beforeId);
         if (!child || (before && before->parent != n))
            return JS_ThrowTypeError(ctx, "Invalid insertion point");
         for (lxb_dom_node_t *p = n; p; p = p->parent)
            if (p == child) return JS_ThrowTypeError(ctx, "DOM hierarchy cycle");
         if (before == child) return JS_UNDEFINED;
         if (child->type == LXB_DOM_NODE_TYPE_DOCUMENT_FRAGMENT) {
            while (child->first_child) {
               lxb_dom_node_t *c = child->first_child; lxb_dom_node_remove(c);
               if (before) lxb_dom_node_insert_before(before, c); else lxb_dom_node_insert_child(n, c);
               s->discover(c);
            }
         } else {
            lxb_dom_node_remove(child);
            if (before) lxb_dom_node_insert_before(before, child); else lxb_dom_node_insert_child(n, child);
            s->discover(child);
         }
      } else if (op == "remove") lxb_dom_node_remove(n);
      else return JS_ThrowTypeError(ctx, "Unknown DOM operation");
      s->textTotal += key.size() + value.size();
      ++s->revision;
      if (affectsDocument) s->host->changed();
      return JS_UNDEFINED;
   }
};

JsEngine::JsEngine(Host *host, const std::string &url): impl(new Impl(host, url))
{
   Impl *s = impl;
   s->parser = lxb_html_parser_create();
   if (!s->parser || lxb_html_parser_init(s->parser) != LXB_STATUS_OK) return;
   lxb_html_parser_scripting_set(s->parser, true);
   s->document = lxb_html_parse_chunk_begin(s->parser);
   if (!s->document) return;
   s->id(lxb_dom_interface_node(s->document));
   s->treeCallback = s->parser->tkz->callback_token_done;
   s->treeContext = s->parser->tkz->callback_token_ctx;
   lxb_html_tokenizer_callback_token_done_set(s->parser->tkz, Impl::token, s);
   s->runtime = JS_NewRuntime();
   if (!s->runtime) return;
   JS_SetMemoryLimit(s->runtime, 128 * 1024 * 1024);
   JS_SetMaxStackSize(s->runtime, 1024 * 1024);
   JS_SetInterruptHandler(s->runtime, Impl::interrupt, s);
   s->context = JS_NewContext(s->runtime);
   if (!s->context) return;
   JS_SetContextOpaque(s->context, s);
   JSValue global = JS_GetGlobalObject(s->context);
   JS_SetPropertyStr(s->context, global, "__native", JS_NewCFunction(s->context, Impl::native, "__native", 4));
   JS_FreeValue(s->context, global);
   if (!s->eval(bootstrap, "dillo:dom")) s->stopped = true;
}
JsEngine::~JsEngine() { delete impl; }
bool JsEngine::valid() const { return impl->context && !impl->stopped; }
bool JsEngine::evaluate(const std::string &source, const std::string &filename)
{ return impl->eval(source, filename); }
void JsEngine::append(const char *data, size_t size)
{
   if (impl->stopped || impl->ended) return;
   if (size > MaxInput - std::min(impl->inputTotal, MaxInput)) {
      impl->host->log("Document input limit exceeded"); impl->stopped = true; return;
   }
   impl->inputTotal += size;
   if (data && size) impl->input.append(data, size);
   impl->pump();
}
void JsEngine::finish() { impl->eof = true; impl->pump(); }
bool JsEngine::nextScript(Script &script)
{
   impl->pump();
   if (impl->scripts.empty() || impl->stopped) return false;
   script = impl->scripts.front(); impl->scripts.pop_front(); return true;
}
void JsEngine::scriptStarted(int)
{
   if (!impl->writes.empty()) { impl->input.insert(0, impl->writes); impl->writes.clear(); }
   impl->blocked = false; impl->pump();
}
bool JsEngine::parsed() const { return impl->ended; }
bool JsEngine::hasPendingJobs() const { return valid() && JS_IsJobPending(impl->runtime); }
void JsEngine::interactive()
{ impl->eval("__dillo.ready(false)", "dillo:interactive"); }
void JsEngine::loaded()
{ impl->eval("__dillo.ready(true)", "dillo:ready"); }
void JsEngine::timeout(int id)
{ impl->eval("__dillo.timeout(" + std::to_string(id) + ")", "dillo:timer"); }
void JsEngine::response(int id, int status, const std::string &body,
                        const std::string &headers, const std::string &url)
{
   if (!valid()) return;
   JSValue global = JS_GetGlobalObject(impl->context);
   JSValue values = JS_NewArray(impl->context);
   JS_SetPropertyUint32(impl->context, values, 0, JS_NewInt32(impl->context, id));
   JS_SetPropertyUint32(impl->context, values, 1, JS_NewInt32(impl->context, status));
   JS_SetPropertyUint32(impl->context, values, 2, JS_NewStringLen(impl->context, body.data(), body.size()));
   JS_SetPropertyUint32(impl->context, values, 3, JS_NewStringLen(impl->context, headers.data(), headers.size()));
   JS_SetPropertyUint32(impl->context, values, 4, JS_NewString(impl->context, url.c_str()));
   JS_SetPropertyStr(impl->context, global, "__response", values);
   JS_FreeValue(impl->context, global);
   impl->eval("__dillo.response(...__response); delete globalThis.__response", "dillo:network");
}
bool JsEngine::event(int node, const std::string &type, const std::string &key, int keyCode,
                     const MouseEvent *mouse)
{
   if (!valid()) return true;
   JSValue global = JS_GetGlobalObject(impl->context);
   JS_SetPropertyStr(impl->context, global, "__eventType", JS_NewString(impl->context, type.c_str()));
   JS_SetPropertyStr(impl->context, global, "__eventKey", JS_NewString(impl->context, key.c_str()));
   JS_SetPropertyStr(impl->context, global, "__eventCode", JS_NewInt32(impl->context, keyCode));
   JSValue options = JS_NewObject(impl->context);
   if (mouse) {
      const char *names[] = {"clientX", "clientY", "pageX", "pageY", "screenX", "screenY", "button", "buttons", "detail"};
      const int values[] = {mouse->clientX, mouse->clientY, mouse->pageX, mouse->pageY, mouse->screenX,
         mouse->screenY, mouse->button, mouse->buttons, mouse->detail};
      for (unsigned i = 0; i < sizeof(values)/sizeof(values[0]); ++i)
         JS_SetPropertyStr(impl->context, options, names[i], JS_NewInt32(impl->context, values[i]));
      JS_SetPropertyStr(impl->context, options, "ctrlKey", JS_NewBool(impl->context, mouse->ctrlKey));
      JS_SetPropertyStr(impl->context, options, "shiftKey", JS_NewBool(impl->context, mouse->shiftKey));
      JS_SetPropertyStr(impl->context, options, "altKey", JS_NewBool(impl->context, mouse->altKey));
      JS_SetPropertyStr(impl->context, options, "metaKey", JS_NewBool(impl->context, mouse->metaKey));
   }
   JS_SetPropertyStr(impl->context, global, "__eventOptions", options);
   JS_FreeValue(impl->context, global);
   impl->eval("globalThis.__eventResult=__dillo.event(" + std::to_string(node) + ", __eventType,__eventKey,__eventCode,__eventOptions)", "dillo:event");
   JSValue result = JS_GetPropertyStr(impl->context, global = JS_GetGlobalObject(impl->context), "__eventResult");
   bool allowed = JS_ToBool(impl->context, result) != 0;
   JS_FreeValue(impl->context, result); JS_FreeValue(impl->context, global);
   return allowed;
}
std::string JsEngine::serialize(bool rendering)
{
   if (!impl->document) return "";
   if (!rendering) return impl->serialize(lxb_dom_interface_node(impl->document), true);
   struct Saved { lxb_dom_node_t *node; std::string name; bool had; std::string value; };
   std::vector<Saved> saved;
   std::map<lxb_dom_node_t *, std::vector<lxb_dom_node_t *> > textSaved;
   std::set<lxb_dom_node_t *> focusWithin;
   std::vector<lxb_dom_node_t *> focusTodo(1, lxb_dom_interface_node(impl->document));
   while (!focusTodo.empty()) {
      lxb_dom_node_t *node = focusTodo.back(); focusTodo.pop_back();
      if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
         std::map<int, std::map<std::string, std::string> >::const_iterator state =
            impl->controlState.find(impl->id(node));
         if (state != impl->controlState.end()) {
            std::map<std::string, std::string>::const_iterator focused =
               state->second.find("focus");
            if (focused != state->second.end() && focused->second == "true")
               for (lxb_dom_node_t *parent = node; parent &&
                    parent->type == LXB_DOM_NODE_TYPE_ELEMENT;
                    parent = parent->parent)
                  focusWithin.insert(parent);
         }
      }
      for (lxb_dom_node_t *child = node->first_child; child; child = child->next)
         focusTodo.push_back(child);
   }
   std::vector<lxb_dom_node_t *> todo(1, lxb_dom_interface_node(impl->document));
   while (!todo.empty()) {
      lxb_dom_node_t *node = todo.back(); todo.pop_back();
      if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
         Saved old = {node, "data-dillo-node", impl->has(node, "data-dillo-node"), impl->attr(node, "data-dillo-node")};
         saved.push_back(old);
         std::string value = std::to_string(impl->id(node));
         lxb_dom_element_set_attribute(lxb_dom_interface_element(node),
            reinterpret_cast<const lxb_char_t *>("data-dillo-node"), 15, bytes(value), value.size());
         std::map<std::string, std::string> &state = impl->controlState[impl->id(node)];
         for (std::map<std::string, std::string>::iterator it = state.begin(); it != state.end(); ++it) {
            if (it->first == "focus") continue;
            Saved attr = {node, it->first, impl->has(node, it->first.c_str()), impl->attr(node, it->first)};
            saved.push_back(attr);
            if ((it->first == "checked" || it->first == "selected") && it->second != "true")
               lxb_dom_element_remove_attribute(lxb_dom_interface_element(node), bytes(it->first), it->first.size());
            else lxb_dom_element_set_attribute(lxb_dom_interface_element(node), bytes(it->first), it->first.size(), bytes(it->second), it->second.size());
         }
         const char *focusAttributes[] = {"data-dillo-internal-focus",
            "data-dillo-internal-focus-within"};
         const bool focusValues[] = {
            state.count("focus") && state["focus"] == "true",
            focusWithin.count(node) != 0
         };
         for (size_t i = 0; i < 2; ++i) {
            std::string name = focusAttributes[i];
            Saved attr = {node, name, impl->has(node, name.c_str()),
               impl->attr(node, name)};
            saved.push_back(attr);
            if (focusValues[i])
               lxb_dom_element_set_attribute(lxb_dom_interface_element(node),
                  bytes(name), name.size(), bytes(std::string()), 0);
            else
               lxb_dom_element_remove_attribute(lxb_dom_interface_element(node),
                  bytes(name), name.size());
         }
         if (node->local_name == LXB_TAG_TEXTAREA && state.count("value")) {
            std::vector<lxb_dom_node_t *> &children = textSaved[node];
            while (node->first_child) { children.push_back(node->first_child); lxb_dom_node_remove(node->first_child); }
            lxb_dom_text_t *text = lxb_dom_document_create_text_node(node->owner_document, bytes(state["value"]), state["value"].size());
            if (text) lxb_dom_node_insert_child(node, lxb_dom_interface_node(text));
         }
      }
      for (lxb_dom_node_t *child = node->first_child; child; child = child->next) todo.push_back(child);
   }
   std::string html = impl->serialize(lxb_dom_interface_node(impl->document), true);
   // The native HTML tree has no SVG widget. Preserve the live DOM for
   // scripts, while giving the renderer an SVG resource for inline SVG. Skip
   // raw-text and comment contents, where literal <svg strings are not tags.
   for (size_t pos = 0; (pos = html.find("<svg", pos)) != std::string::npos;) {
      size_t commentOpen = html.rfind("<!--", pos), commentClose = html.rfind("-->", pos);
      size_t scriptOpen = html.rfind("<script", pos), scriptClose = html.rfind("</script", pos);
      size_t styleOpen = html.rfind("<style", pos), styleClose = html.rfind("</style", pos);
      if ((commentOpen != std::string::npos && (commentClose == std::string::npos || commentOpen > commentClose)) ||
          (scriptOpen != std::string::npos && (scriptClose == std::string::npos || scriptOpen > scriptClose)) ||
          (styleOpen != std::string::npos && (styleClose == std::string::npos || styleOpen > styleClose))) {
         size_t end = html.find('>', pos);
         if (end == std::string::npos) break;
         pos = end + 1; continue;
      }
      size_t openEnd = html.find('>', pos);
      if (openEnd == std::string::npos) break;
      size_t close = html.find("</svg", openEnd + 1);
      if (close == std::string::npos) break;
      size_t closeEnd = html.find('>', close);
      if (closeEnd == std::string::npos) break;
      std::string svg = html.substr(pos, closeEnd - pos + 1);
      std::string encoded;
      const char hex[] = "0123456789ABCDEF";
      for (size_t i = 0; i < svg.size(); ++i) {
         unsigned char c = static_cast<unsigned char>(svg[i]);
         if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
             (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
             c == '/' || c == ':' || c == '#' || c == ';' || c == ',' ||
             c == '=' || c == ' ' || c == '\n' || c == '\r') encoded += c;
         else { encoded += '%'; encoded += hex[c >> 4]; encoded += hex[c & 15]; }
      }
      std::string replacement = "<img src=\"data:image/svg+xml," + encoded + "\">";
      html.replace(pos, closeEnd - pos + 1, replacement);
      pos += replacement.size();
   }
   for (size_t i = 0; i < saved.size(); ++i) {
      if (saved[i].had) lxb_dom_element_set_attribute(lxb_dom_interface_element(saved[i].node),
         bytes(saved[i].name), saved[i].name.size(), bytes(saved[i].value), saved[i].value.size());
      else lxb_dom_element_remove_attribute(lxb_dom_interface_element(saved[i].node),
         bytes(saved[i].name), saved[i].name.size());
   }
   for (std::map<lxb_dom_node_t *, std::vector<lxb_dom_node_t *> >::iterator it = textSaved.begin(); it != textSaved.end(); ++it) {
      while (it->first->first_child) lxb_dom_node_destroy(it->first->first_child);
      for (size_t i = 0; i < it->second.size(); ++i) lxb_dom_node_insert_child(it->first, it->second[i]);
   }
   return html;
}
std::string JsEngine::stringValue(const std::string &expression)
{
   if (!valid()) return "";
   impl->deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
   impl->running = true;
   JSValue v = JS_Eval(impl->context, expression.data(), expression.size(), "dillo:inspect", JS_EVAL_TYPE_GLOBAL);
   std::string result;
   if (JS_IsException(v)) impl->exception("dillo:inspect");
   else result = jsString(impl->context, v);
   JS_FreeValue(impl->context, v); impl->running = false;
   return result;
}
int JsEngine::find(const std::string &selector)
{
   std::vector<lxb_dom_node_t *> found;
   if (!impl->document || !impl->query(lxb_dom_interface_node(impl->document), selector, false, found) || found.empty()) return 0;
   return impl->id(found.front());
}
void JsEngine::setUrl(const std::string &url) { impl->url = url; }
void JsEngine::historyTraversal(const std::string &oldUrl)
{
   if (!valid()) return;
   JSValue global = JS_GetGlobalObject(impl->context);
   JS_SetPropertyStr(impl->context, global, "__dilloOldURL",
      JS_NewStringLen(impl->context, oldUrl.data(), oldUrl.size()));
   JS_FreeValue(impl->context, global);
   impl->eval("__dillo.history(__dilloOldURL)", "dillo:history");
}
void JsEngine::setAttribute(int node, const std::string &name, const std::string &value)
{
   lxb_dom_node_t *n = impl->node(node);
   if (n && n->type == LXB_DOM_NODE_TYPE_ELEMENT) {
      lxb_dom_element_set_attribute(lxb_dom_interface_element(n), bytes(name), name.size(), bytes(value), value.size());
      ++impl->revision;
   }
}
void JsEngine::stop() { impl->stopped = true; }
void JsEngine::setControlState(int node, const std::string &name, const std::string &value)
{
   if (impl->controlState[node][name] != value) {
      impl->controlState[node][name] = value;
      ++impl->revision;
      if (name == "focus")
         impl->host->focusChanged(node, value == "true");
      if (name == "checked" || name == "selected" || name == "focus")
         impl->host->changed(name == "focus" ? 150 : 0);
   }
}
#pragma GCC diagnostic pop

#else
struct JsEngine::Impl {};
JsEngine::JsEngine(Host *, const std::string &): impl(NULL) {}
JsEngine::~JsEngine() {}
bool JsEngine::valid() const { return false; }
bool JsEngine::evaluate(const std::string &, const std::string &) { return false; }
void JsEngine::append(const char *, size_t) {}
void JsEngine::finish() {}
bool JsEngine::nextScript(Script &) { return false; }
void JsEngine::scriptStarted(int) {}
bool JsEngine::parsed() const { return true; }
bool JsEngine::hasPendingJobs() const { return false; }
void JsEngine::interactive() {}
void JsEngine::loaded() {}
void JsEngine::timeout(int) {}
void JsEngine::response(int, int, const std::string &, const std::string &, const std::string &) {}
bool JsEngine::event(int, const std::string &, const std::string &, int, const MouseEvent *) { return true; }
std::string JsEngine::serialize(bool) { return ""; }
std::string JsEngine::stringValue(const std::string &) { return ""; }
int JsEngine::find(const std::string &) { return 0; }
void JsEngine::setAttribute(int, const std::string &, const std::string &) {}
void JsEngine::setControlState(int, const std::string &, const std::string &) {}
void JsEngine::setUrl(const std::string &) {}
void JsEngine::historyTraversal(const std::string &) {}
void JsEngine::stop() {}
#endif
