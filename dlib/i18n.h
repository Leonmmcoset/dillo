/* UI translations shared by the browser and its plugins. */
#ifndef DILLO_I18N_H
#define DILLO_I18N_H

#ifdef __cplusplus
extern "C" {
#endif

void a_I18n_init(void);
const char *a_I18n_language(void);
const char *a_I18n_gettext(const char *message)
#if defined(__GNUC__)
   __attribute__((format_arg(1)))
#endif
   ;

#ifdef __cplusplus
}
#endif

#define _(message) a_I18n_gettext(message)
/* Mark strings in static C initializers; translate them when displayed. */
#define N_(message) message

#endif
