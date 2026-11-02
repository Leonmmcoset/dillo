/*
 * File: styleengine.cc
 *
 * Copyright 2008-2009 Johannes Hofmann <Johannes.Hofmann@gmx.de>
 * Copyright 2024 Rodrigo Arias Mallo <rodarima@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include "../dlib/dlib.h"
#include "msg.h"
#include "prefs.h"
#include "misc.h"
#include "html_common.hh"
#include "styleengine.hh"
#include "cssvariables.hh"
#include "web.hh"
#include "capi.h"
#include "uicmd.hh"
#include "timeout.hh"

using namespace lout::misc;
using namespace dw::core::style;

/**
 * Signal handler for "delete": This handles the case when an instance
 * of StyleImage is deleted, possibly when the cache client is still
 * active.
 *
 * \todo Not necessary for dw::Image? (dw::Image also implements
 * lout::signal::ObservedObject.)
 */
class StyleImageDeletionReceiver:
   public lout::signal::ObservedObject::DeletionReceiver
{
   int clientKey;
   BrowserWindow *bw;

public:
   StyleImageDeletionReceiver (int clientKey, BrowserWindow *bw);
   ~StyleImageDeletionReceiver ();

   void deleted (lout::signal::ObservedObject *object);
};

StyleImageDeletionReceiver::StyleImageDeletionReceiver (int clientKey, BrowserWindow *bw)
{
   this->clientKey = clientKey;
   this->bw = bw;
}

StyleImageDeletionReceiver::~StyleImageDeletionReceiver ()
{
}

void StyleImageDeletionReceiver::deleted (lout::signal::ObservedObject *object)
{
   // Restyling the root can discard an image before its request completes.
   // A stopped cache client never sends CA_Close, so retire its loading key.
   // Widget/layout styles are destroyed before their BrowserWindow is freed.
   a_Bw_remove_client(bw, clientKey);
   a_Capi_stop_client (clientKey, 0);
   delete this;
}

// ----------------------------------------------------------------------

StyleEngine::StyleEngine (dw::core::Layout *layout,
                          const DilloUrl *pageUrl, const DilloUrl *baseUrl,
                          float zoom) {
   StyleAttrs style_attrs;
   FontAttrs font_attrs;

   doctree = new Doctree ();
   stack = new lout::misc::SimpleVector <Node> (1);
   cssContext = new CssContext ();
   this->layout = layout;
   this->zoom = zoom;
   mediaBw = NULL;
   cssContext->setMediaEnvironment(mediaEnvironment());
   buildUserStyle ();
   layout->connect(this);
   this->pageUrl = pageUrl ? a_Url_dup(pageUrl) : NULL;
   this->baseUrl = baseUrl ? a_Url_dup(baseUrl) : NULL;
   importDepth = 0;
   dpmm = layout->dpiX () / 25.4; /* assume dpiX == dpiY */
   this->zoom = zoom;

   stackPush ();
   Node *n = stack->getLastRef ();

   /* Create a dummy font, attribute, and tag for the bottom of the stack. */
   font_attrs.name = prefs.font_sans_serif;
   font_attrs.size = roundInt(14 * prefs.font_factor * zoom);
   if (font_attrs.size < prefs.font_min_size)
      font_attrs.size = prefs.font_min_size;
   if (font_attrs.size > prefs.font_max_size)
      font_attrs.size = prefs.font_max_size;
   font_attrs.weight = 400;
   font_attrs.style = FONT_STYLE_NORMAL;
   font_attrs.letterSpacing = 0;
   font_attrs.fontVariant = FONT_VARIANT_NORMAL;

   style_attrs.initValues ();
   style_attrs.font = Font::create (layout, &font_attrs);
   style_attrs.color = Color::create (layout, 0);
   style_attrs.backgroundColor = Color::create (layout, prefs.bg_color);

   n->style = Style::create (&style_attrs);
}

StyleEngine::~StyleEngine () {
   a_Timeout_actually_remove(mediaResize, this);
   while (doctree->top ())
      endElement (doctree->top ()->element);

   stackPop (); // dummy node on the bottom of the stack
   assert (stack->size () == 0);

   a_Url_free(pageUrl);
   a_Url_free(baseUrl);

   delete stack;
   delete doctree;
   delete cssContext;
}

void StyleEngine::stackPush () {
   static const Node emptyNode = {
      NULL, NULL, NULL, NULL, NULL, NULL, false, false, NULL, NULL
   };

   stack->setSize (stack->size () + 1, emptyNode);
}

void StyleEngine::stackPop () {
   Node *n = stack->getRef (stack->size () - 1);

   delete n->styleAttrProperties;
   delete n->styleAttrPropertiesImportant;
   delete n->nonCssProperties;
   delete n->customProperties;
   if (n->style)
      n->style->unref ();
   if (n->wordStyle)
      n->wordStyle->unref ();
   if (n->backgroundStyle)
      n->backgroundStyle->unref ();
   stack->setSize (stack->size () - 1);
}

/**
 * \brief tell the styleEngine that a new html element has started.
 */
void StyleEngine::startElement (int element, BrowserWindow *bw) {
   mediaBw = bw;
   style (bw); // ensure that style of current node is computed

   stackPush ();
   Node *n = stack->getLastRef ();
   DoctreeNode *dn = doctree->push ();

   dn->element = element;
   n->doctreeNode = dn;
   if (stack->size () > 1)
      n->displayNone = stack->getRef (stack->size () - 2)->displayNone;
}

void StyleEngine::startElement (const char *tagname, BrowserWindow *bw) {
   startElement (a_Html_tag_index (tagname), bw);
}

void StyleEngine::setId (const char *id) {
   DoctreeNode *dn = doctree->top ();
   assert (dn->id == NULL);
   dn->id = dStrdup (id);
}

/**
 * \brief split a string at sep chars and return a SimpleVector of strings
 */
static lout::misc::SimpleVector<char *> *splitStr (const char *str, char sep) {
   const char *p1 = NULL;
   lout::misc::SimpleVector<char *> *list =
      new lout::misc::SimpleVector<char *> (1);

   for (;; str++) {
      if (*str != '\0' && *str != sep) {
         if (!p1)
            p1 = str;
      } else if (p1) {
         list->increase ();
         list->set (list->size () - 1, dStrndup (p1, str - p1));
         p1 = NULL;
      }

      if (*str == '\0')
         break;
   }

   return list;
}

void StyleEngine::setClass (const char *klass) {
   DoctreeNode *dn = doctree->top ();
   assert (dn->klass == NULL);
   dn->klass = splitStr (klass, ' ');
}

void StyleEngine::setStyle (const char *styleAttr) {
   Node *n = stack->getRef (stack->size () - 1);
   assert (n->styleAttrProperties == NULL);
   // parse style information from style="" attribute, if it exists
   if (styleAttr && prefs.parse_embedded_css) {
      n->styleAttrProperties = new CssPropertyList (true);
      n->styleAttrPropertiesImportant = new CssPropertyList (true);

      CssParser::parseDeclarationBlock (baseUrl, styleAttr, strlen (styleAttr),
                                        n->styleAttrProperties,
                                        n->styleAttrPropertiesImportant);
   }
}

/**
 * \brief Instruct StyleEngine to use the nonCssHints from parent element
 * This is only used for tables where nonCssHints on the TABLE-element
 * (e.g. border=1) also affect child elements like TD.
 */
void StyleEngine::inheritNonCssHints () {
   Node *pn = stack->getRef (stack->size () - 2);

   if (pn->nonCssProperties) {
      Node *n = stack->getRef (stack->size () - 1);
      CssPropertyList *origNonCssProperties = n->nonCssProperties;

      n->nonCssProperties = new CssPropertyList(*pn->nonCssProperties, true);

      if (origNonCssProperties) // original nonCssProperties have precedence
         origNonCssProperties->apply (n->nonCssProperties);

      delete origNonCssProperties;
   }
}

