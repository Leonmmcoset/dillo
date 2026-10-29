#ifndef __CSSPARSER_HH__
#define __CSSPARSER_HH__

#include "css.hh"

class DilloHtml;

class CssParser {
   private:
      typedef enum {
         CSS_TK_DECINT, CSS_TK_FLOAT, CSS_TK_COLOR, CSS_TK_SYMBOL,
         CSS_TK_STRING, CSS_TK_CHAR, CSS_TK_END
      } CssTokenType;

      static const int maxStrLen = 256;
      CssContext *context;
      CssOrigin origin;
      const DilloUrl *baseUrl;

      const char *buf;
      int buflen, bufptr;

      CssTokenType ttype;
      char tval[maxStrLen];
      bool withinBlock;
      bool spaceSeparated; /* used when parsing CSS selectors */

      CssParser(CssContext *context, CssOrigin origin, const DilloUrl *baseUrl,
                const char *buf, int buflen);
      int getChar();
      void ungetChar();
      void nextToken();
      bool skipString(int c, const char *string);
      bool tokenMatchesProperty(CssPropertyName prop, CssValueType * type);
      bool parseValue(CssPropertyName prop, CssValueType type,
                      CssPropertyValue * val);
      bool parseWeight();
      bool parseRgbColorComponent(int32_t *cc, int *percentage);
      bool parseRgbColor(int32_t *c);
      void parseDeclaration(CssPropertyList * props,
                            CssPropertyList * importantProps);
      bool parseSimpleSelector(CssSimpleSelector *selector, bool negation = false);
      bool parseAttributeSelector(CssSimpleSelector *selector);
      bool parsePseudoSelector(CssSimpleSelector *selector, bool negation);
      char *parseUrl();
      void parseImport(DilloHtml *html);
      char *mediaList(char delimiter);
      void parseMedia(DilloHtml *html, int depth);
      void parseRules(DilloHtml *html, bool block, int depth);
      CssSelector *parseSelector();
      void parseRuleset();
      void ignoreBlock();
      void ignoreStatement();

   public:
      static void parseDeclarationBlock(const DilloUrl *baseUrl,
                                        const char *buf, int buflen,
                                        CssPropertyList *props,
                                        CssPropertyList *propsImortant);
      static void parse(DilloHtml *html, const DilloUrl *baseUrl, CssContext *context,
                        const char *buf, int buflen, CssOrigin origin);
      static const char *propertyNameString(CssPropertyName name);
};

#endif
