/*
 * Media query evaluation for screen stylesheets.
 * Licensed under the GNU GPL, version 3 or later.
 */
#include "cssmedia.hh"
#include "dlib/dlib.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace {

class Query {
   const char *p, *end;
   const CssMediaEnvironment &env;
   void space() { while (p < end && dIsspace(*p)) p++; }
   char *word() {
      space();
      const char *start = p;
      while (p < end && (dIsalnum(*p) || *p == '-')) p++;
      return dStrndup(start, p - start);
   }
   bool number(double *value) {
      space();
      char *tail;
      *value = strtod(p, &tail);
      if (tail == p || tail > end || !isfinite(*value) || *value < 0)
         return false;
      p = tail;
      return true;
   }
   bool feature(bool *valid) {
      p++; // '('
      char *name = word();
      int comparison = 0;
      const char *base = name;
      if (!dStrnAsciiCasecmp(name, "min-", 4)) { comparison = 1; base += 4; }
      else if (!dStrnAsciiCasecmp(name, "max-", 4)) { comparison = -1; base += 4; }
      space();
      bool hasValue = p < end && *p == ':';
      if (hasValue) p++;
      space();
      const char *valueStart = p;
      while (p < end && *p != ')') {
         if (*p == '(') { *valid = false; break; }
         p++;
      }
      const char *valueEnd = p;
      while (valueEnd > valueStart && dIsspace(valueEnd[-1])) valueEnd--;
      const char *close = p;
      if (close == end || !*name || (!hasValue && valueStart != valueEnd))
         *valid = false;
      double actual = 0, expected = 0;
      bool known = true;
      if (!dStrAsciiCasecmp(base, "width")) actual = env.width;
      else if (!dStrAsciiCasecmp(base, "height")) actual = env.height;
      else if (!dStrAsciiCasecmp(base, "aspect-ratio"))
         actual = env.height > 0 ? env.width / env.height : 0;
      else if (!dStrAsciiCasecmp(base, "resolution")) actual = env.dpi;
      else if (!dStrAsciiCasecmp(base, "color")) actual = 8;
      else if (!dStrAsciiCasecmp(base, "color-index") ||
               !dStrAsciiCasecmp(base, "monochrome")) actual = 0;
      else known = false;

      bool result = false;
      if (!dStrAsciiCasecmp(base, "orientation") && !comparison && hasValue) {
         char *value = dStrndup(valueStart, valueEnd - valueStart);
         result = (!dStrAsciiCasecmp(value, "landscape") && env.width > env.height) ||
                  (!dStrAsciiCasecmp(value, "portrait") && env.height >= env.width);
         dFree(value);
      } else if (known && *valid) {
         if (!hasValue) {
            result = !comparison && actual > 0;
         } else {
            p = valueStart;
            bool numeric = number(&expected);
            if (!dStrAsciiCasecmp(base, "aspect-ratio")) {
               space();
               double denominator;
               numeric = numeric && p < close && *p++ == '/' &&
                         number(&denominator) && denominator > 0 &&
                         floor(expected) == expected && floor(denominator) == denominator;
               if (numeric) expected /= denominator;
            } else {
               char *unit = word();
               if (!dStrAsciiCasecmp(base, "width") || !dStrAsciiCasecmp(base, "height")) {
                  if (!dStrAsciiCasecmp(unit, "em") || !dStrAsciiCasecmp(unit, "rem")) expected *= 16;
                  else if (!dStrAsciiCasecmp(unit, "ex") || !dStrAsciiCasecmp(unit, "ch")) expected *= 8;
                  else if (!dStrAsciiCasecmp(unit, "in")) expected *= 96;
                  else if (!dStrAsciiCasecmp(unit, "cm")) expected *= 96 / 2.54;
                  else if (!dStrAsciiCasecmp(unit, "mm")) expected *= 96 / 25.4;
                  else if (!dStrAsciiCasecmp(unit, "pt")) expected *= 96 / 72.0;
                  else if (!dStrAsciiCasecmp(unit, "pc")) expected *= 16;
                  else if (dStrAsciiCasecmp(unit, "px") && (*unit || expected != 0)) numeric = false;
               } else if (!dStrAsciiCasecmp(base, "resolution")) {
                  if (!dStrAsciiCasecmp(unit, "dppx")) expected *= 96;
                  else if (!dStrAsciiCasecmp(unit, "dpcm")) expected *= 2.54;
                  else if (dStrAsciiCasecmp(unit, "dpi")) numeric = false;
               } else if (*unit || floor(expected) != expected) numeric = false;
               dFree(unit);
            }
            space();
            *valid = *valid && numeric && p == close;
            if (*valid) {
               result = comparison == 1 ? actual >= expected :
                        comparison == -1 ? actual <= expected :
                        fabs(actual - expected) < 0.00001;
            }
         }
      }
      p = close < end ? close + 1 : end;
      dFree(name);
      return result;
   }
public:
   Query(const char *start, const char *end, const CssMediaEnvironment &env) :
      p(start), end(end), env(env) {}

   bool matches() {
      space();
      if (p == end) return false;
      bool valid = true, result = true, negate = false, typed = false;
      if (*p != '(') {
         char *type = word();
         bool modifier = !dStrAsciiCasecmp(type, "not") || !dStrAsciiCasecmp(type, "only");
         if (modifier) {
            negate = !dStrAsciiCasecmp(type, "not");
            dFree(type);
            type = word();
         }
         valid = *type;
         result = !dStrAsciiCasecmp(type, "screen") || !dStrAsciiCasecmp(type, "all");
         dFree(type);
         typed = true;
      }
      bool first = !typed;
      while (valid) {
         space();
         if (p == end) break;
         if (!first) {
            char *join = word();
            valid = !dStrAsciiCasecmp(join, "and");
            dFree(join);
         }
         space();
         if (!valid || p == end || *p != '(') { valid = false; break; }
         bool value = feature(&valid);
         result = result && value;
         first = false;
      }
      return valid && (negate ? !result : result);
   }
};
}

bool a_Css_media_matches(const char *query, const CssMediaEnvironment &env)
{
   if (!query || !*query) return true;
   /* Strip comments before splitting a list; a comma inside a comment is not
    * a query separator. Unclosed comments and parentheses fail closed. */
   Dstr *clean = dStr_new("");
   for (const char *p = query; *p; p++) {
      if (*p == '/' && p[1] == '*') {
         const char *close = strstr(p + 2, "*/");
         if (!close) { dStr_free(clean, true); return false; }
         dStr_append_c(clean, ' ');
         p = close + 1;
      } else dStr_append_c(clean, *p);
   }
   bool result = false;
   const char *start = clean->str;
   int depth = 0;
   for (const char *p = start;; p++) {
      if (*p == '(') depth++;
      if (*p == ')') depth--;
      if ((!*p || *p == ',') && depth == 0) {
         if (Query(start, p, env).matches()) result = true;
         start = p + 1;
      }
      if (!*p) break;
      if (depth < 0) { result = false; break; }
   }
   dStr_free(clean, true);
   return depth == 0 && result;
}
