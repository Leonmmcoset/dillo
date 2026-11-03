#include "flexbox.hh"
#include <algorithm>
#include <cmath>

using namespace dw::core;
using namespace dw::core::style;
using lout::misc::min;
using lout::misc::max;

namespace dw {
int Flexbox::index(Widget *widget)
{
   for (size_t i = 0; i < items.size(); ++i) if (items[i].widget == widget) return i;
   return -1;
}

void Flexbox::addWidget(Widget *widget, Style *style)
{
   anonymous = NULL;
   Textblock::addWidget(widget, style);
   if (childOutOfFlow(widget)) return;
   Item item = {}; item.widget = widget; item.width = item.height = -1;
   items.push_back(item);
}

void Flexbox::addText(const char *text, size_t length, Style *style)
{
   if (!anonymous) {
      Textblock *block = new Textblock(false);
      StyleAttrs attrs = *style; attrs.display = DISPLAY_BLOCK;
      Style *s = Style::create(&attrs);
      addWidget(block, s); s->unref(); anonymous = block;
   }
   anonymous->currentDomNode = currentDomNode;
   anonymous->addText(text, length, style);
}

void Flexbox::addSpace(Style *style)
{ if (anonymous) anonymous->addSpace(style); }

int Flexbox::getAvailWidthOfChild(Widget *child, bool force)
{
   int i = index(child);
   if (i >= 0 && items[i].width >= 0) return items[i].width;
   if (calculating) return max(0, contentWidth);
   return Textblock::getAvailWidthOfChild(child, force);
}
int Flexbox::getAvailHeightOfChild(Widget *child, bool force)
{
   int i = index(child);
   if (i >= 0 && items[i].height >= 0) return items[i].height;
   return Textblock::getAvailHeightOfChild(child, force);
}

void Flexbox::correctRequisitionOfChild(Widget *child, Requisition *req,
                                       void (*split)(int,int*,int*), bool width, bool height)
{
   Widget::correctRequisitionOfChild(child, req, split, width, height);
   int i = index(child);
   if (i >= 0) {
      if (items[i].width >= 0) req->width = items[i].width;
      if (items[i].height >= 0) split(items[i].height, &req->ascent, &req->descent);
   }
}

void Flexbox::measure(Item &item, int width, int height)
{
   if (item.width != width || item.height != height) {
      item.width = width; item.height = height;
      item.widget->invalidateSizeRequest();
   }
   item.widget->sizeRequest(&item.request);
}

static int length(Length value, int reference, int fallback)
{
   if (isAbsLength(value)) return absLengthVal(value);
   if (isPerLength(value) && reference >= 0) return multiplyWithPerLength(reference, value);
   return fallback;
}

static void spacing(int alignment, int free, int count, double &start, double &between)
{
   start = between = 0;
   if (alignment == ALIGN_FLEX_END || alignment == ALIGN_END || alignment == ALIGN_SELF_END) start = max(0, free);
   else if (alignment == ALIGN_CENTER) start = max(0, free) / 2.0;
   else if (free > 0 && count > 0) {
      if (alignment == ALIGN_SPACE_BETWEEN && count > 1) between = free / double(count - 1);
      else if (alignment == ALIGN_SPACE_AROUND) { between = free / double(count); start = between / 2; }
      else if (alignment == ALIGN_SPACE_EVENLY) start = between = free / double(count + 1);
   }
}

void Flexbox::arrangeFlex()
{
   const ContainerStyle &style = getStyle()->container;
   bool column = style.direction >= FLEX_COLUMN, reverse = style.direction & 1;
   int mainLimit = column ? contentHeight : contentWidth;
   int mainGap = max(0, column ? style.rowGap : style.columnGap);
   int crossGap = max(0, column ? style.columnGap : style.rowGap);
   std::vector<std::vector<size_t> > lines(1);
   int used = 0;
   for (size_t i = 0; i < items.size(); ++i) {
      Item &item = items[i]; Style *s = item.widget->getStyle(); Extremes ext;
      item.widget->getExtremes(&ext);
      int box = column ? s->boxDiffHeight() : s->boxDiffWidth();
      int natural;
      if (column) { measure(item, max(s->boxDiffWidth(), min(contentWidth, ext.maxWidth))); natural = item.request.ascent + item.request.descent; }
      else natural = ext.maxWidth;
      Length basis = s->container.basis == LENGTH_AUTO ? (column ? s->height : s->width) : s->container.basis;
      int base = length(basis, mainLimit, -1);
      item.minimum = length(column ? s->minHeight : s->minWidth, mainLimit,
         column || s->overflow != OVERFLOW_VISIBLE ? box : ext.minWidthIntrinsic);
      if (column ? s->minHeight != LENGTH_AUTO : s->minWidth != LENGTH_AUTO) item.minimum = (column ? s->cssHeightToContent(item.minimum) : s->cssWidthToContent(item.minimum)) + box;
      item.maximum = length(column ? s->maxHeight : s->maxWidth, mainLimit, 10000000);
      if (item.maximum != 10000000) item.maximum = (column ? s->cssHeightToContent(item.maximum) : s->cssWidthToContent(item.maximum)) + box;
      if (column ? s->minHeight == LENGTH_AUTO : s->minWidth == LENGTH_AUTO) {
         // The automatic minimum cannot exceed a definite preferred/max size.
         int preferred = length(column ? s->height : s->width, mainLimit, -1);
         if (preferred >= 0) {
            preferred = (column ? s->cssHeightToContent(preferred) :
               s->cssWidthToContent(preferred)) + box;
            item.minimum = min(item.minimum, preferred);
         }
         item.minimum = min(item.minimum, item.maximum);
      }
      item.maximum = max(item.minimum, item.maximum);
      item.base = base < 0 ? natural : (column ? s->cssHeightToContent(base) : s->cssWidthToContent(base)) + box;
      item.base = max(item.minimum, min(item.maximum, item.base));
      if (style.wrap && mainLimit >= 0 && !lines.back().empty() && used + mainGap + item.base > mainLimit) { lines.push_back(std::vector<size_t>()); used = 0; }
      if (!lines.back().empty()) used += mainGap;
      used += item.base; lines.back().push_back(i);
   }
   std::vector<int> crossSizes(lines.size(), 0), baselines(lines.size(), 0);
   int totalCross = 0, longest = 0;
   for (size_t line = 0; line < lines.size(); ++line) {
      const std::vector<size_t> &members = lines[line];
      int occupied = members.empty() ? 0 : mainGap * (members.size() - 1);
      for (size_t i : members) occupied += items[i].base;
      int target = mainLimit < 0 ? occupied : mainLimit;
      std::vector<double> sizes(members.size()); std::vector<bool> frozen(members.size(), false);
      for (size_t j = 0; j < members.size(); ++j) sizes[j] = items[members[j]].base;
      bool grow = target >= occupied;
      for (size_t pass = 0; pass <= members.size(); ++pass) {
         double free = target - (members.empty() ? 0 : mainGap * (members.size() - 1)), factors = 0;
         for (size_t j = 0; j < members.size(); ++j) {
            Item &item = items[members[j]]; free -= sizes[j];
            if (!frozen[j]) factors += grow ? item.widget->getStyle()->container.grow : item.widget->getStyle()->container.shrink * item.base;
         }
         if (std::fabs(free) < .01 || factors <= 0) break;
         bool clamped = false;
         for (size_t j = 0; j < members.size(); ++j) if (!frozen[j]) {
            Item &item = items[members[j]];
            double factor = grow ? item.widget->getStyle()->container.grow : item.widget->getStyle()->container.shrink * item.base;
            double next = sizes[j] + free * factor / max(1.0, factors);
            if (next < item.minimum || next > item.maximum) { sizes[j] = max(double(item.minimum), min(double(item.maximum), next)); frozen[j] = true; clamped = true; }
         }
         if (!clamped) {
            for (size_t j = 0; j < members.size(); ++j) if (!frozen[j]) {
               Item &item = items[members[j]];
               double factor = grow ? item.widget->getStyle()->container.grow : item.widget->getStyle()->container.shrink * item.base;
               sizes[j] += free * factor / max(1.0, factors);
            }
            break;
         }
      }
      occupied = members.empty() ? 0 : mainGap * (members.size() - 1);
      double cumulative = 0; int previous = 0;
      for (size_t j = 0; j < members.size(); ++j) {
         Item &item = items[members[j]]; Style *s = item.widget->getStyle();
         cumulative += sizes[j]; int size = lout::misc::roundInt(cumulative) - previous; previous += size;
         int cross = length(column ? s->width : s->height, column ? contentWidth : contentHeight, -1);
         if (cross >= 0) cross = (column ? s->cssWidthToContent(cross) + s->boxDiffWidth() : s->cssHeightToContent(cross) + s->boxDiffHeight());
         bool crossAutoMargin = column ? s->marginLeftAuto || s->marginRightAuto :
            s->marginTopAuto || s->marginBottomAuto;
         if (column) {
            if (cross < 0 && crossAutoMargin) {
               Extremes ext; item.widget->getExtremes(&ext);
               cross = max(s->boxDiffWidth(), min(contentWidth, ext.maxWidth));
            }
            measure(item, cross >= 0 ? cross : contentWidth, size);
         }
         else measure(item, size, cross);
         if (!column) item.height = item.request.ascent + item.request.descent;
         if (column) item.width = item.request.width;
         crossSizes[line] = max(crossSizes[line], column ? item.width : item.height);
         baselines[line] = max(baselines[line], item.request.ascent);
         occupied += size;
      }
      unsigned autoMargins = 0;
      for (size_t i : members) {
         Style *s = items[i].widget->getStyle();
         autoMargins += column ? unsigned(s->marginTopAuto) + unsigned(s->marginBottomAuto) :
            unsigned(s->marginLeftAuto) + unsigned(s->marginRightAuto);
      }
      // Resolve automatic margins after flexing and before justification.
      double autoSpace = autoMargins ? max(0, target - occupied) / double(autoMargins) : 0;
      double cursor = 0, between = 0;
      if (autoSpace == 0)
         spacing(style.justify, target - occupied, members.size(), cursor, between);
      for (size_t i : members) {
         Item &item = items[i]; Style *s = item.widget->getStyle();
         bool beforeAuto = column ? s->marginTopAuto : s->marginLeftAuto;
         bool afterAuto = column ? s->marginBottomAuto : s->marginRightAuto;
         if (reverse) std::swap(beforeAuto, afterAuto);
         if (beforeAuto) cursor += autoSpace;
         int size = column ? item.height : item.width;
         int position = lout::misc::roundInt(reverse ? target - cursor - size : cursor);
         if (column) item.y = position; else item.x = position;
         cursor += size + mainGap + between;
         if (afterAuto) cursor += autoSpace;
      }
      longest = max(longest, target); totalCross += crossSizes[line];
   }
   int crossLimit = column ? contentWidth : contentHeight;
   if (lines.size() > 1) totalCross += crossGap * (lines.size() - 1);
   if (crossLimit < 0) crossLimit = totalCross;
   if (lines.size() == 1) crossSizes[0] = max(crossSizes[0], crossLimit);
   else if (style.alignContent == ALIGN_STRETCH && crossLimit > totalCross) {
      int remaining = crossLimit - totalCross;
      for (size_t i = 0; i < crossSizes.size(); ++i) crossSizes[i] += (remaining * (i + 1)) / crossSizes.size() - (remaining * i) / crossSizes.size();
      totalCross = crossLimit;
   }
   double cursor, between; spacing(style.alignContent, crossLimit - totalCross, lines.size(), cursor, between);
   for (size_t line = 0; line < lines.size(); ++line) {
      for (size_t i : lines[line]) {
         Item &item = items[i]; Style *s = item.widget->getStyle();
         int align = s->container.alignSelf == ALIGN_NORMAL ? style.alignItems : s->container.alignSelf;
         if (align == ALIGN_NORMAL) align = ALIGN_STRETCH;
         bool automatic = column ? s->width == LENGTH_AUTO : s->height == LENGTH_AUTO;
         bool beforeAuto = column ? s->marginLeftAuto : s->marginTopAuto;
         bool afterAuto = column ? s->marginRightAuto : s->marginBottomAuto;
         if (style.wrap == 2) std::swap(beforeAuto, afterAuto);
         int cross = column ? item.width : item.height;
         if (align == ALIGN_STRETCH && automatic && !beforeAuto && !afterAuto) {
            cross = crossSizes[line];
            if (column) measure(item, cross, item.height); else measure(item, item.width, cross);
         }
         double offset, unused; spacing(align, crossSizes[line] - cross, 1, offset, unused);
         if (beforeAuto || afterAuto) {
            int free = max(0, crossSizes[line] - cross);
            offset = beforeAuto ? free / (afterAuto ? 2.0 : 1.0) : 0;
         } else if (!column && align == ALIGN_BASELINE) offset = baselines[line] - item.request.ascent;
         int position = lout::misc::roundInt(cursor + offset);
         if (style.wrap == 2) position = crossLimit - position - cross;
         if (column) item.x = position; else item.y = position;
      }
      cursor += crossSizes[line] + crossGap + between;
   }
   if (column) contentHeight = longest; else contentHeight = crossLimit;
}

void Flexbox::arrangeGrid()
{
   const ContainerStyle &s = getStyle()->container;
   int columnGap = max(0, s.columnGap);
   std::vector<Length> columnTracks, columnMinimums;
   std::vector<bool> columnHasMinimum;
   unsigned repeats = 1;
   if (s.autoRepeatMode && s.autoRepeatCount &&
       s.autoRepeatStart + s.autoRepeatCount <= s.columns) {
      unsigned outside = s.columns - s.autoRepeatCount;
      int outsideWidth = 0, groupWidth = 0;
      for (unsigned c = 0; c < s.columns; ++c) {
         bool repeated = c >= s.autoRepeatStart && c < s.autoRepeatStart + s.autoRepeatCount;
         Length track = s.columnTracks[c];
         int minimum = s.columnHasMin[c] ? max(0, length(s.columnMinTracks[c], contentWidth, 0)) : 0;
         int base = minimum;
         if (!s.columnHasMin[c] && track == LENGTH_AUTO) {
            for (size_t i = c; i < items.size(); i += max(1u, s.columns)) {
               Extremes ext; items[i].widget->getExtremes(&ext); base = max(base, ext.maxWidth);
            }
         } else if (!isRelLength(track) && track != LENGTH_AUTO)
            base = max(base, max(0, length(track, contentWidth, 0)));
         if (repeated) groupWidth += base; else outsideWidth += base;
      }
      int available = contentWidth - outsideWidth - columnGap * (int(outside) - 1);
      int denominator = max(1, groupWidth + int(s.autoRepeatCount) * columnGap);
      repeats = (unsigned)max(1, (available + columnGap) / denominator);
      repeats = min(repeats, max(1u, (32u - outside) / s.autoRepeatCount));
      if (s.autoRepeatMode == 2 && s.autoRepeatStart == 0 && s.columns == s.autoRepeatCount) {
         unsigned occupied = (unsigned)((items.size() + s.autoRepeatCount - 1) / s.autoRepeatCount);
         repeats = min(repeats, max(1u, occupied));
      }
      for (unsigned c = 0; c < s.columns; ++c) {
         if (c == s.autoRepeatStart) {
            for (unsigned n = 0; n < repeats; ++n)
               for (unsigned j = 0; j < s.autoRepeatCount; ++j) {
                  unsigned source = s.autoRepeatStart + j;
                  columnTracks.push_back(s.columnTracks[source]);
                  columnMinimums.push_back(s.columnMinTracks[source]);
                  columnHasMinimum.push_back(s.columnHasMin[source]);
               }
            c += s.autoRepeatCount - 1;
         } else {
            columnTracks.push_back(s.columnTracks[c]);
            columnMinimums.push_back(s.columnMinTracks[c]);
            columnHasMinimum.push_back(s.columnHasMin[c]);
         }
      }
   } else {
      for (unsigned c = 0; c < s.columns; ++c) {
         columnTracks.push_back(s.columnTracks[c]);
         columnMinimums.push_back(s.columnMinTracks[c]);
         columnHasMinimum.push_back(s.columnHasMin[c]);
      }
   }
   unsigned columns = max(1u, (unsigned)columnTracks.size());
   if (columnTracks.empty()) {
      columnTracks.push_back(LENGTH_AUTO); columnMinimums.push_back(LENGTH_AUTO);
      columnHasMinimum.push_back(false);
   }
   unsigned rows = (items.size() + columns - 1) / columns;
   rows = max(rows, s.rows);
   std::vector<int> widths(columns, 0), heights(rows, 0);
   int used = (columns - 1) * columnGap;
   double fractions = 0;
   for (unsigned c = 0; c < columns; ++c) {
      Length track = columnTracks[c];
      int minimum = columnHasMinimum[c] ? max(0, length(columnMinimums[c], contentWidth, 0)) : 0;
      if (isRelLength(track)) { widths[c] = minimum; fractions += relLengthVal(track); }
      else if (track != LENGTH_AUTO) widths[c] = max(minimum, max(0, length(track, contentWidth, 0)));
      else {
         for (size_t i = c; i < items.size(); i += columns) {
            Extremes ext; items[i].widget->getExtremes(&ext);
            widths[c] = max(widths[c], ext.maxWidth);
         }
         widths[c] = max(widths[c], minimum);
      }
      used += widths[c];
   }
   if (fractions > 0) {
      double sum = 0; int previous = 0, remaining = max(0, contentWidth - used);
      for (unsigned c = 0; c < columns; ++c) if (isRelLength(columnTracks[c])) {
         sum += relLengthVal(columnTracks[c]); int boundary = lout::misc::roundInt(remaining * sum / max(1.0, fractions));
         widths[c] += boundary - previous; previous = boundary;
      }
   } else if (used < contentWidth) {
      unsigned automatic = 0;
      for (unsigned c = 0; c < columns; ++c) if (columnTracks[c] == LENGTH_AUTO) ++automatic;
      unsigned index = 0;
      for (unsigned c = 0; c < columns; ++c) if (columnTracks[c] == LENGTH_AUTO) {
         widths[c] += (contentWidth - used) * (index + 1) / automatic - (contentWidth - used) * index / automatic; ++index;
      }
   }
   used = columnGap * (columns - 1);
   for (unsigned c = 0; c < columns; ++c) used += widths[c];
   double columnStart, columnBetween;
   spacing(s.justify, contentWidth - used, columns, columnStart, columnBetween);

   for (size_t i = 0; i < items.size(); ++i) {
      Item &item = items[i]; Style *child = item.widget->getStyle(); unsigned row = i / columns;
      unsigned column = i % columns;
      int width = length(child->width, widths[column], -1);
      if (width >= 0) width = child->cssWidthToContent(width) + child->boxDiffWidth();
      else {
         int justify = child->container.justifySelf == ALIGN_NORMAL ? s.justifyItems : child->container.justifySelf;
         if (justify == ALIGN_NORMAL || justify == ALIGN_STRETCH) width = widths[column];
         else {
            Extremes ext; item.widget->getExtremes(&ext);
            width = min(widths[column], ext.maxWidth);
         }
      }
      measure(item, width); heights[row] = max(heights[row], item.request.ascent + item.request.descent);
   }
   int rowGap = max(0, s.rowGap), usedRows = rows > 0 ? (rows - 1) * rowGap : 0;
   double rowFractions = 0;
   for (unsigned r = 0; r < s.rows; ++r) {
      Length track = s.rowTracks[r];
      int minimum = s.rowHasMin[r] ? max(0, length(s.rowMinTracks[r], contentHeight, 0)) : 0;
      if (isRelLength(track) && contentHeight >= 0) {
         heights[r] = minimum; rowFractions += relLengthVal(track);
      } else if (track == LENGTH_AUTO || isRelLength(track)) heights[r] = max(heights[r], minimum);
      else heights[r] = max(minimum, max(0, length(track, contentHeight, heights[r])));
   }
   for (unsigned r = 0; r < rows; ++r) usedRows += heights[r];
   if (rowFractions > 0 && contentHeight >= 0) {
      int remaining = max(0, contentHeight - usedRows), previous = 0;
      double sum = 0;
      for (unsigned r = 0; r < s.rows; ++r) if (isRelLength(s.rowTracks[r])) {
         sum += relLengthVal(s.rowTracks[r]);
         int boundary = lout::misc::roundInt(remaining * sum / max(1.0, rowFractions));
         heights[r] += boundary - previous; previous = boundary;
      }
      usedRows = (rows > 0 ? (rows - 1) * rowGap : 0);
      for (unsigned r = 0; r < rows; ++r) usedRows += heights[r];
   }
   double rowStart, rowBetween;
   spacing(s.alignContent, contentHeight < 0 ? 0 : contentHeight - usedRows,
           rows, rowStart, rowBetween);
   if (s.alignContent == ALIGN_STRETCH && contentHeight > usedRows) {
      int extra = contentHeight - usedRows, automatic = 0;
      for (unsigned r = 0; r < rows; ++r)
         if (r >= s.rows || s.rowTracks[r] == LENGTH_AUTO) ++automatic;
      if (automatic) {
         int index = 0;
         for (unsigned r = 0; r < rows; ++r)
            if (r >= s.rows || s.rowTracks[r] == LENGTH_AUTO) {
               heights[r] += (extra * (index + 1)) / automatic - (extra * index) / automatic;
               ++index;
            }
         usedRows = contentHeight;
         rowBetween = 0;
      }
   }
   double y = rowStart;
   for (unsigned r = 0; r < rows; ++r) {
      double x = columnStart;
      for (unsigned c = 0; c < columns; ++c) {
         size_t i = r * columns + c;
         if (i < items.size()) {
            Item &item = items[i]; Style *child = item.widget->getStyle();
            int align = child->container.alignSelf == ALIGN_NORMAL ? s.alignItems : child->container.alignSelf;
            int justify = child->container.justifySelf == ALIGN_NORMAL ? s.justifyItems : child->container.justifySelf;
            int height = item.request.ascent + item.request.descent;
            if ((align == ALIGN_STRETCH || align == ALIGN_NORMAL) && child->height == LENGTH_AUTO) height = heights[r];
            double xOffset, unused; spacing(justify, widths[c] - item.width, 1, xOffset, unused);
            double yOffset; spacing(align, heights[r] - height, 1, yOffset, unused);
            measure(item, item.width, height);
            item.x = lout::misc::roundInt(x + xOffset);
            item.y = lout::misc::roundInt(y + yOffset);
         }
         x += widths[c] + columnGap + columnBetween;
      }
      y += heights[r] + (r + 1 < rows ? rowGap + rowBetween : 0);
   }
   contentHeight = max(contentHeight, lout::misc::roundInt(y));
}

void Flexbox::calculate()
{
   if (calculating) return;
   calculating = true;
   std::stable_sort(items.begin(), items.end(), [](const Item &a, const Item &b) {return a.widget->getStyle()->container.order < b.widget->getStyle()->container.order;});
   contentWidth = max(0, getAvailWidth(true) - boxDiffWidth());
   contentHeight = length(getStyle()->height, getAvailHeight(false), -1);
   if (contentHeight >= 0) contentHeight = getStyle()->cssHeightToContent(contentHeight);
   if (getStyle()->display == DISPLAY_GRID || getStyle()->display == DISPLAY_INLINE_GRID) arrangeGrid(); else arrangeFlex();
   calculating = false;
}

void Flexbox::sizeRequestImpl(Requisition *req, int count, Widget **refs, int *x, int *y)
{
   sizeRequestParams.fill(count, refs, x, y); calculate();
   req->width = contentWidth + boxDiffWidth(); req->ascent = boxOffsetY() + max(0, contentHeight); req->descent = boxRestHeight();
   correctRequisition(req, splitHeightPreserveAscent, true, true);
   correctRequisitionByOOF(req, splitHeightPreserveAscent);
}
void Flexbox::getExtremesSimpl(Extremes *ext)
{
   int minimum = 0, maximum = 0; bool row = getStyle()->container.direction < FLEX_COLUMN && getStyle()->display != DISPLAY_GRID && getStyle()->display != DISPLAY_INLINE_GRID;
   for (const Item &item : items) {
      Extremes child; item.widget->getExtremes(&child);
      maximum = row ? maximum + child.maxWidth : max(maximum, child.maxWidth);
      minimum = row && !getStyle()->container.wrap ? minimum + child.minWidth : max(minimum, child.minWidth);
   }
   if (row && items.size() > 1) maximum += (items.size() - 1) * max(0, getStyle()->container.columnGap);
   ext->minWidth = ext->minWidthIntrinsic = ext->adjustmentWidth = minimum + boxDiffWidth();
   ext->maxWidth = ext->maxWidthIntrinsic = max(minimum, maximum) + boxDiffWidth();
   correctExtremes(ext, true);
}
void Flexbox::sizeAllocateImpl(Allocation *a)
{
   sizeAllocateStart(a);
   for (Item &item : items) {
      Allocation child = {a->x + boxOffsetX() + item.x, a->y + boxOffsetY() + item.y,
         max(0, item.width), min(max(0, item.height), item.request.ascent), 0};
      child.descent = max(0, item.height - child.ascent); item.widget->sizeAllocate(&child);
   }
   sizeAllocateEnd();
}
void Flexbox::drawLevel(View *view, Rectangle *area, int level, DrawingContext *context)
{
   if (level == SL_IN_FLOW) {
      for (Item &item : items) { Rectangle child; if (!StackingContextMgr::handledByStackingContextMgr(item.widget) && item.widget->intersects(this, area, &child)) item.widget->draw(view, &child, context); }
   } else Textblock::drawLevel(view, area, level, context);
}
Widget *Flexbox::getWidgetAtPointLevel(int x, int y, int level, GettingWidgetAtPointContext *context)
{
   if (level != SL_IN_FLOW) return Textblock::getWidgetAtPointLevel(x, y, level, context);
   for (auto it = items.rbegin(); it != items.rend(); ++it) if (!StackingContextMgr::handledByStackingContextMgr(it->widget)) { Widget *hit = it->widget->getWidgetAtPoint(x, y, context); if (hit) return hit; }
   return NULL;
}
bool Flexbox::getDomAllocation(int node, Allocation *result)
{
   if (node == getDomNode()) { *result = *getAllocation(); return true; }
   for (Item &item : items) if (item.widget->getDomAllocation(node, result)) return true;
   return Widget::getDomAllocation(node, result);
}
}
