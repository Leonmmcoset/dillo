#ifndef DILLO_FLTK_I18N_HH
#define DILLO_FLTK_I18N_HH

#include <FL/Fl_File_Chooser.H>
#include <FL/Fl.H>
#include <FL/fl_ask.H>
#include <string.h>
#include "dlib/i18n.h"

/* FLTK 1.3 does not consistently fall back to CJK fonts. Pick an installed
 * Chinese family for UI labels and the browser's default sans-serif font. */
inline const char *a_I18n_fltk_font(Fl_Font *font = NULL)
{
   static const char *const families[] = {
      "Noto Sans CJK SC", "Noto Sans SC", "Source Han Sans SC",
      "WenQuanYi Micro Hei", "WenQuanYi Zen Hei", "Microsoft YaHei",
      "PingFang SC", "Heiti SC", "Arial Unicode MS"
   };
   if (strcmp(a_I18n_language(), "zh_CN"))
      return NULL;

   int count = Fl::set_fonts("-*-iso10646-1");
   for (unsigned int j = 0; j < sizeof(families) / sizeof(families[0]); j++) {
      for (int i = 0; i < count; i++) {
         int attributes;
         const char *name = Fl::get_font_name((Fl_Font)i, &attributes);
         if (!attributes && !strcmp(name, families[j])) {
            if (font)
               *font = (Fl_Font)i;
            return families[j];
         }
      }
   }
   return NULL;
}

/* FLTK provides these shared labels for applications to translate. Set them
 * before creating any dialogs, including in the separate downloads process. */
inline void a_I18n_init_fltk()
{
   fl_no = _("No");
   fl_yes = _("Yes");
   fl_ok = _("OK");
   fl_cancel = _("Cancel");
   fl_close = _("Close");
   fl_message_title_default(_("Dillo: Message"));
   fl_file_chooser_ok_label(_("OK"));

   Fl_File_Chooser::add_favorites_label = _("Add to Favorites");
   Fl_File_Chooser::all_files_label = _("All Files (*)");
   Fl_File_Chooser::custom_filter_label = _("Custom Filter");
   Fl_File_Chooser::existing_file_label = _("Existing File?");
   Fl_File_Chooser::favorites_label = _("Favorites");
   Fl_File_Chooser::filename_label = _("Filename:");
   Fl_File_Chooser::filesystems_label = _("Filesystems");
   Fl_File_Chooser::manage_favorites_label = _("Manage Favorites");
   Fl_File_Chooser::new_directory_label = _("New Directory?");
   Fl_File_Chooser::new_directory_tooltip = _("New Directory");
   Fl_File_Chooser::preview_label = _("Preview");
   Fl_File_Chooser::save_label = _("Save");
   Fl_File_Chooser::show_label = _("Show:");
   Fl_File_Chooser::hidden_label = _("Show hidden files");
}

#endif
