/*
 * Dillo Widget
 *
 * Copyright 2014 Sebastian Geerken <sgeerken@dillo.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "oofposfixedmgr.hh"

namespace dw {

namespace oof {

OOFPosFixedMgr::OOFPosFixedMgr (OOFAwareWidget *container) :
   OOFPosAbsLikeMgr (container)
{
   DBG_OBJ_CREATE ("dw::oof::OOFPosFixedMgr");
}

OOFPosFixedMgr::~OOFPosFixedMgr ()
{
   DBG_OBJ_DELETE ();
}

int OOFPosFixedMgr::containerBoxOffsetX ()
{
   return container->getParent() ? container->boxOffsetX() - container->getStyle()->padding.left : 0;
}

int OOFPosFixedMgr::containerBoxOffsetY ()
{
   return container->getParent() ? container->boxOffsetY() - container->getStyle()->padding.top : 0;
}

int OOFPosFixedMgr::containerBoxRestWidth ()
{
   return container->getParent() ? container->boxRestWidth() - container->getStyle()->padding.right : 0;
}

int OOFPosFixedMgr::containerBoxRestHeight ()
{
   return container->getParent() ? container->boxRestHeight() - container->getStyle()->padding.bottom : 0;
}

bool OOFPosFixedMgr::viewportRelative()
{
   return !container->getParent() && !container->getStyle()->hasTransform;
}

int OOFPosFixedMgr::referenceWidth(bool forceValue)
{
   return viewportRelative() ? container->getLayout()->getContentWidthViewport() :
      OOFPosAbsLikeMgr::referenceWidth(forceValue);
}

int OOFPosFixedMgr::referenceHeight(bool forceValue)
{
   return viewportRelative() ? container->getLayout()->getContentHeightViewport() :
      OOFPosAbsLikeMgr::referenceHeight(forceValue);
}

void OOFPosFixedMgr::sizeAllocateChildren()
{
   if (!viewportRelative()) {
      OOFPosAbsLikeMgr::sizeAllocateChildren();
      return;
   }
   dw::core::Layout *layout = container->getLayout();
   for (int i = 0; i < children->size(); ++i) {
      Child *child = children->get(i);
      dw::core::Allocation allocation;
      int x, y;
      calcPosAndSizeChildOfChild(child, referenceWidth(true), referenceHeight(true),
         &x, &y, &allocation.width, &allocation.ascent, &allocation.descent);
      allocation.x = layout->getScrollPosX() + x;
      allocation.y = layout->getScrollPosY() + y;
      child->widget->sizeAllocate(&allocation);
   }
}

void OOFPosFixedMgr::viewportPositionChanged()
{
   if (viewportRelative() && containerAllocationState == WAS_ALLOCATED)
      sizeAllocateChildren();
}

} // namespace oof

} // namespace dw