void StyleEngine::clearNonCssHints () {
   Node *n = stack->getRef (stack->size () - 1);

   delete n->nonCssProperties;
   n->nonCssProperties = NULL;
}

/**
 * \brief Use of the background color of the parent style as default.
 *   This is only used in table code to allow for colors specified for
 *   table rows as table rows are currently no widgets and therefore
 *   don't draw any background.
 */
void StyleEngine::inheritBackgroundColor () {
   stack->getRef (stack->size () - 1)->inheritBackgroundColor = true;
}

dw::core::style::Color *StyleEngine::backgroundColor () {
   for (int i = 1; i < stack->size (); i++) {
      Node *n = stack->getRef (i);

      if (n->style && n->style->backgroundColor) {
         if (n->style->backgroundAlpha == 255) return n->style->backgroundColor;
         unsigned rgb = n->style->backgroundColor->getColor(), alpha = n->style->backgroundAlpha;
         unsigned base = stack->getRef(0)->style->backgroundColor->getColor(), composed = 0;
         for (int shift = 0; shift < 24; shift += 8)
            composed |= ((((rgb >> shift) & 255) * alpha + ((base >> shift) & 255) * (255 - alpha) + 127) / 255) << shift;
         return Color::create(layout, composed);
      }
   }

   return NULL;
}

dw::core::style::StyleImage *StyleEngine::backgroundImage
   (dw::core::style::BackgroundRepeat *bgRepeat,
    dw::core::style::BackgroundAttachment *bgAttachment,
    dw::core::style::Length *bgPositionX,
    dw::core::style::Length *bgPositionY,
    dw::core::style::BackgroundSizes *bgSize) {
   for (int i = 1; i < stack->size (); i++) {
      Node *n = stack->getRef (i);

      if (n->style && n->style->backgroundImage) {
         *bgRepeat = n->style->backgroundRepeat;
         *bgAttachment = n->style->backgroundAttachment;
         *bgPositionX = n->style->backgroundPositionX;
         *bgPositionY = n->style->backgroundPositionY;
         *bgSize = n->style->backgroundSize;
         return n->style->backgroundImage;
      }
   }

   return NULL;
}

/**
 * \brief set the CSS pseudo class :link.
 */
void StyleEngine::setPseudoLink () {
   DoctreeNode *dn = doctree->top ();
   dn->pseudo = "link";
}

/**
 * \brief set the CSS pseudo class :visited.
 */
void StyleEngine::setPseudoVisited () {
   DoctreeNode *dn = doctree->top ();
   dn->pseudo = "visited";
}

/**
 * \brief tell the styleEngine that a html element has ended.
 */
void StyleEngine::endElement (int element) {
   assert (element == doctree->top ()->element);

   stackPop ();
   doctree->pop ();
}

void StyleEngine::preprocessAttrs (dw::core::style::StyleAttrs *attrs) {
   /* workaround for styling of inline elements */
   if (stack->getRef (stack->size () - 2)->inheritBackgroundColor) {
      attrs->backgroundColor =
         stack->getRef (stack->size () - 2)->style->backgroundColor;
      attrs->backgroundImage =
         stack->getRef (stack->size () - 2)->style->backgroundImage;
      attrs->backgroundRepeat =
         stack->getRef (stack->size () - 2)->style->backgroundRepeat;
      attrs->backgroundAttachment =
         stack->getRef (stack->size () - 2)->style->backgroundAttachment;
      attrs->backgroundPositionX =
         stack->getRef (stack->size () - 2)->style->backgroundPositionX;
      attrs->backgroundPositionY =
         stack->getRef (stack->size () - 2)->style->backgroundPositionY;

      attrs->valign = stack->getRef (stack->size () - 2)->style->valign;
   }
   attrs->borderColor.top = (Color *) -1;
   attrs->borderColor.bottom = (Color *) -1;
   attrs->borderColor.left = (Color *) -1;
   attrs->borderColor.right = (Color *) -1;
   /* initial value of border-width is 'medium' */
   attrs->borderWidth.top = 2;
   attrs->borderWidth.bottom = 2;
   attrs->borderWidth.left = 2;
   attrs->borderWidth.right = 2;
}

void StyleEngine::postprocessAttrs (dw::core::style::StyleAttrs *attrs) {
   /* if border-color is not specified, use color as computed value */
   if (attrs->borderColor.top == (Color *) -1)
      attrs->borderColor.top = attrs->color;
   if (attrs->borderColor.bottom == (Color *) -1)
      attrs->borderColor.bottom = attrs->color;
   if (attrs->borderColor.left == (Color *) -1)
      attrs->borderColor.left = attrs->color;
   if (attrs->borderColor.right == (Color *) -1)
      attrs->borderColor.right = attrs->color;
   /* computed value of border-width is 0 if border-style
      is 'none' or 'hidden' */
   if (attrs->borderStyle.top == BORDER_NONE ||
       attrs->borderStyle.top == BORDER_HIDDEN)
      attrs->borderWidth.top = 0;
   if (attrs->borderStyle.bottom == BORDER_NONE ||
       attrs->borderStyle.bottom == BORDER_HIDDEN)
      attrs->borderWidth.bottom = 0;
   if (attrs->borderStyle.left == BORDER_NONE ||
       attrs->borderStyle.left == BORDER_HIDDEN)
      attrs->borderWidth.left = 0;
   if (attrs->borderStyle.right == BORDER_NONE ||
       attrs->borderStyle.right == BORDER_HIDDEN)
      attrs->borderWidth.right = 0;
}

/**
 * \brief Make changes to StyleAttrs attrs according to CssPropertyList props.
 */
