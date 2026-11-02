/* Linux text shaping and Unicode font fallback, shared by measurement and paint. */
#include "config.h"
#include "fltktext.hh"
#ifdef HAVE_PANGO_CAIRO
#include <pango/pangocairo.h>
#include <cairo-xlib.h>
#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/fl_draw.H>
#include <FL/x.H>
#include <map>

namespace dw { namespace fltk {
static PangoFontDescription *description(core::style::Font *font)
{
   PangoFontDescription *desc = pango_font_description_new();
   pango_font_description_set_family(desc, font->name);
   pango_font_description_set_absolute_size(desc, font->size * PANGO_SCALE);
   pango_font_description_set_weight(desc, static_cast<PangoWeight>(font->weight));
   pango_font_description_set_style(desc, font->style == core::style::FONT_STYLE_NORMAL ? PANGO_STYLE_NORMAL :
      font->style == core::style::FONT_STYLE_ITALIC ? PANGO_STYLE_ITALIC : PANGO_STYLE_OBLIQUE);
   return desc;
}
static PangoLayout *textLayout(core::style::Font *font, const char *text, int length)
{
   PangoContext *context = pango_font_map_create_context(pango_cairo_font_map_get_default());
   PangoLayout *layout = pango_layout_new(context); g_object_unref(context);
   PangoFontDescription *desc = description(font);
   pango_layout_set_font_description(layout, desc); pango_font_description_free(desc);
   pango_layout_set_single_paragraph_mode(layout, TRUE);
   pango_layout_set_text(layout, text, length);
   PangoAttrList *attrs = pango_attr_list_new();
   if (font->letterSpacing) pango_attr_list_insert(attrs, pango_attr_letter_spacing_new(font->letterSpacing * PANGO_SCALE));
   if (font->fontVariant == core::style::FONT_VARIANT_SMALL_CAPS)
      pango_attr_list_insert(attrs, pango_attr_font_features_new("smcp=1"));
   pango_layout_set_attributes(layout, attrs); pango_attr_list_unref(attrs);
   return layout;
}
int shapedTextWidth(core::style::Font *font, const char *text, int length)
{
   PangoLayout *layout = textLayout(font, text, length);
   int width, height; pango_layout_get_pixel_size(layout, &width, &height);
   g_object_unref(layout); return width;
}
void shapedFontMetrics(core::style::Font *font)
{
   PangoContext *context = pango_font_map_create_context(pango_cairo_font_map_get_default());
   PangoFontDescription *desc = description(font);
   PangoFontMetrics *metrics = pango_context_get_metrics(context, desc, pango_language_get_default());
   font->ascent = PANGO_PIXELS_CEIL(pango_font_metrics_get_ascent(metrics));
   font->descent = PANGO_PIXELS_CEIL(pango_font_metrics_get_descent(metrics));
   pango_font_metrics_unref(metrics); pango_font_description_free(desc); g_object_unref(context);
   font->spaceWidth = shapedTextWidth(font, " ", 1);
   font->zeroWidth = shapedTextWidth(font, "0", 1);
}
std::string nativeControlFontFamily(core::style::Font *font)
{
   // FLTK's editable controls use one native font. Select the system fallback
   // that covers Chinese as well as Latin; page text still uses full shaping.
   static std::map<std::string, std::string> families;
   if (families.count(font->name)) return families[font->name];
   PangoFontMap *map = pango_cairo_font_map_get_default();
   PangoContext *context = pango_font_map_create_context(map);
   PangoFontDescription *desc = description(font);
   PangoFontset *set = pango_font_map_load_fontset(map, context, desc, pango_language_from_string("zh-cn"));
   PangoFont *fallback = set ? pango_fontset_get_font(set, 0x4e2d) : NULL;
   std::string family;
   if (fallback) {
      PangoFontDescription *fallbackDesc = pango_font_describe(fallback);
      const char *name = pango_font_description_get_family(fallbackDesc);
      if (name) family = name;
      pango_font_description_free(fallbackDesc); g_object_unref(fallback);
   }
   if (set) g_object_unref(set);
   pango_font_description_free(desc); g_object_unref(context);
   families[font->name] = family;
   return family;
}
void drawShapedText(core::style::Font *font, unsigned color,
                    int x, int baseline, const char *text, int length)
{
   Fl_Window *window = Fl_Window::current();
   if (!window || length <= 0) return;
   // fl_window also identifies the current FLTK offscreen drawable. Respect
   // FLTK's clip so text cannot paint into controls or other damaged regions.
   int cx, cy, cw, ch;
   fl_clip_box(0, 0, window->w(), window->h(), cx, cy, cw, ch);
   if (cw <= 0 || ch <= 0) return;
   cairo_surface_t *surface = cairo_xlib_surface_create(fl_display, fl_window, fl_visual->visual, window->w(), window->h());
   cairo_t *cr = cairo_create(surface);
   cairo_rectangle(cr, cx, cy, cw, ch); cairo_clip(cr);
   cairo_set_source_rgb(cr, ((color >> 16) & 255) / 255.0, ((color >> 8) & 255) / 255.0, (color & 255) / 255.0);
   PangoLayout *layout = textLayout(font, text, length);
   cairo_move_to(cr, x, baseline - pango_layout_get_baseline(layout) / static_cast<double>(PANGO_SCALE));
   pango_cairo_show_layout(cr, layout); g_object_unref(layout);
   cairo_destroy(cr); cairo_surface_flush(surface); cairo_surface_destroy(surface);
}
}}
#else
namespace dw { namespace fltk {
int shapedTextWidth(core::style::Font *, const char *, int) { return 0; }
void shapedFontMetrics(core::style::Font *) {}
std::string nativeControlFontFamily(core::style::Font *) { return ""; }
void drawShapedText(core::style::Font *, unsigned, int, int, const char *, int) {}
}}
#endif
