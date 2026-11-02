/*
 * Graphical preferences editor.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_File_Chooser.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Input_Choice.H>
#include <FL/Fl_Return_Button.H>
#include <FL/Fl_Tabs.H>
#include <FL/Fl_Value_Input.H>
#include <FL/Fl_Window.H>

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "dlib/i18n.h"
#include "dw/fltkcore.hh"
#include "dialog.hh"
#include "paths.hh"
#include "prefs.h"
#include "prefsrc.hh"
#include "settings.hh"
#include "uicmd.hh"
#include "javascript.hh"

namespace {

enum Page { GENERAL, FONTS, NETWORK, PRIVACY };
enum Type { TEXT, URL, FONT, CHECK, INTEGER, SCALE, REFERER };

struct Setting {
   const char *name, *label;
   Page page;
   Type type;
   void *pref;
   double minimum, maximum;
};

static const Setting settings[] = {
   {"start_page", N_("Startup page"), GENERAL, URL, &prefs.start_page, 0, 0},
   {"home", N_("Home page"), GENERAL, URL, &prefs.home, 0, 0},
   {"new_tab_page", N_("New tab page"), GENERAL, URL, &prefs.new_tab_page, 0, 0},
   {"save_dir", N_("Download folder"), GENERAL, TEXT, &prefs.save_dir, 0, 0},
   {"focus_new_tab", N_("Switch to newly opened tabs"), GENERAL, CHECK,
    &prefs.focus_new_tab, 0, 0},
   {"show_quit_dialog", N_("Confirm before closing multiple tabs"), GENERAL,
    CHECK, &prefs.show_quit_dialog, 0, 0},
   {"load_images", N_("Load images"), GENERAL, CHECK, &prefs.load_images, 0, 0},
   {"load_background_images", N_("Load background images"), GENERAL, CHECK,
    &prefs.load_background_images, 0, 0},
   {"load_stylesheets", N_("Use remote CSS"), GENERAL, CHECK,
    &prefs.load_stylesheets, 0, 0},
   {"parse_embedded_css", N_("Use embedded CSS"), GENERAL, CHECK,
    &prefs.parse_embedded_css, 0, 0},
#ifdef ENABLE_JAVASCRIPT
   {"javascript_enabled", N_("Enable JavaScript"), GENERAL, CHECK,
    &prefs.javascript_enabled, 0, 0},
#endif
   {"font_sans_serif", N_("Sans-serif font"), FONTS, FONT,
    &prefs.font_sans_serif, 0, 0},
   {"font_serif", N_("Serif font"), FONTS, FONT, &prefs.font_serif, 0, 0},
   {"font_monospace", N_("Monospace font"), FONTS, FONT,
    &prefs.font_monospace, 0, 0},
   {"font_factor", N_("Text scale (%)"), FONTS, SCALE,
    &prefs.font_factor, 50, 300},
   {"font_min_size", N_("Minimum font size (px)"), FONTS, INTEGER,
    &prefs.font_min_size, 1, 72},
   {"font_max_size", N_("Maximum font size (px)"), FONTS, INTEGER,
    &prefs.font_max_size, 6, 200},
   {"http_proxy", N_("HTTP proxy"), NETWORK, URL, &prefs.http_proxy, 0, 0},
   {"http_proxyuser", N_("Proxy username"), NETWORK, TEXT,
    &prefs.http_proxyuser, 0, 0},
   {"no_proxy", N_("Bypass proxy for"), NETWORK, TEXT, &prefs.no_proxy, 0, 0},
   {"http_language", N_("Website language"), NETWORK, TEXT,
    &prefs.http_language, 0, 0},
   {"cookies_enabled", N_("Allow cookies permitted by site rules"), PRIVACY,
    CHECK, &prefs.cookies_enabled, 0, 0},
   {"http_force_https", N_("Always use HTTPS"), PRIVACY, CHECK,
    &prefs.http_force_https, 0, 0},
   {"http_strict_transport_security", N_("Honor HSTS security policies"),
    PRIVACY, CHECK, &prefs.http_strict_transport_security, 0, 0},
   {"http_referer", N_("Referer header"), PRIVACY, REFERER,
    &prefs.http_referer, 0, 0},
   {"http_user_agent", N_("User agent"), PRIVACY, TEXT,
    &prefs.http_user_agent, 0, 0}
};

const unsigned int count = sizeof(settings) / sizeof(settings[0]);
const char *const referers[] = {"none", "host", "path"};

class SettingsWindow : public Fl_Window {
   Fl_Tabs *tabs;
   Fl_Group *pages[4];
   Fl_Widget *fields[count];
   Fl_Box *preview;
   bool saved;

   static void save_cb(Fl_Widget *, void *data) {
      ((SettingsWindow *)data)->save();
   }
   static void cancel_cb(Fl_Widget *, void *data) {
      ((SettingsWindow *)data)->hide();
   }
   static void browse_cb(Fl_Widget *, void *data);
   static void preview_cb(Fl_Widget *widget, void *data);
   void save();
   void apply(const PrefsUpdate *updates);
   bool validate(unsigned int i, const char *value);
   void error(unsigned int i, const char *message);
   void note(int y, const char *text);
   char *value(unsigned int i);

public:
   SettingsWindow();
   bool was_saved() const { return saved; }
};

void SettingsWindow::note(int y, const char *text)
{
   Fl_Box *box = new Fl_Box(32, y, 656, 60, text);
   box->align(FL_ALIGN_INSIDE | FL_ALIGN_LEFT | FL_ALIGN_WRAP);
   box->labelsize(13);
}

SettingsWindow::SettingsWindow() :
   Fl_Window(720, 590, _("Dillo: Preferences")), saved(false)
{
   set_modal();
   callback(cancel_cb, this);
   begin();
   tabs = new Fl_Tabs(16, 16, 688, 450);
   const char *titles[] = {_("General"), _("Fonts"), _("Network"), _("Privacy")};
   for (int page = GENERAL; page <= PRIVACY; page++) {
      pages[page] = new Fl_Group(16, 48, 688, 418, titles[page]);
      pages[page]->labelsize(14);
      int y = 66;
      for (unsigned int i = 0; i < count; i++) {
         const Setting &s = settings[i];
         if (s.page != page)
            continue;
         Fl_Widget *field;
         if (s.type == CHECK) {
            Fl_Check_Button *check = new Fl_Check_Button(32, y, 650, 28,
                                                         _(s.label));
            check->value(*(bool_t *)s.pref);
            field = check;
            y += 32;
         } else {
            Fl_Box *label = new Fl_Box(32, y, 200, 28, _(s.label));
            label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
            label->labelsize(14);
            if (s.type == FONT) {
               Fl_Input_Choice *choice = new Fl_Input_Choice(240, y, 442, 28);
               int fonts = Fl::set_fonts("-*-iso10646-1");
               for (int f = 0; f < fonts; f++) {
                  int attributes;
                  char *name = dStrdup(Fl::get_font_name((Fl_Font)f, &attributes));
                  if (!attributes) {
                     size_t length = strlen(name);
                     if (length >= 5 &&
                         !dStrAsciiCasecmp(name + length - 5, " book"))
                        name[length - 5] = 0;
                     /* '/' and '\\' have special meaning in FLTK menus. */
                     Dstr *escaped = dStr_new("");
                     for (const char *c = name; *c; c++) {
                        if (*c == '/' || *c == '\\')
                           dStr_append_c(escaped, '\\');
                        dStr_append_c(escaped, *c);
                     }
                     choice->add(escaped->str);
                     dStr_free(escaped, 1);
                  }
                  dFree(name);
               }
               choice->value(*(char **)s.pref);
               choice->textsize(14);
               choice->callback(preview_cb, this);
               field = choice;
            } else if (s.type == INTEGER || s.type == SCALE) {
               Fl_Value_Input *input = new Fl_Value_Input(240, y, 120, 28);
               input->range(s.minimum, s.maximum);
               input->step(1);
               input->value(s.type == SCALE ? 100 * *(double *)s.pref :
                                             *(int32_t *)s.pref);
               input->textsize(14);
               input->callback(preview_cb, this);
               field = input;
            } else if (s.type == REFERER) {
               Fl_Choice *choice = new Fl_Choice(240, y, 442, 28);
               choice->add(_("Do not send"));
               choice->add(_("Requested host only"));
               choice->add(_("Requested host and path"));
               choice->textsize(14);
               choice->value(0);
               for (int r = 0; r < 3; r++)
                  if (!strcmp(*(char **)s.pref, referers[r]))
                     choice->value(r);
               field = choice;
            } else {
               int width = !strcmp(s.name, "save_dir") ? 342 : 442;
               Fl_Input *input = new Fl_Input(240, y, width, 28);
               const char *text = s.type == URL ?
                  (*(DilloUrl **)s.pref ? URL_STR(*(DilloUrl **)s.pref) : "") :
                  NPTR2STR(*(char **)s.pref);
               input->value(text);
               input->textsize(14);
               field = input;
               if (!strcmp(s.name, "save_dir")) {
                  Fl_Button *browse = new Fl_Button(590, y, 92, 28,
                                                     _("Browse..."));
                  browse->callback(browse_cb, input);
                  browse->labelsize(14);
               }
            }
            y += 46;
         }
         fields[i] = field;
         field->labelsize(14);
         field->tooltip(_(s.label));
      }
      if (page == FONTS) {
         preview = new Fl_Box(32, 370, 650, 64,
                              _("Font preview: The quick brown fox 0123456789"));
         preview->box(FL_THIN_DOWN_BOX);
         preview->align(FL_ALIGN_INSIDE | FL_ALIGN_LEFT | FL_ALIGN_CLIP);
      } else if (page == NETWORK) {
         note(260, _("Leave the proxy blank for a direct connection. "
            "Use http://host:port, for example http://127.0.0.1:8080. "
            "Separate bypass domains with spaces."));
         note(328, _("The http_proxy environment variable overrides this setting. "
            "Downloads use wget's proxy settings."));
         note(392, _("Website language controls Accept-Language, for example "
            "zh-CN,en;q=0.5. Interface language follows the system locale."));
      } else if (page == PRIVACY) {
         note(258, _("Cookies are denied by default. Enabling cookies here "
            "still follows the existing per-site rules. Disabling this "
            "option blocks all cookies."));
#ifdef DISABLE_COOKIES
         for (unsigned int i = 0; i < count; i++)
            if (!strcmp(settings[i].name, "cookies_enabled"))
               fields[i]->deactivate();
         note(326, _("Cookie support is disabled in this build."));
#endif
      }
      pages[page]->end();
   }
   tabs->end();
   tabs->value(pages[GENERAL]);
   note(474, _("Save changes to your Dillo configuration. Reload pages for font and "
      "content changes. Restart Dillo for proxy, website language and "
      "interface font changes."));
   Fl_Button *cancel = new Fl_Button(484, 544, 100, 30, _("Cancel"));
   cancel->callback(cancel_cb, this);
   Fl_Return_Button *save = new Fl_Return_Button(594, 544, 110, 30, _("Save"));
   save->callback(save_cb, this);
   end();
   preview_cb(NULL, this);
}

