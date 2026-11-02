/* Browser integration for the optional JavaScript engine. */
#ifndef DILLO_JAVASCRIPT_HH
#define DILLO_JAVASCRIPT_HH

#include "bw.h"
#include "cache.h"
#ifdef __cplusplus
extern "C" {
#endif
void a_Javascript_disable_all(void);
void a_Javascript_stop(BrowserWindow *bw);
void a_Javascript_forget_window(BrowserWindow *bw);
int a_Javascript_pending(BrowserWindow *bw);
char *a_Javascript_dump(BrowserWindow *bw);
char *a_Javascript_evaluate(BrowserWindow *bw, const char *expression);
int a_Javascript_history_traverse(BrowserWindow *bw, const DilloUrl *url);
#ifdef __cplusplus
}

class DilloHtml;
class JavascriptDocument;
JavascriptDocument *a_Javascript_new(DilloHtml *view);
void a_Javascript_callback(int op, CacheClient_t *client);
void a_Javascript_delete(JavascriptDocument *document, DilloHtml *view);
void a_Javascript_write(JavascriptDocument *document, const char *buf,
                        size_t size, bool eof, int clientKey);
void a_Javascript_render(JavascriptDocument *document);
bool a_Javascript_event(JavascriptDocument *document, int node, const char *type);
bool a_Javascript_ui_event(void *document, int node, const char *type,
                           const char *key, int keyCode);
int a_Javascript_node(JavascriptDocument *document, const char *selector);
void a_Javascript_set_value(JavascriptDocument *document, int node, const char *value);
void a_Javascript_set_state(JavascriptDocument *document, int node, const char *name, const char *value);
#endif
#endif
