#ifndef __DW_OOFPOSFIXEDMGR_HH__
#define __DW_OOFPOSFIXEDMGR_HH__

#include "oofposabslikemgr.hh"

namespace dw {

namespace oof {

class OOFPosFixedMgr: public OOFPosAbsLikeMgr
{
protected:
   bool viewportRelative();
   int referenceWidth(bool forceValue);
   int referenceHeight(bool forceValue);
   void sizeAllocateChildren();
   int containerBoxOffsetX ();
   int containerBoxOffsetY ();
   int containerBoxRestWidth ();
   int containerBoxRestHeight ();

public:
   OOFPosFixedMgr (OOFAwareWidget *container);
   ~OOFPosFixedMgr ();
   void viewportPositionChanged();
};

} // namespace oof

} // namespace dw

#endif // __DW_OOFPOSFIXEDMGR_HH__
