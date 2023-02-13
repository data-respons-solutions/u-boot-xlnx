#include <common.h>
#include <string.h>
#include <stdbool.h>
#include <string.h>
#include <mtd.h>
#include <errno.h>
#include <spi.h>
#include <spi_flash.h>
#include "env_priv.h"
#include "nvram.h"
#include <linux/err.h>
#include <rand.h>
#include <dm.h>
#include <dm/uclass-internal.h>
#include <dm/device-internal.h>

struct nvram_mtd {
	uint32_t b_offset;
	struct mtd_info *mtd_dev;
	uint32_t capacity;
};

static struct nvram_mtd priv = { 0 };

void *nvram_interface_init(void)
{
	struct udevice *dev;
	struct udevice *spi_dev = 0;
	int idx = 0;
	while (!uclass_find_device(UCLASS_SPI_FLASH, idx, &dev) && dev) {
		if (!device_active(dev))
			device_probe(dev);
		spi_dev = dev;
		break;
		idx++;
	}
	if (!spi_dev) {
		pr_err("No flash device\n");
		return ERR_PTR(-ENODEV);
	}
	mtd_probe_devices();
	struct mtd_info *mtd = get_mtd_device_nm(CONFIG_ENV_RAUC_MTD_NAME);
	if (IS_ERR_OR_NULL(mtd)) {
		return mtd;
	}
	priv.mtd_dev = mtd;
	priv.b_offset = priv.mtd_dev->size / 2;
	priv.capacity = priv.mtd_dev->size / 2;

	debug("Found MTD %s with cap %llu eraseblock = %u, offs=0x%llx\n", priv.mtd_dev->name, priv.mtd_dev->size,
	      priv.mtd_dev->erasesize, priv.mtd_dev->offset);

	if (priv.capacity > CONFIG_ENV_RAUC_MAX_SIZE) {
		debug("Capping capacity from %d to %d\n", priv.capacity, CONFIG_ENV_RAUC_MAX_SIZE);
		priv.capacity = CONFIG_ENV_RAUC_MAX_SIZE;
	}
	return &priv;
}

void nvram_interface_deinit(void *p)
{
	struct nvram_mtd *priv = (struct nvram_mtd *)p;
	if (priv->mtd_dev) {
		debug("%s\n", __func__);
		put_mtd_device(priv->mtd_dev);
		priv->mtd_dev = 0;
	}
}

int nvram_interface_size(void *priv)
{
	struct nvram_mtd *p = (struct nvram_mtd *)priv;
	return p->capacity;
}

int nvram_interface_read(void *p, unsigned section, uint8_t *buf, size_t size)
{
	struct nvram_mtd *priv = (struct nvram_mtd *)p;
	size_t retlen;
	debug("%s: Read %lu bytes from section %d into %px\n", __func__, size, section, buf);
	int r = mtd_read(priv->mtd_dev, section == 1 ? priv->b_offset : 0, size, &retlen, buf);
	if (r)
		debug("Failed to read MTD (%d)\n", r);
	return r;
}

int nvram_mtd_erase(struct mtd_info *mtd, uint64_t offset, uint64_t size)
{
	struct erase_info ei;
	memset(&ei, 0, sizeof(ei));
	if (mtd_mod_by_eb(offset, mtd)) {
		pr_err("%s: offset 0x%llx not aligned to erase block size\n", __func__, offset);
		return -EINVAL;
	}
	uint32_t blocks = mtd_div_by_eb(size, mtd);
	if (mtd_mod_by_eb(size, mtd))
		blocks++;
	ei.addr = offset;
	ei.len = blocks * mtd->erasesize;
	ei.mtd = mtd;
	debug("%s: Erase %d blocks\n", __func__, blocks);
	int r = mtd_erase(mtd, &ei);
	if (r < 0) {
		pr_err("%s: Erase failed (%d)\n", __func__, r);
		return r;
	}
	return 0;
}

int nvram_interface_write(void *p, unsigned section, const uint8_t *buf, size_t size)
{
	struct nvram_mtd *priv = (struct nvram_mtd *)p;
	int r = 0;
	size_t retlen;

	uint64_t offset = section ? priv->b_offset : 0;
	debug("%s: Write %lu bytes @ 0x%llx to section %d\n", __func__, size, offset, section);
	r = nvram_mtd_erase(priv->mtd_dev, offset, size);
	if (r < 0)
		return r;

	/* 	TODO: Remove padding when driver supports odd numbered writes
	*	Pad to 16 to be safe
	*/
	size_t padded_len = size % 16 ? size + 16 - size % 16 : size;
	r = mtd_write(priv->mtd_dev, offset, padded_len, &retlen, buf);
	if (r < 0)
		pr_err("%s: write failed (%d)\n", __func__, r);
#ifdef DEBUG
	uint8_t *buf_rb = malloc(padded_len);
	mtd_read(priv->mtd_dev, offset, padded_len, &retlen, buf_rb);
	int n = memcmp(buf, buf_rb, size);
	if (n) {
		pr_err("%s: Readback failed in=0x%px, rb=0x%px\n", __func__, buf, buf_rb);
		free(buf_rb);
		r = -EIO;
	}
#endif
	return r;
}

int nvram_interface_clear(void *p)
{
	struct nvram_mtd *priv = (struct nvram_mtd *)p;
	if (priv->mtd_dev) {
		debug("%s\n", __func__);
		return nvram_mtd_erase(priv->mtd_dev, 0, priv->mtd_dev->size);
	}
	return 0;
}

static uint8_t *in_data = 0;
static uint8_t *out_data = 0;

int nvram_interface_test(void *p, uint32_t test_len)
{
	struct nvram_mtd *priv = (struct nvram_mtd *)p;
	struct mtd_info *mtd = priv->mtd_dev;
	size_t retlen;
	if (!in_data)
		in_data = malloc(mtd->size);
	if (!out_data)
		out_data = malloc(mtd->size);
	memset(in_data, 0, mtd->size);
	memset(out_data, 0, mtd->size);
	printf("Testing %d byte segments\n", test_len);
	for (int n = 0; n < test_len; n++) {
		in_data[n] = rand();
	}
	int r = nvram_mtd_erase(mtd, 0, test_len);
	if (r < 0)
		return r;
	r = mtd_write(mtd, 0, test_len, &retlen, in_data);
	if (r < 0) {
		printf("Write failed (%d)\n", r);
		return r;
	}
	r = mtd_read(mtd, 0, test_len, &retlen, out_data);
	if (r < 0) {
		printf("Read failed (%d)\n", r);
		return r;
	}
	int n = memcmp(in_data, out_data, test_len);
	if (n) {
		pr_err("%s: Readback failed in=0x%px, rb=0x%px\n", __func__, in_data, out_data);
		r = -EIO;
	}
	return 0;
}
