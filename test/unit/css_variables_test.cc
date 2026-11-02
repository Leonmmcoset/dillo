#include "src/cssvariables.hh"
#include <cstdio>
#include <cstdlib>

static void check(bool ok, const char *message)
{ if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(1); } }

int main()
{
   std::map<std::string, std::string> declarations = {
      {"--color", "red"}, {"--Color", "blue"}, {"--nested", "var(--color)"},
      {"--a", "var(--b)"}, {"--b", "var(--a, green)"},
      {"--fallback", "var(--a, orange)"}, {"--empty", ""}
   };
   CssVariables variables(declarations);
   auto computed = variables.computed();
   check(computed["--nested"] == "red" && computed["--Color"] == "blue", "variables resolve with case-sensitive names");
   check(!computed.count("--a") && !computed.count("--b"), "cycles invalidate all participating variables");
   check(computed["--fallback"] == " orange", "a noncyclic caller can fall back from a cycle");
   std::string value;
   check(variables.substitute("var(--missing, var(--nested))", value) && value == " red", "nested fallbacks");
   check(variables.substitute("var(--empty,unused)", value) && value.empty(), "empty custom properties remain valid");
   check(variables.substitute("'var(--color)' var(--Color)", value) && value == "'var(--color)' blue", "quoted text is not substituted");
   check(!variables.substitute("var(--missing)", value), "missing property without fallback invalidates declaration");
   check(!variables.substitute("var(--color", value), "unterminated var is rejected");
   return 0;
}