void StyleEngine::apply (int i, StyleAttrs *attrs, CssPropertyList *props,
                         BrowserWindow *bw) {
   FontAttrs fontAttrs = *attrs->font;
   Font *parentFont = stack->get (i - 1).style->font;
   char *c, *fontName;
   int lineHeight;
   DilloUrl *imgUrl = NULL;

   /* Determine font first so it can be used to resolve relative lengths. */
   for (int j = 0; j < props->size (); j++) {
      CssProperty *p = props->getRef (j);

      switch (p->name) {
         case CSS_PROPERTY_FONT_FAMILY:
            // Check font names in comma separated list.
            // Note, that p->value.strVal is modified, so that in future calls
            // the matching font name can be used directly.
            fontName = NULL;
            while (p->value.strVal) {
               if ((c = strchr(p->value.strVal, ',')))
                  *c = '\0';
               dStrstrip(p->value.strVal);

               if (dStrAsciiCasecmp (p->value.strVal, "serif") == 0)
                  fontName = prefs.font_serif;
               else if (dStrAsciiCasecmp (p->value.strVal, "sans-serif") == 0)
                  fontName = prefs.font_sans_serif;
               else if (dStrAsciiCasecmp (p->value.strVal, "cursive") == 0)
                  fontName = prefs.font_cursive;
               else if (dStrAsciiCasecmp (p->value.strVal, "fantasy") == 0)
                  fontName = prefs.font_fantasy;
               else if (dStrAsciiCasecmp (p->value.strVal, "monospace") == 0)
                  fontName = prefs.font_monospace;
               else if (Font::exists(layout, p->value.strVal))
                  fontName = p->value.strVal;

               if (fontName) {   // font found
                  fontAttrs.name = fontName;
                  break;
               } else if (c) {   // try next from list
                  memmove(p->value.strVal, c + 1, strlen(c + 1) + 1);
               } else {          // no font found
                  break;
               }
            }

            break;
         case CSS_PROPERTY_FONT_SIZE:
            if (p->type == CSS_TYPE_ENUM) {
               switch (p->value.intVal) {
                  case CSS_FONT_SIZE_XX_SMALL:
                     fontAttrs.size = roundInt(8.1 * prefs.font_factor * zoom);
                     break;
                  case CSS_FONT_SIZE_X_SMALL:
                     fontAttrs.size = roundInt(9.7 * prefs.font_factor * zoom);
                     break;
                  case CSS_FONT_SIZE_SMALL:
                     fontAttrs.size = roundInt(11.7 * prefs.font_factor * zoom);
                     break;
                  case CSS_FONT_SIZE_MEDIUM:
                     fontAttrs.size = roundInt(14.0 * prefs.font_factor * zoom);
                     break;
                  case CSS_FONT_SIZE_LARGE:
                     fontAttrs.size = roundInt(16.8 * prefs.font_factor * zoom);
                     break;
                  case CSS_FONT_SIZE_X_LARGE:
                     fontAttrs.size = roundInt(20.2 * prefs.font_factor * zoom);
                     break;
                  case CSS_FONT_SIZE_XX_LARGE:
                     fontAttrs.size = roundInt(24.2 * prefs.font_factor * zoom);
                     break;
                  case CSS_FONT_SIZE_SMALLER:
                     fontAttrs.size = roundInt(fontAttrs.size * 0.83 * zoom);
                     break;
                  case CSS_FONT_SIZE_LARGER:
                     fontAttrs.size = roundInt(fontAttrs.size * 1.2 * zoom);
                     break;
                  default:
                     assert(false); // invalid font-size enum
               }
            } else {
               computeValue (&fontAttrs.size, p->value.lenVal, parentFont,
                  parentFont->size);
            }

            if (fontAttrs.size < prefs.font_min_size)
               fontAttrs.size = prefs.font_min_size;
            if (fontAttrs.size > prefs.font_max_size)
               fontAttrs.size = prefs.font_max_size;

            break;
         case CSS_PROPERTY_FONT_STYLE:
            fontAttrs.style = (FontStyle) p->value.intVal;
            break;
         case CSS_PROPERTY_FONT_WEIGHT:

            if (p->type == CSS_TYPE_ENUM) {
               switch (p->value.intVal) {
                  case CSS_FONT_WEIGHT_BOLD:
                     fontAttrs.weight = 700;
                     break;
                  case CSS_FONT_WEIGHT_BOLDER:
                     fontAttrs.weight += 300;
                     break;
                  case CSS_FONT_WEIGHT_LIGHT:
                     fontAttrs.weight = 100;
                     break;
                  case CSS_FONT_WEIGHT_LIGHTER:
                     fontAttrs.weight -= 300;
                     break;
                  case CSS_FONT_WEIGHT_NORMAL:
                     fontAttrs.weight = 400;
                     break;
                  default:
                     assert(false); // invalid font weight value
                     break;
               }
            } else {
               fontAttrs.weight = p->value.intVal;
            }

            if (fontAttrs.weight < 100)
               fontAttrs.weight = 100;
            if (fontAttrs.weight > 900)
               fontAttrs.weight = 900;

            break;
         case CSS_PROPERTY_LETTER_SPACING:
            if (p->type == CSS_TYPE_ENUM) {
               if (p->value.intVal == CSS_LETTER_SPACING_NORMAL) {
                  fontAttrs.letterSpacing = 0;
               }
            } else {
               computeValue (&fontAttrs.letterSpacing, p->value.lenVal,
                  parentFont, parentFont->size);
            }

            /* Limit letterSpacing to reasonable values to avoid overflows e.g,
             * when measuring word width.
             */
            if (fontAttrs.letterSpacing > 1000)
               fontAttrs.letterSpacing = 1000;
            else if (fontAttrs.letterSpacing < -1000)
               fontAttrs.letterSpacing = -1000;
            break;
         case CSS_PROPERTY_FONT_VARIANT:
            fontAttrs.fontVariant = (FontVariant) p->value.intVal;
            break;
         default:
            break;
      }
   }

   attrs->font = Font::create (layout, &fontAttrs);

   // currentColor inside a gradient uses the cascaded foreground, even if
   // the color declaration follows background-image in the stylesheet.
   for (int j = 0; j < props->size(); ++j)
      if (props->getRef(j)->name == CSS_PROPERTY_COLOR)
         attrs->color = Color::create(layout, props->getRef(j)->value.intVal);
   attrs->outline.rgba = ((unsigned)attrs->color->getColor() << 8) | 255;

   for (int j = 0; j < props->size (); j++) {
      CssProperty *p = props->getRef (j);

      if (p->type == CSS_TYPE_WIDE_KEYWORD) {
         StyleAttrs initial;
         initial.initValues();
         const StyleAttrs &value = p->value.intVal == 1 ? *stack->getRef(i - 1)->style : initial;
         switch (p->name) {
         case CSS_PROPERTY_POINTER_EVENTS:
            attrs->pointerEvents = p->value.intVal == 2 ?
               stack->getRef(i - 1)->style->pointerEvents : value.pointerEvents; break;
         case CSS_PROPERTY_BACKGROUND_COLOR:
            attrs->backgroundColor = value.backgroundColor; attrs->backgroundAlpha = value.backgroundAlpha; break;
         case CSS_PROPERTY_BACKGROUND_IMAGE:
            attrs->backgroundImage = value.backgroundImage; attrs->backgroundLayers = value.backgroundLayers; break;
         case CSS_PROPERTY_BACKGROUND_CLIP: attrs->backgroundClip = value.backgroundClip; break;
         case CSS_PROPERTY_BACKGROUND_ORIGIN: attrs->backgroundOrigin = value.backgroundOrigin; break;
         case CSS_PROPERTY_BACKGROUND_SIZE: attrs->backgroundSize = value.backgroundSize; break;
         case CSS_PROPERTY_BACKGROUND_REPEAT: attrs->backgroundRepeat = value.backgroundRepeat; break;
         case CSS_PROPERTY_BACKGROUND_ATTACHMENT: attrs->backgroundAttachment = value.backgroundAttachment; break;
         case CSS_PROPERTY_BACKGROUND_POSITION:
            attrs->backgroundPositionX = value.backgroundPositionX;
            attrs->backgroundPositionY = value.backgroundPositionY; break;
         case CSS_PROPERTY_BOX_SHADOW: attrs->boxShadows = value.boxShadows; break;
         case CSS_PROPERTY_OUTLINE_COLOR:
            attrs->outline.rgba = p->value.intVal == 1 ? value.outline.rgba :
               ((unsigned)attrs->color->getColor() << 8) | 255; break;
         case CSS_PROPERTY_OUTLINE_STYLE: attrs->outline.style = value.outline.style; break;
         case CSS_PROPERTY_OUTLINE_WIDTH: attrs->outline.width = value.outline.width; break;
         case CSS_PROPERTY_OUTLINE_OFFSET: attrs->outline.offset = value.outline.offset; break;
         case CSS_PROPERTY_TRANSFORM:
            attrs->hasTransform = value.hasTransform;
            attrs->translateX = value.translateX; attrs->translateY = value.translateY; break;
         default:
            unsigned corner = p->name == CSS_PROPERTY_BORDER_TOP_LEFT_RADIUS ? 0 :
               p->name == CSS_PROPERTY_BORDER_TOP_RIGHT_RADIUS ? 1 :
               p->name == CSS_PROPERTY_BORDER_BOTTOM_RIGHT_RADIUS ? 2 : 3;
            attrs->borderRadius.x[corner] = value.borderRadius.x[corner];
            attrs->borderRadius.y[corner] = value.borderRadius.y[corner]; break;
         }
         continue;
      }

      switch (p->name) {
         case CSS_PROPERTY_POINTER_EVENTS:
            attrs->pointerEvents = (StyleAttrs::PointerEvents)p->value.intVal; break;
         case CSS_PROPERTY_ALIGN_CONTENT: attrs->container.alignContent = p->value.intVal; break;
         case CSS_PROPERTY_ALIGN_ITEMS: attrs->container.alignItems = p->value.intVal; break;
         case CSS_PROPERTY_ALIGN_SELF: attrs->container.alignSelf = p->value.intVal; break;
         case CSS_PROPERTY_JUSTIFY_CONTENT: attrs->container.justify = p->value.intVal; break;
         case CSS_PROPERTY_FLEX_DIRECTION: attrs->container.direction = p->value.intVal; break;
         case CSS_PROPERTY_FLEX_WRAP: attrs->container.wrap = p->value.intVal; break;
         case CSS_PROPERTY_FLEX_GROW:
            if (p->value.lenVal.type == CSS_LENGTH_TYPE_NONE) attrs->container.grow = MAX(0, MIN(10000, p->value.lenVal.f));
            break;
         case CSS_PROPERTY_FLEX_SHRINK:
            if (p->value.lenVal.type == CSS_LENGTH_TYPE_NONE) attrs->container.shrink = MAX(0, MIN(10000, p->value.lenVal.f));
            break;
         case CSS_PROPERTY_FLEX_BASIS: computeLength(&attrs->container.basis, p->value.lenVal, attrs->font); break;
         case CSS_PROPERTY_COLUMN_GAP: computeValue(&attrs->container.columnGap, p->value.lenVal, attrs->font); break;
         case CSS_PROPERTY_ROW_GAP: computeValue(&attrs->container.rowGap, p->value.lenVal, attrs->font); break;
         case CSS_PROPERTY_ORDER: attrs->container.order = p->value.intVal; break;
         case CSS_PROPERTY_GRID_TEMPLATE_COLUMNS:
            attrs->container.columns = 0;
            if (!computeTracks(p->value.strVal, attrs->container.columnTracks, &attrs->container.columns, attrs->font)) attrs->container.columns = 0;
            break;
         case CSS_PROPERTY_GRID_TEMPLATE_ROWS:
            attrs->container.rows = 0;
            if (!computeTracks(p->value.strVal, attrs->container.rowTracks, &attrs->container.rows, attrs->font)) attrs->container.rows = 0;
            break;
         /* \todo missing cases */
         case CSS_PROPERTY_BACKGROUND_ATTACHMENT:
            attrs->backgroundAttachment =
               (BackgroundAttachment) p->value.intVal;
            break;
         case CSS_PROPERTY_BACKGROUND_CLIP:
         case CSS_PROPERTY_BACKGROUND_ORIGIN: {
            BackgroundBoxes &boxes = p->name == CSS_PROPERTY_BACKGROUND_CLIP ? attrs->backgroundClip : attrs->backgroundOrigin;
            memset(&boxes, 0, sizeof(boxes));
            boxes.count = p->value.boxesVal->count;
            for (unsigned n = 0; n < boxes.count; ++n) boxes.values[n] = (BackgroundBox)p->value.boxesVal->values[n];
            break;
         }
         case CSS_PROPERTY_BACKGROUND_SIZE: {
            BackgroundSizes &sizes = attrs->backgroundSize;
            memset(&sizes, 0, sizeof(sizes)); sizes.count = p->value.sizesVal->count;
            for (unsigned n = 0; n < sizes.count; ++n) {
               sizes.values[n].mode = (BackgroundSizes::Size::Mode)p->value.sizesVal->values[n].mode;
               computeLength(&sizes.values[n].x, p->value.sizesVal->values[n].x, attrs->font);
               computeLength(&sizes.values[n].y, p->value.sizesVal->values[n].y, attrs->font);
            }
            break;
         }
         case CSS_PROPERTY_BACKGROUND_COLOR:
            {
               unsigned color = p->type == CSS_TYPE_RGBA ? p->value.colorVal.rgba :
                  p->value.intVal == -1 ? 0 : ((unsigned)p->value.intVal << 8) | 255;
               if (p->type == CSS_TYPE_RGBA && p->value.colorVal.currentColor)
                  color = ((unsigned)attrs->color->getColor() << 8) | 255;
               attrs->backgroundAlpha = color & 255;
               attrs->backgroundColor = attrs->backgroundAlpha ? Color::create(layout,
                  !prefs.allow_white_bg && (color >> 8) == 0xffffff ? prefs.white_bg_replacement : color >> 8) : NULL;
            }
            break;
         case CSS_PROPERTY_BACKGROUND_IMAGE:
            attrs->backgroundImage = NULL;
            attrs->backgroundLayers.clear();
            if (p->type == CSS_TYPE_BACKGROUND_IMAGES) {
               for (const CssGradient &g : p->value.imagesVal->layers) {
                  Gradient gradient = {};
                  gradient.count = g.count;
                  gradient.angle = g.angle;
                  gradient.cornerX = g.cornerX;
                  gradient.cornerY = g.cornerY;
                  gradient.repeating = g.repeating;
                  for (unsigned n = 0; n < g.count; ++n) {
                     gradient.stops[n].rgba = g.stops[n].currentColor ?
                        ((unsigned)attrs->color->getColor() << 8) | 255 : g.stops[n].rgba;
                     gradient.stops[n].positioned = g.stops[n].positioned;
                     if (g.stops[n].positioned)
                        computeLength(&gradient.stops[n].position, g.stops[n].position, attrs->font);
                  }
                  attrs->backgroundLayers.push_back(gradient);
               }
            } else if (p->value.strVal) {
               // URI properties are already absolute.
               imgUrl = a_Url_new(p->value.strVal, NULL);
            }
            break;
         case CSS_PROPERTY_BORDER_TOP_LEFT_RADIUS:
         case CSS_PROPERTY_BORDER_TOP_RIGHT_RADIUS:
         case CSS_PROPERTY_BORDER_BOTTOM_RIGHT_RADIUS:
         case CSS_PROPERTY_BORDER_BOTTOM_LEFT_RADIUS: {
            unsigned corner = p->name == CSS_PROPERTY_BORDER_TOP_LEFT_RADIUS ? 0 :
               p->name == CSS_PROPERTY_BORDER_TOP_RIGHT_RADIUS ? 1 :
               p->name == CSS_PROPERTY_BORDER_BOTTOM_RIGHT_RADIUS ? 2 : 3;
            computeLength(&attrs->borderRadius.x[corner], p->value.posVal->posX, attrs->font);
            computeLength(&attrs->borderRadius.y[corner], p->value.posVal->posY, attrs->font);
            break;
         }
         case CSS_PROPERTY_TRANSFORM:
            attrs->hasTransform = p->value.transformVal->active;
            computeLength(&attrs->translateX, p->value.transformVal->x, attrs->font);
            computeLength(&attrs->translateY, p->value.transformVal->y, attrs->font);
            break;
         case CSS_PROPERTY_BACKGROUND_POSITION:
            computeLength (&attrs->backgroundPositionX, p->value.posVal->posX,
                           attrs->font);
            computeLength (&attrs->backgroundPositionY, p->value.posVal->posY,
                           attrs->font);
            break;
         case CSS_PROPERTY_BACKGROUND_REPEAT:
            attrs->backgroundRepeat = (BackgroundRepeat) p->value.intVal;
            break;
         case CSS_PROPERTY_BORDER_COLLAPSE:
            attrs->borderCollapse = (BorderCollapse) p->value.intVal;
            break;
         case CSS_PROPERTY_BORDER_TOP_COLOR:
            attrs->borderColor.top = (p->type == CSS_TYPE_ENUM) ? NULL :
                                     Color::create (layout, p->value.intVal);
            break;
         case CSS_PROPERTY_BORDER_BOTTOM_COLOR:
            attrs->borderColor.bottom = (p->type == CSS_TYPE_ENUM) ? NULL :
                                       Color::create (layout, p->value.intVal);
            break;
         case CSS_PROPERTY_BORDER_LEFT_COLOR:
            attrs->borderColor.left = (p->type == CSS_TYPE_ENUM) ? NULL :
                                      Color::create (layout, p->value.intVal);
            break;
         case CSS_PROPERTY_BORDER_RIGHT_COLOR:
            attrs->borderColor.right = (p->type == CSS_TYPE_ENUM) ? NULL :
                                       Color::create (layout, p->value.intVal);
            break;
         case CSS_PROPERTY_BORDER_BOTTOM_STYLE:
            attrs->borderStyle.bottom = (BorderStyle) p->value.intVal;
            break;
         case CSS_PROPERTY_BORDER_LEFT_STYLE:
            attrs->borderStyle.left = (BorderStyle) p->value.intVal;
            break;
         case CSS_PROPERTY_BORDER_RIGHT_STYLE:
            attrs->borderStyle.right = (BorderStyle) p->value.intVal;
            break;
         case CSS_PROPERTY_BORDER_TOP_STYLE:
            attrs->borderStyle.top = (BorderStyle) p->value.intVal;
            break;
         case CSS_PROPERTY_BORDER_BOTTOM_WIDTH:
            computeBorderWidth (&attrs->borderWidth.bottom, p, attrs->font);
            break;
         case CSS_PROPERTY_BORDER_LEFT_WIDTH:
            computeBorderWidth (&attrs->borderWidth.left, p, attrs->font);
            break;
         case CSS_PROPERTY_BORDER_RIGHT_WIDTH:
            computeBorderWidth (&attrs->borderWidth.right, p, attrs->font);
            break;
         case CSS_PROPERTY_BORDER_TOP_WIDTH:
            computeBorderWidth (&attrs->borderWidth.top, p, attrs->font);
            break;
         case CSS_PROPERTY_BORDER_SPACING:
            computeValue (&attrs->hBorderSpacing, p->value.lenVal,attrs->font);
            computeValue (&attrs->vBorderSpacing, p->value.lenVal,attrs->font);
            break;
         case CSS_PROPERTY_BOTTOM:
            computeLength (&attrs->bottom, p->value.lenVal, attrs->font);
            break;
         case CSS_PROPERTY_BOX_SIZING:
            attrs->boxSizing = p->value.intVal == 2 ?
               stack->getRef(i - 1)->style->boxSizing :
               p->value.intVal == 1 ? StyleAttrs::BORDER_BOX : StyleAttrs::CONTENT_BOX;
            break;
         case CSS_PROPERTY_BOX_SHADOW: {
            const CssShadows &shadows = *p->value.shadowsVal;
            attrs->boxShadows.count = shadows.count;
            for (unsigned n = 0; n < shadows.count; ++n) {
               const CssShadows::Shadow &source = shadows.shadows[n];
               BoxShadows::Shadow &shadow = attrs->boxShadows.shadows[n];
               computeValue(&shadow.x, source.lengths[0], attrs->font);
               computeValue(&shadow.y, source.lengths[1], attrs->font);
               computeValue(&shadow.blur, source.lengths[2], attrs->font);
               computeValue(&shadow.spread, source.lengths[3], attrs->font);
               // Keep blur masks and coordinate arithmetic bounded even for
               // unreasonably large CSS values.
               shadow.x = lout::misc::max(-1000000, lout::misc::min(1000000, shadow.x));
               shadow.y = lout::misc::max(-1000000, lout::misc::min(1000000, shadow.y));
               shadow.spread = lout::misc::max(-1000000, lout::misc::min(1000000, shadow.spread));
               shadow.blur = lout::misc::max(0, lout::misc::min(128, shadow.blur));
               shadow.inset = source.inset;
               shadow.rgba = source.color.currentColor ?
                  ((unsigned)attrs->color->getColor() << 8) | 255 : source.color.rgba;
            }
            break;
         }
         case CSS_PROPERTY_OUTLINE_COLOR:
            attrs->outline.rgba = p->value.colorVal.currentColor ?
               ((unsigned)attrs->color->getColor() << 8) | 255 : p->value.colorVal.rgba;
            break;
         case CSS_PROPERTY_OUTLINE_STYLE:
            attrs->outline.style = (BorderStyle)p->value.intVal;
            break;
         case CSS_PROPERTY_OUTLINE_WIDTH:
            computeBorderWidth(&attrs->outline.width, p, attrs->font);
            attrs->outline.width = lout::misc::max(0, lout::misc::min(1000000, attrs->outline.width));
            break;
         case CSS_PROPERTY_OUTLINE_OFFSET:
            computeValue(&attrs->outline.offset, p->value.lenVal, attrs->font);
            attrs->outline.offset = lout::misc::max(-1000000, lout::misc::min(1000000, attrs->outline.offset));
            break;
         case CSS_PROPERTY_CLEAR:
            attrs->clear = (ClearType) p->value.intVal;
            break;
         case CSS_PROPERTY_COLOR:
            attrs->color = Color::create (layout, p->value.intVal);
            break;
         case CSS_PROPERTY_CURSOR:
            attrs->cursor = (Cursor) p->value.intVal;
            break;
         case CSS_PROPERTY_DISPLAY:
            attrs->display = (DisplayType) p->value.intVal;
            if (attrs->display == DISPLAY_NONE)
               stack->getRef (i)->displayNone = true;
            break;
         case CSS_PROPERTY_FLOAT:
            attrs->vloat = (FloatType) p->value.intVal;
            break;
         case CSS_PROPERTY_LEFT:
            computeLength (&attrs->left, p->value.lenVal, attrs->font);
            break;
         case CSS_PROPERTY_LINE_HEIGHT:
            if (p->type == CSS_TYPE_ENUM) { //only valid enum value is "normal"
               attrs->lineHeight = dw::core::style::LENGTH_AUTO;
            } else if (p->type == CSS_TYPE_LENGTH_PERCENTAGE_NUMBER) {
               if (CSS_LENGTH_TYPE (p->value.lenVal) == CSS_LENGTH_TYPE_NONE) {
                  attrs->lineHeight =
                     createPerLength(CSS_LENGTH_VALUE(p->value.lenVal));
               } else if (computeValue (&lineHeight, p->value.lenVal,
                                        attrs->font, attrs->font->size)) {
                  attrs->lineHeight = createAbsLength(lineHeight);
               }
            }
            break;
         case CSS_PROPERTY_LIST_STYLE_POSITION:
            attrs->listStylePosition = (ListStylePosition) p->value.intVal;
            break;
         case CSS_PROPERTY_LIST_STYLE_TYPE:
            attrs->listStyleType = (ListStyleType) p->value.intVal;
            break;
         case CSS_PROPERTY_MARGIN_BOTTOM:
            computeValue (&attrs->margin.bottom, p->value.lenVal, attrs->font);
            if (attrs->margin.bottom < 0) // \todo fix negative margins in dw/*
               attrs->margin.bottom = 0;
            break;
         case CSS_PROPERTY_MARGIN_LEFT:
            attrs->marginLeftAuto = p->value.lenVal.type == CSS_LENGTH_TYPE_AUTO;
            computeValue (&attrs->margin.left, p->value.lenVal, attrs->font);
            if (attrs->margin.left < 0) // \todo fix negative margins in dw/*
               attrs->margin.left = 0;
            break;
         case CSS_PROPERTY_MARGIN_RIGHT:
            attrs->marginRightAuto = p->value.lenVal.type == CSS_LENGTH_TYPE_AUTO;
            computeValue (&attrs->margin.right, p->value.lenVal, attrs->font);
            if (attrs->margin.right < 0) // \todo fix negative margins in dw/*
               attrs->margin.right = 0;
            break;
         case CSS_PROPERTY_MARGIN_TOP:
            computeValue (&attrs->margin.top, p->value.lenVal, attrs->font);
            if (attrs->margin.top < 0) // \todo fix negative margins in dw/*
               attrs->margin.top = 0;
            break;
         case CSS_PROPERTY_OVERFLOW:
            attrs->overflow = (Overflow) p->value.intVal;
            break;
         case CSS_PROPERTY_PADDING_TOP:
            computeValue (&attrs->padding.top, p->value.lenVal, attrs->font);
            break;
         case CSS_PROPERTY_PADDING_BOTTOM:
            computeValue (&attrs->padding.bottom, p->value.lenVal,attrs->font);
            break;
         case CSS_PROPERTY_PADDING_LEFT:
            computeValue (&attrs->padding.left, p->value.lenVal, attrs->font);
            break;
         case CSS_PROPERTY_PADDING_RIGHT:
            computeValue (&attrs->padding.right, p->value.lenVal, attrs->font);
            break;
         case CSS_PROPERTY_POSITION:
            attrs->position = (Position) p->value.intVal;
            break;
         case CSS_PROPERTY_RIGHT:
            computeLength (&attrs->right, p->value.lenVal, attrs->font);
            break;
         case CSS_PROPERTY_TEXT_ALIGN:
            attrs->textAlign = (TextAlignType) p->value.intVal;
            break;
         case CSS_PROPERTY_TEXT_DECORATION:
            attrs->textDecoration |= p->value.intVal;
            break;
         case CSS_PROPERTY_TEXT_INDENT:
            computeLength (&attrs->textIndent, p->value.lenVal, attrs->font);
            break;
         case CSS_PROPERTY_TEXT_TRANSFORM:
            attrs->textTransform = (TextTransform) p->value.intVal;
            break;
         case CSS_PROPERTY_TOP:
            computeLength (&attrs->top, p->value.lenVal, attrs->font);
            break;
         case CSS_PROPERTY_VERTICAL_ALIGN:
            attrs->valign = (VAlignType) p->value.intVal;
            break;
         case CSS_PROPERTY_WHITE_SPACE:
            attrs->whiteSpace = (WhiteSpace) p->value.intVal;
            break;
         case CSS_PROPERTY_WIDTH:
            computeLength (&attrs->width, p->value.lenVal, attrs->font);
            break;
         case CSS_PROPERTY_HEIGHT:
            computeLength (&attrs->height, p->value.lenVal, attrs->font);
            break;
         case CSS_PROPERTY_WORD_SPACING:
            if (p->type == CSS_TYPE_ENUM) {
               if (p->value.intVal == CSS_WORD_SPACING_NORMAL) {
                  attrs->wordSpacing = 0;
               }
            } else {
               computeValue(&attrs->wordSpacing, p->value.lenVal, attrs->font);
            }

            /* Limit to reasonable values to avoid overflows */
            if (attrs->wordSpacing > 1000)
               attrs->wordSpacing = 1000;
            else if (attrs->wordSpacing < -1000)
               attrs->wordSpacing = -1000;
            break;
         case CSS_PROPERTY_MIN_WIDTH:
            computeLength (&attrs->minWidth, p->value.lenVal, attrs->font);
            break;
         case CSS_PROPERTY_MAX_WIDTH:
            computeLength (&attrs->maxWidth, p->value.lenVal, attrs->font);
            break;
         case CSS_PROPERTY_MIN_HEIGHT:
            computeLength (&attrs->minHeight, p->value.lenVal, attrs->font);
            break;
         case CSS_PROPERTY_MAX_HEIGHT:
            computeLength (&attrs->maxHeight, p->value.lenVal, attrs->font);
            break;
         case CSS_PROPERTY_Z_INDEX:
            if (p->type == CSS_TYPE_AUTO)
               attrs->zIndex = dw::core::style::Z_INDEX_AUTO;
            else
               attrs->zIndex = p->value.intVal;
            break;
         case PROPERTY_X_LINK:
            attrs->x_link = p->value.intVal;
            break;
         case PROPERTY_X_LANG:
            attrs->x_lang[0] = D_ASCII_TOLOWER(p->value.strVal[0]);
            if (attrs->x_lang[0])
               attrs->x_lang[1] = D_ASCII_TOLOWER(p->value.strVal[1]);
            else
               attrs->x_lang[1] = 0;
            break;
         case PROPERTY_X_IMG:
            attrs->x_img = p->value.intVal;
            break;
         case PROPERTY_X_TOOLTIP:
            attrs->x_tooltip = Tooltip::create(layout, p->value.strVal);
            break;
         default:
            break;
      }
   }

   if (imgUrl && prefs.load_background_images &&
       !stack->getRef (i)->displayNone &&
       !(URL_FLAGS(pageUrl) & URL_SpamSafe))
   {
      attrs->backgroundImage = StyleImage::create();
      DilloImage *image =
         a_Image_new(layout,
            (void*)attrs->backgroundImage
            ->getMainImgRenderer(),
            0xffffff, 0x000000);

      // we use the pageUrl as requester to prevent cross
      // domain requests as specified in domainrc
      DilloWeb *web = a_Web_new(bw, imgUrl, pageUrl);
      web->Image = image;
      a_Image_ref(image);
      web->flags |= WEB_Image;

      int clientKey;
      if ((clientKey = a_Capi_open_url(web, NULL, NULL)) != 0) {
                  a_Bw_add_client(bw, clientKey, 0);
                  a_Bw_add_url(bw, imgUrl);
                  attrs->backgroundImage->connectDeletion
                     (new StyleImageDeletionReceiver (clientKey, bw));
      }
   }
   a_Url_free (imgUrl);
}

