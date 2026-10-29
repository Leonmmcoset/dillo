/* Regression tests for saving user preferences without losing custom settings. */
#include "src/prefsrc.hh"
#include "dlib/dlib.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void write_file(const char *path, const char *text)
{
   FILE *fp = fopen(path, "w");
   assert(fp);
   assert(fputs(text, fp) >= 0);
   assert(fclose(fp) == 0);
}

static Dstr *read_file(const char *path)
{
   FILE *fp = fopen(path, "r");
   assert(fp);
   Dstr *text = dStr_new("");
   char *line;
   while ((line = dGetline(fp))) {
      dStr_append(text, line);
      dFree(line);
   }
   assert(!ferror(fp));
   fclose(fp);
   return text;
}

int main()
{
   char directory[] = "/tmp/dillo-prefsrc-XXXXXX";
   assert(mkdtemp(directory));
   char *path = dStrconcat(directory, "/dillorc", NULL);
   char *fallback = dStrconcat(directory, "/systemrc", NULL);
   char *link = dStrconcat(directory, "/linkedrc", NULL);
   const char *base =
      "# My settings: 中文\n"
      "# home=about:blank\n"
      "  home = \"https://old.example/\"\n"
      "search_url=\"a Search A https://a.example/?q=%s\"\n"
      "search_url=\"b Search B https://b.example/?q=%s\"\n"
      "home=\"https://last.example/\"\n"
      "page_action=\"Read:some-command '$url'\"\n"
      "custom_future_option=leave-this-alone";
   const PrefsUpdate updates[] = {
      {"home", "https://new.example/#section"},
      {"font_sans_serif", "Noto Sans CJK SC"},
      {"save_dir", "/tmp/中文 files\\with \"quotes\""},
      {"http_proxy", ""}
   };
   unsigned int count = sizeof(updates) / sizeof(updates[0]);
   write_file(path, base);
   assert(a_Prefs_save(path, NULL, updates, count) == 0);
   Dstr *text = read_file(path);
   assert(strstr(text->str, "# My settings: 中文\n# home=about:blank\n"));
   assert(strstr(text->str, "home=\"https://new.example/#section\"\n"));
   assert(!strstr(text->str, "https://old.example/"));
   assert(!strstr(text->str, "https://last.example/"));
   assert(strstr(text->str, "search_url=\"a Search A https://a.example/?q=%s\"\n"
                            "search_url=\"b Search B https://b.example/?q=%s\"\n"));
   assert(strstr(text->str, "page_action=\"Read:some-command '$url'\"\n"));
   assert(strstr(text->str, "custom_future_option=leave-this-alone\n"));

   /* Every serialized value must survive the actual rc parser unchanged. */
   FILE *fp = fopen(path, "r");
   char *line;
   unsigned int found = 0;
   while ((line = dGetline(fp))) {
      char *name, *value;
      if (dParser_parse_rc_line(&line, &name, &value) == 0) {
         for (unsigned int i = 0; i < count; i++) {
            if (!strcmp(name, updates[i].name)) {
               assert(!strcmp(value, updates[i].value));
               found++;
            }
         }
      }
      dFree(line);
   }
   fclose(fp);
   assert(found == count + 1); // both original home entries were updated
   struct stat st;
   assert(stat(path, &st) == 0 && (st.st_mode & 0777) == 0600);

   /* Repeated saves are idempotent, including repeated keys. */
   assert(a_Prefs_save(path, NULL, updates, count) == 0);
   Dstr *again = read_file(path);
   assert(!strcmp(text->str, again->str));
   dStr_free(again, 1);

   /* Invalid values cannot inject new settings or truncate the old file. */
   const PrefsUpdate bad_value[] = {{"home", "about:blank\nload_images=NO"}};
   const PrefsUpdate bad_key[] = {{"home\nhttp_proxy", "bad"}};
   assert(a_Prefs_save(path, NULL, bad_value, 1) == -1 && errno == EINVAL);
   assert(a_Prefs_save(path, NULL, bad_key, 1) == -1 && errno == EINVAL);
   again = read_file(path);
   assert(!strcmp(text->str, again->str));
   dStr_free(again, 1);
   dStr_free(text, 1);

   /* Follow a user's dotfile symlink while preserving the symlink itself. */
   assert(symlink(path, link) == 0);
   assert(a_Prefs_save(link, NULL, updates, count) == 0);
   assert(lstat(link, &st) == 0 && S_ISLNK(st.st_mode));
   unlink(link);

   /* A new user file inherits otherwise-unexposed system preferences. */
   unlink(path);
   write_file(fallback, base);
   assert(a_Prefs_save(path, fallback, updates, count) == 0);
   text = read_file(path);
   assert(strstr(text->str, "page_action=\"Read:some-command '$url'\"\n"));
   dStr_free(text, 1);
   unlink(path);
   unlink(fallback);

   /* A completely fresh installation works without either rc file. */
   assert(a_Prefs_save(path, fallback, updates, count) == 0);
   text = read_file(path);
   assert(strstr(text->str, "font_sans_serif=\"Noto Sans CJK SC\"\n"));
   dStr_free(text, 1);
   unlink(path);

   char *missing = dStrconcat(directory, "/missing/dillorc", NULL);
   assert(a_Prefs_save(missing, NULL, updates, count) == -1);
   assert(access(missing, F_OK) < 0);
   assert(a_Prefs_save(directory, NULL, updates, count) == -1);
   assert(rmdir(directory) == 0); // no abandoned temporary files
   dFree(missing);
   dFree(link);
   dFree(path);
   dFree(fallback);
   puts("Preference round trips, preservation, fallback and failure cases passed.");
   return 0;
}
