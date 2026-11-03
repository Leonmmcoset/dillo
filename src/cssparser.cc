/*
 * File: cssparser.cc
 *
 * Copyright 2004 Sebastian Geerken <sgeerken@dillo.org>
 * Copyright 2008-2009 Johannes Hofmann <Johannes.Hofmann@gmx.de>
 * Copyright 2024 Rodrigo Arias Mallo <rodarima@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

/*
 * This file is heavily based on the CSS parser of dillo-0.8.0-css-3 -
 * a dillo1 based CSS prototype written by Sebastian Geerken.
 */

#include <stdlib.h>
#include <stdio.h>
#include <cmath>

#include "msg.h"
#include "colors.h"
#include "html_common.hh"
#include "css.hh"
#include "cssparser.hh"
#include "dlib/dlib.h" /* dIsspace */

using namespace dw::core::style;

//#define MSG_CSS(A, ...) _MSG(A, __VA_ARGS__)
#define MSG_CSS(A, ...) do {} while(0)
#define DEBUG_TOKEN_LEVEL   0
#define DEBUG_PARSE_LEVEL   0
#define DEBUG_CREATE_LEVEL  0

#define DEBUG_LEVEL 10

/* Only the alphabetically sorted CSS properties participate in bsearch.
 * Internal properties and their aliases below this boundary are not CSS. */
#define CSS_NUM_PARSED_PROPERTIES CSS_PROPERTY_X_LINK


typedef struct {
   const char *symbol;
   const CssValueType type[3];
   const char *const *enum_symbols;
} CssPropertyInfo;

static const char *const Css_background_attachment_enum_vals[] = {
   "scroll", "fixed", NULL
};

static const char *const Css_background_repeat_enum_vals[] = {
   "repeat", "repeat-x", "repeat-y", "no-repeat", NULL
};

static const char *const Css_border_collapse_enum_vals[] = {
   "separate", "collapse", NULL
};

static const char *const Css_border_color_enum_vals[] = {
   "transparent", NULL
};

static const char *const Css_border_style_enum_vals[] = {
   "none", "hidden", "dotted", "dashed", "solid", "double", "groove",
   "ridge", "inset", "outset", NULL
};

static const char *const Css_border_width_enum_vals[] = {
   "thin", "medium", "thick", NULL
};

static const char *const Css_clear_enum_vals[] = {
   "left", "right", "both", "none", NULL
};

static const char *const Css_box_sizing_enum_vals[] = {
   "content-box", "border-box", "inherit", "initial", "unset", NULL
};

static const char *const Css_cursor_enum_vals[] = {
   "crosshair", "default", "pointer", "move", "e-resize", "ne-resize",
   "nw-resize", "n-resize", "se-resize", "sw-resize", "s-resize",
   "w-resize", "text", "wait", "help", NULL
};

static const char *const Css_display_enum_vals[] = {
   "block", "inline", "inline-block", "list-item", "none", "table",
   "table-row-group", "table-header-group", "table-footer-group", "table-row",
   "table-cell", "flex", "inline-flex", "grid", "inline-grid",
   "-webkit-box", "-webkit-inline-box", NULL
};

static const char *const Css_webkit_box_orient_vals[] = {
   "horizontal", "vertical", NULL
};

static const char *const Css_webkit_line_clamp_vals[] = {"none", NULL};

static const char *const Css_float_enum_vals[] = {
   "none", "left", "right", NULL
};

static const char *const Css_font_size_enum_vals[] = {
   "large", "larger", "medium", "small", "smaller", "xx-large", "xx-small",
   "x-large", "x-small", NULL
};

static const char *const Css_font_style_enum_vals[] = {
   "normal", "italic", "oblique", NULL
};

static const char *const Css_font_variant_enum_vals[] = {
   "normal", "small-caps", NULL
};

static const char *const Css_font_weight_enum_vals[] = {
   "bold", "bolder", "light", "lighter", "normal", NULL
};

static const char *const Css_letter_spacing_enum_vals[] = {
   "normal", NULL
};

static const char *const Css_list_style_position_enum_vals[] = {
   "inside", "outside", NULL
};

static const char *const Css_line_height_enum_vals[] = {
   "normal", NULL
};

static const char *const Css_list_style_type_enum_vals[] = {
   "disc", "circle", "square", "decimal", "decimal-leading-zero",
   "lower-roman", "upper-roman", "lower-greek", "lower-alpha",
   "lower-latin", "upper-alpha", "upper-latin", "hebrew", "armenian",
   "georgian", "cjk-ideographic", "hiragana", "katakana", "hiragana-iroha",
   "katakana-iroha", "none", NULL
};

static const char *const Css_overflow_enum_vals[] = {
   "visible", "hidden", "scroll", "auto", NULL
};

static const char *const Css_position_enum_vals[] = {
   "static", "relative", "absolute", "fixed", NULL
};

static const char *const Css_pointer_events_vals[] = {"auto", "none", NULL};

static const char *const Css_text_align_enum_vals[] = {
   "left", "right", "center", "justify", "string", NULL
};

static const char *const Css_text_decoration_enum_vals[] = {
   "underline", "overline", "line-through", "blink", NULL
};

static const char *const Css_text_transform_enum_vals[] = {
   "none", "capitalize", "uppercase", "lowercase", NULL
};

static const char *const Css_vertical_align_vals[] = {
   "top", "bottom", "middle", "baseline", "sub", "super", "text-top",
   "text-bottom", NULL
};

static const char *const Css_visibility_enum_vals[] = {
   "visible", "hidden", NULL
};

static const char *const Css_white_space_vals[] = {
   "normal", "pre", "nowrap", "pre-wrap", "pre-line", NULL
};

static const char *const Css_word_break_vals[] = {
   "normal", "break-all", "keep-all", "break-word", NULL
};

static const char *const Css_overflow_wrap_vals[] = {
   "normal", "break-word", "anywhere", NULL
};

static const char *const Css_word_spacing_enum_vals[] = {
   "normal", NULL
};

static const char *const Css_box_alignment_vals[] = {
   "normal", "stretch", "flex-start", "flex-end", "center", "baseline",
   "space-between", "space-around", "space-evenly", "start", "end", "self-start", "self-end", NULL
};
static const char *const Css_flex_direction_vals[] = {"row", "row-reverse", "column", "column-reverse", NULL};
static const char *const Css_flex_wrap_vals[] = {"nowrap", "wrap", "wrap-reverse", NULL};

static const char *const Css_object_fit_vals[] = {"fill", "contain", "cover", "none", "scale-down", NULL};

const CssPropertyInfo Css_property_info[CSS_PROPERTY_LAST] = {
   {"-webkit-box-orient", {CSS_TYPE_ENUM, CSS_TYPE_UNUSED},
    Css_webkit_box_orient_vals},
   {"-webkit-line-clamp", {CSS_TYPE_INTEGER, CSS_TYPE_ENUM, CSS_TYPE_UNUSED},
    Css_webkit_line_clamp_vals},
   {"align-content", {CSS_TYPE_ENUM, CSS_TYPE_UNUSED}, Css_box_alignment_vals},
   {"align-items", {CSS_TYPE_ENUM, CSS_TYPE_UNUSED}, Css_box_alignment_vals},
   {"align-self", {CSS_TYPE_ENUM, CSS_TYPE_UNUSED}, Css_box_alignment_vals},
   {"background-attachment", {CSS_TYPE_ENUM, CSS_TYPE_UNUSED},
    Css_background_attachment_enum_vals},
   {"background-clip", {CSS_TYPE_BACKGROUND_BOXES, CSS_TYPE_UNUSED}, NULL},
   {"background-color", {CSS_TYPE_RGBA, CSS_TYPE_UNUSED}, NULL},
   {"background-image", {CSS_TYPE_BACKGROUND_IMAGES, CSS_TYPE_URI, CSS_TYPE_UNUSED}, NULL},
   {"background-origin", {CSS_TYPE_BACKGROUND_BOXES, CSS_TYPE_UNUSED}, NULL},
   {"background-position", {CSS_TYPE_BACKGROUND_POSITION, CSS_TYPE_UNUSED},
    NULL},
   {"background-repeat", {CSS_TYPE_ENUM, CSS_TYPE_UNUSED},
    Css_background_repeat_enum_vals},
   {"background-size", {CSS_TYPE_BACKGROUND_SIZES, CSS_TYPE_UNUSED}, NULL},
   {"border-bottom-color", {CSS_TYPE_ENUM, CSS_TYPE_COLOR, CSS_TYPE_UNUSED},
    Css_border_color_enum_vals},
   {"border-bottom-left-radius", {CSS_TYPE_CORNER_RADIUS, CSS_TYPE_UNUSED}, NULL},
   {"border-bottom-right-radius", {CSS_TYPE_CORNER_RADIUS, CSS_TYPE_UNUSED}, NULL},
   {"border-bottom-style", {CSS_TYPE_ENUM, CSS_TYPE_UNUSED},
    Css_border_style_enum_vals},
   {"border-bottom-width", {CSS_TYPE_ENUM, CSS_TYPE_LENGTH, CSS_TYPE_UNUSED},
    Css_border_width_enum_vals},
   {"border-collapse", {CSS_TYPE_ENUM, CSS_TYPE_UNUSED},
    Css_border_collapse_enum_vals},
   {"border-left-color", {CSS_TYPE_ENUM, CSS_TYPE_COLOR, CSS_TYPE_UNUSED},
    Css_border_color_enum_vals},
   {"border-left-style", {CSS_TYPE_ENUM, CSS_TYPE_UNUSED},
    Css_border_style_enum_vals},
   {"border-left-width", {CSS_TYPE_ENUM, CSS_TYPE_LENGTH, CSS_TYPE_UNUSED},
    Css_border_width_enum_vals},
   {"border-right-color", {CSS_TYPE_ENUM, CSS_TYPE_COLOR, CSS_TYPE_UNUSED},
    Css_border_color_enum_vals},
   {"border-right-style", {CSS_TYPE_ENUM, CSS_TYPE_UNUSED},
    Css_border_style_enum_vals},
   {"border-right-width", {CSS_TYPE_ENUM, CSS_TYPE_LENGTH, CSS_TYPE_UNUSED},
    Css_border_width_enum_vals},
   {"border-spacing", {CSS_TYPE_LENGTH, CSS_TYPE_UNUSED}, NULL},
   {"border-top-color", {CSS_TYPE_ENUM, CSS_TYPE_COLOR, CSS_TYPE_UNUSED},
    Css_border_color_enum_vals},
   {"border-top-left-radius", {CSS_TYPE_CORNER_RADIUS, CSS_TYPE_UNUSED}, NULL},
   {"border-top-right-radius", {CSS_TYPE_CORNER_RADIUS, CSS_TYPE_UNUSED}, NULL},
   {"border-top-style", {CSS_TYPE_ENUM, CSS_TYPE_UNUSED},
    Css_border_style_enum_vals},
   {"border-top-width", {CSS_TYPE_ENUM, CSS_TYPE_LENGTH, CSS_TYPE_UNUSED},
    Css_border_width_enum_vals},
   {"bottom", {CSS_TYPE_SIGNED_LENGTH_PERCENTAGE, CSS_TYPE_AUTO, CSS_TYPE_UNUSED}, NULL},
   {"box-shadow", {CSS_TYPE_SHADOW, CSS_TYPE_UNUSED}, NULL},
   {"box-sizing", {CSS_TYPE_ENUM, CSS_TYPE_UNUSED}, Css_box_sizing_enum_vals},
   {"caption-side", {CSS_TYPE_UNUSED}, NULL},
   {"clear", {CSS_TYPE_ENUM, CSS_TYPE_UNUSED}, Css_clear_enum_vals},
   {"clip", {CSS_TYPE_UNUSED}, NULL},
   {"color", {CSS_TYPE_COLOR, CSS_TYPE_UNUSED}, NULL},
   {"column-gap", {CSS_TYPE_LENGTH, CSS_TYPE_UNUSED}, NULL},
   {"content", {CSS_TYPE_STRING, CSS_TYPE_UNUSED}, NULL},
   {"counter-increment", {CSS_TYPE_UNUSED}, NULL},
   {"counter-reset", {CSS_TYPE_UNUSED}, NULL},
   {"cursor", {CSS_TYPE_ENUM, CSS_TYPE_UNUSED}, Css_cursor_enum_vals},
   {"direction", {CSS_TYPE_UNUSED}, NULL},
   {"display", {CSS_TYPE_ENUM, CSS_TYPE_UNUSED}, Css_display_enum_vals},
   {"empty-cells", {CSS_TYPE_UNUSED}, NULL},
   {"flex-basis", {CSS_TYPE_LENGTH_PERCENTAGE, CSS_TYPE_AUTO, CSS_TYPE_UNUSED}, NULL},
   {"flex-direction", {CSS_TYPE_ENUM, CSS_TYPE_UNUSED}, Css_flex_direction_vals},
   {"flex-grow", {CSS_TYPE_LENGTH_PERCENTAGE_NUMBER, CSS_TYPE_UNUSED}, NULL},
   {"flex-shrink", {CSS_TYPE_LENGTH_PERCENTAGE_NUMBER, CSS_TYPE_UNUSED}, NULL},
   {"flex-wrap", {CSS_TYPE_ENUM, CSS_TYPE_UNUSED}, Css_flex_wrap_vals},
   {"float", {CSS_TYPE_ENUM, CSS_TYPE_UNUSED}, Css_float_enum_vals},
   {"font-family", {CSS_TYPE_SYMBOL, CSS_TYPE_UNUSED}, NULL},
   {"font-size", {CSS_TYPE_ENUM, CSS_TYPE_LENGTH_PERCENTAGE, CSS_TYPE_UNUSED},
    Css_font_size_enum_vals},
   {"font-size-adjust", {CSS_TYPE_UNUSED}, NULL},
   {"font-stretch", {CSS_TYPE_UNUSED}, NULL},
   {"font-style", {CSS_TYPE_ENUM, CSS_TYPE_UNUSED}, Css_font_style_enum_vals},
   {"font-variant", {CSS_TYPE_ENUM, CSS_TYPE_UNUSED},
    Css_font_variant_enum_vals},
   {"font-weight", {CSS_TYPE_ENUM, CSS_TYPE_FONT_WEIGHT, CSS_TYPE_UNUSED},
    Css_font_weight_enum_vals},
   {"grid-template-columns", {CSS_TYPE_COMPONENTS, CSS_TYPE_UNUSED}, NULL},
   {"grid-template-rows", {CSS_TYPE_COMPONENTS, CSS_TYPE_UNUSED}, NULL},
   {"height", {CSS_TYPE_LENGTH_PERCENTAGE, CSS_TYPE_AUTO, CSS_TYPE_UNUSED}, NULL},
   {"justify-content", {CSS_TYPE_ENUM, CSS_TYPE_UNUSED}, Css_box_alignment_vals},
   {"justify-items", {CSS_TYPE_ENUM, CSS_TYPE_UNUSED}, Css_box_alignment_vals},
   {"justify-self", {CSS_TYPE_ENUM, CSS_TYPE_UNUSED}, Css_box_alignment_vals},
   {"left", {CSS_TYPE_SIGNED_LENGTH_PERCENTAGE, CSS_TYPE_AUTO, CSS_TYPE_UNUSED}, NULL},
   {"letter-spacing", {CSS_TYPE_ENUM, CSS_TYPE_SIGNED_LENGTH, CSS_TYPE_UNUSED},
    Css_letter_spacing_enum_vals},
   {"line-height",
    {CSS_TYPE_ENUM, CSS_TYPE_LENGTH_PERCENTAGE_NUMBER, CSS_TYPE_UNUSED},
    Css_line_height_enum_vals},
   {"list-style-image", {CSS_TYPE_UNUSED}, NULL},
   {"list-style-position", {CSS_TYPE_ENUM, CSS_TYPE_UNUSED},
    Css_list_style_position_enum_vals},
   {"list-style-type", {CSS_TYPE_ENUM, CSS_TYPE_UNUSED},
    Css_list_style_type_enum_vals},
   {"margin-bottom",
    {CSS_TYPE_SIGNED_LENGTH, CSS_TYPE_AUTO, CSS_TYPE_UNUSED}, NULL},
   {"margin-left",
    {CSS_TYPE_SIGNED_LENGTH, CSS_TYPE_AUTO, CSS_TYPE_UNUSED}, NULL},
   {"margin-right",
    {CSS_TYPE_SIGNED_LENGTH, CSS_TYPE_AUTO, CSS_TYPE_UNUSED}, NULL},
   {"margin-top",
    {CSS_TYPE_SIGNED_LENGTH, CSS_TYPE_AUTO, CSS_TYPE_UNUSED}, NULL},
   {"marker-offset", {CSS_TYPE_UNUSED}, NULL},
   {"marks", {CSS_TYPE_UNUSED}, NULL},
   {"max-height", {CSS_TYPE_LENGTH_PERCENTAGE, CSS_TYPE_AUTO, CSS_TYPE_UNUSED},
    NULL},
   {"max-width", {CSS_TYPE_LENGTH_PERCENTAGE, CSS_TYPE_AUTO, CSS_TYPE_UNUSED},
    NULL},
   {"min-height", {CSS_TYPE_LENGTH_PERCENTAGE, CSS_TYPE_AUTO, CSS_TYPE_UNUSED},
    NULL},
   {"min-width", {CSS_TYPE_LENGTH_PERCENTAGE, CSS_TYPE_AUTO, CSS_TYPE_UNUSED},
    NULL},
   {"object-fit", {CSS_TYPE_ENUM, CSS_TYPE_UNUSED}, Css_object_fit_vals},
   {"object-position", {CSS_TYPE_BACKGROUND_POSITION, CSS_TYPE_UNUSED}, NULL},
   {"order", {CSS_TYPE_INTEGER, CSS_TYPE_UNUSED}, NULL},
   {"outline-color", {CSS_TYPE_RGBA, CSS_TYPE_UNUSED}, NULL},
   {"outline-offset", {CSS_TYPE_SIGNED_LENGTH, CSS_TYPE_UNUSED}, NULL},
   {"outline-style", {CSS_TYPE_ENUM, CSS_TYPE_UNUSED}, Css_border_style_enum_vals},
   {"outline-width", {CSS_TYPE_ENUM, CSS_TYPE_LENGTH, CSS_TYPE_UNUSED}, Css_border_width_enum_vals},
   {"overflow", {CSS_TYPE_ENUM, CSS_TYPE_UNUSED}, Css_overflow_enum_vals},
   {"overflow-wrap", {CSS_TYPE_ENUM, CSS_TYPE_UNUSED}, Css_overflow_wrap_vals},
   {"padding-bottom", {CSS_TYPE_LENGTH, CSS_TYPE_UNUSED}, NULL},
   {"padding-left", {CSS_TYPE_LENGTH, CSS_TYPE_UNUSED}, NULL},
   {"padding-right", {CSS_TYPE_LENGTH, CSS_TYPE_UNUSED}, NULL},
   {"padding-top", {CSS_TYPE_LENGTH, CSS_TYPE_UNUSED}, NULL},
   {"pointer-events", {CSS_TYPE_ENUM, CSS_TYPE_UNUSED}, Css_pointer_events_vals},
   {"position", {CSS_TYPE_ENUM, CSS_TYPE_UNUSED}, Css_position_enum_vals},
   {"quotes", {CSS_TYPE_UNUSED}, NULL},
   {"right", {CSS_TYPE_SIGNED_LENGTH_PERCENTAGE, CSS_TYPE_AUTO, CSS_TYPE_UNUSED}, NULL},
   {"row-gap", {CSS_TYPE_LENGTH, CSS_TYPE_UNUSED}, NULL},
   {"text-align", {CSS_TYPE_ENUM, CSS_TYPE_UNUSED}, Css_text_align_enum_vals},
   {"text-decoration", {CSS_TYPE_MULTI_ENUM, CSS_TYPE_UNUSED},
    Css_text_decoration_enum_vals},
   {"text-indent", {CSS_TYPE_LENGTH_PERCENTAGE, CSS_TYPE_UNUSED}, NULL},
   {"text-shadow", {CSS_TYPE_UNUSED}, NULL},
   {"text-transform", {CSS_TYPE_ENUM, CSS_TYPE_UNUSED},
    Css_text_transform_enum_vals},
   {"top", {CSS_TYPE_SIGNED_LENGTH_PERCENTAGE, CSS_TYPE_AUTO, CSS_TYPE_UNUSED}, NULL},
   {"transform", {CSS_TYPE_TRANSFORM, CSS_TYPE_UNUSED}, NULL},
   {"unicode-bidi", {CSS_TYPE_UNUSED}, NULL},
   {"vertical-align",{CSS_TYPE_ENUM, CSS_TYPE_UNUSED},Css_vertical_align_vals},
   {"visibility", {CSS_TYPE_ENUM, CSS_TYPE_UNUSED}, Css_visibility_enum_vals},
   {"white-space", {CSS_TYPE_ENUM, CSS_TYPE_UNUSED}, Css_white_space_vals},
   {"width", {CSS_TYPE_LENGTH_PERCENTAGE, CSS_TYPE_AUTO, CSS_TYPE_UNUSED}, NULL},
   {"word-break", {CSS_TYPE_ENUM, CSS_TYPE_UNUSED}, Css_word_break_vals},
   {"word-spacing", {CSS_TYPE_ENUM, CSS_TYPE_SIGNED_LENGTH, CSS_TYPE_UNUSED},
    Css_word_spacing_enum_vals},
   {"word-wrap", {CSS_TYPE_ENUM, CSS_TYPE_UNUSED}, Css_overflow_wrap_vals},
   {"z-index", {CSS_TYPE_INTEGER, CSS_TYPE_AUTO, CSS_TYPE_UNUSED}, NULL},

   /* These are extensions, for internal used, and never parsed. */
   {"x-link", {CSS_TYPE_INTEGER, CSS_TYPE_UNUSED}, NULL},
   {"x-colspan", {CSS_TYPE_INTEGER, CSS_TYPE_UNUSED}, NULL},
   {"x-rowspan", {CSS_TYPE_INTEGER, CSS_TYPE_UNUSED}, NULL},
   {"last", {CSS_TYPE_UNUSED}, NULL},
};