/**
 * \brief Resolve relative lengths to absolute values.
 */
bool StyleEngine::computeTracks(const std::string &text, Length *tracks, unsigned *count,
                                Font *font, unsigned depth)
{
   if (depth > 8 || text.size() > 65536) return false;
   size_t pos = 0;
   while (pos < text.size()) {
      while (pos < text.size() && dIsspace(text[pos])) ++pos;
      if (pos == text.size()) break;
      size_t start = pos; unsigned nesting = 0;
      do {
         if (text[pos] == '(') ++nesting;
         if (text[pos] == ')' && nesting) --nesting;
         ++pos;
      } while (pos < text.size() && (nesting || !dIsspace(text[pos])));
      std::string token = text.substr(start, pos - start);
      if (token == "none") return *count == 0;
      if (token.compare(0, 7, "repeat(") == 0 && token.back() == ')') {
         size_t comma = token.find(',');
         if (comma == std::string::npos) return false;
         char *end; long repeat = strtol(token.c_str() + 7, &end, 10);
         while (dIsspace(*end)) ++end;
         if (end != token.c_str() + comma || repeat < 1 || repeat > 32) return false;
         std::string inner = token.substr(comma + 1, token.size() - comma - 2);
         for (long i = 0; i < repeat; ++i) if (!computeTracks(inner, tracks, count, font, depth + 1)) return false;
      } else {
         if (*count >= 32) return false;
         Length track = LENGTH_AUTO;
         if (token.size() > 2 && token.compare(token.size() - 2, 2, "fr") == 0) {
            char *end; double fraction = strtod(token.c_str(), &end);
            if (end != token.c_str() + token.size() - 2 || fraction < 0 || fraction > 1000) return false;
            track = createRelLength(fraction);
         } else if (token != "auto" && token != "min-content" && token != "max-content") {
            CssPropertyList value(true);
            std::string declaration = "width:" + token;
            CssParser::parseDeclarationBlock(baseUrl, declaration.c_str(), declaration.size(), &value, NULL);
            if (value.size() != 1 || value.getRef(0)->type == CSS_TYPE_DEFERRED ||
                !computeLength(&track, value.getRef(0)->value.lenVal, font)) return false;
         }
         tracks[(*count)++] = track;
      }
   }
   return true;
}

