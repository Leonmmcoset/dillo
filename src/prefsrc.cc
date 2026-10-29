/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include "prefsrc.hh"
#include "dlib/dlib.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int a_Prefs_save(const char *path, const char *fallback,
                 const PrefsUpdate *updates, unsigned int count)
{
   /* The rc parser strips enclosing quotes but does not interpret escapes.
    * Reject line breaks, which would introduce extra configuration entries. */
   for (unsigned int i = 0; i < count; i++) {
      const char *name = updates[i].name, *value = updates[i].value;
      if (!name || !*name || !value ||
          strspn(name, "abcdefghijklmnopqrstuvwxyz_0123456789") != strlen(name)
          || strchr(value, '\n') || strchr(value, '\r')) {
         errno = EINVAL;
         return -1;
      }
   }
   if (!count)
      return 0;

   /* Follow a dotfile symlink rather than replacing the link itself. */
   struct stat st;
   char *target;
   if (lstat(path, &st) == 0 && S_ISLNK(st.st_mode)) {
      target = realpath(path, NULL);
      if (!target)
         return -1;
   } else {
      target = dStrdup(path);
   }
   if (stat(target, &st) == 0 && !S_ISREG(st.st_mode)) {
      dFree(target);
      errno = EINVAL;
      return -1;
   }

   FILE *source = fopen(target, "r");
   if (!source && errno == ENOENT && fallback)
      source = fopen(fallback, "r");
   if (!source && errno != ENOENT) {
      int saved_errno = errno;
      dFree(target);
      errno = saved_errno;
      return -1;
   }

   Dstr *text = dStr_new("");
   bool *seen = dNew0(bool, count);
   char *line;
   if (source) {
      while ((line = dGetline(source))) {
         char *copy = dStrdup(line), *name, *value;
         bool replaced = false;
         if (dParser_parse_rc_line(&copy, &name, &value) == 0) {
            for (unsigned int i = 0; i < count; i++) {
               if (!strcmp(name, updates[i].name)) {
                  dStr_sprintfa(text, "%s=\"%s\"\n", name, updates[i].value);
                  seen[i] = replaced = true;
                  break;
               }
            }
         }
         if (!replaced)
            dStr_append(text, line);
         dFree(copy);
         dFree(line);
      }
      if (ferror(source)) {
         int saved_errno = errno ? errno : EIO;
         fclose(source);
         dStr_free(text, 1);
         dFree(seen);
         dFree(target);
         errno = saved_errno;
         return -1;
      }
      fclose(source);
   }
   if (text->len && text->str[text->len - 1] != '\n')
      dStr_append_c(text, '\n');
   for (unsigned int i = 0; i < count; i++) {
      if (!seen[i])
         dStr_sprintfa(text, "%s=\"%s\"\n", updates[i].name, updates[i].value);
   }
   dFree(seen);

   /* Write next to the destination, then atomically replace it. Until rename
    * succeeds, the previous configuration remains intact. mkstemp uses 0600. */
   char *temporary = dStrconcat(target, ".tmp.XXXXXX", NULL);
   int fd = mkstemp(temporary), result = -1;
   if (fd >= 0) {
      FILE *output = fdopen(fd, "w");
      if (output) {
         bool written = fwrite(text->str, 1, text->len, output) ==
                        (size_t)text->len && fflush(output) == 0 &&
                        fsync(fd) == 0;
         int write_errno = errno;
         int close_result = fclose(output);
         if (written && close_result == 0)
            result = rename(temporary, target);
         else
            errno = written ? errno : (write_errno ? write_errno : EIO);
      } else {
         int saved_errno = errno;
         close(fd);
         errno = saved_errno;
      }
   }
   int saved_errno = errno;
   if (result < 0 && fd >= 0)
      unlink(temporary);
   dStr_free(text, 1);
   dFree(temporary);
   dFree(target);
   errno = saved_errno;
   return result;
}