typedef struct {
   const char *symbol;
   enum {
      CSS_SHORTHAND_BACKGROUND,
      CSS_SHORTHAND_OUTLINE,
      CSS_SHORTHAND_RADIUS,
      CSS_SHORTHAND_FLEX,
      CSS_SHORTHAND_FLOW,
      CSS_SHORTHAND_GAP,
      CSS_SHORTHAND_MULTIPLE,   /**< [ p1 || p2 || ...], the property pi is
                                 * determined  by the type */
      CSS_SHORTHAND_DIRECTIONS, /**< <t>{1,4} */
      CSS_SHORTHAND_BORDER,     /**< special, used for 'border' */
      CSS_SHORTHAND_FONT        /**< special, used for 'font' */
   } type;
   const CssPropertyName *properties; /* CSS_SHORTHAND_MULTIPLE:
                                       *   must be terminated by
                                       *   CSS_PROPERTY_END
                                       * CSS_SHORTHAND_DIRECTIONS:
                                       *   must have length 4
                                       * CSS_SHORTHAND_BORDERS:
                                       *   must have length 12
                                       * CSS_SHORTHAND_FONT:
                                       *   variable */
} CssShorthandInfo;

const CssPropertyName Css_background_properties[] = {
   CSS_PROPERTY_BACKGROUND_COLOR,
   CSS_PROPERTY_BACKGROUND_IMAGE,
   CSS_PROPERTY_BACKGROUND_REPEAT,
   CSS_PROPERTY_BACKGROUND_ATTACHMENT,
   CSS_PROPERTY_BACKGROUND_POSITION,
   CSS_PROPERTY_BACKGROUND_ORIGIN,
   CSS_PROPERTY_BACKGROUND_CLIP,
   CSS_PROPERTY_BACKGROUND_SIZE,
   CSS_PROPERTY_END
};

const CssPropertyName Css_flex_properties[] = {
   CSS_PROPERTY_FLEX_GROW, CSS_PROPERTY_FLEX_SHRINK, CSS_PROPERTY_FLEX_BASIS, CSS_PROPERTY_END
};
const CssPropertyName Css_flow_properties[] = {CSS_PROPERTY_FLEX_DIRECTION, CSS_PROPERTY_FLEX_WRAP, CSS_PROPERTY_END};
const CssPropertyName Css_gap_properties[] = {CSS_PROPERTY_ROW_GAP, CSS_PROPERTY_COLUMN_GAP, CSS_PROPERTY_END};

const CssPropertyName Css_border_bottom_properties[] = {
   CSS_PROPERTY_BORDER_BOTTOM_WIDTH,
   CSS_PROPERTY_BORDER_BOTTOM_STYLE,
   CSS_PROPERTY_BORDER_BOTTOM_COLOR,
   CSS_PROPERTY_END
};

const CssPropertyName Css_border_color_properties[4] = {
   CSS_PROPERTY_BORDER_TOP_COLOR,
   CSS_PROPERTY_BORDER_BOTTOM_COLOR,
   CSS_PROPERTY_BORDER_LEFT_COLOR,
   CSS_PROPERTY_BORDER_RIGHT_COLOR
};

const CssPropertyName Css_border_left_properties[] = {
   CSS_PROPERTY_BORDER_LEFT_WIDTH,
   CSS_PROPERTY_BORDER_LEFT_STYLE,
   CSS_PROPERTY_BORDER_LEFT_COLOR,
   CSS_PROPERTY_END
};

const CssPropertyName Css_border_right_properties[] = {
   CSS_PROPERTY_BORDER_RIGHT_WIDTH,
   CSS_PROPERTY_BORDER_RIGHT_STYLE,
   CSS_PROPERTY_BORDER_RIGHT_COLOR,
   CSS_PROPERTY_END
};

const CssPropertyName Css_border_style_properties[] = {
   CSS_PROPERTY_BORDER_TOP_STYLE,
   CSS_PROPERTY_BORDER_BOTTOM_STYLE,
   CSS_PROPERTY_BORDER_LEFT_STYLE,
   CSS_PROPERTY_BORDER_RIGHT_STYLE
};

const CssPropertyName Css_border_top_properties[] = {
   CSS_PROPERTY_BORDER_TOP_WIDTH,
   CSS_PROPERTY_BORDER_TOP_STYLE,
   CSS_PROPERTY_BORDER_TOP_COLOR,
   CSS_PROPERTY_END
};

const CssPropertyName Css_border_width_properties[] = {
   CSS_PROPERTY_BORDER_TOP_WIDTH,
   CSS_PROPERTY_BORDER_BOTTOM_WIDTH,
   CSS_PROPERTY_BORDER_LEFT_WIDTH,
   CSS_PROPERTY_BORDER_RIGHT_WIDTH
};

const CssPropertyName Css_list_style_properties[] = {
   CSS_PROPERTY_LIST_STYLE_TYPE,
   CSS_PROPERTY_LIST_STYLE_POSITION,
   CSS_PROPERTY_LIST_STYLE_IMAGE,
   CSS_PROPERTY_END
};

const CssPropertyName Css_margin_properties[] = {
   CSS_PROPERTY_MARGIN_TOP,
   CSS_PROPERTY_MARGIN_BOTTOM,
   CSS_PROPERTY_MARGIN_LEFT,
   CSS_PROPERTY_MARGIN_RIGHT
};

const CssPropertyName Css_outline_properties[] = {
   CSS_PROPERTY_OUTLINE_COLOR,
   CSS_PROPERTY_OUTLINE_STYLE,
   CSS_PROPERTY_OUTLINE_WIDTH,
   CSS_PROPERTY_END
};

const CssPropertyName Css_padding_properties[] = {
   CSS_PROPERTY_PADDING_TOP,
   CSS_PROPERTY_PADDING_BOTTOM,
   CSS_PROPERTY_PADDING_LEFT,
   CSS_PROPERTY_PADDING_RIGHT
};

const CssPropertyName Css_border_properties[] = {
   CSS_PROPERTY_BORDER_TOP_WIDTH,
   CSS_PROPERTY_BORDER_TOP_STYLE,
   CSS_PROPERTY_BORDER_TOP_COLOR,
   CSS_PROPERTY_BORDER_BOTTOM_WIDTH,
   CSS_PROPERTY_BORDER_BOTTOM_STYLE,
   CSS_PROPERTY_BORDER_BOTTOM_COLOR,
   CSS_PROPERTY_BORDER_LEFT_WIDTH,
   CSS_PROPERTY_BORDER_LEFT_STYLE,
   CSS_PROPERTY_BORDER_LEFT_COLOR,
   CSS_PROPERTY_BORDER_RIGHT_WIDTH,
   CSS_PROPERTY_BORDER_RIGHT_STYLE,
   CSS_PROPERTY_BORDER_RIGHT_COLOR,
   CSS_PROPERTY_END
};

const CssPropertyName Css_radius_properties[] = {
   CSS_PROPERTY_BORDER_TOP_LEFT_RADIUS, CSS_PROPERTY_BORDER_TOP_RIGHT_RADIUS,
   CSS_PROPERTY_BORDER_BOTTOM_RIGHT_RADIUS, CSS_PROPERTY_BORDER_BOTTOM_LEFT_RADIUS,
   CSS_PROPERTY_END
};

const CssPropertyName Css_font_properties[] = {
   CSS_PROPERTY_FONT_SIZE,
   CSS_PROPERTY_FONT_STYLE,
   CSS_PROPERTY_FONT_VARIANT,
   CSS_PROPERTY_FONT_WEIGHT,
   CSS_PROPERTY_FONT_FAMILY,
   CSS_PROPERTY_LINE_HEIGHT,
   CSS_PROPERTY_END
};

static const CssShorthandInfo Css_shorthand_info[] = {
   {"background", CssShorthandInfo::CSS_SHORTHAND_BACKGROUND,
    Css_background_properties},
   {"border", CssShorthandInfo::CSS_SHORTHAND_BORDER,
    Css_border_properties},
   {"border-bottom", CssShorthandInfo::CSS_SHORTHAND_MULTIPLE,
    Css_border_bottom_properties},
   {"border-color", CssShorthandInfo::CSS_SHORTHAND_DIRECTIONS,
    Css_border_color_properties},
   {"border-left", CssShorthandInfo::CSS_SHORTHAND_MULTIPLE,
    Css_border_left_properties},
   {"border-radius", CssShorthandInfo::CSS_SHORTHAND_RADIUS, Css_radius_properties},
   {"border-right", CssShorthandInfo::CSS_SHORTHAND_MULTIPLE,
    Css_border_right_properties},
   {"border-style", CssShorthandInfo::CSS_SHORTHAND_DIRECTIONS,
    Css_border_style_properties},
   {"border-top", CssShorthandInfo::CSS_SHORTHAND_MULTIPLE,
    Css_border_top_properties},
   {"border-width", CssShorthandInfo::CSS_SHORTHAND_DIRECTIONS,
    Css_border_width_properties},
   {"flex", CssShorthandInfo::CSS_SHORTHAND_FLEX, Css_flex_properties},
   {"flex-flow", CssShorthandInfo::CSS_SHORTHAND_FLOW, Css_flow_properties},
   {"font", CssShorthandInfo::CSS_SHORTHAND_FONT,
    Css_font_properties},
   {"gap", CssShorthandInfo::CSS_SHORTHAND_GAP, Css_gap_properties},
   {"list-style", CssShorthandInfo::CSS_SHORTHAND_MULTIPLE,
    Css_list_style_properties},
   {"margin", CssShorthandInfo::CSS_SHORTHAND_DIRECTIONS,
    Css_margin_properties},
   {"outline", CssShorthandInfo::CSS_SHORTHAND_OUTLINE,
    Css_outline_properties},
   {"padding", CssShorthandInfo::CSS_SHORTHAND_DIRECTIONS,
    Css_padding_properties},
};

#define CSS_SHORTHAND_NUM \
   (sizeof(Css_shorthand_info) / sizeof(Css_shorthand_info[0]))

/* ----------------------------------------------------------------------
 *    Parsing
 * ---------------------------------------------------------------------- */

