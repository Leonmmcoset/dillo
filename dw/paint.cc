/* CSS gradients and elliptical border radii. GPL-3.0-or-later. */
#include "paint.hh"
#include <algorithm>
#include <cmath>
#include <vector>

namespace dw { namespace core { namespace style {
namespace {

double length(Length value, double reference)
{
   return isAbsLength(value) ? absLengthVal(value) : isPerLength(value) ?
      reference * perLengthVal_useThisOnlyForDebugging(value) +
      (isCalcLength(value) ? calcLengthPixels(value) : 0) : 0;
}

struct RoundedBox {
   double width, height, rx[4], ry[4];

   RoundedBox(Style *style, double width, double height) :
      width(std::max(0.0, width)), height(std::max(0.0, height))
   {
      for (unsigned n = 0; n < 4; ++n) {
         rx[n] = std::max(0.0, length(style->borderRadius.x[n], width));
         ry[n] = std::max(0.0, length(style->borderRadius.y[n], height));
         if (!rx[n] || !ry[n]) rx[n] = ry[n] = 0;
      }
      normalize();
   }

   void normalize()
   {
      double scale = 1;
      auto fit = [&](double available, double sum) {
         if (sum > 0) scale = std::min(scale, available / sum);
      };
      fit(width, rx[0] + rx[1]); fit(width, rx[3] + rx[2]);
      fit(height, ry[0] + ry[3]); fit(height, ry[1] + ry[2]);
      for (unsigned n = 0; n < 4; ++n) { rx[n] *= scale; ry[n] *= scale; }
   }

   RoundedBox inset(double left, double top, double right, double bottom) const
   {
      RoundedBox inner = *this;
      inner.width = std::max(0.0, width - left - right);
      inner.height = std::max(0.0, height - top - bottom);
      for (unsigned n = 0; n < 4; ++n) {
         inner.rx[n] = std::max(0.0, rx[n] - ((n == 0 || n == 3) ? left : right));
         inner.ry[n] = std::max(0.0, ry[n] - (n < 2 ? top : bottom));
         if (!inner.rx[n] || !inner.ry[n]) inner.rx[n] = inner.ry[n] = 0;
      }
      inner.normalize();
      return inner;
   }