void SettingsWindow::browse_cb(Fl_Widget *, void *data)
{
   Fl_Input *input = (Fl_Input *)data;
   const char *dir = fl_dir_chooser(_("Choose download folder"), input->value());
   if (dir)
      input->value(dir);
}

void SettingsWindow::preview_cb(Fl_Widget *widget, void *data)
{
   SettingsWindow *window = (SettingsWindow *)data;
   const char *family = prefs.font_sans_serif;
   int size = 18;
   for (unsigned int i = 0; i < count; i++) {
      if (settings[i].type == FONT &&
          (!strcmp(settings[i].name, "font_sans_serif") ||
           window->fields[i] == widget)) {
         family = ((Fl_Input_Choice *)window->fields[i])->value();
         if (window->fields[i] == widget)
            break;
      }
   }
   for (unsigned int i = 0; i < count; i++) {
      if (settings[i].type == SCALE) {
         double scale = ((Fl_Value_Input *)window->fields[i])->value();
         if (isfinite(scale))
            size = (int)MAX(9, MIN(18 * scale / 100, 40));
      }
   }
   window->preview->labelfont(dw::fltk::FltkFont::get(family, 0));
   window->preview->labelsize(size);
   window->preview->redraw();
}

char *SettingsWindow::value(unsigned int i)
{
   const Setting &s = settings[i];
   char number[64];
   if (s.type == CHECK)
      return dStrdup(((Fl_Check_Button *)fields[i])->value() ? "YES" : "NO");
   if (s.type == SCALE || s.type == INTEGER) {
      double n = ((Fl_Value_Input *)fields[i])->value();
      snprintf(number, sizeof(number), "%.12g", s.type == SCALE ? n / 100 : n);
      return dStrdup(number);
   }
   if (s.type == REFERER) {
      int selected = ((Fl_Choice *)fields[i])->value();
      return dStrdup(referers[MAX(0, MIN(selected, 2))]);
   }
   const char *text = s.type == FONT ?
      ((Fl_Input_Choice *)fields[i])->value() : ((Fl_Input *)fields[i])->value();
   char *result = dStrdup(text);
   dStrstrip(result);
   return result;
}

