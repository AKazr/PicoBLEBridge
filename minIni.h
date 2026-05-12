#ifndef MININI_H
#define MININI_H

#include <stdint.h>

int ini_gets(const char *section, const char *key, const char *def_value, char *buffer, int buffer_size, const char *filename);
long ini_getl(const char *section, const char *key, long def_value, const char *filename);

#endif