CssParser::CssParser(CssContext *context, CssOrigin origin,
                     const DilloUrl *baseUrl,
                     const char *buf, int buflen)
{
   this->context = context;
   this->origin = origin;
   this->buf = buf;
   this->buflen = buflen;
   this->bufptr = 0;
   this->spaceSeparated = false;
   this->withinBlock = false;
   this->baseUrl = baseUrl;

   nextToken ();
}

/**
 * Gets the next character from the buffer, or EOF.
 */
int CssParser::getChar()
{
   int c;

   if (bufptr >= buflen)
      c = EOF;
   else
      c = buf[bufptr];

   /* The buffer pointer is increased in any case, so that ungetChar works
    * correctly at the end of the buffer. */
   bufptr++;
   return c;
}

/**
 * Undoes the last getChar().
 */
void CssParser::ungetChar()
{
   bufptr--;
}

/**
 * Skip string str if it is found in the input buffer.
 * If string is found leave bufptr pointing to last matched char.
 * If not wind back. The first char is passed as parameter c
 * to avoid unnecessary getChar() / ungetChar() calls.
 */
inline bool CssParser::skipString(int c, const char *str)
{
   for (int n = 0; str[n]; n++) {
      if (n > 0)
         c = getChar();

      if (str[n] != c) {
         while (n--)
            ungetChar();
         return false;
      }
   }

   return true;
}

void CssParser::nextToken()
{
   int c, c1, d, j;
   char hexbuf[5];
   int i = 0;

   ttype = CSS_TK_CHAR; /* init */
   spaceSeparated = false;

   while (true) {
      c = getChar();
      if (dIsspace(c)) {                    // ignore whitespace
         spaceSeparated = true;
      } else if (skipString(c, "/*")) {    // ignore comments
         do {
            c = getChar();
         } while (c != EOF && ! skipString(c, "*/"));
      } else if (skipString(c, "<!--")) {  // ignore XML comment markers
      } else if (skipString(c, "-->")) {
      } else {
         break;
      }
   }

   // handle negative numbers
   if (c == '-') {
      if (i < maxStrLen - 1)
         tval[i++] = c;
      c = getChar();
   }

   if (dIsdigit(c)) {
      ttype = CSS_TK_DECINT;
      do {
         if (i < maxStrLen - 1) {
            tval[i++] = c;
         }
         /* else silently truncated */
         c = getChar();
      } while (dIsdigit(c));
      if (c != '.')
         ungetChar();

      /* ...but keep going to see whether it's really a float */
   }

   if (c == '.') {
      c = getChar();
      if (dIsdigit(c)) {
         ttype = CSS_TK_FLOAT;
         if (i < maxStrLen - 1)
            tval[i++] = '.';
         do {
            if (i < maxStrLen - 1)
               tval[i++] = c;
            /* else silently truncated */
            c = getChar();
         } while (dIsdigit(c));

         ungetChar();
         tval[i] = 0;
         DEBUG_MSG(DEBUG_TOKEN_LEVEL, "token number %s\n", tval);
         return;
      } else {
         ungetChar();
         if (ttype == CSS_TK_DECINT) {
            ungetChar();
         } else {
            c = '.';
         }
      }
   }

   if (ttype == CSS_TK_DECINT) {
      tval[i] = 0;
      DEBUG_MSG(DEBUG_TOKEN_LEVEL, "token number %s\n", tval);
      return;
   }

   if (i) {
      ungetChar(); /* first character following '-' belongs to the symbol */
      i--;
      c = '-';
   }

   if (dIsalpha(c) || c == '_' || c == '-') {
      ttype = CSS_TK_SYMBOL;

      tval[0] = c;
      i = 1;
      c = getChar();
      while (dIsalnum(c) || c == '_' || c == '-') {
         if (i < maxStrLen - 1) {
            tval[i] = c;
            i++;
         }                      /* else silently truncated */
         c = getChar();
      }
      tval[i] = 0;
      ungetChar();
      DEBUG_MSG(DEBUG_TOKEN_LEVEL, "token symbol '%s'\n", tval);
      return;
   }

   if (c == '"' || c == '\'') {
      c1 = c;
      ttype = CSS_TK_STRING;

      i = 0;
      c = getChar();

      while (c != EOF && c != c1) {
         if (c == '\\') {
            d = getChar();
            if (dIsxdigit(d)) {
               /* Read hex Unicode char. (Actually, strings are yet only 8
                * bit.) */
               hexbuf[0] = d;
               j = 1;
               d = getChar();
               while (j < 4 && dIsxdigit(d)) {
                  hexbuf[j] = d;
                  j++;
                  d = getChar();
               }
               hexbuf[j] = 0;
               ungetChar();
               c = strtol(hexbuf, NULL, 16);
            } else {
               /* Take character literally. */
               c = d;
            }
         }

         if (i < maxStrLen - 1) {
            tval[i] = c;
            i++;
         }                      /* else silently truncated */
         c = getChar();
      }
      tval[i] = 0;
      /* No ungetChar(). */
      DEBUG_MSG(DEBUG_TOKEN_LEVEL, "token string '%s'\n", tval);
      return;
   }

   /*
    * Within blocks, '#' starts a color, outside, it is used in selectors.
    */
   if (c == '#' && withinBlock) {
      ttype = CSS_TK_COLOR;

      tval[0] = c;
      i = 1;
      c = getChar();
      while (dIsxdigit(c)) {
         if (i < maxStrLen - 1) {
            tval[i] = c;
            i++;
         }                      /* else silently truncated */
         c = getChar();
      }
      tval[i] = 0;
      ungetChar();
      DEBUG_MSG(DEBUG_TOKEN_LEVEL, "token color '%s'\n", tval);
      return;
   }

   if (c == EOF) {
      DEBUG_MSG(DEBUG_TOKEN_LEVEL, "token %s\n", "EOF");
      ttype = CSS_TK_END;
      return;
   }

   ttype = CSS_TK_CHAR;
   tval[0] = c;
   tval[1] = 0;
   DEBUG_MSG(DEBUG_TOKEN_LEVEL, "token char '%c'\n", c);
}


bool CssParser::tokenMatchesProperty(CssPropertyName prop, CssValueType *type)
{
   int i, err = 1;
   CssValueType savedType = *type;

   for (int j = 0; Css_property_info[prop].type[j] != CSS_TYPE_UNUSED; j++) {
      *type = Css_property_info[prop].type[j];

      switch (Css_property_info[prop].type[j]) {

      case CSS_TYPE_ENUM:
         if (ttype == CSS_TK_SYMBOL) {
            for (i = 0; Css_property_info[prop].enum_symbols[i]; i++)
               if (dStrAsciiCasecmp(tval,
                     Css_property_info[prop].enum_symbols[i]) == 0)
                  return true;
         }
         break;

      case CSS_TYPE_MULTI_ENUM:
         if (ttype == CSS_TK_SYMBOL) {
            if (dStrAsciiCasecmp(tval, "none") == 0) {
               return true;
            } else {
               for (i = 0; Css_property_info[prop].enum_symbols[i]; i++) {
                  if (dStrAsciiCasecmp(tval,
                        Css_property_info[prop].enum_symbols[i]) == 0)
                     return true;
               }
            }
         }
         break;

      case CSS_TYPE_TRANSFORM:
         if (ttype == CSS_TK_SYMBOL && (!dStrAsciiCasecmp(tval, "none") ||
             !dStrAsciiCasecmp(tval, "translate") || !dStrAsciiCasecmp(tval, "translateX") ||
             !dStrAsciiCasecmp(tval, "translateY") || !dStrAsciiCasecmp(tval, "translate3d")))
            return true;
         break;
      case CSS_TYPE_BACKGROUND_IMAGES:
         if (ttype == CSS_TK_SYMBOL && (!dStrAsciiCasecmp(tval, "linear-gradient") ||
             !dStrAsciiCasecmp(tval, "repeating-linear-gradient") || !dStrAsciiCasecmp(tval, "none") ||
             !dStrAsciiCasecmp(tval, "url"))) return true;
         break;
      case CSS_TYPE_BACKGROUND_BOXES:
         if (backgroundBox() >= 0) return true;
         break;
      case CSS_TYPE_BACKGROUND_SIZES:
         if (ttype == CSS_TK_DECINT || ttype == CSS_TK_FLOAT ||
             (ttype == CSS_TK_SYMBOL && (!dStrAsciiCasecmp(tval, "cover") ||
              !dStrAsciiCasecmp(tval, "contain") || !dStrAsciiCasecmp(tval, "auto") ||
              !dStrAsciiCasecmp(tval, "calc")))) return true;
         break;
      case CSS_TYPE_CORNER_RADIUS:
         if (ttype == CSS_TK_DECINT || ttype == CSS_TK_FLOAT ||
             (ttype == CSS_TK_SYMBOL && !dStrAsciiCasecmp(tval, "calc"))) return true;
         break;
      case CSS_TYPE_BACKGROUND_POSITION:
         if (ttype == CSS_TK_SYMBOL &&
             (dStrAsciiCasecmp(tval, "center") == 0 ||
              dStrAsciiCasecmp(tval, "left") == 0 ||
              dStrAsciiCasecmp(tval, "right") == 0 ||
              dStrAsciiCasecmp(tval, "top") == 0 ||
              dStrAsciiCasecmp(tval, "bottom") == 0))
            return true;
         if (ttype == CSS_TK_DECINT || ttype == CSS_TK_FLOAT)
            return true;
         break;
      case CSS_TYPE_LENGTH_PERCENTAGE:
      case CSS_TYPE_LENGTH_PERCENTAGE_NUMBER:
      case CSS_TYPE_LENGTH:
         if (tval[0] == '-')
            return false;
         // Fall Through
      case CSS_TYPE_SIGNED_LENGTH:
      case CSS_TYPE_SIGNED_LENGTH_PERCENTAGE:
         if (ttype == CSS_TK_SYMBOL && !dStrAsciiCasecmp(tval, "calc"))
            return true;
         if (ttype == CSS_TK_DECINT || ttype == CSS_TK_FLOAT)
            return true;
         break;

      case CSS_TYPE_AUTO:
         if (ttype == CSS_TK_SYMBOL && dStrAsciiCasecmp(tval, "auto") == 0)
            return true;
         break;

      case CSS_TYPE_RGBA:
         if (ttype == CSS_TK_SYMBOL && !dStrAsciiCasecmp(tval, "currentColor")) return true;
         // fall through
      case CSS_TYPE_COLOR:
         if ((ttype == CSS_TK_COLOR ||
              ttype == CSS_TK_SYMBOL) &&
            (ttype == CSS_TK_COLOR || !dStrAsciiCasecmp(tval, "transparent") ||
             dStrAsciiCasecmp(tval, "rgb") == 0 ||
             dStrAsciiCasecmp(tval, "rgba") == 0 ||
             dStrAsciiCasecmp(tval, "hsl") == 0 ||
             dStrAsciiCasecmp(tval, "hsla") == 0 ||
             a_Color_parse(tval, -1, &err) != -1))
            return true;
         break;

      case CSS_TYPE_SHADOW:
         return ttype == CSS_TK_SYMBOL || ttype == CSS_TK_COLOR ||
            ttype == CSS_TK_DECINT || ttype == CSS_TK_FLOAT;

      case CSS_TYPE_STRING:
         if (ttype == CSS_TK_STRING)
            return true;
         break;

      case CSS_TYPE_SYMBOL:
         if (ttype == CSS_TK_SYMBOL ||
             ttype == CSS_TK_STRING)
            return true;
         break;

      case CSS_TYPE_FONT_WEIGHT:
         if (ttype == CSS_TK_DECINT) {
            i = strtol(tval, NULL, 10);
            if (i >= 100 && i <= 900)
               return true;
         }
         break;

      case CSS_TYPE_URI:
         if (ttype == CSS_TK_SYMBOL &&
             (dStrAsciiCasecmp(tval, "url") == 0 ||
              dStrAsciiCasecmp(tval, "none") == 0))
            return true;
         break;

      case CSS_TYPE_INTEGER:
         if (ttype == CSS_TK_DECINT)
            return true;
         break;

      case CSS_TYPE_UNUSED:
      default:
         assert(false);
         break;
      }
   }

   *type = savedType;
   return false;
}

static void Css_calc_terms(CssLength *value)
{
   if (value->type != CSS_LENGTH_TYPE_CALC) {
      if (value->type != CSS_LENGTH_TYPE_NONE) value->terms[value->type] = CSS_LENGTH_VALUE(*value);
      value->type = CSS_LENGTH_TYPE_CALC;
   }
}

bool CssParser::parseCalcAtom(CssLength *value, unsigned depth)
{
   if (depth > 32) return false;
   if (ttype == CSS_TK_SYMBOL && !dStrAsciiCasecmp(tval, "calc")) nextToken();
   if (ttype == CSS_TK_CHAR && tval[0] == '(') {
      nextToken();
      if (!parseCalcSum(value, depth + 1) || ttype != CSS_TK_CHAR || tval[0] != ')') return false;
      nextToken(); return true;
   }
   CssPropertyValue parsed;
   if (!parseValue(CSS_PROPERTY_WIDTH, CSS_TYPE_LENGTH_PERCENTAGE_NUMBER, &parsed)) return false;
   *value = parsed.lenVal;
   return value->type != CSS_LENGTH_TYPE_NONE || std::isfinite(value->f);
}

bool CssParser::parseCalcProduct(CssLength *value, unsigned depth)
{
   if (!parseCalcAtom(value, depth)) return false;
   while (ttype == CSS_TK_CHAR && (tval[0] == '*' || tval[0] == '/')) {
      bool divide = tval[0] == '/'; nextToken();
      CssLength right;
      if (!parseCalcAtom(&right, depth)) return false;
      if (divide && (right.type != CSS_LENGTH_TYPE_NONE || right.f == 0)) return false;
      if (value->type != CSS_LENGTH_TYPE_NONE && right.type != CSS_LENGTH_TYPE_NONE) return false;
      if (value->type == CSS_LENGTH_TYPE_NONE && right.type == CSS_LENGTH_TYPE_NONE) {
         value->f = divide ? value->f / right.f : value->f * right.f;
         if (!std::isfinite(value->f)) return false;
      } else {
         float factor;
         if (right.type == CSS_LENGTH_TYPE_NONE) factor = divide ? 1 / right.f : right.f;
         else { factor = value->f; *value = right; }
         Css_calc_terms(value);
         for (int i = 1; i <= CSS_LENGTH_TYPE_PERCENTAGE; ++i) {
            value->terms[i] *= factor;
            if (!std::isfinite(value->terms[i]) || std::fabs(value->terms[i]) > 10000000) return false;
         }
      }
   }
   return true;
}

bool CssParser::parseCalcSum(CssLength *value, unsigned depth)
{
   if (!parseCalcProduct(value, depth)) return false;
   while ((ttype == CSS_TK_CHAR || ttype == CSS_TK_SYMBOL) && !tval[1] && (tval[0] == '+' || tval[0] == '-')) {
      bool subtract = tval[0] == '-'; nextToken();
      CssLength right;
      if (!parseCalcProduct(&right, depth)) return false;
      if (value->type == CSS_LENGTH_TYPE_NONE && right.type == CSS_LENGTH_TYPE_NONE) {
         value->f += (subtract ? -1 : 1) * right.f;
      } else {
         if ((value->type == CSS_LENGTH_TYPE_NONE && value->f != 0) ||
             (right.type == CSS_LENGTH_TYPE_NONE && right.f != 0)) return false;
         Css_calc_terms(value); Css_calc_terms(&right);
         for (int i = 1; i <= CSS_LENGTH_TYPE_PERCENTAGE; ++i) {
            value->terms[i] += (subtract ? -1 : 1) * right.terms[i];
            if (!std::isfinite(value->terms[i]) || std::fabs(value->terms[i]) > 10000000) return false;
         }
      }
   }
   return true;
}