void SettingsWindow::error(unsigned int i, const char *message)
{
   tabs->value(pages[settings[i].page]);
   fields[i]->take_focus();
   a_Dialog_msg(_("Dillo: Preferences"), message);
}

bool SettingsWindow::validate(unsigned int i, const char *text)
{
   const Setting &s = settings[i];
   if (strchr(text, '\r') || strchr(text, '\n')) {
      error(i, _("Settings must not contain line breaks."));
      return false;
   }
   if (s.type == INTEGER || s.type == SCALE) {
      double n = ((Fl_Value_Input *)fields[i])->value();
      if (!isfinite(n) || n < s.minimum || n > s.maximum || floor(n) != n) {
         char message[160];
         snprintf(message, sizeof(message),
                  _("Enter a whole number between %.0f and %.0f."),
                  s.minimum, s.maximum);
         error(i, message);
         return false;
      }
   } else if (s.type == FONT) {
      if (!*text || (strcmp(text, *(char **)s.pref) &&
                    !dw::fltk::FltkFont::fontExists(text))) {
         error(i, _("Choose an installed font or enter its exact family name."));
         return false;
      }
   } else if (s.type == URL) {
      bool proxy = !strcmp(s.name, "http_proxy");
      DilloUrl *url = *text ? (proxy ? a_Url_new_proxy(text) :
                                                   a_Url_new(text, NULL)) : NULL;
      bool valid = proxy && !*text;
      if (url) {
         valid = *URL_SCHEME(url) && (proxy ?
            !dStrnAsciiCasecmp(text, "http://", 7) &&
            !dStrAsciiCasecmp(URL_SCHEME(url), "http") && *URL_HOST(url) &&
            !URL_ILLEGAL_CHARS(url) &&
            URL_PORT(url) > 0 && URL_PORT(url) <= 65535 &&
            !strchr(URL_AUTHORITY(url), '@') &&
            (!*URL_PATH(url) || !strcmp(URL_PATH(url), "/")) &&
            !*URL_QUERY(url) && !*URL_FRAGMENT(url) :
            dStrAsciiCasecmp(URL_SCHEME(url), "javascript") &&
            dStrAsciiCasecmp(URL_SCHEME(url), "data"));
         if (proxy && valid) {
            /* The general URL parser accepts non-numeric ports as port 80.
             * Preferences should report these mistakes rather than save them. */
            const char *authority = URL_AUTHORITY(url);
            const char *port = *authority == '[' ? strchr(authority, ']') :
                                                  strchr(authority, ':');
            if (port && *authority == '[')
               port++;
            if (port && *port) {
               valid = *port++ == ':' && *port &&
                       strspn(port, "0123456789") == strlen(port) &&
                       strtoul(port, NULL, 10) > 0 &&
                       strtoul(port, NULL, 10) <= 65535;
            }
         }
         if (!proxy && valid) {
            /* Require an explicit scheme, including for about: and file:. */
            size_t scheme_length = strspn(text,
               "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789+.-");
            valid = scheme_length && text[scheme_length] == ':';
            if (!dStrAsciiCasecmp(URL_SCHEME(url), "http") ||
                !dStrAsciiCasecmp(URL_SCHEME(url), "https"))
               valid = valid && *URL_HOST(url) && !strchr(URL_HOST(url), '%');
         }
         a_Url_free(url);
      }
      if (!valid) {
         error(i, proxy ? _("Enter an HTTP proxy such as http://127.0.0.1:8080, "
                            "or leave it blank. Enter the username separately.") :
                         _("Enter a page address, for example https://example.com "
                            "or about:blank."));
         return false;
      }
   } else if (!strcmp(s.name, "save_dir")) {
      struct stat st;
      if (*text != '/' || stat(text, &st) != 0 || !S_ISDIR(st.st_mode) ||
          access(text, W_OK) != 0) {
         error(i, _("Choose an existing, writable download folder "
                    "using an absolute path."));
         return false;
      }
   }
   return true;
}