bool StyleEngine::computeValue (int *dest, CssLength value, Font *font) {
   switch (CSS_LENGTH_TYPE (value)) {
      case CSS_LENGTH_TYPE_CALC: {
         if (value.terms[CSS_LENGTH_TYPE_PERCENTAGE] != 0) return false;
         double sum = 0;
         for (int i = 1; i < CSS_LENGTH_TYPE_PERCENTAGE; ++i) if (value.terms[i]) {
            int term = 0;
            if (!computeValue(&term, CSS_CREATE_LENGTH(value.terms[i], (CssLengthType)i), font)) return false;
            sum += term;
         }
         *dest = roundInt(MAX(-10000000.0, MIN(10000000.0, sum))); return true;
      }
      case CSS_LENGTH_TYPE_PX:
         *dest = roundInt (CSS_LENGTH_VALUE (value) * zoom);
         return true;
      case CSS_LENGTH_TYPE_MM:
         *dest = roundInt (CSS_LENGTH_VALUE (value) * dpmm * zoom);
         return true;
      case CSS_LENGTH_TYPE_EM:
         /* Doesn't need zoom as it is already applied to font->size */
         *dest = roundInt (CSS_LENGTH_VALUE (value) * font->size);
         return true;
      case CSS_LENGTH_TYPE_EX:
         /* Doesn't need zoom as it is already applied to font->xHeight */
         *dest = roundInt (CSS_LENGTH_VALUE(value) * font->xHeight);
         return true;
      case CSS_LENGTH_TYPE_CH:
         *dest = roundInt (CSS_LENGTH_VALUE(value) * font->zeroWidth);
         return true;
      case CSS_LENGTH_TYPE_REM:
         if (stack->size() < 2) {
            *dest = 0;
         } else {
            dw::core::style::Style *root_style = stack->getRef (1)->style;
            if (root_style)
               *dest = roundInt (CSS_LENGTH_VALUE(value) * root_style->font->size);
            else
               *dest = 0;
         }
         return true;
      case CSS_LENGTH_TYPE_VW:
         *dest = roundInt (CSS_LENGTH_VALUE(value) * layout->getWidthViewport() / 100);
         return true;
      case CSS_LENGTH_TYPE_VH:
         *dest = roundInt (CSS_LENGTH_VALUE(value) * layout->getHeightViewport() / 100);
         return true;
      case CSS_LENGTH_TYPE_VMIN:
         *dest = roundInt (CSS_LENGTH_VALUE(value) * MIN(layout->getWidthViewport(), layout->getHeightViewport()) / 100);
         return true;
      case CSS_LENGTH_TYPE_VMAX:
         *dest = roundInt (CSS_LENGTH_VALUE(value) * MAX(layout->getWidthViewport(), layout->getHeightViewport()) / 100);
         return true;
      case CSS_LENGTH_TYPE_NONE:
         // length values other than 0 without unit are only allowed
         // in special cases (line-height) and have to be handled
         // separately.
         assert ((int) CSS_LENGTH_VALUE (value) == 0);
         *dest = 0;
         return true;
      default:
         break;
   }

   return false;
}