bool CssParser::parseTransform(CssTransform *value)
{
   value->active = false;
   value->x = value->y = CSS_CREATE_LENGTH(0, CSS_LENGTH_TYPE_CALC);
   if (ttype == CSS_TK_SYMBOL && !dStrAsciiCasecmp(tval, "none")) {
      nextToken();
   } else {
      unsigned functions = 0;
      while (ttype == CSS_TK_SYMBOL) {
         bool xOnly = !dStrAsciiCasecmp(tval, "translateX");
         bool yOnly = !dStrAsciiCasecmp(tval, "translateY");
         bool three = !dStrAsciiCasecmp(tval, "translate3d");
         if ((!xOnly && !yOnly && !three && dStrAsciiCasecmp(tval, "translate")) ||
             ++functions > 32) return false;
         nextToken();
         if (ttype != CSS_TK_CHAR || tval[0] != '(') return false;
         nextToken();
         CssPropertyValue first, second, third;
         second.lenVal = CSS_CREATE_LENGTH(0, CSS_LENGTH_TYPE_PX);
         if (!parseValue(CSS_PROPERTY_LEFT, CSS_TYPE_SIGNED_LENGTH_PERCENTAGE, &first)) return false;
         if (!xOnly && !yOnly && ttype == CSS_TK_CHAR && tval[0] == ',') {
            nextToken();
            if (!parseValue(CSS_PROPERTY_TOP, CSS_TYPE_SIGNED_LENGTH_PERCENTAGE, &second)) return false;
         } else if (three) return false;
         if (three) {
            if (ttype != CSS_TK_CHAR || tval[0] != ',') return false;
            nextToken();
            if (!parseValue(CSS_PROPERTY_TOP, CSS_TYPE_SIGNED_LENGTH, &third) ||
                third.lenVal.type == CSS_LENGTH_TYPE_CALC || CSS_LENGTH_VALUE(third.lenVal) != 0) return false;
         }
         if (ttype != CSS_TK_CHAR || tval[0] != ')') return false;
         nextToken();
         auto add = [](CssLength *sum, const CssLength &part) {
            if (part.type == CSS_LENGTH_TYPE_CALC) {
               for (unsigned n = 0; n < sizeof(sum->terms) / sizeof(sum->terms[0]); ++n)
                  sum->terms[n] += part.terms[n];
            } else if (part.type <= CSS_LENGTH_TYPE_PERCENTAGE)
               sum->terms[part.type] += CSS_LENGTH_VALUE(part);
         };
         add(yOnly ? &value->y : &value->x, first.lenVal);
         if (!xOnly && !yOnly) add(&value->y, second.lenVal);
         value->active = true;
      }
      if (!value->active) return false;
   }
   return ttype == CSS_TK_END || (ttype == CSS_TK_CHAR &&
      (tval[0] == ';' || tval[0] == '}' || tval[0] == '!'));
}

bool CssParser::parseColor(unsigned *rgba)
{
   if (ttype == CSS_TK_COLOR) {
      const char *hex = tval + (tval[0] == '#');
      size_t len = strlen(hex);
      if (len != 3 && len != 4 && len != 6 && len != 8) return false;
      for (size_t n = 0; n < len; ++n) if (!dIsxdigit(hex[n])) return false;
      unsigned long v = strtoul(hex, NULL, 16);
      if (len == 3 || len == 4) {
         *rgba = 0;
         for (size_t n = 0; n < len; ++n)
            *rgba = (*rgba << 8) | (((v >> (4 * (len - 1 - n))) & 15) * 17);
         if (len == 3) *rgba = (*rgba << 8) | 255;
      } else *rgba = len == 6 ? (unsigned)(v << 8) | 255 : (unsigned)v;
      nextToken(); return true;
   }
   if (ttype != CSS_TK_SYMBOL) return false;
   if (!dStrAsciiCasecmp(tval, "transparent")) {
      *rgba = 0; nextToken(); return true;
   }
   bool hsl = !dStrAsciiCasecmp(tval, "hsl") || !dStrAsciiCasecmp(tval, "hsla");
   if (!hsl && dStrAsciiCasecmp(tval, "rgb") && dStrAsciiCasecmp(tval, "rgba")) {
      int error;
      int color = a_Color_parse(tval, -1, &error);
      if (error) return false;
      *rgba = ((unsigned)color << 8) | 255; nextToken(); return true;
   }
   nextToken();
   if (ttype != CSS_TK_CHAR || tval[0] != '(') return false;
   nextToken();
   double components[4] = {0, 0, 0, 1};
   bool comma = false, percentage = false;
   for (int n = 0; n < 4; ++n) {
      if (n == 3 && ttype == CSS_TK_CHAR && tval[0] == ')') break;
      if (n) {
         if (ttype == CSS_TK_CHAR && (tval[0] == ',' || (n == 3 && tval[0] == '/'))) {
            if (n == 1) comma = tval[0] == ',';
            if ((tval[0] == ',') != comma) return false;
            nextToken();
         } else if (comma || !spaceSeparated || n == 3) return false;
      }
      if (ttype != CSS_TK_DECINT && ttype != CSS_TK_FLOAT) return false;
      double component = strtod(tval, NULL);
      if (!std::isfinite(component)) return false;
      nextToken();
      if (hsl && n == 0) {
         if (ttype == CSS_TK_SYMBOL && !spaceSeparated) {
            if (!dStrAsciiCasecmp(tval, "turn")) component *= 360;
            else if (!dStrAsciiCasecmp(tval, "rad")) component *= 180 / std::acos(-1.0);
            else if (!dStrAsciiCasecmp(tval, "grad")) component *= .9;
            else if (dStrAsciiCasecmp(tval, "deg")) return false;
            nextToken();
         }
         if (!std::isfinite(component)) return false;
         components[n] = std::fmod(std::fmod(component, 360) + 360, 360) / 60;
         continue;
      }
      bool percent = ttype == CSS_TK_CHAR && tval[0] == '%';
      if (n == 0) percentage = percent;
      else if (!hsl && comma && n < 3 && percentage != percent) return false;
      if (percent) {
         component /= 100; nextToken();
      } else if (hsl && n < 3) return false;
      else if (n < 3) component /= 255;
      components[n] = lout::misc::min(1.0, lout::misc::max(0.0, component));
   }
   if (ttype != CSS_TK_CHAR || tval[0] != ')') return false;
   if (hsl) {
      double hue = components[0], light = components[2];
      double chroma = (1 - std::fabs(2 * light - 1)) * components[1];
      double secondary = chroma * (1 - std::fabs(std::fmod(hue, 2) - 1));
      double channels[6][3] = {{chroma,secondary,0},{secondary,chroma,0},
         {0,chroma,secondary},{0,secondary,chroma},{secondary,0,chroma},{chroma,0,secondary}};
      for (int n = 0; n < 3; ++n) components[n] = channels[(int)hue][n] + light - chroma / 2;
   }
   *rgba = 0;
   for (int n = 0; n < 4; ++n) *rgba = (*rgba << 8) | (unsigned)(components[n] * 255 + .5);
   nextToken(); return true;
}

bool CssParser::parseShadows(CssShadows *value)
{
   if (ttype == CSS_TK_SYMBOL && !dStrAsciiCasecmp(tval, "none")) {
      nextToken(); return true;
   }
   do {
      if (value->count == CssShadows::MAX_SHADOWS) return false;
      CssShadows::Shadow &shadow = value->shadows[value->count++];
      shadow.color.currentColor = true;
      unsigned lengths = 0;
      bool color = false, inset = false;
      for (;;) {
         if (ttype == CSS_TK_SYMBOL && !dStrAsciiCasecmp(tval, "inset")) {
            if (inset) return false;
            inset = shadow.inset = true; nextToken();
         } else if (ttype == CSS_TK_DECINT || ttype == CSS_TK_FLOAT ||
                    (ttype == CSS_TK_SYMBOL && !dStrAsciiCasecmp(tval, "calc"))) {
            if (lengths) return false;
            do {
               CssPropertyValue length;
               if (!parseValue(CSS_PROPERTY_BOX_SHADOW, CSS_TYPE_SIGNED_LENGTH, &length)) return false;
               if (lengths == 2 && length.lenVal.type != CSS_LENGTH_TYPE_CALC &&
                   CSS_LENGTH_VALUE(length.lenVal) < 0) return false;
               shadow.lengths[lengths++] = length.lenVal;
            } while (lengths < 4 && (ttype == CSS_TK_DECINT || ttype == CSS_TK_FLOAT ||
                     (ttype == CSS_TK_SYMBOL && !dStrAsciiCasecmp(tval, "calc"))));
         } else if (!color && (ttype == CSS_TK_COLOR || ttype == CSS_TK_SYMBOL)) {
            color = true;
            CssPropertyValue shade;
            if (!parseValue(CSS_PROPERTY_BACKGROUND_COLOR, CSS_TYPE_RGBA, &shade)) return false;
            shadow.color = shade.colorVal;
         } else break;
      }
      if (lengths < 2) return false;
      if (ttype != CSS_TK_CHAR || tval[0] != ',') return true;
      nextToken();
   } while (true);
}

bool CssParser::parseBackgroundSize(CssBackgroundSizes::Size *size)
{
   memset(size, 0, sizeof(*size));
   size->x = size->y = CSS_CREATE_LENGTH(0, CSS_LENGTH_TYPE_AUTO);
   if (ttype == CSS_TK_SYMBOL && (!dStrAsciiCasecmp(tval, "cover") || !dStrAsciiCasecmp(tval, "contain"))) {
      size->mode = !dStrAsciiCasecmp(tval, "cover") ? 1 : 2;
      nextToken(); return true;
   }
   for (unsigned n = 0; n < 2; ++n) {
      CssValueType type = CSS_TYPE_UNUSED;
      if (!tokenMatchesProperty(CSS_PROPERTY_WIDTH, &type)) return n != 0;
      CssPropertyValue length;
      bool calculated = ttype == CSS_TK_SYMBOL && !dStrAsciiCasecmp(tval, "calc");
      if (!parseValue(CSS_PROPERTY_WIDTH, type, &length) ||
          (!calculated && length.lenVal.type != CSS_LENGTH_TYPE_AUTO &&
           CSS_LENGTH_VALUE(length.lenVal) < 0)) return false;
      (n ? size->y : size->x) = length.lenVal;
   }
   return true;
}

int CssParser::backgroundBox()
{
   if (ttype != CSS_TK_SYMBOL) return -1;
   if (!dStrAsciiCasecmp(tval, "border-box")) return 0;
   if (!dStrAsciiCasecmp(tval, "padding-box")) return 1;
   if (!dStrAsciiCasecmp(tval, "content-box")) return 2;
   return -1;
}

bool CssParser::parseBackgroundImage(CssGradient *value, std::string *url)
{
   if (ttype == CSS_TK_SYMBOL && !dStrAsciiCasecmp(tval, "none")) {
      nextToken(); return true;
   }
   if (ttype == CSS_TK_SYMBOL && !dStrAsciiCasecmp(tval, "url")) {
      char *parsed = parseUrl();
      if (!parsed) return false;
      if (url) *url = parsed;
      dFree(parsed); nextToken(); return true;
   }
   if (ttype != CSS_TK_SYMBOL || (dStrAsciiCasecmp(tval, "linear-gradient") &&
       dStrAsciiCasecmp(tval, "repeating-linear-gradient"))) return false;
   return parseGradient(value);
}

bool CssParser::parseGradient(CssGradient *value)
{
   value->repeating = !dStrAsciiCasecmp(tval, "repeating-linear-gradient");
   value->angle = 180;
   nextToken();
   if (ttype != CSS_TK_CHAR || tval[0] != '(') return false;
   nextToken();
   bool direction = false;
   if (ttype == CSS_TK_SYMBOL && !dStrAsciiCasecmp(tval, "to")) {
      nextToken();
      while (ttype == CSS_TK_SYMBOL) {
         if (!dStrAsciiCasecmp(tval, "left") && !value->cornerX) value->cornerX = -1;
         else if (!dStrAsciiCasecmp(tval, "right") && !value->cornerX) value->cornerX = 1;
         else if (!dStrAsciiCasecmp(tval, "top") && !value->cornerY) value->cornerY = -1;
         else if (!dStrAsciiCasecmp(tval, "bottom") && !value->cornerY) value->cornerY = 1;
         else return false;
         direction = true; nextToken();
      }
      if (!direction) return false;
      if (!value->cornerX || !value->cornerY) {
         value->angle = value->cornerX ? (value->cornerX > 0 ? 90 : 270) : (value->cornerY > 0 ? 180 : 0);
         value->cornerX = value->cornerY = 0;
      }
   } else if (ttype == CSS_TK_DECINT || ttype == CSS_TK_FLOAT) {
      double angle = strtod(tval, NULL);
      nextToken();
      if (!spaceSeparated && ttype == CSS_TK_SYMBOL) {
         if (!dStrAsciiCasecmp(tval, "turn")) angle *= 360;
         else if (!dStrAsciiCasecmp(tval, "rad")) angle *= 180 / std::acos(-1.0);
         else if (!dStrAsciiCasecmp(tval, "grad")) angle *= .9;
         else if (dStrAsciiCasecmp(tval, "deg")) return false;
         nextToken();
      } else if (angle != 0) return false;
      if (!std::isfinite(angle)) return false;
      value->angle = std::fmod(angle, 360); direction = true;
   }
   if (direction) {
      if (ttype != CSS_TK_CHAR || tval[0] != ',') return false;
      nextToken();
   }
   while (value->count < CssGradient::MAX_STOPS) {
      CssGradient::Stop stop = {};
      if (ttype == CSS_TK_SYMBOL && !dStrAsciiCasecmp(tval, "currentColor")) {
         stop.currentColor = true; nextToken();
      } else if (!parseColor(&stop.rgba)) return false;
      for (int position = 0; position < 2; ++position) {
         bool hasPosition = ttype == CSS_TK_DECINT || ttype == CSS_TK_FLOAT ||
            (ttype == CSS_TK_SYMBOL && !dStrAsciiCasecmp(tval, "calc"));
         if (position && !hasPosition) break;
         if (hasPosition) {
            CssPropertyValue length;
            if (!parseValue(CSS_PROPERTY_LEFT, CSS_TYPE_SIGNED_LENGTH_PERCENTAGE, &length)) return false;
            stop.position = length.lenVal; stop.positioned = true;
         }
         if (value->count == CssGradient::MAX_STOPS) return false;
         value->stops[value->count++] = stop;
      }
      if (ttype == CSS_TK_CHAR && tval[0] == ')') {
         if (value->count < 2) return false;
         nextToken(); return true;
      }
      if (ttype != CSS_TK_CHAR || tval[0] != ',') return false;
      nextToken();
   }
   return false;
}

