#ifndef _ENV_PRIV_H_
#define _ENV_PRIV_H_

void *nvram_interface_init(void);
void nvram_interface_deinit(void *p);
int nvram_interface_read(void *priv, unsigned section, uint8_t *buf, size_t size);
int nvram_interface_size(void *priv);

int nvram_interface_write(void *priv, unsigned section, const uint8_t *buf, size_t size);
int nvram_interface_clear(void *p);
int nvram_interface_test(void *p, uint32_t len);
#endif
