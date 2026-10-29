/*
 * UI translations. Catalogs are compiled in so that uninstalled builds and
 * standalone DPI programs use the same translations without extra dependencies.
 */
#include "i18n.h"

#include <locale.h>
#include <stdlib.h>
#include <string.h>

static int chinese = -1;

static const struct {
   const char *english;
   const char *chinese;
} messages[] = {
#include "i18n_zh_CN.h"
};

static int I18n_is_chinese(const char *locale)
{
   const char *part;
   size_t len;

   if (strncmp(locale, "zh", 2) != 0)
      return 0;
   part = locale + 2;
   if (!*part || *part == '.' || *part == '@')
      return 1;
   if (*part != '_' && *part != '-')
      return 0;
   part++;
   len = strcspn(part, "_-.@");
   /* Explicit Traditional Chinese locales fall back to English. */
   return (len == 2 && (!strncmp(part, "CN", 2) ||
                       !strncmp(part, "SG", 2))) ||
          (len == 4 && !strncmp(part, "Hans", 4));
}

void a_I18n_init(void)
{
   const char *locale = getenv("LC_ALL");
   if (!locale || !*locale)
      locale = getenv("LC_MESSAGES");
   if (!locale || !*locale)
      locale = getenv("LANG");
   if (!locale || !*locale)
      locale = "C";

   /* Read the requested locale even when its locale data isn't installed.
    * FLTK labels and our catalogs always contain UTF-8. Do not change
    * LC_NUMERIC: CSS, HTTP and other parsers require the C decimal point. */
   chinese = I18n_is_chinese(locale);
   setlocale(LC_CTYPE, "");
}

const char *a_I18n_language(void)
{
   if (chinese < 0)
      a_I18n_init();
   return chinese ? "zh_CN" : "en";
}

const char *a_I18n_gettext(const char *message)
{
   size_t i;

   if (chinese < 0)
      a_I18n_init();
   if (chinese && message) {
      for (i = 0; i < sizeof(messages) / sizeof(messages[0]); i++) {
         if (!strcmp(message, messages[i].english))
            return messages[i].chinese;
      }
   }
   return message;
}