bool CssParser::parseValue(CssPropertyName prop,
                           CssValueType type,
                           CssPropertyValue *val)
{
   CssLengthType lentype;
   bool found, ret = false;
   float fval;
   int i, ival;
   Dstr *dstr;

   switch (type) {
   case CSS_TYPE_ENUM:
      if (ttype == CSS_TK_SYMBOL) {
         for (i = 0; Css_property_info[prop].enum_symbols[i]; i++)
            if (dStrAsciiCasecmp(tval,
                            Css_property_info[prop].enum_symbols[i]) == 0) {
               val->intVal = i;
               ret = true;
               break;
            }
         nextToken();
      }
      break;

   case CSS_TYPE_MULTI_ENUM:
      val->intVal = 0;
      ret = true;

      while (ttype == CSS_TK_SYMBOL) {
         if (dStrAsciiCasecmp(tval, "none") != 0) {
            for (i = 0, found = false;
                 !found && Css_property_info[prop].enum_symbols[i]; i++) {
               if (dStrAsciiCasecmp(tval,
                               Css_property_info[prop].enum_symbols[i]) == 0)
                  val->intVal |= (1 << i);
            }
         }
         nextToken();
      }
      break;

   case CSS_TYPE_LENGTH_PERCENTAGE:
   case CSS_TYPE_LENGTH_PERCENTAGE_NUMBER:
   case CSS_TYPE_LENGTH:
   case CSS_TYPE_SIGNED_LENGTH:
   case CSS_TYPE_SIGNED_LENGTH_PERCENTAGE:
      if (ttype == CSS_TK_SYMBOL && !dStrAsciiCasecmp(tval, "calc")) {
         nextToken();
         if (ttype != CSS_TK_CHAR || tval[0] != '(') break;
         nextToken();
         if (!parseCalcSum(&val->lenVal, 0) || ttype != CSS_TK_CHAR || tval[0] != ')') break;
         nextToken();
         ret = val->lenVal.type != CSS_LENGTH_TYPE_NONE || val->lenVal.f == 0;
         break;
      }
      if (ttype == CSS_TK_DECINT || ttype == CSS_TK_FLOAT) {
         fval = atof(tval);
         lentype = CSS_LENGTH_TYPE_NONE;

         nextToken();
         if (!spaceSeparated && ttype == CSS_TK_SYMBOL) {
            ret = true;

            if (dStrAsciiCasecmp(tval, "px") == 0) {
               lentype = CSS_LENGTH_TYPE_PX;
               nextToken();
            } else if (dStrAsciiCasecmp(tval, "mm") == 0) {
               lentype = CSS_LENGTH_TYPE_MM;
               nextToken();
            } else if (dStrAsciiCasecmp(tval, "cm") == 0) {
               lentype = CSS_LENGTH_TYPE_MM;
               fval *= 10;
               nextToken();
            } else if (dStrAsciiCasecmp(tval, "in") == 0) {
               lentype = CSS_LENGTH_TYPE_MM;
               fval *= 25.4;
               nextToken();
            } else if (dStrAsciiCasecmp(tval, "pt") == 0) {
               lentype = CSS_LENGTH_TYPE_MM;
               fval *= (25.4 / 72);
               nextToken();
            } else if (dStrAsciiCasecmp(tval, "pc") == 0) {
               lentype = CSS_LENGTH_TYPE_MM;
               fval *= (25.4 / 6);
               nextToken();
            } else if (dStrAsciiCasecmp(tval, "em") == 0) {
               lentype = CSS_LENGTH_TYPE_EM;
               nextToken();
            } else if (dStrAsciiCasecmp(tval, "ex") == 0) {
               lentype = CSS_LENGTH_TYPE_EX;
               nextToken();
            } else if (dStrAsciiCasecmp(tval, "ch") == 0) {
               lentype = CSS_LENGTH_TYPE_CH;
               nextToken();
            } else if (dStrAsciiCasecmp(tval, "rem") == 0) {
               lentype = CSS_LENGTH_TYPE_REM;
               nextToken();
            } else if (dStrAsciiCasecmp(tval, "vw") == 0) {
               lentype = CSS_LENGTH_TYPE_VW;
               nextToken();
            } else if (dStrAsciiCasecmp(tval, "vh") == 0) {
               lentype = CSS_LENGTH_TYPE_VH;
               nextToken();
            } else if (dStrAsciiCasecmp(tval, "vmin") == 0) {
               lentype = CSS_LENGTH_TYPE_VMIN;
               nextToken();
            } else if (dStrAsciiCasecmp(tval, "vmax") == 0) {
               lentype = CSS_LENGTH_TYPE_VMAX;
               nextToken();
            } else {
               ret = false;
            }
         } else if (!spaceSeparated &&
                    (type == CSS_TYPE_SIGNED_LENGTH_PERCENTAGE ||
                     type == CSS_TYPE_LENGTH_PERCENTAGE ||
                     type == CSS_TYPE_LENGTH_PERCENTAGE_NUMBER) &&
                    ttype == CSS_TK_CHAR &&
                    tval[0] == '%') {
            fval /= 100;
            lentype = CSS_LENGTH_TYPE_PERCENTAGE;
            ret = true;
            nextToken();
         }

         /* Allow numbers without unit only for 0 or
          * CSS_TYPE_LENGTH_PERCENTAGE_NUMBER
          */
         if (lentype == CSS_LENGTH_TYPE_NONE &&
            (type == CSS_TYPE_LENGTH_PERCENTAGE_NUMBER || fval == 0.0))
            ret = true;

         val->lenVal = CSS_CREATE_LENGTH(fval, lentype);
      }
      break;

   case CSS_TYPE_AUTO:
      assert (ttype == CSS_TK_SYMBOL && !dStrAsciiCasecmp(tval, "auto"));
      ret = true;
      val->lenVal = CSS_CREATE_LENGTH(0, CSS_LENGTH_TYPE_AUTO);
      nextToken();
      break;

   case CSS_TYPE_RGBA:
      val->colorVal.currentColor = ttype == CSS_TK_SYMBOL && !dStrAsciiCasecmp(tval, "currentColor");
      val->colorVal.rgba = 0;
      if (val->colorVal.currentColor) { nextToken(); ret = true; }
      else ret = parseColor(&val->colorVal.rgba);
      break;
   case CSS_TYPE_COLOR: {
      unsigned color;
      if (parseColor(&color) && (color & 255) == 255) {
         val->intVal = color >> 8; ret = true;
      }
      break;
   }

   case CSS_TYPE_STRING:
      if (ttype == CSS_TK_STRING) {
         val->strVal = dStrdup(tval);
         ret = true;
         nextToken();
      }
      break;

   case CSS_TYPE_SYMBOL:
      /* Read comma separated list of font family names */
      dstr = dStr_new("");
      while (ttype == CSS_TK_SYMBOL || ttype == CSS_TK_STRING ||
             (ttype == CSS_TK_CHAR && tval[0] == ',')) {
         if (spaceSeparated)
            dStr_append_c(dstr, ' ');
         dStr_append(dstr, tval);
         ret = true;
         nextToken();
      }

      if (ret) {
         val->strVal = dStrstrip(dstr->str);
         dStr_free(dstr, 0);
      } else {
         dStr_free(dstr, 1);
      }
      break;

   case CSS_TYPE_FONT_WEIGHT:
      ival = 0;
      if (ttype == CSS_TK_DECINT) {
         ival = strtol(tval, NULL, 10);
         if (ival < 100 || ival > 900)
            /* invalid */
            ival = 0;
      }

      if (ival != 0) {
         val->intVal = ival;
         ret = true;
         nextToken();
      }
      break;

   case CSS_TYPE_URI:
      if (ttype == CSS_TK_SYMBOL) {
         if (dStrAsciiCasecmp(tval, "url") == 0) {
            val->strVal = parseUrl();
            if (val->strVal)
               ret = true;
         } else if (dStrAsciiCasecmp(tval, "none") == 0) {
            val->strVal = NULL;
            ret = true;
         }
         nextToken();
      }
      break;

   case CSS_TYPE_TRANSFORM: {
      CssTransform transform;
      if (parseTransform(&transform)) {
         val->transformVal = (CssTransform *)dMalloc(sizeof(transform));
         *val->transformVal = transform; ret = true;
      }
      break;
   }
   case CSS_TYPE_BACKGROUND_IMAGES: {
      CssBackgroundImages images;
      for (;;) {
         CssGradient gradient = {};
         std::string url;
         if (images.layers.size() == CssBackgroundImages::MAX_LAYERS ||
             !parseBackgroundImage(&gradient, &url)) return false;
         images.layers.push_back(gradient);
         images.urls.push_back(url);
         images.isUrl.push_back(!url.empty());
         if (ttype != CSS_TK_CHAR || tval[0] != ',') break;
         nextToken();
      }
      val->imagesVal = new CssBackgroundImages(images); ret = true;
      break;
   }
   case CSS_TYPE_BACKGROUND_BOXES: {
      CssBackgroundBoxes boxes = {};
      for (;;) {
         int box = backgroundBox();
         if (box < 0 || boxes.count == CssBackgroundImages::MAX_LAYERS) return false;
         boxes.values[boxes.count++] = box; nextToken();
         if (ttype != CSS_TK_CHAR || tval[0] != ',') break;
         nextToken();
      }
      val->boxesVal = (CssBackgroundBoxes *)dMalloc(sizeof(boxes));
      *val->boxesVal = boxes; ret = true;
      break;
   }
   case CSS_TYPE_BACKGROUND_SIZES: {
      CssBackgroundSizes sizes = {};
      do {
         if (sizes.count == CssBackgroundImages::MAX_LAYERS ||
             !parseBackgroundSize(&sizes.values[sizes.count++])) return false;
         if (ttype != CSS_TK_CHAR || tval[0] != ',') break;
         nextToken();
      } while (true);
      val->sizesVal = (CssBackgroundSizes *)dMalloc(sizeof(sizes));
      *val->sizesVal = sizes; ret = true;
      break;
   }
   case CSS_TYPE_SHADOW: {
      CssShadows shadows = {};
      if (parseShadows(&shadows)) {
         val->shadowsVal = (CssShadows *)dMalloc(sizeof(shadows));
         *val->shadowsVal = shadows; ret = true;
      }
      break;
   }
   case CSS_TYPE_CORNER_RADIUS: {
      CssPropertyValue x, y;
      if (parseValue(prop, CSS_TYPE_LENGTH_PERCENTAGE, &x)) {
         y = x;
         if (ttype == CSS_TK_DECINT || ttype == CSS_TK_FLOAT ||
             (ttype == CSS_TK_SYMBOL && !dStrAsciiCasecmp(tval, "calc"))) {
            if (!parseValue(prop, CSS_TYPE_LENGTH_PERCENTAGE, &y)) break;
         }
         if ((x.lenVal.type != CSS_LENGTH_TYPE_CALC && CSS_LENGTH_VALUE(x.lenVal) < 0) ||
             (y.lenVal.type != CSS_LENGTH_TYPE_CALC && CSS_LENGTH_VALUE(y.lenVal) < 0)) break;
         val->posVal = (CssBackgroundPosition *)dMalloc(sizeof(CssBackgroundPosition));
         val->posVal->posX = x.lenVal; val->posVal->posY = y.lenVal; ret = true;
      }
      break;
   }

   case CSS_TYPE_BACKGROUND_POSITION:
      // 'background-position' consists of one or two values: vertical and
      // horizontal position; in most cases in this order. However, as long it
      // is unambiguous, the order can be switched: "10px left" and "left 10px"
      // are both possible and have the same effect. For this reason, all
      // possibilities are tested in parallel.

      bool h[2], v[2];
      CssLength pos[2];
      h[0] = v[0] = h[1] = v[1] = false;

      // First: collect values in pos[0] and pos[1], and determine whether
      // they can be used for a horizontal (h[i]) or vertical (v[i]) position
      // (or both). When neither h[i] or v[i] is set, pos[i] is undefined.
      for (i = 0; i < 2; i++) {
         CssValueType typeTmp = CSS_TYPE_UNUSED;
         // tokenMatchesProperty will, for CSS_PROPERTY_BACKGROUND_POSITION,
         // work on both parts, since they are exchangeable.
         if (tokenMatchesProperty (CSS_PROPERTY_BACKGROUND_POSITION,
                                   &typeTmp)) {
            h[i] = ttype != CSS_TK_SYMBOL ||
               (dStrAsciiCasecmp(tval, "top") != 0 &&
                dStrAsciiCasecmp(tval, "bottom") != 0);
            v[i] = ttype != CSS_TK_SYMBOL ||
               (dStrAsciiCasecmp(tval, "left") != 0 &&
                dStrAsciiCasecmp(tval, "right") != 0);
         } else
            // No match.
            h[i] = v[i] = false;

         if (h[i] || v[i]) {
            // Calculate values.
            if (ttype == CSS_TK_SYMBOL) {
               if (dStrAsciiCasecmp(tval, "top") == 0 ||
                   dStrAsciiCasecmp(tval, "left") == 0) {
                  pos[i] = CSS_CREATE_LENGTH (0.0, CSS_LENGTH_TYPE_PERCENTAGE);
                  nextToken();
               } else if (dStrAsciiCasecmp(tval, "center") == 0) {
                  pos[i] = CSS_CREATE_LENGTH (0.5, CSS_LENGTH_TYPE_PERCENTAGE);
                  nextToken();
               } else if (dStrAsciiCasecmp(tval, "bottom") == 0 ||
                          dStrAsciiCasecmp(tval, "right") == 0) {
                  pos[i] = CSS_CREATE_LENGTH (1.0, CSS_LENGTH_TYPE_PERCENTAGE);
                  nextToken();
               } else
                  // tokenMatchesProperty should have returned "false" already.
                  lout::misc::assertNotReached ();
            } else {
               // We can assume <length> or <percentage> here ...
               CssPropertyValue valTmp;
               if (parseValue(prop, CSS_TYPE_LENGTH_PERCENTAGE, &valTmp)) {
                  pos[i] = valTmp.lenVal;
                  ret = true;
               } else if (parseValue(prop, CSS_TYPE_SIGNED_LENGTH, &valTmp)) {
                  pos[i] = valTmp.lenVal;
                  ret = true;
               } else
                  // ... but something may still fail.
                  h[i] = v[i] = false;
            }
         }

         // If the first value cannot be read, do not read the second.
         if (!h[i] && !v[i])
            break;
      }

      // Second: Create the final value. Order will be determined here.
      if (v[0] || h[0]) {
         // If second value is not set, it is set to "center", i. e. 50%, (see
         // CSS specification), which is suitable for both dimensions.
         if (!h[1] && !v[1]) {
            pos[1] = CSS_CREATE_LENGTH (0.5, CSS_LENGTH_TYPE_PERCENTAGE);
            h[1] = v[1] = true;
         }

         // Only valid, when a combination h/v or v/h is possible.
         if ((h[0] && v[1]) || (v[0] && h[1])) {
            ret = true;
            val->posVal = dNew(CssBackgroundPosition, 1);

            // Prefer combination h/v:
            if (h[0] && v[1]) {
                val->posVal->posX = pos[0];
                val->posVal->posY = pos[1];
            } else {
               // This should be v/h:
                val->posVal->posX = pos[1];
                val->posVal->posY = pos[0];
            }
         }
      }
      break;

   case CSS_TYPE_INTEGER:
      if (ttype == CSS_TK_DECINT) {
         long value = strtol(tval, NULL, 10);
         val->intVal = value;
         ret = prop != CSS_PROPERTY_WEBKIT_LINE_CLAMP ||
            (value > 0 && value <= 100);
         nextToken();
      }
      break;

   case CSS_TYPE_UNUSED:
      /* nothing */
      break;

   default:
      assert(false);            /* not reached */
   }

   return ret;
}

bool CssParser::parseWeight()
{
   if (ttype == CSS_TK_CHAR && tval[0] == '!') {
      nextToken();
      if (ttype == CSS_TK_SYMBOL &&
          dStrAsciiCasecmp(tval, "important") == 0) {
         nextToken();
         return true;
      }
   }

   return false;
}

/**
 * bsearch(3) compare function for searching properties
 */
static int Css_property_info_cmp(const void *a, const void *b)
{
   return dStrAsciiCasecmp(((CssPropertyInfo *) a)->symbol,
                      ((CssPropertyInfo *) b)->symbol);
}


/**
 * bsearch(3) compare function for searching shorthands
 */
static int Css_shorthand_info_cmp(const void *a, const void *b)
{
   return dStrAsciiCasecmp(((CssShorthandInfo *) a)->symbol,
                      ((CssShorthandInfo *) b)->symbol);
}

static bool Css_decoration_property(CssPropertyName prop)
{
   return (prop >= CSS_PROPERTY_BACKGROUND_ATTACHMENT && prop <= CSS_PROPERTY_BACKGROUND_SIZE) ||
      (prop >= CSS_PROPERTY_OUTLINE_COLOR && prop <= CSS_PROPERTY_OUTLINE_WIDTH) ||
      prop == CSS_PROPERTY_BOX_SHADOW || prop == CSS_PROPERTY_TRANSFORM ||
      prop == CSS_PROPERTY_POINTER_EVENTS || prop == CSS_PROPERTY_VISIBILITY ||
      prop == CSS_PROPERTY_OVERFLOW_WRAP || prop == CSS_PROPERTY_WORD_BREAK ||
      prop == CSS_PROPERTY_WORD_WRAP ||
      prop == CSS_PROPERTY_BORDER_TOP_LEFT_RADIUS || prop == CSS_PROPERTY_BORDER_TOP_RIGHT_RADIUS ||
      prop == CSS_PROPERTY_BORDER_BOTTOM_LEFT_RADIUS || prop == CSS_PROPERTY_BORDER_BOTTOM_RIGHT_RADIUS;
}

