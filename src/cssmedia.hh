/* Media Queries Level 3 evaluation, in CSS pixels. */
#ifndef DILLO_CSSMEDIA_HH
#define DILLO_CSSMEDIA_HH

struct CssMediaEnvironment {
   double width, height, dpi;
};

bool a_Css_media_matches(const char *query, const CssMediaEnvironment &env);

#endif
