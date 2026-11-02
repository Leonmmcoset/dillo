/* Test the cookie jar directly, without a dpid daemon or the user's files. */
#include "config.h"
#ifdef DISABLE_COOKIES
int main(void) { return 77; }
#else
#define main cookie_daemon_main
#include "dpi/cookies.c"
#undef main

static void require(int ok, const char *message)
{
   if (!ok) { fprintf(stderr, "FAIL: %s\n", message); exit(1); }
}
static int set(const char *text, const char *host, bool_t document, bool_t tls)
{
   char *copy = dStrdup(text), *h = dStrdup(host), path[] = "/area/page";
   int result = Cookies_set(copy, h, path, NULL, document, tls);
   dFree(copy); dFree(h); return result;
}
static char *get(const char *host, const char *path, bool_t document, bool_t tls)
{
   char *h = dStrdup(host), *p = dStrdup(path), scheme[6];
   strcpy(scheme, tls ? "https" : "http");
   char *result = Cookies_get(h, p, scheme, document);
   dFree(h); dFree(p); return result;
}
int main(void)
{
   struct tm future = {7, 14, 3, 19, 0, 138, 0, 0, 0, 0, 0};
   cookies_epoch_time = mktime(&cookies_epoch_tm);
   cookies_future_time = mktime(&future);
   disabled = FALSE; default_action = COOKIE_ACCEPT;
   file_stream = tmpfile(); require(file_stream != NULL, "temporary jar");
   Cookies_load_cookies(file_stream);
   require(set("private=secret; Path=/; HttpOnly; Max-Age=3600", "example.test", FALSE, TRUE) == 0, "HTTP may set HttpOnly");
   require(set("visible=yes; Path=/area; Max-Age=3600", "example.test", TRUE, TRUE) == 0, "document may set ordinary cookie");
   char *s = get("example.test", "/area/page", TRUE, TRUE);
   require(strstr(s, "visible=yes") && !strstr(s, "private="), "document hides HttpOnly"); dFree(s);
   s = get("example.test", "/area/page", FALSE, TRUE);
   require(strstr(s, "private=secret") != NULL, "HTTP includes HttpOnly"); dFree(s);
   require(set("private=changed; Path=/", "example.test", TRUE, TRUE) != 0, "document cannot overwrite HttpOnly");
   require(set("private=gone; Path=/; Max-Age=0", "example.test", TRUE, TRUE) != 0, "document cannot delete HttpOnly");
   require(set("new=no; HttpOnly", "example.test", TRUE, TRUE) != 0, "document cannot create HttpOnly");
   require(set("tls=yes; Secure; Path=/", "example.test", TRUE, TRUE) == 0, "secure cookie on TLS");
   require(set("tls=no; Path=/", "example.test", TRUE, FALSE) != 0, "HTTP script cannot overwrite secure cookie");
   s = get("example.test", "/other", TRUE, FALSE);
   require(!strstr(s, "visible=") && !strstr(s, "tls="), "path and secure restrictions"); dFree(s);
   s = get("other.test", "/area/page", FALSE, TRUE);
   require(!*s, "cookie host isolation"); dFree(s);
   require(set("bad=no; Domain=other.test", "example.test", TRUE, TRUE) != 0, "foreign domain rejected");
   int saved = dup(fileno(file_stream)); require(saved != -1, "preserve temporary file");
   Cookies_save_and_free();
   file_stream = fdopen(saved, "r+"); require(file_stream != NULL, "reopen jar"); rewind(file_stream);
   Cookies_load_cookies(file_stream);
   s = get("example.test", "/area/page", TRUE, TRUE);
   require(!strstr(s, "private="), "HttpOnly survives disk serialization"); dFree(s);
   s = get("example.test", "/area/page", FALSE, TRUE);
   require(strstr(s, "private=secret") != NULL, "HttpOnly value survives disk serialization"); dFree(s);
   Cookies_save_and_free();
   return 0;
}
#endif
