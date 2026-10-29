/* Saving selected preferences while preserving the rest of dillorc. */
#ifndef DILLO_PREFSRC_HH
#define DILLO_PREFSRC_HH

struct PrefsUpdate {
   const char *name;
   const char *value;
};

/* On first save, use fallback as the base if path does not exist.
 * Returns 0 on success, -1 with errno set on failure. */
int a_Prefs_save(const char *path, const char *fallback,
                 const PrefsUpdate *updates, unsigned int count);

#endif
