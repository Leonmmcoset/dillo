#include "cssvariables.hh"
#include <cctype>

static std::string trim(const std::string &s)
{
   size_t first = s.find_first_not_of(" \t\r\n\f"), last = s.find_last_not_of(" \t\r\n\f");
   return first == std::string::npos ? "" : s.substr(first, last - first + 1);
}

bool CssVariables::resolve(const std::string &name, std::string &value)
{
   if (invalid.count(name) || !values.count(name)) return false;
   if (visiting.count(name)) {
      bool cycle = false;
      for (size_t i = 0; i < path.size(); ++i) {
         if (path[i] == name) cycle = true;
         if (cycle) invalid.insert(path[i]);
      }
      return false;
   }
   if (!resolved.count(name)) {
      if (visiting.size() >= 64) return false;
      visiting.insert(name);
      path.push_back(name);
      std::string result;
      bool valid = expand(values[name], result, 0);
      visiting.erase(name); resolved.insert(name);
      path.pop_back();
      if (!valid || invalid.count(name)) { invalid.insert(name); return false; }
      values[name] = result;
   }
   value = values[name]; return true;
}

bool CssVariables::expand(const std::string &input, std::string &output, unsigned depth)
{
   if (depth > 64 || input.size() > 65536) return false;
   output.clear(); char quote = 0;
   for (size_t i = 0; i < input.size();) {
      char c = input[i];
      if (c == '\\' && i + 1 < input.size()) { output.append(input, i, 2); i += 2; continue; }
      if (quote) { if (c == quote) quote = 0; output += c; ++i; continue; }
      if (c == '\'' || c == '"') { quote = c; output += c; ++i; continue; }
      if (input.compare(i, 2, "/*") == 0) {
         size_t end = input.find("*/", i + 2); if (end == std::string::npos) return false;
         output += ' '; i = end + 2; continue;
      }
      bool boundary = i == 0 || !(std::isalnum(static_cast<unsigned char>(input[i - 1])) || input[i - 1] == '-' || input[i - 1] == '_');
      if (boundary && input.compare(i, 4, "var(") == 0) {
         size_t start = i + 4, comma = std::string::npos, end = start;
         unsigned nesting = 1; char innerQuote = 0;
         for (; end < input.size(); ++end) {
            char inner = input[end];
            if (inner == '\\') { if (end + 1 < input.size()) ++end; continue; }
            if (innerQuote) { if (inner == innerQuote) innerQuote = 0; continue; }
            if (inner == '\'' || inner == '"') { innerQuote = inner; continue; }
            if (inner == '(') ++nesting;
            else if (inner == ')' && --nesting == 0) break;
            else if (inner == ',' && nesting == 1 && comma == std::string::npos) comma = end;
         }
         if (end == input.size()) return false;
         std::string name = trim(input.substr(start, (comma == std::string::npos ? end : comma) - start)), replacement;
         if (name.size() < 3 || name.compare(0, 2, "--")) return false;
         if (!resolve(name, replacement)) {
            if (comma == std::string::npos || !expand(input.substr(comma + 1, end - comma - 1), replacement, depth + 1)) return false;
         }
         if (output.size() + replacement.size() > 65536) return false;
         output += replacement; i = end + 1;
      } else { output += c; ++i; }
      if (output.size() > 65536) return false;
   }
   return true;
}

std::map<std::string, std::string> CssVariables::computed()
{
   std::map<std::string, std::string> result;
   for (std::map<std::string, std::string>::const_iterator it = values.begin(); it != values.end(); ++it) {
      std::string value;
      if (resolve(it->first, value)) result[it->first] = value;
   }
   return result;
}
