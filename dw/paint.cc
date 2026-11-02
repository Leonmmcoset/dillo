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

   RoundedBox(const StyleAttrs *style, double width, double height) :
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

   RoundedBox outset(double amount) const
   {
      RoundedBox result = *this;
      result.width = std::max(0.0, width + 2 * amount);
      result.height = std::max(0.0, height + 2 * amount);
      for (unsigned n = 0; n < 4; ++n) {
         result.rx[n] = rx[n] ? std::max(0.0, rx[n] + amount) : 0;
         result.ry[n] = ry[n] ? std::max(0.0, ry[n] + amount) : 0;
      }
      result.normalize();
      return result;
   }
};

// Callers pass the padding box. Each layer independently chooses the box
// used for positioning and the rounded box that clips its paint.
struct BackgroundArea {
   Rectangle bounds;
   RoundedBox rounded;
   BackgroundArea(const StyleAttrs *style, int x, int y, int width, int height, BackgroundBox box) :
      bounds(x - style->borderWidth.left, y - style->borderWidth.top,
         width + style->borderWidth.left + style->borderWidth.right,
         height + style->borderWidth.top + style->borderWidth.bottom),
      rounded(style, bounds.width, bounds.height)
   {
      if (box != BACKGROUND_BORDER_BOX) inset(style->borderWidth);
      if (box == BACKGROUND_CONTENT_BOX) inset(style->padding);
   }
   void inset(const Box &box) {
      rounded = rounded.inset(box.left, box.top, box.right, box.bottom);
      bounds.x += box.left; bounds.y += box.top;
      bounds.width = std::max(0, bounds.width - box.left - box.right);
      bounds.height = std::max(0, bounds.height - box.top - box.bottom);
   }
   bool contains(double x, double y) const {
      return rounded.contains(x - bounds.x, y - bounds.y);
   }
};

