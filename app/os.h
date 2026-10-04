#ifndef CHARTS_APP_OS_H
#define CHARTS_APP_OS_H
/* OS headers stay in the application. Watcom's strict ANSI mode deliberately
 * reserves unprefixed extension names; use its documented underscored API. */
#include <sys/stat.h>
#ifdef __WATCOMC__
typedef struct _stat app_stat_info;
#define app_stat _stat
#define APP_S_IFMT _S_IFMT
#define APP_S_IFDIR _S_IFDIR
#define APP_ISDIR(mode) (((mode)&APP_S_IFMT)==APP_S_IFDIR)
#else
typedef struct stat app_stat_info;
#define app_stat stat
#define APP_ISDIR(mode) S_ISDIR(mode)
#endif
#endif
