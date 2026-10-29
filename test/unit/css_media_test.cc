/* Media query matching and malformed-input regression tests. */
#include "src/cssmedia.hh"
#include <stdio.h>
#include <stdlib.h>

int main()
{
   const CssMediaEnvironment screen = {800, 600, 96};
   const char *yes[] = {
      "", "all", "screen", "SCREEN", "only screen", "not print",
      "print, screen", "(width:800px)", "(min-width: 800px)",
      "(max-width:800px)", "(min-height: 600px)", "(height:600px)",
      "screen and (min-width:600px) and (max-height:600px)",
      "(orientation: landscape)", "(min-width:50em)", "(width:50rem)",
      "(width: 600pt)", "(width: 8.333333333333333in)",
      "(aspect-ratio:4/3)", "(min-aspect-ratio: 1/1)",
      "(max-aspect-ratio:16/9)", "(resolution:96dpi)",
      "(resolution:1dppx)", "(min-resolution:30dpcm)",
      "(color)", "(color:8)", "(min-color:8)",
      "not screen and (max-width: 500px)", "not all and (unknown: value)",
      "screen /* comment, ( */ and (width: 800px)",
      "nonsense, (min-width: 700px)", "(width)"
   };
   const char *no[] = {
      "print", "not screen", "only print", "screen and (min-width:801px)",
      "(max-width:799px)", "(min-height:601px)", "(max-height:599px)",
      "(orientation:portrait)", "(unknown: value)", "(unknown)",
      "(width:50%)", "(width:800)", "(width:-1px)", "(width:nan)",
      "(width:1e999px)", "(width:)", "(width: 800px garbage)",
      "(resolution:0dpi)", "(aspect-ratio:4/0)", "(aspect-ratio:4.5/3)",
      "(color-index)", "(monochrome)", "(min-width)", "(color:8.5)",
      "screen and", "screen and ()", "screen (width:800px)",
      "screen or (width:800px)", "not", "only", "screen and and (color)",
      "screen and (width:800px", "screen)", "/* unfinished", "(())"
   };
   for (unsigned int i = 0; i < sizeof(yes) / sizeof(yes[0]); i++) {
      if (!a_Css_media_matches(yes[i], screen)) {
         fprintf(stderr, "Query should match: %s\n", yes[i]); return 1;
      }
   }
   for (unsigned int i = 0; i < sizeof(no) / sizeof(no[0]); i++) {
      if (a_Css_media_matches(no[i], screen)) {
         fprintf(stderr, "Query should not match: %s\n", no[i]); return 1;
      }
   }
   const CssMediaEnvironment portrait = {600, 800, 192};
   if (!a_Css_media_matches("(orientation:portrait) and (resolution:2dppx)", portrait) ||
       a_Css_media_matches("(min-width:700px)", portrait)) return 1;
   puts("Media types, features, query lists, units and invalid input passed.");
   return 0;
}