bool StyleEngine::computeValue (int *dest, CssLength value, Font *font,
                                int percentageBase) {
   if (CSS_LENGTH_TYPE(value) == CSS_LENGTH_TYPE_CALC) {
      float fraction = value.terms[CSS_LENGTH_TYPE_PERCENTAGE];
      value.terms[CSS_LENGTH_TYPE_PERCENTAGE] = 0;
      if (!computeValue(dest, value, font)) return false;
      *dest += roundInt(fraction * percentageBase); return true;
   }
   if (CSS_LENGTH_TYPE (value) == CSS_LENGTH_TYPE_PERCENTAGE) {
      *dest = roundInt (CSS_LENGTH_VALUE (value) * percentageBase);
      return true;
   } else
      return computeValue (dest, value, font);
}

bool StyleEngine::computeLength (dw::core::style::Length *dest,
                                 CssLength value, Font *font) {
   int v;

   if (CSS_LENGTH_TYPE(value) == CSS_LENGTH_TYPE_CALC) {
      float fraction = value.terms[CSS_LENGTH_TYPE_PERCENTAGE];
      value.terms[CSS_LENGTH_TYPE_PERCENTAGE] = 0;
      if (!computeValue(&v, value, font)) return false;
      *dest = createCalcLength(v, fraction); return true;
   } else if (CSS_LENGTH_TYPE (value) == CSS_LENGTH_TYPE_PERCENTAGE) {
      *dest = createPerLength (CSS_LENGTH_VALUE (value));
      return true;
   } else if (CSS_LENGTH_TYPE (value) == CSS_LENGTH_TYPE_AUTO) {
      *dest = dw::core::style::LENGTH_AUTO;
      return true;
   } else if (computeValue (&v, value, font)) {
      *dest = createAbsLength (v);
      return true;
   }

   return false;
}