void CssParser::parseDeclaration(CssPropertyList *props,
                                 CssPropertyList *importantProps)
{
   /* Keep token streams containing var() until custom properties have been
    * cascaded for this element. A shorthand reserves every affected longhand
    * so later declarations and !important still take precedence correctly. */
   if (ttype == CSS_TK_SYMBOL) {
      std::string name = tval;
      int colon = bufptr;
      while (colon < buflen && dIsspace(buf[colon])) ++colon;
      if (colon < buflen && buf[colon] == ':') {
         int end = colon + 1, nesting = 0, bang = -1;
         char quote = 0;
         for (; end < buflen; ++end) {
            char c = buf[end];
            if (c == '\\') { if (end + 1 < buflen) ++end; continue; }
            if (quote) { if (c == quote) quote = 0; continue; }
            if (c == '\'' || c == '"') { quote = c; continue; }
            if (c == '/' && end + 1 < buflen && buf[end + 1] == '*') {
               end += 2;
               while (end + 1 < buflen && !(buf[end] == '*' && buf[end + 1] == '/')) ++end;
               ++end; continue;
            }
            if (c == '(' || c == '[' || c == '{') ++nesting;
            else if ((c == ')' || c == ']' || c == '}') && nesting) --nesting;
            else if ((c == ';' || c == '}') && !nesting) break;
            else if (c == '!' && !nesting) bang = end;
         }
         std::string value(buf + colon + 1, end - colon - 1);
         bool custom = name.compare(0, 2, "--") == 0;
         bool components = name == "grid-template-columns" || name == "grid-template-rows";
         if (custom || components || value.find("var(") != std::string::npos) {
            bool important = false;
            if (bang >= 0) {
               std::string weight(buf + bang + 1, end - bang - 1);
               size_t first = weight.find_first_not_of(" \t\r\n\f"), last = weight.find_last_not_of(" \t\r\n\f");
               important = first != std::string::npos && !dStrAsciiCasecmp(weight.substr(first, last - first + 1).c_str(), "important");
               if (important) value.assign(buf + colon + 1, bang - colon - 1);
            }
            CssPropertyList *dest = important && importantProps ? importantProps : props;
            if (custom) dest->customProperties[name] = value;
            else {
               CssPropertyInfo key = {name.c_str(), {CSS_TYPE_UNUSED}, NULL};
               CssPropertyInfo *property = (CssPropertyInfo *)bsearch(&key, Css_property_info,
                  CSS_NUM_PARSED_PROPERTIES, sizeof(CssPropertyInfo), Css_property_info_cmp);
               CssShorthandInfo *shorthand = (CssShorthandInfo *)bsearch(&key, Css_shorthand_info,
                  CSS_SHORTHAND_NUM, sizeof(CssShorthandInfo), Css_shorthand_info_cmp);
               std::string declaration = name + ":" + value;
               CssPropertyValue deferred;
               if (property) {
                  deferred.strVal = dStrdup((components && value.find("var(") == std::string::npos ? value : declaration).c_str());
                  dest->set((CssPropertyName)(property - Css_property_info), components && value.find("var(") == std::string::npos ? CSS_TYPE_COMPONENTS : CSS_TYPE_DEFERRED, deferred);
               } else if (shorthand) for (int i = 0; shorthand->type == CssShorthandInfo::CSS_SHORTHAND_DIRECTIONS ? i < 4 : shorthand->properties[i] != CSS_PROPERTY_END; ++i) {
                  deferred.strVal = dStrdup(declaration.c_str());
                  dest->set(shorthand->properties[i], CSS_TYPE_DEFERRED, deferred);
               }
            }
            bufptr = end; nextToken();
            if (ttype == CSS_TK_CHAR && tval[0] == ';') nextToken();
            return;
         }
      }
   }
   CssPropertyInfo pi = {NULL, {CSS_TYPE_UNUSED}, NULL}, *pip;
   CssShorthandInfo *sip;
   CssValueType type = CSS_TYPE_UNUSED;

   CssPropertyName prop;
   CssPropertyValue val, dir_vals[4];
   CssValueType dir_types[4];
   bool found, weight;
   int sh_index, i, j, n;
   int dir_set[4][4] = {
      /* 1 value  */ {0, 0, 0, 0},
      /* 2 values */ {0, 0, 1, 1},
      /* 3 values */ {0, 2, 1, 1},
      /* 4 values */ {0, 2, 3, 1}
   };

   if (ttype == CSS_TK_SYMBOL) {
      pi.symbol = tval;
      pip =
          (CssPropertyInfo *) bsearch(&pi, Css_property_info,
                                      CSS_NUM_PARSED_PROPERTIES,
                                      sizeof(CssPropertyInfo),
                                      Css_property_info_cmp);
      if (pip) {
         prop = (CssPropertyName) (pip - Css_property_info);
         nextToken();
         if (ttype == CSS_TK_CHAR && tval[0] == ':') {
            nextToken();
            if (Css_decoration_property(prop) && ttype == CSS_TK_SYMBOL &&
                (!dStrAsciiCasecmp(tval, "initial") || !dStrAsciiCasecmp(tval, "inherit") ||
                 !dStrAsciiCasecmp(tval, "unset"))) {
               val.intVal = !dStrAsciiCasecmp(tval, "inherit") ? 1 : !dStrAsciiCasecmp(tval, "unset") ? 2 : 0;
               nextToken();
               if (ttype != CSS_TK_END && !(ttype == CSS_TK_CHAR &&
                   (tval[0] == ';' || tval[0] == '}' || tval[0] == '!'))) return;
               weight = parseWeight();
               (weight && importantProps ? importantProps : props)->set(prop, CSS_TYPE_WIDE_KEYWORD, val);
               return;
            }
            if (tokenMatchesProperty (prop, &type) &&
                parseValue(prop, type, &val)) {
               if (Css_decoration_property(prop) &&
                   ttype != CSS_TK_END && !(ttype == CSS_TK_CHAR &&
                   (tval[0] == ';' || tval[0] == '}' || tval[0] == '!'))) {
                  CssProperty invalid; invalid.type = type; invalid.value = val;
                  invalid.free();
                  return;
               }
               weight = parseWeight();
               if (weight && importantProps)
                  importantProps->set(prop, type, val);
               else
                  props->set(prop, type, val);
            }
         }
      } else {
         /* Try shorthands. */
         sip =
             (CssShorthandInfo *) bsearch(&pi, Css_shorthand_info,
                                          CSS_SHORTHAND_NUM,
                                          sizeof(CssShorthandInfo),
                                          Css_shorthand_info_cmp);
         if (sip) {
            sh_index = sip - Css_shorthand_info;
            nextToken();
            if (ttype == CSS_TK_CHAR && tval[0] == ':') {
               nextToken();

               if ((sip->type == CssShorthandInfo::CSS_SHORTHAND_BACKGROUND ||
                    sip->type == CssShorthandInfo::CSS_SHORTHAND_OUTLINE ||
                    sip->type == CssShorthandInfo::CSS_SHORTHAND_RADIUS) && ttype == CSS_TK_SYMBOL &&
                   (!dStrAsciiCasecmp(tval, "initial") || !dStrAsciiCasecmp(tval, "inherit") ||
                    !dStrAsciiCasecmp(tval, "unset"))) {
                  val.intVal = !dStrAsciiCasecmp(tval, "inherit") ? 1 : !dStrAsciiCasecmp(tval, "unset") ? 2 : 0;
                  nextToken();
                  if (ttype != CSS_TK_END && !(ttype == CSS_TK_CHAR &&
                      (tval[0] == ';' || tval[0] == '}' || tval[0] == '!'))) return;
                  weight = parseWeight();
                  for (int n = 0; sip->properties[n] != CSS_PROPERTY_END; ++n)
                     (weight && importantProps ? importantProps : props)->set(sip->properties[n], CSS_TYPE_WIDE_KEYWORD, val);
                  return;
               }

               switch (Css_shorthand_info[sh_index].type) {

               case CssShorthandInfo::CSS_SHORTHAND_OUTLINE: {
                  CssPropertyList parsed(true);
                  CssPropertyValue initial;
                  initial.colorVal.rgba = 0; initial.colorVal.currentColor = true;
                  parsed.set(CSS_PROPERTY_OUTLINE_COLOR, CSS_TYPE_RGBA, initial);
                  initial.intVal = 0;
                  parsed.set(CSS_PROPERTY_OUTLINE_STYLE, CSS_TYPE_ENUM, initial);
                  initial.intVal = 1;
                  parsed.set(CSS_PROPERTY_OUTLINE_WIDTH, CSS_TYPE_ENUM, initial);
                  bool seen[3] = {}, valid = true, any = false;
                  while (ttype != CSS_TK_END && !(ttype == CSS_TK_CHAR &&
                         (tval[0] == ';' || tval[0] == '}' || tval[0] == '!'))) {
                     bool matched = false;
                     for (unsigned n = 0; n < 3 && !matched; ++n) {
                        CssPropertyName name = Css_outline_properties[n];
                        if (!seen[n] && tokenMatchesProperty(name, &type)) {
                           matched = true;
                           CssPropertyValue part;
                           if (!parseValue(name, type, &part)) { valid = false; break; }
                           parsed.set(name, type, part); seen[n] = any = true;
                        }
                     }
                     if (!valid || !matched) { valid = false; break; }
                  }
                  if (valid && any) {
                     weight = parseWeight();
                     CssPropertyList *dest = weight && importantProps ? importantProps : props;
                     parsed.apply(dest);
                  }
                  break;
               }

               case CssShorthandInfo::CSS_SHORTHAND_BACKGROUND: {
                  CssPropertyList parsed(true);
                  CssPropertyValue initial;
                  initial.intVal = -1;
                  parsed.set(CSS_PROPERTY_BACKGROUND_COLOR, CSS_TYPE_COLOR, initial);
                  initial.intVal = 0;
                  parsed.set(CSS_PROPERTY_BACKGROUND_REPEAT, CSS_TYPE_ENUM, initial);
                  parsed.set(CSS_PROPERTY_BACKGROUND_ATTACHMENT, CSS_TYPE_ENUM, initial);
                  initial.posVal = (CssBackgroundPosition *)dMalloc(sizeof(CssBackgroundPosition));
                  initial.posVal->posX = initial.posVal->posY = CSS_CREATE_LENGTH(0, CSS_LENGTH_TYPE_PERCENTAGE);
                  parsed.set(CSS_PROPERTY_BACKGROUND_POSITION, CSS_TYPE_BACKGROUND_POSITION, initial);
                  CssBackgroundImages images;
                  CssBackgroundBoxes origins = {}, clips = {};
                  CssBackgroundSizes sizes = {};
                  bool valid = true, any = false, legacyPosition = false;
                  do {
                     CssGradient image = {};
                     CssBackgroundSizes::Size size = {};
                     size.x = size.y = CSS_CREATE_LENGTH(0, CSS_LENGTH_TYPE_AUTO);
                     bool seen[5] = {}, layer = false;
                     bool sizeSeen = false;
                     int origin = 1, clip = 0, boxes = 0;
                     while (ttype != CSS_TK_END && !(ttype == CSS_TK_CHAR &&
                            (tval[0] == ';' || tval[0] == '}' || tval[0] == '!' || tval[0] == ','))) {
                        if (ttype == CSS_TK_CHAR && tval[0] == '/') {
                           if (!seen[4] || sizeSeen) { valid = false; break; }
                           nextToken();
                           if (!parseBackgroundSize(&size)) { valid = false; break; }
                           sizeSeen = true; continue;
                        }
                        int box = backgroundBox();
                        if (box >= 0) {
                           if (++boxes > 2) { valid = false; break; }
                           if (boxes == 1) origin = clip = box;
                           else clip = box;
                           nextToken(); layer = true; continue;
                        }
                        bool matched = false;
                        for (unsigned n = 0; n < 5 && !matched; ++n) {
                           CssPropertyName name = Css_background_properties[n];
                           if (!seen[n] && tokenMatchesProperty(name, &type)) {
                              matched = true;
                              if (name == CSS_PROPERTY_BACKGROUND_IMAGE && type == CSS_TYPE_BACKGROUND_IMAGES) {
                                 std::string layerUrl;
                                 valid = parseBackgroundImage(&image, &layerUrl);
                                 images.urls.push_back(layerUrl);
                                 images.isUrl.push_back(!layerUrl.empty());
                              } else {
                                 CssPropertyValue part;
                                 valid = parseValue(name, type, &part);
                                 if (valid) parsed.set(name, type, part);
                                 if (n >= 2) legacyPosition = true;
                              }
                              seen[n] = layer = true;
                           }
                        }
                        if (!matched || !valid) { valid = false; break; }
                     }
                     if (!valid || !layer || images.layers.size() == CssBackgroundImages::MAX_LAYERS) {
                        valid = false; break;
                     }
                     any = true;
                     images.layers.push_back(image);
                     origins.values[origins.count++] = origin;
                     clips.values[clips.count++] = clip;
                     sizes.values[sizes.count++] = size;
                     if (ttype != CSS_TK_CHAR || tval[0] != ',') break;
                     if (seen[0]) { valid = false; break; } // color is allowed only in the last layer
                     nextToken();
                  } while (true);
                  // URL images and their positioning retain the existing single-layer path.
                  if (images.layers.size() > 1 && legacyPosition) valid = false;
                  bool bang = ttype == CSS_TK_CHAR && tval[0] == '!';
                  weight = parseWeight();
                  if (bang && !weight) valid = false;
                  if (ttype != CSS_TK_END && !(ttype == CSS_TK_CHAR && (tval[0] == ';' || tval[0] == '}'))) valid = false;
                  if (valid && any) {
                     CssPropertyValue part;
                     if (!images.layers.empty()) {
                        part.imagesVal = new CssBackgroundImages(images);
                        parsed.set(CSS_PROPERTY_BACKGROUND_IMAGE, CSS_TYPE_BACKGROUND_IMAGES, part);
                     }
                     part.boxesVal = (CssBackgroundBoxes *)dMalloc(sizeof(origins));
                     *part.boxesVal = origins;
                     parsed.set(CSS_PROPERTY_BACKGROUND_ORIGIN, CSS_TYPE_BACKGROUND_BOXES, part);
                     part.boxesVal = (CssBackgroundBoxes *)dMalloc(sizeof(clips));
                     *part.boxesVal = clips;
                     parsed.set(CSS_PROPERTY_BACKGROUND_CLIP, CSS_TYPE_BACKGROUND_BOXES, part);
                     part.sizesVal = (CssBackgroundSizes *)dMalloc(sizeof(sizes));
                     *part.sizesVal = sizes;
                     parsed.set(CSS_PROPERTY_BACKGROUND_SIZE, CSS_TYPE_BACKGROUND_SIZES, part);
                     CssPropertyList *dest = weight && importantProps ? importantProps : props;
                     parsed.apply(dest);
                  }
                  break;
               }

               case CssShorthandInfo::CSS_SHORTHAND_RADIUS: {
                  CssLength radii[2][4];
                  int count[2] = {0, 0}; bool valid = true;
                  for (int axis = 0; axis < 2; ++axis) {
                     while (count[axis] < 4 && (ttype == CSS_TK_DECINT || ttype == CSS_TK_FLOAT ||
                            (ttype == CSS_TK_SYMBOL && !dStrAsciiCasecmp(tval, "calc")))) {
                        CssPropertyValue part;
                        if (!parseValue(CSS_PROPERTY_BORDER_TOP_LEFT_RADIUS, CSS_TYPE_LENGTH_PERCENTAGE, &part) ||
                            (part.lenVal.type != CSS_LENGTH_TYPE_CALC && CSS_LENGTH_VALUE(part.lenVal) < 0)) { valid = false; break; }
                        radii[axis][count[axis]++] = part.lenVal;
                     }
                     if (!valid || !count[axis]) { valid = false; break; }
                     if (!axis) {
                        if (ttype == CSS_TK_CHAR && tval[0] == '/') nextToken();
                        else { count[1] = count[0]; for (int n = 0; n < count[0]; ++n) radii[1][n] = radii[0][n]; break; }
                     }
                  }
                  if (ttype != CSS_TK_END && !(ttype == CSS_TK_CHAR &&
                      (tval[0] == ';' || tval[0] == '}' || tval[0] == '!'))) valid = false;
                  if (valid) {
                     const int map[4][4] = {{0,0,0,0}, {0,1,0,1}, {0,1,2,1}, {0,1,2,3}};
                     weight = parseWeight();
                     for (int n = 0; n < 4; ++n) {
                        CssPropertyValue radius;
                        radius.posVal = (CssBackgroundPosition *)dMalloc(sizeof(CssBackgroundPosition));
                        radius.posVal->posX = radii[0][map[count[0]-1][n]];
                        radius.posVal->posY = radii[1][map[count[1]-1][n]];
                        (weight && importantProps ? importantProps : props)->set(Css_radius_properties[n], CSS_TYPE_CORNER_RADIUS, radius);
                     }
                  }
                  break;
               }

               case CssShorthandInfo::CSS_SHORTHAND_FLEX: {
                  CssPropertyValue flex[3];
                  flex[0].lenVal = CSS_CREATE_LENGTH(1, CSS_LENGTH_TYPE_NONE);
                  flex[1].lenVal = CSS_CREATE_LENGTH(1, CSS_LENGTH_TYPE_NONE);
                  flex[2].lenVal = CSS_CREATE_LENGTH(0, CSS_LENGTH_TYPE_PERCENTAGE);
                  bool valid = true;
                  if (ttype == CSS_TK_SYMBOL && (!strcmp(tval, "none") || !strcmp(tval, "auto") || !strcmp(tval, "initial"))) {
                     flex[0].lenVal.f = !strcmp(tval, "auto") ? 1 : 0;
                     flex[1].lenVal.f = !strcmp(tval, "none") ? 0 : 1;
                     flex[2].lenVal = CSS_CREATE_LENGTH(0, CSS_LENGTH_TYPE_AUTO); nextToken();
                  } else {
                     int numbers = 0; bool basis = false;
                     while (valid) {
                        if ((ttype == CSS_TK_DECINT || ttype == CSS_TK_FLOAT) && numbers < 2) {
                           CssPropertyValue part;
                           if (!parseValue(CSS_PROPERTY_FLEX_BASIS, CSS_TYPE_LENGTH_PERCENTAGE_NUMBER, &part)) { valid = false; break; }
                           if (part.lenVal.type == CSS_LENGTH_TYPE_NONE && part.lenVal.f >= 0) flex[numbers++] = part;
                           else if (!basis) { flex[2] = part; basis = true; }
                           else valid = false;
                        } else if (!basis && tokenMatchesProperty(CSS_PROPERTY_FLEX_BASIS, &type)) {
                           valid = parseValue(CSS_PROPERTY_FLEX_BASIS, type, &flex[2]); basis = true;
                        } else break;
                     }
                     if (!numbers && !basis) valid = false;
                  }
                  weight = parseWeight();
                  if (valid) for (int k = 0; k < 3; ++k)
                     (weight && importantProps ? importantProps : props)->set(Css_flex_properties[k],
                        k == 2 ? CSS_TYPE_LENGTH_PERCENTAGE : CSS_TYPE_LENGTH_PERCENTAGE_NUMBER, flex[k]);
                  break;
               }
               case CssShorthandInfo::CSS_SHORTHAND_FLOW: {
                  CssPropertyValue values[2]; values[0].intVal = 0; values[1].intVal = 0;
                  bool seen[2] = {false, false};
                  for (int k = 0; k < 2; ++k) for (int n = 0; n < 2; ++n)
                     if (!seen[n] && tokenMatchesProperty(Css_flow_properties[n], &type)) {
                        seen[n] = parseValue(Css_flow_properties[n], type, &values[n]); break;
                     }
                  weight = parseWeight();
                  if (seen[0] || seen[1]) for (int k = 0; k < 2; ++k)
                     (weight && importantProps ? importantProps : props)->set(Css_flow_properties[k], CSS_TYPE_ENUM, values[k]);
                  break;
               }
               case CssShorthandInfo::CSS_SHORTHAND_GAP: {
                  CssPropertyValue values[2];
                  if (parseValue(CSS_PROPERTY_ROW_GAP, CSS_TYPE_LENGTH, &values[0])) {
                     if (!parseValue(CSS_PROPERTY_COLUMN_GAP, CSS_TYPE_LENGTH, &values[1])) values[1] = values[0];
                     weight = parseWeight();
                     for (int k = 0; k < 2; ++k)
                        (weight && importantProps ? importantProps : props)->set(Css_gap_properties[k], CSS_TYPE_LENGTH, values[k]);
                  }
                  break;
               }

               case CssShorthandInfo::CSS_SHORTHAND_FONT: {
                  /* Parse the required font-size and font-family, with the
                   * optional style/variant/weight and line-height fields.
                   * A shorthand resets every omitted font longhand. */
                  CssPropertyValue fontValues[6] = {};
                  CssValueType fontTypes[6] = {
                     CSS_TYPE_ENUM, CSS_TYPE_ENUM, CSS_TYPE_ENUM,
                     CSS_TYPE_ENUM, CSS_TYPE_SYMBOL, CSS_TYPE_ENUM
                  };
                  bool seen[4] = {false, false, false, false};
                  bool valid = true, sizeSeen = false;
                  fontValues[0].intVal = CSS_FONT_SIZE_MEDIUM;
                  fontValues[1].intVal = 0; /* normal style */
                  fontValues[2].intVal = 0; /* normal variant */
                  fontValues[3].intVal = CSS_FONT_WEIGHT_NORMAL;
                  fontValues[5].intVal = 0; /* normal line-height */

                  while (valid && !sizeSeen) {
                     CssPropertyName optional[3] = {
                        CSS_PROPERTY_FONT_STYLE,
                        CSS_PROPERTY_FONT_VARIANT,
                        CSS_PROPERTY_FONT_WEIGHT
                     };
                     int slot[3] = {1, 2, 3};
                     bool matched = false;
                     for (int k = 0; k < 3 && !matched; ++k) {
                        if (!seen[slot[k]] && tokenMatchesProperty(optional[k], &type)) {
                           valid = parseValue(optional[k], type, &fontValues[slot[k]]);
                           if (valid) {
                              fontTypes[slot[k]] = type;
                              seen[slot[k]] = true;
                           }
                           matched = true;
                        }
                     }
                     if (matched) continue;

                     if (tokenMatchesProperty(CSS_PROPERTY_FONT_SIZE, &type)) {
                        valid = parseValue(CSS_PROPERTY_FONT_SIZE, type, &fontValues[0]);
                        fontTypes[0] = type;
                        sizeSeen = valid;
                     } else {
                        valid = false;
                     }
                  }

                  if (valid && ttype == CSS_TK_CHAR && tval[0] == '/') {
                     nextToken();
                     if (tokenMatchesProperty(CSS_PROPERTY_LINE_HEIGHT, &type)) {
                        valid = parseValue(CSS_PROPERTY_LINE_HEIGHT, type, &fontValues[5]);
                        fontTypes[5] = type;
                     } else {
                        valid = false;
                     }
                  }
                  if (valid && tokenMatchesProperty(CSS_PROPERTY_FONT_FAMILY, &type))
                     valid = parseValue(CSS_PROPERTY_FONT_FAMILY, type, &fontValues[4]);
                  else
                     valid = false;

                  if (valid && ttype != CSS_TK_END && !(ttype == CSS_TK_CHAR &&
                      (tval[0] == ';' || tval[0] == '}' || tval[0] == '!')))
                     valid = false;
                  weight = parseWeight();
                  if (weight && ttype != CSS_TK_END && !(ttype == CSS_TK_CHAR &&
                      (tval[0] == ';' || tval[0] == '}')))
                     valid = false;
                  if (valid) {
                     CssPropertyList *dest = weight && importantProps ? importantProps : props;
                     for (int k = 0; k < 6; ++k)
                        dest->set(Css_font_properties[k], fontTypes[k], fontValues[k]);
                  } else if (fontValues[4].strVal) {
                     dFree(fontValues[4].strVal);
                  }
                  break;
               }
               case CssShorthandInfo::CSS_SHORTHAND_MULTIPLE:
                  do {
                     for (found = false, i = 0;
                          !found &&
                          Css_shorthand_info[sh_index].properties[i] !=
                          CSS_PROPERTY_END;
                          i++)
                        if (tokenMatchesProperty(Css_shorthand_info[sh_index].
                                                 properties[i], &type)) {
                           found = true;
                           DEBUG_MSG(DEBUG_PARSE_LEVEL,
                                     "will assign to '%s'\n",
                                     Css_property_info
                                     [Css_shorthand_info[sh_index]
                                      .properties[i]].symbol);
                           if (parseValue(Css_shorthand_info[sh_index]
                                          .properties[i], type, &val)) {
                              weight = parseWeight();
                              if (weight && importantProps)
                                 importantProps->
                                     set(Css_shorthand_info[sh_index].
                                         properties[i], type, val);
                              else
                                 props->set(Css_shorthand_info[sh_index].
                                            properties[i], type, val);
                           }
                        }
                  } while (found);
                  break;

               case CssShorthandInfo::CSS_SHORTHAND_DIRECTIONS:
                  n = 0;
                  while (n < 4) {
                     if (tokenMatchesProperty(Css_shorthand_info[sh_index].
                                              properties[0], &type) &&
                         parseValue(Css_shorthand_info[sh_index]
                                    .properties[0], type, &val)) {
                        dir_vals[n] = val;
                        dir_types[n] = type;
                        n++;
                     } else
                        break;
                  }

                  weight = parseWeight();
                  if (n > 0) {
                     for (i = 0; i < 4; i++)
                        if (weight && importantProps)
                           importantProps->set(Css_shorthand_info[sh_index]
                                               .properties[i],
                                               dir_types[dir_set[n - 1][i]],
                                               dir_vals[dir_set[n - 1][i]]);
                        else
                           props->set(Css_shorthand_info[sh_index]
                                      .properties[i],
                                      dir_types[dir_set[n - 1][i]],
                                      dir_vals[dir_set[n - 1][i]]);
                  } else
                     MSG_CSS("no values for shorthand property '%s'\n",
                             Css_shorthand_info[sh_index].symbol);

                  break;

               case CssShorthandInfo::CSS_SHORTHAND_BORDER:
                  do {
                     for (found = false, i = 0;
                          !found && i < 3;
                          i++)
                        if (tokenMatchesProperty(Css_shorthand_info[sh_index].
                                                 properties[i], &type)) {
                           found = true;
                           if (parseValue(Css_shorthand_info[sh_index]
                                          .properties[i], type, &val)) {
                              weight = parseWeight();
                              for (j = 0; j < 4; j++)
                                 if (weight && importantProps)
                                    importantProps->
                                       set(Css_shorthand_info[sh_index].
                                          properties[j * 3 + i], type, val);
                                 else
                                    props->set(Css_shorthand_info[sh_index].
                                       properties[j * 3 + i], type, val);
                           }
                        }
                  } while (found);
                  break;
               }
            }
         }
      }
   }

   /* Skip all tokens until the expected end. */
   while (!(ttype == CSS_TK_END ||
            (ttype == CSS_TK_CHAR &&
             (tval[0] == ';' || tval[0] == '}'))))
      nextToken();

   if (ttype == CSS_TK_CHAR && tval[0] == ';')
      nextToken();
}

