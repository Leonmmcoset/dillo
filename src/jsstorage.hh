#ifndef DILLO_JSSTORAGE_HH
#define DILLO_JSSTORAGE_HH
#include <string>
class JavascriptStorage {
public:
   static std::string get(const std::string &origin, const void *tab, bool local);
   static bool set(const std::string &origin, const void *tab, bool local,
                    const std::string &json);
   static void flush();
   static void forget(const void *tab);
};
#endif