void StyleEngine::computeBorderWidth (int *dest, CssProperty *p,
                                      dw::core::style::Font *font) {
   if (p->type == CSS_TYPE_ENUM) {
      switch (p->value.intVal) {
         case CSS_BORDER_WIDTH_THIN:
            *dest = 1;
            break;
         case CSS_BORDER_WIDTH_MEDIUM:
            *dest = 2;
            break;
         case CSS_BORDER_WIDTH_THICK:
            *dest = 3;
            break;
         default:
            assert(false);
      }
   } else {
      computeValue (dest, p->value.lenVal, font);
   }
}

/**
 * \brief Similar to StyleEngine::style(), but with backgroundColor set.
 * A normal style might have backgroundColor == NULL to indicate a transparent
 * background. This method ensures that backgroundColor is set.
 */
Style * StyleEngine::backgroundStyle (BrowserWindow *bw) {
   if (!stack->getRef (stack->size () - 1)->backgroundStyle) {
      StyleAttrs attrs = *style (bw);

      for (int i = stack->size () - 1; i >= 0 && ! attrs.backgroundColor; i--)
         attrs.backgroundColor = stack->getRef (i)->style->backgroundColor;

      assert (attrs.backgroundColor);
      stack->getRef (stack->size () - 1)->backgroundStyle =
         Style::create (&attrs);
   }
   return stack->getRef (stack->size () - 1)->backgroundStyle;
}

/**
 * \brief Create a new style object based on the previously opened / closed
 * HTML elements and the nonCssProperties that have been set.
 * This method is private. Call style() to get a current style object.
 */
Style * StyleEngine::style0 (int i, BrowserWindow *bw) {
   CssPropertyList props, *styleAttrProperties, *styleAttrPropertiesImportant;
   CssPropertyList *nonCssProperties;
   // get previous style from the stack
   StyleAttrs attrs = *stack->getRef (i - 1)->style;

   // Ensure that StyleEngine::style0() has not been called before for
   // this element.
   // Style computation is expensive so limit it as much as possible.
   // If this assertion is hit, you need to rearrange the code that is
   // doing styleEngine calls to call setNonCssHint() before calling
   // style() or wordStyle() for each new element.
   assert (stack->getRef (i)->style == NULL);

   // reset values that are not inherited according to CSS
   attrs.resetValues ();
   preprocessAttrs (&attrs);
#ifdef ENABLE_JAVASCRIPT
   attrs.modernLayout = prefs.javascript_enabled;
#endif

   styleAttrProperties = stack->getRef (i)->styleAttrProperties;
   styleAttrPropertiesImportant = stack->getRef(i)->styleAttrPropertiesImportant;
   nonCssProperties = stack->getRef (i)->nonCssProperties;

   // merge style information
   cssContext->apply (&props, doctree, stack->getRef(i)->doctreeNode,
                      styleAttrProperties, styleAttrPropertiesImportant,
                      nonCssProperties);

   std::map<std::string, std::string> variables;
   if (stack->getRef(i - 1)->customProperties) variables = *stack->getRef(i - 1)->customProperties;
   for (std::map<std::string, std::string>::const_iterator it = props.customProperties.begin(); it != props.customProperties.end(); ++it) {
      std::string value = it->second;
      size_t first = value.find_first_not_of(" \t\r\n\f"), last = value.find_last_not_of(" \t\r\n\f");
      std::string keyword = first == std::string::npos ? "" : value.substr(first, last - first + 1);
      if (!dStrAsciiCasecmp(keyword.c_str(), "initial")) variables.erase(it->first);
      else if (dStrAsciiCasecmp(keyword.c_str(), "inherit") && dStrAsciiCasecmp(keyword.c_str(), "unset")) variables[it->first] = value;
   }
   CssVariables resolver(variables);
   delete stack->getRef(i)->customProperties;
   stack->getRef(i)->customProperties = new std::map<std::string, std::string>(resolver.computed());
   CssPropertyList computed(true);
   for (int j = 0; j < props.size(); ++j) {
      CssProperty *p = props.getRef(j);
      if (p->type == CSS_TYPE_DEFERRED) {
         std::string declaration;
         if (resolver.substitute(p->value.strVal, declaration)) {
            CssPropertyList expanded(true);
            CssParser::parseDeclarationBlock(baseUrl, declaration.c_str(), declaration.size(), &expanded, NULL);
            for (int k = 0; k < expanded.size(); ++k) if (expanded.getRef(k)->name == p->name && expanded.getRef(k)->type != CSS_TYPE_DEFERRED) {
               CssPropertyList selected;
               selected.increase(); *selected.getRef(0) = *expanded.getRef(k); selected.apply(&computed); break;
            }
         }
      } else {
         CssPropertyList selected;
         selected.increase(); *selected.getRef(0) = *p; selected.apply(&computed);
      }
   }
   apply (i, &attrs, &computed, bw);

   postprocessAttrs (&attrs);

   stack->getRef (i)->style = Style::create (&attrs);

   return stack->getRef (i)->style;
}

