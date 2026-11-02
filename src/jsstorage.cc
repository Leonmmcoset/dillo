/* Origin-separated Web Storage. Files contain data, never executable code. */
#include "jsstorage.hh"
#include "dlib/dlib.h"
#include "md5.h"
#include <map>
#include <set>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace {
const size_t Quota = 5 * 1024 * 1024;
std::map<std::string, std::string> persistent;
std::map<const void *, std::map<std::string, std::string> > sessions;
std::set<std::string> dirty;
bool allowed(const std::string &origin)
{ return origin.compare(0, 7, "http://") == 0 || origin.compare(0, 8, "https://") == 0; }
std::string directory()
{ return std::string(dGethomedir()) + "/.dillo/web-storage"; }
std::string filename(const std::string &origin)
{
   md5_state_t state; md5_byte_t digest[16];
   md5_init(&state);
   md5_append(&state, reinterpret_cast<const md5_byte_t *>(origin.data()), origin.size());
   md5_finish(&state, digest);
   const char hex[] = "0123456789abcdef";
   std::string name = directory() + "/";
   for (size_t i = 0; i < sizeof(digest); ++i) { name += hex[digest[i] >> 4]; name += hex[digest[i] & 15]; }
   return name;
}
std::string read(const std::string &origin)
{
   int fd = open(filename(origin).c_str(), O_RDONLY | O_NOFOLLOW);
   if (fd == -1) return "[]";
   struct stat info;
   if (fstat(fd, &info) || !S_ISREG(info.st_mode) || info.st_size < 0 ||
       static_cast<size_t>(info.st_size) > Quota + origin.size() + 1) { close(fd); return "[]"; }
   FILE *file = fdopen(fd, "rb");
   if (!file) { close(fd); return "[]"; }
   std::string data(static_cast<size_t>(info.st_size), '\0');
   size_t count = data.empty() ? 0 : fread(&data[0], 1, data.size(), file);
   fclose(file);
   // Check the full origin even though its hash was used for the file name.
   std::string prefix = origin + "\n";
   if (count != data.size() || data.compare(0, prefix.size(), prefix)) return "[]";
   return data.substr(prefix.size());
}
}
std::string JavascriptStorage::get(const std::string &origin, const void *tab, bool local)
{
   if (!allowed(origin)) return "[]";
   if (local) {
      if (!persistent.count(origin)) persistent[origin] = read(origin);
      return persistent[origin];
   }
   return sessions[tab].count(origin) ? sessions[tab][origin] : "[]";
}
bool JavascriptStorage::set(const std::string &origin, const void *tab, bool local,
                            const std::string &json)
{
   if (!allowed(origin) || json.size() > Quota) return false;
   if (local) { persistent[origin] = json; dirty.insert(origin); }
   else sessions[tab][origin] = json;
   return true;
}
void JavascriptStorage::flush()
{
   if (dirty.empty()) return;
   mkdir(directory().c_str(), 0700);
   for (std::set<std::string>::iterator it = dirty.begin(); it != dirty.end(); ++it) {
      std::string target = filename(*it), temporary = target + ".XXXXXX";
      int fd = mkstemp(&temporary[0]);
      if (fd == -1) continue;
      FILE *file = fdopen(fd, "wb");
      if (!file) { close(fd); unlink(temporary.c_str()); continue; }
      std::string data = *it + "\n" + persistent[*it];
      bool ok = fwrite(data.data(), 1, data.size(), file) == data.size();
      if (fclose(file)) ok = false;
      if (ok) rename(temporary.c_str(), target.c_str());
      unlink(temporary.c_str());
   }
   dirty.clear();
}
void JavascriptStorage::forget(const void *tab)
{ sessions.erase(tab); flush(); }
