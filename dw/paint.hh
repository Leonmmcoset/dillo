/* CSS box decoration painting, independent of the native UI toolkit. */
#ifndef DW_PAINT_HH
#define DW_PAINT_HH

#include "core.hh"

namespace dw { namespace core { namespace style {
void drawRoundedImage(View *view, Imgbuf *buffer, Rectangle *area,
   int imageX, int imageY, int x, int y, int width, int height, const StyleAttrs *style);
bool hasRoundedCorners(const StyleAttrs *style);
void drawBoxShadows(View *view, Layout *layout, Rectangle *area,
   int x, int y, int width, int height, Style *style, bool inset, bool inverse);
void drawOutline(View *view, Layout *layout, Rectangle *area,
   int x, int y, int width, int height, Style *style, bool inverse);
bool drawRoundedBorder(View *view, Layout *layout, Rectangle *area,
   int x, int y, int width, int height, Style *style, bool inverse);
void drawBackgroundPaint(View *view, Layout *layout, Rectangle *area,
   int x, int y, int width, int height, Style *style, Color *color, bool inverse);
void drawRoundedBackgroundImage(View *view, Layout *layout, Rectangle *area,
   int x, int y, int width, int height, int xRef, int yRef, int widthRef,
   int heightRef, const StyleAttrs *style);
} } }
#endif