Style * StyleEngine::wordStyle0 (BrowserWindow *bw) {
   StyleAttrs attrs = *style (bw);
   attrs.resetValues ();

   if (stack->getRef (stack->size() - 1)->inheritBackgroundColor) {
      attrs.backgroundColor = style (bw)->backgroundColor;
      attrs.backgroundAlpha = style(bw)->backgroundAlpha;
      attrs.backgroundImage = style (bw)->backgroundImage;
      attrs.backgroundRepeat = style (bw)->backgroundRepeat;
      attrs.backgroundAttachment = style (bw)->backgroundAttachment;
      attrs.backgroundPositionX = style (bw)->backgroundPositionX;
      attrs.backgroundPositionY = style (bw)->backgroundPositionY;
   }

   attrs.valign = style (bw)->valign;

   stack->getRef(stack->size() - 1)->wordStyle = Style::create(&attrs);
   return stack->getRef (stack->size () - 1)->wordStyle;
}

/**
 * \brief Recompute all style information from scratch
 * This is used to take into account CSS styles for the HTML-element.
 * The CSS data is only completely available after parsing the HEAD-section
 * and thereby after the HTML-element has been opened.
 * Note that restyle() does not change any styles in the widget tree.
 */
void StyleEngine::restyle (BrowserWindow *bw) {
   for (int i = 1; i < stack->size (); i++) {
      Node *n = stack->getRef (i);
      if (n->style) {
         n->style->unref ();
         n->style = NULL;
      }
      if (n->wordStyle) {
         n->wordStyle->unref ();
         n->wordStyle = NULL;
      }
      if (n->backgroundStyle) {
         n->backgroundStyle->unref ();
         n->backgroundStyle = NULL;
      }

      style0 (i, bw);
   }
}

CssMediaEnvironment StyleEngine::mediaEnvironment() const
{
   CssMediaEnvironment env = {
      layout->getWidthViewport() / zoom,
      layout->getHeightViewport() / zoom,
      layout->dpiX() * zoom
   };
   return env;
}

void StyleEngine::viewportSizeChanged(int width, int height)
{
   a_Timeout_actually_remove(mediaResize, this);
   if (mediaBw && width > 0 && height > 0 &&
       cssContext->mediaChanged(mediaEnvironment()))
      a_Timeout_add(0.15, mediaResize, this);
}

void StyleEngine::mediaResize(void *data)
{
   StyleEngine *engine = (StyleEngine *)data;
   /* Rebuild from cached HTML only when a query changes truth value. The
    * deferred callback never destroys widgets from inside a layout signal. */
   void *document = a_Bw_get_current_doc(engine->mediaBw);
   /* Dillo rebuilds widgets when styles change. Preserve form edits and file
    * selections: pages with controls apply changed queries on manual reload
    * until the renderer can restyle an existing widget tree in place. */
   if (document && !a_Html_has_form_controls(document) &&
       engine->cssContext->mediaChanged(engine->mediaEnvironment()))
      a_UIcmd_repush(engine->mediaBw);
   a_Timeout_remove();
}

void StyleEngine::parse (DilloHtml *html, DilloUrl *url, const char *buf,
                         int buflen, CssOrigin origin) {
   if (importDepth > 10) { // avoid looping with recursive @import directives
      MSG_WARN("Maximum depth of CSS @import reached--ignoring stylesheet.\n");
      return;
   }

   importDepth++;
   CssParser::parse (html, url, cssContext, buf, buflen, origin);
   importDepth--;
}

/**
 * \brief Create the user agent style.
 *
 * The user agent style defines how dillo renders HTML in the absence of
 * author or user styles.
 */
void StyleEngine::init () {
   const char *cssBuf =
      "body  {margin: 5px}"
      "big {font-size: 1.17em}"
      "blockquote, dd {margin-left: 40px; margin-right: 40px}"
      "center {text-align: center}"
      "button {text-align: center}"
      "dt {font-weight: bolder}"
      ":link {color: blue; text-decoration: underline; cursor: pointer}"
      ":visited {color: #800080; text-decoration: underline; cursor: pointer}"
      "h1, h2, h3, h4, h5, h6, b, strong {font-weight: bolder}"
      "address, article, aside, center, div, figure, figcaption, footer,"
      " h1, h2, h3, h4, h5, h6, header, main, nav, ol, p, pre, section, ul"
      " {display: block}"
      "i, em, cite, address, var {font-style: italic}"
      "frameset, ul, ol, dir {margin-left: 40px}"
      /* WORKAROUND: It should be margin: 1em 0
       * but as we don't collapse these margins yet, it
       * look better like this.
       */
      "p {margin: 0.5em 0}"
      "figure {margin: 1em 40px}"
      "h1 {font-size: 2em; margin-top: .67em; margin-bottom: 0}"
      "h2 {font-size: 1.5em; margin-top: .75em; margin-bottom: 0}"
      "h3 {font-size: 1.17em; margin-top: .83em; margin-bottom: 0}"
      "h4 {margin-top: 1.12em; margin-bottom: 0}"
      "h5 {font-size: 0.83em; margin-top: 1.5em; margin-bottom: 0}"
      "h6 {font-size: 0.75em; margin-top: 1.67em; margin-bottom: 0}"
      "hr {width: 100%; border: 1px inset}"
      "li {margin-top: 0.1em; display: list-item}"
      "pre {white-space: pre}"
      "ol {list-style-type: decimal}"
      "ul {list-style-type: disc}"
      "ul ul {list-style-type: circle}"
      "ul ul ul {list-style-type: square}"
      "ul ul ul ul {list-style-type: disc}"
      "ins, u {text-decoration: underline}"
      "small, sub, sup {font-size: 0.83em}"
      "sub {vertical-align: sub}"
      "sup {vertical-align: super}"
      "s, strike, del {text-decoration: line-through}"
      /* HTML5 spec notes that mark styling "is just a suggestion and can be
       * changed based on implementation feedback"
       */
      "mark {background: yellow; color: black;}"
      "table {border-spacing: 2px}"
      "td, th {padding: 2px}"
      "thead, tbody, tfoot {vertical-align: middle}"
      "th {font-weight: bolder; text-align: center}"
      "code, tt, pre, samp, kbd {font-family: monospace}"
      /* WORKAROUND: Reset font properties in tables as some
       * pages rely on it (e.g. gmail).
       * http://developer.mozilla.org/en-US/Fixing_Table_Inheritance_in_Quirks_Mode
       * has a detailed description of the issue.
       */
      "table, caption {font-size: medium; font-weight: normal}";

   CssContext context;
   CssParser::parse (NULL, NULL, &context, cssBuf, strlen (cssBuf),
                     CSS_ORIGIN_USER_AGENT);
}

void StyleEngine::buildUserStyle () {
   Dstr *style;
   char *filename = dStrconcat(dGethomedir(), "/.dillo/style.css", NULL);

   if ((style = a_Misc_file2dstr(filename))) {
      CssParser::parse (NULL,NULL,cssContext,style->str, style->len,CSS_ORIGIN_USER);
      dStr_free (style, 1);
   }
   dFree (filename);
}
