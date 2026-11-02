#ifndef DILLO_FLTK_TEXT_HH
#define DILLO_FLTK_TEXT_HH
#include "core.hh"
#include <string>
namespace dw { namespace fltk {
int shapedTextWidth(core::style::Font *font, const char *text, int length);
void shapedFontMetrics(core::style::Font *font);
std::string nativeControlFontFamily(core::style::Font *font);
void drawShapedText(core::style::Font *font, unsigned color,
                    int x, int baseline, const char *text, int length);
}}
#endif