struct BackgroundTile {
   double x, y, width, height;
   BackgroundTile(const StyleAttrs *style, const Rectangle &origin, unsigned layer,
                  int intrinsicWidth = 0, int intrinsicHeight = 0)
   {
      const BackgroundSizes::Size &size = style->backgroundSize.at(layer);
      bool intrinsic = intrinsicWidth > 0 && intrinsicHeight > 0;
      width = origin.width; height = origin.height;
      if (size.mode != BackgroundSizes::Size::EXPLICIT && intrinsic) {
         double sx = width / intrinsicWidth, sy = height / intrinsicHeight;
         double scale = size.mode == BackgroundSizes::Size::COVER ? std::max(sx, sy) : std::min(sx, sy);
         width = scale * intrinsicWidth; height = scale * intrinsicHeight;
      } else if (size.mode == BackgroundSizes::Size::EXPLICIT) {
         bool autoX = size.x == LENGTH_AUTO, autoY = size.y == LENGTH_AUTO;
         if (!autoX) width = std::max(0.0, length(size.x, origin.width));
         else if (intrinsic) width = intrinsicWidth;
         if (!autoY) height = std::max(0.0, length(size.y, origin.height));
         else if (intrinsic) height = intrinsicHeight;
         if (intrinsic && autoX && !autoY) width = height * intrinsicWidth / intrinsicHeight;
         if (intrinsic && autoY && !autoX) height = width * intrinsicHeight / intrinsicWidth;
      }
      x = origin.x + length(style->backgroundPositionX, origin.width - width);
      y = origin.y + length(style->backgroundPositionY, origin.height - height);
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

void blurMask(std::vector<float> &mask, std::vector<float> &scratch,
              int width, int height, int radius, bool horizontal)
{
   if (!radius) return;
   int count = horizontal ? width : height, lines = horizontal ? height : width;
   int stride = horizontal ? 1 : width;
   for (int line = 0; line < lines; ++line) {
      int base = horizontal ? line * width : line;
      auto at = [&](int p) { return mask[base + std::max(0, std::min(count - 1, p)) * stride]; };
      double sum = 0;
      for (int p = -radius; p <= radius; ++p) sum += at(p);
      for (int p = 0; p < count; ++p) {
         scratch[base + p * stride] = sum / (2 * radius + 1);
         sum += at(p + radius + 1) - at(p - radius);
      }
   }
   mask.swap(scratch);
}
} // namespace

void drawBoxShadows(View *view, Layout *layout, Rectangle *area,
   int x, int y, int width, int height, Style *style, bool inset, bool inverse)
{
   Rectangle border(x + style->margin.left, y + style->margin.top,
      width - style->margin.left - style->margin.right,
      height - style->margin.top - style->margin.bottom);
   if (border.isEmpty()) return;
   RoundedBox outer(style, border.width, border.height);
   const Box &b = style->borderWidth;
   RoundedBox clip = inset ? outer.inset(b.left, b.top, b.right, b.bottom) : outer;
   int clipX = border.x + (inset ? b.left : 0), clipY = border.y + (inset ? b.top : 0);
   for (unsigned n = style->boxShadows.count; n > 0; --n) {
      const BoxShadows::Shadow &shadow = style->boxShadows.shadows[n - 1];
      if (shadow.inset != inset || !(shadow.rgba & 255)) continue;
      double spread = inset ? -shadow.spread : shadow.spread;
      RoundedBox shape = clip.outset(spread);
      int shapeX = clipX + shadow.x - spread, shapeY = clipY + shadow.y - spread;
      int radii[3] = {}, halo = 0;
      if (shadow.blur) {
         double variance = shadow.blur * shadow.blur / 4.0;
         int low = (int)std::floor(std::sqrt(4 * variance + 1));
         if (!(low & 1)) --low;
         int small = (int)std::round((12 * variance - 3 * low * low - 12 * low - 9) / (-4 * low - 4));
         for (int p = 0; p < 3; ++p) {
            radii[p] = (low + (p < small ? 0 : 2)) / 2;
            halo += radii[p];
         }
      }
      Rectangle bounds = inset ? Rectangle(clipX, clipY, clip.width, clip.height) :
         Rectangle(shapeX - halo, shapeY - halo, shape.width + 2 * halo, shape.height + 2 * halo);
      Rectangle visible;
      if (!area->intersectsWith(&bounds, &visible)) continue;
      Pixel color(inverse ? shadow.rgba ^ 0xffffff00u : shadow.rgba);
      auto allowed = [&](double px, double py) {
         bool inside = clip.contains(px - clipX, py - clipY);
         return inset ? inside : !inside;
      };
      if (!halo) {
         raster(view, layout, visible, [&](double px, double py) {
            bool inside = shape.contains(px - shapeX, py - shapeY);
            return allowed(px, py) && (inset ? !inside : inside) ? color : Pixel();
         });
         continue;
      }
      // Three separable box filters approximate the Gaussian CSS blur.
      // Include the filter halo per tile, never allocate a box-sized mask.
      const int tile = 128;
      for (int ty = visible.y; ty < visible.y + visible.height; ty += tile)
         for (int tx = visible.x; tx < visible.x + visible.width; tx += tile) {
            int tw = std::min(tile, visible.x + visible.width - tx);
            int th = std::min(tile, visible.y + visible.height - ty);
            int mw = tw + 2 * halo, mh = th + 2 * halo;
            std::vector<float> mask(mw * mh), scratch(mw * mh);
            for (int row = 0; row < mh; ++row) for (int col = 0; col < mw; ++col) {
               bool inside = shape.contains(tx + col - halo + .5 - shapeX,
                                             ty + row - halo + .5 - shapeY);
               mask[row * mw + col] = inset ? !inside : inside;
            }
            for (int p = 0; p < 3; ++p) {
               blurMask(mask, scratch, mw, mh, radii[p], true);
               blurMask(mask, scratch, mw, mh, radii[p], false);
            }
            Rectangle tileArea(tx, ty, tw, th);
            raster(view, layout, tileArea, [&](double px, double py) {
               if (!allowed(px, py)) return Pixel();
               double coverage = std::max(0.0f, std::min(1.0f,
                  mask[((int)py - ty + halo) * mw + (int)px - tx + halo]));
               Pixel result = color;
               result.r *= coverage; result.g *= coverage;
               result.b *= coverage; result.a *= coverage;
               return result;
            });
         }
   }
}

bool hasRoundedCorners(const StyleAttrs *style)
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

void drawOutline(View *view, Layout *layout, Rectangle *area,
   int x, int y, int width, int height, Style *style, bool inverse)
{
   const Outline &o = style->outline;
   if (o.width <= 0 || o.style == BORDER_NONE || o.style == BORDER_HIDDEN || !(o.rgba & 255)) return;
   int bx = x + style->margin.left, by = y + style->margin.top;
   RoundedBox border(style, width - style->margin.left - style->margin.right,
      height - style->margin.top - style->margin.bottom);
   RoundedBox outer = border.outset(o.offset + o.width), inner = border.outset(o.offset);
   int ox = bx - o.offset - o.width, oy = by - o.offset - o.width;
   Rectangle bounds(ox, oy, outer.width, outer.height), visible;
   if (!area->intersectsWith(&bounds, &visible)) return;
   RoundedBox stripeOuter = border.outset(o.offset + 2 * o.width / 3.0);
   RoundedBox stripeInner = border.outset(o.offset + o.width / 3.0);
   Pixel color(inverse ? o.rgba ^ 0xffffff00u : o.rgba);
   raster(view, layout, visible, [&](double px, double py) {
      if (!outer.contains(px - ox, py - oy) || inner.contains(px - bx + o.offset, py - by + o.offset)) return Pixel();
      if (o.style == BORDER_DOUBLE && o.width >= 3 &&
          stripeOuter.contains(px - bx + o.offset + 2 * o.width / 3.0,
                               py - by + o.offset + 2 * o.width / 3.0) &&
          !stripeInner.contains(px - bx + o.offset + o.width / 3.0,
                                py - by + o.offset + o.width / 3.0)) return Pixel();
      if (o.style == BORDER_DASHED || o.style == BORDER_DOTTED) {
         double lx = px - ox, ly = py - oy;
         double distances[4] = {ly, outer.width - lx, outer.height - ly, lx};
         unsigned side = std::min_element(distances, distances + 4) - distances;
         double along = side == 0 ? lx : side == 1 ? ly : side == 2 ? outer.width - lx : outer.height - ly;
         if (o.style == BORDER_DASHED) {
            if (std::fmod(along, 6 * o.width) >= 3 * o.width) return Pixel();
         } else {
            double dx = std::fmod(along, 2 * o.width) - o.width / 2.0;
            double dy = distances[side] - o.width / 2.0;
            if (dx * dx + dy * dy > o.width * (double)o.width / 4) return Pixel();
         }
      }
      return color;
   });
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
   unsigned count = std::max((size_t)1, style->backgroundLayers.size());
   BackgroundArea colorArea(style, x, y, width, height, style->backgroundClip.at(count - 1));
   if (!style->hasBackgroundGradient() && color && style->backgroundAlpha == 255 && !hasRoundedCorners(style)) {
      Rectangle visible;
      if (area->intersectsWith(&colorArea.bounds, &visible))
         view->drawRectangle(color, inverse ? Color::SHADING_INVERSE : Color::SHADING_NORMAL,
            true, visible.x, visible.y, visible.width, visible.height);
      return;
   }
   BackgroundArea borderArea(style, x, y, width, height, BACKGROUND_BORDER_BOX);
   Rectangle visible;
   if (!area->intersectsWith(&borderArea.bounds, &visible)) return;
   struct Layer {
      BackgroundArea clip, origin;
      BackgroundTile tile;
      LinearGradient gradient;
      Layer(Style *style, int x, int y, int width, int height, unsigned n) :
         clip(style, x, y, width, height, style->backgroundClip.at(n)),
         origin(style, x, y, width, height, style->backgroundOrigin.at(n)),
         tile(style, origin.bounds, n),
         gradient(style->backgroundLayers[n], tile.width, tile.height) { }
   };
   std::vector<Layer> layers;
   for (unsigned n = 0; n < style->backgroundLayers.size(); ++n)
      layers.emplace_back(style, x, y, width, height, n);
   Pixel background((rgba(color, inverse) & ~255u) | (color ? style->backgroundAlpha : 0));
   raster(view, layout, visible, [&](double px, double py) {
      Pixel result = colorArea.contains(px, py) ? background : Pixel();
      for (size_t n = layers.size(); n > 0; --n) {
         const Layer &layer = layers[n - 1];
         const BackgroundTile &tile = layer.tile;
         if (!layer.gradient.gradient.count || tile.width <= 0 || tile.height <= 0 || !layer.clip.contains(px, py)) continue;
         double gx = px - tile.x, gy = py - tile.y;
         bool repeatX = style->backgroundRepeat == BACKGROUND_REPEAT || style->backgroundRepeat == BACKGROUND_REPEAT_X;
         bool repeatY = style->backgroundRepeat == BACKGROUND_REPEAT || style->backgroundRepeat == BACKGROUND_REPEAT_Y;
         if (!repeatX && (gx < 0 || gx >= tile.width)) continue;
         if (!repeatY && (gy < 0 || gy >= tile.height)) continue;
         if (repeatX) gx -= std::floor(gx / tile.width) * tile.width;
         if (repeatY) gy -= std::floor(gy / tile.height) * tile.height;
         result = layer.gradient.sample(gx, gy).over(result);
      }
      return result;
   });
}

void drawRoundedImage(View *view, Imgbuf *buffer, Rectangle *area,
   int imageX, int imageY, int x, int y, int width, int height, const StyleAttrs *style)
{
   int leftInset = style->borderWidth.left + style->padding.left;
   int topInset = style->borderWidth.top + style->padding.top;
   int rightInset = style->borderWidth.right + style->padding.right;
   int bottomInset = style->borderWidth.bottom + style->padding.bottom;
   RoundedBox outer(style, width + leftInset + rightInset, height + topInset + bottomInset);
   RoundedBox clip = outer.inset(leftInset, topInset, rightInset, bottomInset);
   if (!hasRoundedCorners(style)) {
      view->drawImage(buffer, imageX, imageY, area->x - imageX, area->y - imageY, area->width, area->height);
      return;
   }
   for (int row = area->y; row < area->y + area->height; ++row) {
      int left = area->x, right = left + area->width;
      while (left < right && !clip.contains(left - x + .5, row - y + .5)) ++left;
      while (right > left && !clip.contains(right - x - .5, row - y + .5)) --right;
      if (left < right) view->drawImage(buffer, imageX, imageY,
         left - imageX, row - imageY, right - left, 1);
   }
}

void drawRoundedBackgroundImage(View *view, Layout *layout, Rectangle *area,
   int x, int y, int width, int height, int xRef, int yRef, int widthRef,
   int heightRef, const StyleAttrs *style)
{
   BackgroundArea clip(style, x, y, width, height, style->backgroundClip.at(0));
   BackgroundArea origin(style, xRef, yRef, widthRef, heightRef, style->backgroundOrigin.at(0));
   if (style->backgroundAttachment == BACKGROUND_ATTACHMENT_FIXED)
      origin.bounds = Rectangle(layout->getScrollPosX(), layout->getScrollPosY(),
         layout->getContentWidthViewport(), layout->getContentHeightViewport());
   Rectangle visible;
   if (!area->intersectsWith(&clip.bounds, &visible)) return;
   const BackgroundSizes::Size &size = style->backgroundSize.at(0);
   if (size.mode != BackgroundSizes::Size::EXPLICIT || size.x != LENGTH_AUTO || size.y != LENGTH_AUTO) {
      Imgbuf *source = style->backgroundImage->getImgbufSrc();
      BackgroundTile tile(style, origin.bounds, 0, source->getRootWidth(), source->getRootHeight());
      if (tile.width < .5 || tile.height < .5 || tile.width > 32767 || tile.height > 32767) return;
      int iw = (int)std::floor(tile.width + .5), ih = (int)std::floor(tile.height + .5);
      Imgbuf *image = style->backgroundImage->getScaledBackground(iw, ih);
      if (!image) return;
      bool repeatX = style->backgroundRepeat == BACKGROUND_REPEAT || style->backgroundRepeat == BACKGROUND_REPEAT_X;
      bool repeatY = style->backgroundRepeat == BACKGROUND_REPEAT || style->backgroundRepeat == BACKGROUND_REPEAT_Y;
      // Group tiny repeated images into a small tile rather than issuing one
      // draw call for every pixel. Rebuild from the streaming scaled buffer.
      int tw = iw, th = ih;
      Imgbuf *group = NULL;
      if ((int64_t)iw * ih < 1024 && (repeatX || repeatY)) {
         if (repeatX) tw = iw * std::max(1, (32 + iw - 1) / iw);
         if (repeatY) th = ih * std::max(1, (32 + ih - 1) / ih);
         group = image->createSimilarBuf(tw, th);
         for (int tx = 0; tx < tw; tx += iw) for (int ty = 0; ty < th; ty += ih)
            image->copyTo(group, tx, ty, 0, 0, iw, ih);
         image = group;
      }
      double ox = origin.bounds.x + length(style->backgroundPositionX, origin.bounds.width - iw);
      double oy = origin.bounds.y + length(style->backgroundPositionY, origin.bounds.height - ih);
      auto drawSpan = [&](int left, int top, int right, int bottom) {
         double x0 = repeatX ? ox + std::floor((left - ox) / tw) * tw : ox;
         double y0 = repeatY ? oy + std::floor((top - oy) / th) * th : oy;
         for (double tx = x0; tx < right; tx += tw) {
            if (tx + tw > left) for (double ty = y0; ty < bottom; ty += th) {
               if (ty + th > top) {
                  int x = (int)std::floor(tx + .5), y = (int)std::floor(ty + .5);
                  int l = std::max(left, x), t = std::max(top, y);
                  int r = std::min(right, x + tw), b = std::min(bottom, y + th);
                  if (r > l && b > t) view->drawImage(image, x, y, l - x, t - y, r - l, b - t);
               }
               if (!repeatY) break;
            }
            if (!repeatX) break;
         }
      };
      if (!hasRoundedCorners(style)) drawSpan(visible.x, visible.y, visible.x + visible.width, visible.y + visible.height);
      else for (int row = visible.y; row < visible.y + visible.height; ++row) {
         int left = visible.x, right = visible.x + visible.width;
         while (left < right && !clip.contains(left + .5, row + .5)) ++left;
         while (right > left && !clip.contains(right - .5, row + .5)) --right;
         if (left < right) drawSpan(left, row, right, row + 1);
      }
      if (group) group->unref();
      return;
   }
   if (!hasRoundedCorners(style)) {
      drawBackgroundImage(view, style->backgroundImage, style->backgroundRepeat,
         style->backgroundAttachment, style->backgroundPositionX, style->backgroundPositionY,
         visible.x, visible.y, visible.width, visible.height,
         origin.bounds.x, origin.bounds.y, origin.bounds.width, origin.bounds.height);
      return;
   }
   // Clip image rows only at rounded corners. The image keeps its original
   // reference box, so clipping cannot restart background tiling/positioning.
   for (int row = visible.y; row < visible.y + visible.height; ++row) {
      int left = visible.x, right = visible.x + visible.width;
      while (left < right && !clip.contains(left + .5, row + .5)) ++left;
      while (right > left && !clip.contains(right - .5, row + .5)) --right;
      if (left < right)
         drawBackgroundImage(view, style->backgroundImage, style->backgroundRepeat,
            style->backgroundAttachment, style->backgroundPositionX, style->backgroundPositionY,
            left, row, right - left, 1, origin.bounds.x, origin.bounds.y, origin.bounds.width, origin.bounds.height);
   }
}

} } }
