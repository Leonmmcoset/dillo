/*
 * File: about.c
 *
 * Copyright (C) 1999-2007 Jorge Arellano Cid <jcid@dillo.org>
 * Copyright (C) 2024-2025 Rodrigo Arias Mallo <rodarima@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include <config.h>
#include "IO.h"
#include "dlib/i18n.h"

/**
 * HTML text for startup screen
 */
static const char splash_template[] =
"<!DOCTYPE HTML>\n"
"<html>\n"
"<head>\n"
"  <meta charset=\"utf-8\">\n"
"  <title>%s</title>\n"
"  <style>\n"
"    body {\n"
"      background: white;\n"
"      margin: 3em;\n"
"      font-size: 16px;\n"
"      font-family: sans-serif;\n"
"      line-height: 1.4em;\n"
"    }\n"
"    .main { max-width: 40em; }\n"
"    p { margin-top: 1em; }\n"
"    ul { margin-left: 1em; }\n"
"    li { margin-top: 0.5em; }\n"
"    kbd {\n"
"      display: inline-block;\n"
"      border: solid 1px #999;\n"
"      padding: 1px 3px;\n"
"    }\n"
"  </style>\n"
"</head>\n"
"<body>\n"
"<div class=\"main\">\n"
"\n"
"<h1>%s</h1>\n"
"\n"
"<p>%s</p>\n"
"\n"
"<ul>\n"
"  <li>%s</li>\n"
"  <li>%s</li>\n"
"  <li>%s</li>\n"
"  <li>%s</li>\n"
"</ul>\n"
"\n"
"<p>%s</p>\n"
"\n"
"</div>\n"
"</body>\n"
"</html>\n";


Dstr *a_About_splash(void)
{
   Dstr *page = dStr_new("");
   Dstr *welcome = dStr_new("");

   dStr_sprintf(welcome, _("Welcome to Dillo %s, a small and fast graphical "
      "web browser. To access the help click the question mark button "
      "<code><kbd>?</kbd></code> in the top right corner at any time. "
      "Here are some tips to get you started:"), VERSION);
   dStr_sprintf(page, splash_template,
      _("Quickstart"),
      _("Quickstart"),
      welcome->str,
      _("The main configuration file is at <code>~/.dillo/dillorc</code>."),
      _("Most actions can also be done by using the "
        "<a href='about:keys'>keyboard</a>."),
      _("Cookies are <em>disabled by default</em>."),
      _("Several Dillo plugins are available."),
      _("See more details in the "
        "<a href=\"https://dillo-browser.org/\">Dillo website</a>."));
   dStr_free(welcome, 1);
   return page;
}
