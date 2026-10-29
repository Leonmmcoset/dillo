/* Locale selection must also work on machines without Chinese locale data. */
#include "dlib/i18n.h"

#include <assert.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check(const char *all, const char *messages, const char *lang,
                  int chinese)
{
   char text[128];

   if (all) setenv("LC_ALL", all, 1); else unsetenv("LC_ALL");
   if (messages) setenv("LC_MESSAGES", messages, 1);
   else unsetenv("LC_MESSAGES");
   if (lang) setenv("LANG", lang, 1); else unsetenv("LANG");
   a_I18n_init();

   assert(!strcmp(_("Cancel"), chinese ? "取消" : "Cancel"));
   assert(!strcmp(a_I18n_language(), chinese ? "zh_CN" : "en"));
   snprintf(text, sizeof(text), _("Zoom set to %.0f%%"), 125.0);
   assert(!strcmp(text, chinese ? "缩放比例设为 125%" : "Zoom set to 125%"));
   /* Translating UI messages must never change parsing of CSS numbers. */
   assert(!strcmp(setlocale(LC_NUMERIC, NULL), "C"));
}

int main(void)
{
   const char *unknown = "An untranslated message";
   const char *locales[] = {
      "zh", "zh_CN", "zh_CN.UTF-8", "zh_CN.GB18030", "zh_SG.UTF-8",
      "zh-Hans", "zh-Hans-CN", "zh_Hans_SG", "zh_CN@custom"
   };
   size_t i;

   setlocale(LC_NUMERIC, "C");
   for (i = 0; i < sizeof(locales) / sizeof(locales[0]); i++)
      check(NULL, NULL, locales[i], 1);

   check(NULL, NULL, NULL, 0);
   check("", "", "", 0);
   check(NULL, NULL, "en_US.UTF-8", 0);
   check(NULL, NULL, "C", 0);
   check(NULL, NULL, "POSIX", 0);
   check(NULL, NULL, "de_DE.UTF-8", 0);
   check(NULL, NULL, "zh_TW.UTF-8", 0);
   check(NULL, NULL, "zh_HK.UTF-8", 0);
   check(NULL, NULL, "zh-Hant-CN", 0);
   check(NULL, NULL, "zh_CNinvalid", 0);
   check(NULL, NULL, "zhinvalid", 0);
   check("C", "zh_CN.UTF-8", "zh_CN.UTF-8", 0);
   check("en_US.UTF-8", "zh_CN.UTF-8", "zh_CN.UTF-8", 0);
   check("zh_CN.UTF-8", "C", "en_US.UTF-8", 1);
   check("", "zh_CN.UTF-8", "en_US.UTF-8", 1);
   check(NULL, "C", "zh_CN.UTF-8", 0);
   check(NULL, "en_US.UTF-8", "zh_CN.UTF-8", 0);
   check(NULL, "", "zh_CN.UTF-8", 1);

   assert(a_I18n_gettext(unknown) == unknown);
   assert(a_I18n_gettext(NULL) == NULL);
   puts("Locale selection and translated formatting passed.");
   return 0;
}