void SettingsWindow::apply(const PrefsUpdate *updates)
{
   /* Resolve page URLs with the newly selected HTTPS policies. */
   for (unsigned int i = 0; i < count; i++) {
      if (settings[i].type == CHECK)
         *(bool_t *)settings[i].pref = !strcmp(updates[i].value, "YES");
   }
   for (unsigned int i = 0; i < count; i++) {
      const Setting &s = settings[i];
      const char *text = updates[i].value;
      switch (s.type) {
      case CHECK:
         break;
      case INTEGER:
         *(int32_t *)s.pref = strtol(text, NULL, 10);
         break;
      case SCALE:
         *(double *)s.pref = strtod(text, NULL);
         break;
      case URL:
         a_Url_free(*(DilloUrl **)s.pref);
         *(DilloUrl **)s.pref = *text ? (!strcmp(s.name, "http_proxy") ?
                         a_Url_new_proxy(text) : a_Url_new(text, NULL)) : NULL;
         break;
      default:
         dFree(*(char **)s.pref);
         *(char **)s.pref = dStrdup(text);
         break;
      }
   }
   if (!prefs.javascript_enabled) a_Javascript_disable_all();
}

void SettingsWindow::save()
{
   PrefsUpdate updates[count];
   bool valid = true;
   for (unsigned int i = 0; i < count; i++) {
      updates[i].name = settings[i].name;
      updates[i].value = value(i);
   }
   for (unsigned int i = 0; i < count && valid; i++)
      valid = validate(i, updates[i].value);
   if (valid) {
      double minimum = 0, maximum = 0;
      unsigned int index = 0;
      for (unsigned int i = 0; i < count; i++) {
         if (!strcmp(settings[i].name, "font_min_size")) {
            minimum = strtod(updates[i].value, NULL);
            index = i;
         } else if (!strcmp(settings[i].name, "font_max_size"))
            maximum = strtod(updates[i].value, NULL);
      }
      if (minimum > maximum) {
         error(index, _("The minimum font size must not exceed the maximum."));
         valid = false;
      }
   }
   if (valid) {
      char *path = dStrconcat(dGethomedir(), "/.dillo/", PATHS_RC_PREFS, NULL);
      if (a_Prefs_save(path, DILLO_SYSCONF PATHS_RC_PREFS, updates, count) == 0) {
         apply(updates);
         saved = true;
         hide();
      } else {
         Dstr *message = dStr_new("");
         dStr_sprintf(message, _("Could not save preferences to %s:\n%s"),
                      path, strerror(errno));
         a_Dialog_msg(_("Dillo: Preferences"), message->str);
         dStr_free(message, 1);
      }
      dFree(path);
   }
   for (unsigned int i = 0; i < count; i++)
      dFree((void *)updates[i].value);
}

} // namespace

void a_Settings_show(BrowserWindow *bw)
{
   SettingsWindow window;
   window.show();
   while (window.shown())
      Fl::wait();
   if (window.was_saved()) {
      for (int i = 0; i < a_Bw_num(); i++) {
         if (a_Bw_get(i) == bw) {
            a_UIcmd_set_msg(bw, "%s", _("Preferences saved. Reload pages for "
               "font changes; restart Dillo for proxy and interface changes."));
            break;
         }
      }
   }
}
