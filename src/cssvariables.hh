/* CSS custom-property substitution, independent of the GUI and JS engine. */
#ifndef DILLO_CSSVARIABLES_HH
#define DILLO_CSSVARIABLES_HH
#include <map>
#include <set>
#include <string>
#include <vector>

class CssVariables {
   std::map<std::string, std::string> values;
   std::set<std::string> visiting, invalid, resolved;
   std::vector<std::string> path;
   bool resolve(const std::string &name, std::string &value);
   bool expand(const std::string &input, std::string &output, unsigned depth);
public:
   explicit CssVariables(const std::map<std::string, std::string> &input): values(input) {}
   std::map<std::string, std::string> computed();
   bool substitute(const std::string &input, std::string &output) { return expand(input, output, 0); }
};
#endif
