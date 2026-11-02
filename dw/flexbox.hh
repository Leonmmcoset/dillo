#ifndef DW_FLEXBOX_HH
#define DW_FLEXBOX_HH
#include "textblock.hh"
#include <vector>

namespace dw {
/* Flex/grid children remain ordinary Dw widgets, so forms, images, selection
 * and event hit testing use the same resources as block layout. */
class Flexbox: public Textblock {
   struct Item {
      core::Widget *widget;
      int width, height, x, y, minimum, maximum, base;
      core::Requisition request;
   };
   std::vector<Item> items;
   Textblock *anonymous;
   int contentWidth, contentHeight;
   bool calculating;
   int index(core::Widget *widget);
   void measure(Item &item, int width, int height = -1);
   void arrangeFlex();
   void arrangeGrid();
   void calculate();
protected:
   bool childOutOfFlow(core::Widget *child) { return testWidgetAbsolutelyPositioned(child) || testWidgetFixedlyPositioned(child); }
   void sizeRequestImpl(core::Requisition *, int, core::Widget **, int *, int *);
   void sizeAllocateImpl(core::Allocation *);
   void getExtremesSimpl(core::Extremes *);
   int getAvailWidthOfChild(core::Widget *, bool);
   int getAvailHeightOfChild(core::Widget *, bool);
   void correctRequisitionOfChild(core::Widget *, core::Requisition *, void (*)(int,int*,int*), bool, bool);
   void drawLevel(core::View *, core::Rectangle *, int, core::DrawingContext *);
   core::Widget *getWidgetAtPointLevel(int, int, int, core::GettingWidgetAtPointContext *);
public:
   explicit Flexbox(bool limit): Textblock(limit), anonymous(NULL), contentWidth(0), contentHeight(0), calculating(false) {}
   bool getDomAllocation(int, core::Allocation *);
   void addWidget(core::Widget *, core::style::Style *);
   void addText(const char *, size_t, core::style::Style *);
   void addSpace(core::style::Style *);
   void addBreakOption(core::style::Style *, bool) {}
   void addParbreak(int, core::style::Style *) {}
   void addLinebreak(core::style::Style *) {}
};
}
#endif