   bool contains(double x, double y) const
   {
      if (x < 0 || y < 0 || x >= width || y >= height) return false;
      for (unsigned n = 0; n < 4; ++n) {
         double dx = (n == 0 || n == 3) ? rx[n] - x : x - (width - rx[n]);
         double dy = n < 2 ? ry[n] - y : y - (height - ry[n]);
         if (rx[n] && ry[n] && dx > 0 && dy > 0 &&
             dx * dx / (rx[n] * rx[n]) + dy * dy / (ry[n] * ry[n]) > 1) return false;
      }
      return true;
   }
};

// Premultiplied channels make transparent color stops interpolate without
// adding a dark fringe (e.g. white -> transparent).
struct Pixel {
   double r, g, b, a;
   Pixel() : r(0), g(0), b(0), a(0) { }
   explicit Pixel(unsigned rgba) {
      a = (rgba & 255) / 255.0;
      r = ((rgba >> 24) & 255) * a;
      g = ((rgba >> 16) & 255) * a;
      b = ((rgba >> 8) & 255) * a;
   }
   Pixel over(const Pixel &under) const {
      Pixel p;
      p.a = a + under.a * (1 - a);
      p.r = r + under.r * (1 - a); p.g = g + under.g * (1 - a); p.b = b + under.b * (1 - a);
      return p;
   }
};

struct LinearGradient {
   const Gradient &gradient;
   double dx, dy, extent, width, height, positions[32];
   Pixel colors[32];
   LinearGradient(const Gradient &gradient, double width, double height) :
      gradient(gradient), width(width), height(height)
   {
      double angle = gradient.angle * std::acos(-1.0) / 180;
      dx = std::sin(angle); dy = -std::cos(angle);
      if (gradient.cornerX && gradient.cornerY) {
         // The gradient line is perpendicular to the diagonal connecting the
         // two adjacent corners; this gives CSS's "magic corner" behavior.
         double norm = std::hypot(width, height);
         dx = gradient.cornerX * height / std::max(1.0, norm);
         dy = gradient.cornerY * width / std::max(1.0, norm);
      }
      extent = std::max(1e-9, std::abs(dx) * width + std::abs(dy) * height);
      unsigned count = gradient.count;
      for (unsigned n = 0; n < count; ++n) {
         positions[n] = gradient.stops[n].positioned ? length(gradient.stops[n].position, extent) : 0;
         colors[n] = Pixel(gradient.stops[n].rgba);
      }
      if (!count) return;
      if (!gradient.stops[0].positioned) positions[0] = 0;
      if (!gradient.stops[count - 1].positioned) positions[count - 1] = extent;
      double previous = positions[0];
      for (unsigned n = 1; n < count; ++n)
         if (gradient.stops[n].positioned || n == count - 1) {
            positions[n] = std::max(previous, positions[n]); previous = positions[n];
         }
      for (unsigned start = 0; start + 1 < count;) {
         unsigned end = start + 1;
         while (end + 1 < count && !gradient.stops[end].positioned) ++end;
         for (unsigned n = start + 1; n < end; ++n)
            positions[n] = positions[start] + (positions[end] - positions[start]) * (n - start) / (end - start);
         start = end;
      }
   }
   Pixel sample(double x, double y) const
   {
      unsigned count = gradient.count;
      if (!count) return Pixel();
      double p = (x - width / 2) * dx + (y - height / 2) * dy + extent / 2;
      if (gradient.repeating) {
         double period = positions[count - 1] - positions[0];
         if (period <= 1e-9) return colors[count - 1];
         p = positions[0] + std::fmod(std::fmod(p - positions[0], period) + period, period);
      }
      if (p < positions[0]) return colors[0];
      unsigned end = 1;
      while (end < count && p >= positions[end]) ++end;
      if (end == count) return colors[count - 1];
      double span = positions[end] - positions[end - 1];
      double t = span > 0 ? (p - positions[end - 1]) / span : 1;
      Pixel result;
      result.r = colors[end - 1].r * (1 - t) + colors[end].r * t;
      result.g = colors[end - 1].g * (1 - t) + colors[end].g * t;
      result.b = colors[end - 1].b * (1 - t) + colors[end].b * t;
      result.a = colors[end - 1].a * (1 - t) + colors[end].a * t;
      return result;
   }
};

// Paint only the exposed region, in bounded tiles. A large CSS box cannot
// trigger a box-sized allocation, and a partial repaint keeps the same
// gradient/radius coordinates as a complete repaint.
template<class Sample>
void raster(View *view, Layout *layout, const Rectangle &area, Sample sample)
{
   const int tileSize = 128, samples = 4;
   std::vector<byte> row(tileSize * 4);
   for (int y = area.y; y < area.y + area.height; y += tileSize) {
      int height = std::min(tileSize, area.y + area.height - y);
      for (int x = area.x; x < area.x + area.width; x += tileSize) {
         int width = std::min(tileSize, area.x + area.width - x);
         Imgbuf *buffer = layout->getPlatform()->createImgbuf(Imgbuf::RGBA, width, height, 1.0);
         for (int r = 0; r < height; ++r) {
            for (int c = 0; c < width; ++c) {
               Pixel pixel;
               for (int sy = 0; sy < samples; ++sy) for (int sx = 0; sx < samples; ++sx) {
                  Pixel p = sample(x + c + (sx + .5) / samples, y + r + (sy + .5) / samples);
                  pixel.r += p.r; pixel.g += p.g; pixel.b += p.b; pixel.a += p.a;
               }
               double a = pixel.a / (samples * samples);
               row[c * 4] = pixel.a > 0 ? (byte)std::min(255.0, pixel.r / pixel.a + .5) : 0;
               row[c * 4 + 1] = pixel.a > 0 ? (byte)std::min(255.0, pixel.g / pixel.a + .5) : 0;
               row[c * 4 + 2] = pixel.a > 0 ? (byte)std::min(255.0, pixel.b / pixel.a + .5) : 0;
               row[c * 4 + 3] = (byte)std::min(255.0, a * 255 + .5);
            }
            buffer->copyRow(r, row.data());
         }
         view->drawImage(buffer, x, y, 0, 0, width, height);
         buffer->unref();
      }
   }
}

unsigned rgba(Color *color, bool inverse)
{
   unsigned rgb = color ? color->getColor() : 0;
   if (inverse) rgb ^= 0xffffff;
   return (rgb << 8) | (color ? 255 : 0);
}
} // namespace

bool hasRoundedCorners(const Style *style)
{
   auto nonzero = [](Length radius) {
      return isAbsLength(radius) ? absLengthVal(radius) > 0 :
         isPerLength(radius) && (perLengthVal_useThisOnlyForDebugging(radius) > 0 ||
            (isCalcLength(radius) && calcLengthPixels(radius) > 0));
   };
   for (unsigned n = 0; n < 4; ++n)
      if (nonzero(style->borderRadius.x[n]) && nonzero(style->borderRadius.y[n])) return true;
   return false;
}

bool drawRoundedBorder(View *view, Layout *layout, Rectangle *area,
   int x, int y, int width, int height, Style *style, bool inverse)
{
   if (!hasRoundedCorners(style)) return false;
   int widths[4] = {style->borderWidth.top, style->borderWidth.right,
      style->borderWidth.bottom, style->borderWidth.left};
   BorderStyle types[4] = {style->borderStyle.top, style->borderStyle.right,
      style->borderStyle.bottom, style->borderStyle.left};
   Color *colors[4] = {style->borderColor.top, style->borderColor.right,
      style->borderColor.bottom, style->borderColor.left};
   Pixel pixels[4];
   if (!widths[0] && !widths[1] && !widths[2] && !widths[3]) return true;
   for (unsigned n = 0; n < 4; ++n) {
      if (widths[n] && types[n] != BORDER_NONE && types[n] != BORDER_HIDDEN && types[n] != BORDER_SOLID)
         return false;
      pixels[n] = Pixel((types[n] == BORDER_NONE || types[n] == BORDER_HIDDEN) ? 0 : rgba(colors[n], inverse));
   }
   Rectangle box(x + style->margin.left, y + style->margin.top,
      width - style->margin.left - style->margin.right, height - style->margin.top - style->margin.bottom);
   Rectangle visible;
   if (!area->intersectsWith(&box, &visible)) return true;
   RoundedBox outer(style, box.width, box.height);
   RoundedBox inner = outer.inset(widths[3], widths[0], widths[1], widths[2]);
   raster(view, layout, visible, [&](double px, double py) {
      double x = px - box.x, y = py - box.y;
      if (!outer.contains(x, y) || inner.contains(x - widths[3], y - widths[0])) return Pixel();
      double distances[4] = {y, box.width - x, box.height - y, x};
      unsigned side = 0; double closest = 1e30;
      for (unsigned n = 0; n < 4; ++n) if (widths[n] && distances[n] / widths[n] < closest) {
         closest = distances[n] / widths[n]; side = n;
      }
      return pixels[side];
   });
   return true;
}

void drawBackgroundPaint(View *view, Layout *layout, Rectangle *area,
   int x, int y, int width, int height, Style *style, Color *color, bool inverse)
{
   Rectangle box(x, y, width, height), visible;
   if (!area->intersectsWith(&box, &visible)) return;
   const Box &border = style->borderWidth;
   RoundedBox outer(style, width + border.left + border.right, height + border.top + border.bottom);
   RoundedBox inner = outer.inset(border.left, border.top, border.right, border.bottom);
   LinearGradient gradient(style->gradient, width, height);
   Pixel background(rgba(color, inverse));
   raster(view, layout, visible, [&](double px, double py) {
      double xLocal = px - x, yLocal = py - y;
      return inner.contains(xLocal, yLocal) ? gradient.sample(xLocal, yLocal).over(background) : Pixel();
   });
}

void drawRoundedBackgroundImage(View *view, Layout *layout, Rectangle *area,
   int x, int y, int width, int height, int xRef, int yRef, int widthRef,
   int heightRef, Style *style)
{
   (void)layout;
   Rectangle box(x, y, width, height), visible;
   if (!area->intersectsWith(&box, &visible)) return;
   const Box &border = style->borderWidth;
   RoundedBox outer(style, width + border.left + border.right, height + border.top + border.bottom);
   RoundedBox inner = outer.inset(border.left, border.top, border.right, border.bottom);
   // Clip image rows only at rounded corners. The image keeps its original
   // reference box, so clipping cannot restart background tiling/positioning.
   for (int row = visible.y; row < visible.y + visible.height; ++row) {
      int left = visible.x, right = visible.x + visible.width;
      while (left < right && !inner.contains(left - x + .5, row - y + .5)) ++left;
      while (right > left && !inner.contains(right - x - .5, row - y + .5)) --right;
      if (left < right)
         drawBackgroundImage(view, style->backgroundImage, style->backgroundRepeat,
            style->backgroundAttachment, style->backgroundPositionX, style->backgroundPositionY,
            left, row, right - left, 1, xRef, yRef, widthRef, heightRef);
   }
}

} } }