bool CssParser::parseAttributeSelector(CssSimpleSelector *selector)
{
   nextToken();
   if (ttype != CSS_TK_SYMBOL)
      return false;
   char *name = dStrdup(tval), *value = NULL;
   char op = 0;
   /* HTML enumerated attributes use ASCII case-insensitive values by default.
    * Class, ID, data-* and arbitrary attribute values remain case-sensitive. */
   const char *folded[] = {"type", "dir", "lang", "rel", "align", "valign",
      "method", "enctype", "charset", "http-equiv", "media", NULL};
   bool insensitive = false, valid = true;
   for (int i = 0; folded[i]; i++)
      if (!dStrAsciiCasecmp(name, folded[i])) insensitive = true;
   nextToken();
   if (ttype == CSS_TK_CHAR && tval[0] != ']') {
      op = tval[0];
      if (!strchr("=~|^$*", op))
         valid = false;
      nextToken();
      if (op != '=') {
         if (ttype != CSS_TK_CHAR || tval[0] != '=' || spaceSeparated)
            valid = false;
         nextToken();
      }
      if (ttype != CSS_TK_STRING && ttype != CSS_TK_SYMBOL)
         valid = false;
      value = dStrdup(tval);
      nextToken();
      if (ttype == CSS_TK_SYMBOL && spaceSeparated) {
         if (!dStrAsciiCasecmp(tval, "i"))
            insensitive = true;
         else if (!dStrAsciiCasecmp(tval, "s"))
            insensitive = false;
         else valid = false;
         nextToken();
      }
   }
   valid = valid && ttype == CSS_TK_CHAR && tval[0] == ']';
   if (valid) {
      selector->addAttribute(name, op, value ? value : "", insensitive);
      nextToken();
   }
   dFree(name);
   dFree(value);
   return valid;
}

/* Parse the Selectors Level 3 an+b syntax, without integer overflow. */
static bool Css_parse_nth(const char *text, int *a, int *b)
{
   if (!dStrAsciiCasecmp(text, "odd")) { *a = 2; *b = 1; return true; }
   if (!dStrAsciiCasecmp(text, "even")) { *a = 2; *b = 0; return true; }
   char *end;
   const char *n = strchr(text, 'n');
   if (!n) n = strchr(text, 'N');
   long coefficient = 0, offset;
   if (n) {
      if (n == text || (n == text + 1 && *text == '+')) coefficient = 1;
      else if (n == text + 1 && *text == '-') coefficient = -1;
      else {
         coefficient = strtol(text, &end, 10);
         if (end != n) return false;
      }
      text = n + 1;
      while (dIsspace(*text)) text++;
      if (!*text) { offset = 0; }
      else {
         if (*text != '+' && *text != '-') return false;
         int sign = *text++ == '-' ? -1 : 1;
         while (dIsspace(*text)) text++;
         if (!dIsdigit(*text)) return false;
         offset = strtol(text, &end, 10);
         if (*end || offset > INT_MAX) return false;
         offset *= sign;
      }
   } else {
      offset = strtol(text, &end, 10);
      if (end == text || *end) return false;
   }
   if (coefficient < INT_MIN || coefficient > INT_MAX ||
       offset < INT_MIN || offset > INT_MAX)
      return false;
   *a = coefficient; *b = offset;
   return true;
}

bool CssParser::parsePseudoSelector(CssSimpleSelector *selector, bool negation)
{
   nextToken();
   if (ttype != CSS_TK_SYMBOL || spaceSeparated)
      return false;
   char name[maxStrLen];
   strcpy(name, tval);
   nextToken();
   if (!dStrAsciiCasecmp(name, "not")) {
      if (negation || ttype != CSS_TK_CHAR || tval[0] != '(' || spaceSeparated)
         return false;
      nextToken();
      CssSimpleSelector *argument = new CssSimpleSelector;
      if (!parseSimpleSelector(argument, true) ||
          ttype != CSS_TK_CHAR || tval[0] != ')') {
         delete argument;
         return false;
      }
      selector->addNegation(argument);
      nextToken();
   } else if (!dStrAsciiCasecmp(name, "nth-child") ||
              !dStrAsciiCasecmp(name, "nth-of-type") ||
              !dStrAsciiCasecmp(name, "nth-last-child") ||
              !dStrAsciiCasecmp(name, "nth-last-of-type")) {
      if (ttype != CSS_TK_CHAR || tval[0] != '(' || spaceSeparated)
         return false;
      Dstr *formula = dStr_new("");
      nextToken();
      while (ttype != CSS_TK_END && !(ttype == CSS_TK_CHAR && tval[0] == ')')) {
         if (ttype == CSS_TK_CHAR && (tval[0] == '{' || tval[0] == '}'))
            break;
         if (spaceSeparated && formula->len) dStr_append_c(formula, ' ');
         dStr_append(formula, tval);
         nextToken();
      }
      int a, b;
      bool valid = ttype == CSS_TK_CHAR && tval[0] == ')' &&
                   Css_parse_nth(formula->str, &a, &b);
      dStr_free(formula, true);
      if (!valid) return false;
      char structural = !dStrAsciiCasecmp(name, "nth-child") ? 'c' :
         !dStrAsciiCasecmp(name, "nth-of-type") ? 't' :
         !dStrAsciiCasecmp(name, "nth-last-child") ? 'C' : 'T';
      selector->addStructural(structural, a, b);
      nextToken();
   } else if (!dStrAsciiCasecmp(name, "first-child")) {
      selector->addStructural('c');
   } else if (!dStrAsciiCasecmp(name, "first-of-type")) {
      selector->addStructural('t');
   } else if (!dStrAsciiCasecmp(name, "last-child")) {
      selector->addStructural('C');
   } else if (!dStrAsciiCasecmp(name, "last-of-type")) {
      selector->addStructural('T');
   } else if (!dStrAsciiCasecmp(name, "only-child")) {
      selector->addStructural('c');
      selector->addStructural('C');
   } else if (!dStrAsciiCasecmp(name, "only-of-type")) {
      selector->addStructural('t');
      selector->addStructural('T');
   } else if (!dStrAsciiCasecmp(name, "root")) {
      selector->addStructural('r');
   } else if (!dStrAsciiCasecmp(name, "link") ||
              !dStrAsciiCasecmp(name, "visited")) {
      if (selector->getPseudoClass()) return false;
      selector->setSelect(CssSimpleSelector::SELECT_PSEUDO_CLASS, name);
   } else {
      return false;
   }
   return true;
}

