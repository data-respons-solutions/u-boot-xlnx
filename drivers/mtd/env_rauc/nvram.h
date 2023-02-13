#ifndef _NVRAM_H_
#define _NVRAM_H_

int rauc_nvram_set(const char *key, const char *val);
int rauc_nvram_get(const char *key, char *val, int max);
void rauc_nvram_list(bool);
int nvram_interface_clear(void *p);
#endif // _NVRAM_H_