bool CssParser::parseSimpleSelector(CssSimpleSelector *selector, bool negation)
{
   bool found = false;
   if (ttype == CSS_TK_SYMBOL) {
      selector->setElement(a_Html_tag_index(tval));
      nextToken();
      found = true;
   } else if (ttype == CSS_TK_CHAR && tval[0] == '*') {
      nextToken();
      found = true;
   }
   while (!(found && spaceSeparated) && ttype == CSS_TK_CHAR) {
      char kind = tval[0];
      if (kind == '#' || kind == '.') {
         nextToken();
         if (ttype != CSS_TK_SYMBOL || spaceSeparated)
            return false;
         selector->setSelect(kind == '#' ? CssSimpleSelector::SELECT_ID :
                                          CssSimpleSelector::SELECT_CLASS, tval);
         nextToken();
      } else if (kind == '[') {
         if (!parseAttributeSelector(selector)) return false;
      } else if (kind == ':') {
         if (!parsePseudoSelector(selector, negation)) return false;
      } else {
         break;
      }
      found = true;
   }
   return found;
}

CssSelector *CssParser::parseSelector()
{
   CssSelector *selector = new CssSelector ();

   while (true) {
      if (! parseSimpleSelector (selector->top ())) {
         delete selector;
         selector = NULL;
         break;
      }

      if (ttype == CSS_TK_CHAR &&
         (tval[0] == ',' || tval[0] == '{')) {
         break;
      } else if (ttype == CSS_TK_CHAR && tval[0] == '>') {
         selector->addSimpleSelector (CssSelector::COMB_CHILD);
         nextToken();
      } else if (ttype == CSS_TK_CHAR && tval[0] == '~') {
         selector->addSimpleSelector(CssSelector::COMB_GENERAL_SIBLING);
         nextToken();
      } else if (ttype == CSS_TK_CHAR && tval[0] == '+') {
         selector->addSimpleSelector (CssSelector::COMB_ADJACENT_SIBLING);
         nextToken();
      } else if (ttype != CSS_TK_END && spaceSeparated) {
         selector->addSimpleSelector (CssSelector::COMB_DESCENDANT);
      } else {
         delete selector;
         selector = NULL;
         break;
      }
   }

   while (ttype != CSS_TK_END &&
          (ttype != CSS_TK_CHAR ||
           (tval[0] != ',' && tval[0] != '{')))
         nextToken();

   return selector;
}

void CssParser::parseRuleset()
{
   lout::misc::SimpleVector < CssSelector * >*list;
   CssPropertyList *props, *importantProps;
   CssSelector *selector;

   list = new lout::misc::SimpleVector < CssSelector * >(1);

   while (true) {
      selector = parseSelector();

      if (selector) {
         selector->ref();
         list->increase();
         list->set(list->size() - 1, selector);
      }

      // \todo dump whole ruleset in case of parse error as required by CSS 2.1
      //       however make sure we don't dump it if only dillo fails to parse
      //       valid CSS.

      if (ttype == CSS_TK_CHAR && tval[0] == ',')
         /* To read the next token. */
         nextToken();
      else
         /* No more selectors. */
         break;
   }

   DEBUG_MSG(DEBUG_PARSE_LEVEL, "end of %s\n", "selectors");

   props = new CssPropertyList(true);
   props->ref();
   importantProps = new CssPropertyList(true);
   importantProps->ref();

   /* Read block. ('{' has already been read.) */
   if (ttype != CSS_TK_END) {
      withinBlock = true;
      nextToken();
      do
         parseDeclaration(props, importantProps);
      while (!(ttype == CSS_TK_END ||
               (ttype == CSS_TK_CHAR && tval[0] == '}')));
      withinBlock = false;
   }

   for (int i = 0; i < list->size(); i++) {
      CssSelector *s = list->get(i);

      if (origin == CSS_ORIGIN_USER_AGENT) {
         context->addRule(s, props, CSS_PRIMARY_USER_AGENT);
      } else if (origin == CSS_ORIGIN_USER) {
         context->addRule(s, props, CSS_PRIMARY_USER);
         context->addRule(s, importantProps, CSS_PRIMARY_USER_IMPORTANT);
      } else if (origin == CSS_ORIGIN_AUTHOR) {
         context->addRule(s, props, CSS_PRIMARY_AUTHOR);
         context->addRule(s, importantProps, CSS_PRIMARY_AUTHOR_IMPORTANT);
      }

      s->unref();
   }

   props->unref();
   importantProps->unref();

   delete list;

   if (ttype == CSS_TK_CHAR && tval[0] == '}')
      nextToken();
}

char * CssParser::parseUrl()
{
   Dstr *urlStr = NULL;

   if (ttype != CSS_TK_SYMBOL ||
      dStrAsciiCasecmp(tval, "url") != 0)
      return NULL;

   nextToken();

   if (ttype != CSS_TK_CHAR || tval[0] != '(')
      return NULL;

   nextToken();

   if (ttype == CSS_TK_STRING) {
      urlStr = dStr_new(tval);
      nextToken();
   } else {
      urlStr = dStr_new("");
      while (ttype != CSS_TK_END &&
             (ttype != CSS_TK_CHAR || tval[0] != ')')) {
         dStr_append(urlStr, tval);
         nextToken();
      }
   }

   if (ttype != CSS_TK_CHAR || tval[0] != ')') {
      dStr_free(urlStr, 1);
      urlStr = NULL;
   }

   if (urlStr) {
      DilloUrl *dilloUrl = a_Url_new(urlStr->str, a_Url_str(this->baseUrl));
      char *url = dStrdup(a_Url_str(dilloUrl));
      a_Url_free(dilloUrl);
      dStr_free(urlStr, 1);
      return url;
   } else {
      return NULL;
   }
}

void CssParser::parseImport(DilloHtml *html)
{
   char *urlStr = NULL;
   bool importSyntaxIsOK = false;
   bool mediaSyntaxIsOK = true;
   bool mediaIsSelected = true;

   nextToken();

   if (ttype == CSS_TK_SYMBOL &&
       dStrAsciiCasecmp(tval, "url") == 0)
      urlStr = parseUrl();
   else if (ttype == CSS_TK_STRING)
      urlStr = dStrdup (tval);

   nextToken();

   char *media = mediaList(';');
   mediaSyntaxIsOK = ttype == CSS_TK_CHAR && tval[0] == ';';
   mediaIsSelected = context->mediaMatches(media);
   dFree(media);
   importSyntaxIsOK = mediaSyntaxIsOK;
   if (mediaSyntaxIsOK) nextToken();
   else ignoreStatement();

   if (urlStr) {
      if (importSyntaxIsOK && mediaIsSelected) {
         MSG("CssParser::parseImport(): @import %s\n", urlStr);
         DilloUrl *url = a_Html_url_new (html, urlStr, a_Url_str(this->baseUrl),
                                         this->baseUrl ? 1 : 0);
         a_Html_load_stylesheet(html, url);
         a_Url_free(url);
      }
      dFree (urlStr);
   }
}

char *CssParser::mediaList(char delimiter)
{
   Dstr *query = dStr_new("");
   while (ttype != CSS_TK_END &&
          !(ttype == CSS_TK_CHAR && tval[0] == delimiter)) {
      if (ttype == CSS_TK_CHAR && (tval[0] == '{' || tval[0] == '}'))
         break;
      if (spaceSeparated && query->len) dStr_append_c(query, ' ');
      dStr_append(query, tval);
      nextToken();
   }
   char *text = query->str;
   dStr_free(query, false);
   return text;
}

void CssParser::parseMedia(DilloHtml *html, int depth)
{
   nextToken();
   char *query = mediaList('{');
   bool selected = context->mediaMatches(query);
   dFree(query);
   if (ttype != CSS_TK_CHAR || tval[0] != '{') {
      ignoreStatement();
      return;
   }
   if (selected && depth < 32) {
      nextToken();
      parseRules(html, true, depth + 1);
   } else {
      ignoreBlock();
   }
}

static std::string trimCssCondition(const std::string &text)
{
   size_t first = text.find_first_not_of(" \t\r\n\f");
   if (first == std::string::npos) return "";
   size_t last = text.find_last_not_of(" \t\r\n\f");
   return text.substr(first, last - first + 1);
}

static std::vector<std::string> splitCssCondition(const std::string &text,
                                                  const char *operatorText)
{
   std::vector<std::string> parts;
   size_t width = strlen(operatorText), start = 0;
   int depth = 0;
   char quote = 0;
   for (size_t i = 0; i + width <= text.size(); ++i) {
      char c = text[i];
      if (quote) {
         if (c == quote && (i == 0 || text[i - 1] != '\\')) quote = 0;
         continue;
      }
      if (c == '\'' || c == '"') { quote = c; continue; }
      if (c == '(') ++depth;
      else if (c == ')' && depth > 0) --depth;
      if (depth == 0 && !dStrAsciiCasecmp(text.substr(i, width).c_str(), operatorText) &&
          (i == 0 || dIsspace(text[i - 1])) &&
          (i + width == text.size() || dIsspace(text[i + width]))) {
         parts.push_back(trimCssCondition(text.substr(start, i - start)));
         i += width - 1;
         start = i + 1;
      }
   }
   if (parts.empty()) return parts;
   parts.push_back(trimCssCondition(text.substr(start)));
   return parts;
}

static bool cssConditionOuterParens(const std::string &text)
{
   if (text.size() < 2 || text[0] != '(' || text[text.size() - 1] != ')')
      return false;
   int depth = 0;
   char quote = 0;
   for (size_t i = 0; i < text.size(); ++i) {
      char c = text[i];
      if (quote) {
         if (c == quote && (i == 0 || text[i - 1] != '\\')) quote = 0;
         continue;
      }
      if (c == '\'' || c == '"') { quote = c; continue; }
      if (c == '(') ++depth;
      else if (c == ')' && --depth == 0) return i == text.size() - 1;
   }
   return false;
}

bool CssParser::supports(const char *condition, unsigned nesting)
{
   std::string text = trimCssCondition(condition ? condition : "");
   if (nesting > 64 || text.empty() || text.size() > 65536) return false;
   std::vector<std::string> parts = splitCssCondition(text, "or");
   if (!parts.empty()) {
      for (size_t i = 0; i < parts.size(); ++i)
         if (supports(parts[i].c_str(), nesting + 1)) return true;
      return false;
   }
   parts = splitCssCondition(text, "and");
   if (!parts.empty()) {
      for (size_t i = 0; i < parts.size(); ++i)
         if (!supports(parts[i].c_str(), nesting + 1)) return false;
      return true;
   }
   if (text.size() >= 4 && !dStrAsciiCasecmp(text.substr(0, 4).c_str(), "not "))
      return !supports(trimCssCondition(text.substr(4)).c_str(), nesting + 1);
   if (cssConditionOuterParens(text))
      return supports(text.substr(1, text.size() - 2).c_str(), nesting + 1);
   if (text.compare(0, 9, "selector(") == 0) return false;

   int depth = 0;
   char quote = 0;
   size_t colon = std::string::npos;
   for (size_t i = 0; i < text.size(); ++i) {
      char c = text[i];
      if (quote) {
         if (c == quote && (i == 0 || text[i - 1] != '\\')) quote = 0;
         continue;
      }
      if (c == '\'' || c == '"') { quote = c; continue; }
      if (c == '(') ++depth;
      else if (c == ')' && depth > 0) --depth;
      else if (c == ':' && depth == 0) { colon = i; break; }
   }
   if (colon == std::string::npos) return false;
   std::string declaration = trimCssCondition(text.substr(0, colon)) + ":" +
      trimCssCondition(text.substr(colon + 1));
   CssPropertyList normal(true), important(true);
   parseDeclarationBlock(NULL, declaration.c_str(), declaration.size(),
                         &normal, &important);
   return normal.size() + important.size() > 0 ||
      normal.customProperties.size() + important.customProperties.size() > 0;
}

void CssParser::parseSupports(DilloHtml *html, int depth)
{
   nextToken();
   char *condition = mediaList('{');
   bool selected = supports(condition);
   dFree(condition);
   if (ttype != CSS_TK_CHAR || tval[0] != '{') {
      ignoreStatement();
      return;
   }
   if (selected && depth < 32) {
      nextToken();
      parseRules(html, true, depth + 1);
   } else {
      ignoreBlock();
   }
}

const char * CssParser::propertyNameString(CssPropertyName name)
{
   return Css_property_info[name].symbol;
}

void CssParser::ignoreBlock()
{
   int depth = 0;

   while (ttype != CSS_TK_END) {
      if (ttype == CSS_TK_CHAR) {
         if (tval[0] == '{') {
            depth++;
         } else if (tval[0] == '}') {
            depth--;
            if (depth == 0) {
               nextToken();
               return;
            }
         }
      }
      nextToken();
   }
}

void CssParser::ignoreStatement()
{
   while (ttype != CSS_TK_END) {
      if (ttype == CSS_TK_CHAR) {
         if (tval[0] == ';') {
            nextToken();
            return;
         } else if (tval[0] =='{') {
            ignoreBlock();
            return;
         }
      }
      nextToken();
   }
}

void CssParser::parseRules(DilloHtml *html, bool block, int depth)
{
   bool importsAreAllowed = !block;
   while (ttype != CSS_TK_END) {
      if (ttype == CSS_TK_CHAR && tval[0] == '}') {
         nextToken();
         if (block) return;
         continue;
      }
      if (ttype == CSS_TK_CHAR && tval[0] == '@') {
         nextToken();
         if (ttype == CSS_TK_SYMBOL &&
             !dStrAsciiCasecmp(tval, "import") && html && importsAreAllowed) {
            parseImport(html);
         } else if (ttype == CSS_TK_SYMBOL && !dStrAsciiCasecmp(tval, "media")) {
            importsAreAllowed = false;
            parseMedia(html, depth);
         } else if (ttype == CSS_TK_SYMBOL && !dStrAsciiCasecmp(tval, "supports")) {
            importsAreAllowed = false;
            parseSupports(html, depth);
         } else {
            importsAreAllowed = false;
            ignoreStatement();
         }
      } else {
         importsAreAllowed = false;
         parseRuleset();
      }
   }
}

void CssParser::parse(DilloHtml *html, const DilloUrl *baseUrl,
                      CssContext *context, const char *buf, int buflen,
                      CssOrigin origin)
{
   CssParser parser(context, origin, baseUrl, buf, buflen);
   parser.parseRules(html, false, 0);
}

void CssParser::parseDeclarationBlock(const DilloUrl *baseUrl,
                                      const char *buf, int buflen,
                                      CssPropertyList *props,
                                      CssPropertyList *propsImortant)
{
   CssParser parser (NULL, CSS_ORIGIN_AUTHOR, baseUrl, buf, buflen);

   parser.withinBlock = true;

   do
      parser.parseDeclaration(props, propsImortant);
   while (!(parser.ttype == CSS_TK_END ||
         (parser.ttype == CSS_TK_CHAR && parser.tval[0] == '}')));
}
