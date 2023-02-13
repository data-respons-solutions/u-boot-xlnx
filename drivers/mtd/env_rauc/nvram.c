#include <common.h>
#include <stdio.h>
#include <stdlib.h>
#include <linux/printk.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <env.h>
#include "env_priv.h"
#include "nvram.h"
#include <command.h>
#include "libnvram.h"
#include <linux/err.h>
#include <version.h>
#include <timestamp.h>

struct nvram_section_data {
	uint8_t *buf;
	size_t buf_size;
	uint32_t counter;
	uint32_t data_len;
	int is_valid;
	nvram_header_t hdr;
};

struct nvram {
	unsigned section;
	struct nvram_interface_priv *priv;
	uint8_t *write_buf;
};

static struct nvram rauc_nvram = { 0 };
static struct nvram_list nv_list = { 0 };
static struct nvram_section_data section_data[2] = { 0 };
static int initialized = 0;
static int nvram_commit(struct nvram_list *list);

static int read_section(unsigned section)
{
	int r = 0;
	size_t size = nvram_interface_size(rauc_nvram.priv);
	int is_valid = 0;
	struct nvram_section_data *data = &section_data[section];
	data->is_valid = 0;

	r = nvram_interface_read(rauc_nvram.priv, section, data->buf, sizeof(nvram_header_t));
	if (r) {
		pr_err("section %d: failed reading header\n", section);
		goto error_exit;
	}
	is_valid = is_valid_nvram_section(data->buf, &data->hdr);
	if (!is_valid) {
		pr_warn("Invalid header in section %d\n", section);
		return 0;
	}

	if (size < (data->hdr.data_size + sizeof(nvram_header_t))) {
		pr_warn("%s: read buffer size %lu to small for data %u\n", __func__, size,
			data->hdr.data_size);
		return -ENOMEM;
	}
	r = nvram_interface_read(rauc_nvram.priv, section, data->buf,
				 sizeof(nvram_header_t) + data->hdr.data_size);
	if (r) {
		pr_err("section %d: failed reading data\n", section);
		goto error_exit;
	}
	is_valid = is_valid_nvram_data(&data->hdr, data->buf + sizeof(nvram_header_t));
	if (!is_valid) {
		pr_warn("%s: data crc error in section %d\n", __func__, section);
		return -EIO;
	}
	data->counter = data->hdr.counter;
	data->data_len = data->hdr.data_size;
	data->is_valid = is_valid;
	debug("%s: bsz=%lu, cnt=%u, dlen=%u, valid=%d\n", __func__, size, data->counter,
	      data->data_len, is_valid);

	return 0;
error_exit:
	return r;
}

static unsigned find_active_section(const struct nvram_section_data *section)
{
	if (section[0].is_valid && section[1].is_valid) {
		if (section[0].counter == section[1].counter)
			pr_err("Section counters are equal! select first\n");
		return (section[0].counter >= section[1].counter) ? 0 : 1;
	}
	return (section[1].is_valid) ? 1 : 0;
}

static int nvram_init(void)
{
	if (initialized)
		return 0;
	initialized = 1;
	int r = 0;
	for (int n = 0; n < 2; n++) {
		if (!section_data[n].buf) {
			section_data[n].buf =
				memalign(sizeof(nvram_header_t), CONFIG_ENV_RAUC_MAX_SIZE + 16);
			if (!section_data[n].buf) {
				pr_err("%s: Failed to allocate data buffer A\n", __func__);
				return -ENOMEM;
			}
			debug("Section %d data at 0x%px\n", n, section_data[n].buf);
		}
	}
	rauc_nvram.priv = nvram_interface_init();
	if (IS_ERR_OR_NULL(rauc_nvram.priv)) {
		pr_err("%s: Failed initializing interface [%ld]\n", __func__,
		       PTR_ERR(rauc_nvram.priv));
		return PTR_ERR(rauc_nvram.priv);
	}

	if (!rauc_nvram.write_buf) {
		rauc_nvram.write_buf =
			memalign(sizeof(nvram_header_t), CONFIG_ENV_RAUC_MAX_SIZE + 16);
		if (!rauc_nvram.write_buf) {
			pr_err("%s: Failed to allocate buffer of size %d\n", __func__,
			       CONFIG_ENV_RAUC_MAX_SIZE);
		}
	}
	r = read_section(0);
	r = read_section(1);

	rauc_nvram.section = find_active_section(section_data);
	debug("%s: Active section %d\n", __func__, rauc_nvram.section);
	if (section_data[rauc_nvram.section].is_valid) {
		r = nvram_section_deserialize(&section_data[rauc_nvram.section].hdr, &nv_list,
					      section_data[rauc_nvram.section].buf +
						      sizeof(nvram_header_t));
	}
	nvram_list_set(&nv_list, "UBOOT_VERSION", U_BOOT_VERSION);
	nvram_list_set(&nv_list, "UBOOT_BUILD_DATE", U_BOOT_DATE);
	nvram_list_set(&nv_list, "UBOOT_BUILD_TIME", U_BOOT_TIME);
	nvram_commit(&nv_list);
	return r;
}

static __attribute((unused)) void nvram_deinit(void)
{
	if (initialized) {
		nvram_interface_deinit(rauc_nvram.priv);
		destroy_nvram_list(&nv_list);
		initialized = 0;
	}
}

void rauc_nvram_list(bool init)
{
	struct nvram_node *cur = nv_list.entry;
	while (cur) {
		printf("%s=%s\n", cur->key, cur->value);
		cur = cur->next;
	}
}

static int nvram_commit(struct nvram_list *list)
{
	uint8_t *buf = rauc_nvram.write_buf;
	uint32_t size = 0;
	int r = 0;

	if (!list->dirty)
		return 0;
	r = nvram_section_serialize_size(list, &size);
	if (r) {
		pr_err("failed calculating serialized size of nvram data [%d]\n", r);
		goto exit;
	}

	uint32_t new_counter = 0;
	unsigned new_section = 0;
	if (section_data[rauc_nvram.section].is_valid) {
		new_counter = section_data[rauc_nvram.section].hdr.counter + 1;
		new_section = (rauc_nvram.section == 0) ? 1 : 0;
	}

	r = nvram_section_serialize(list, new_counter, buf, size);
	if (r) {
		pr_err("failed serializing nvram data [%d]\n", -r);
		goto exit;
	}
	r = nvram_interface_write(rauc_nvram.priv, new_section, buf, size);
	if (r) {
		pr_err("%s: Failed to write %d bytes\n", __func__, size);
		goto exit;
	}
	debug("section %d: valid: true: counter: %u: data_len: %u\n", new_section, new_counter,
	      size);
	list->dirty = 0;
	r = 0;
exit:
	return r;
}

int rauc_nvram_commit(void)
{
	return nvram_commit(&nv_list);
}

int rauc_nvram_get(const char *key, char *val, int max)
{
	const struct nvram_node *node = nvram_list_get(&nv_list, key);
	if (node) {
		if (strnlen(node->value, max) > max)
			return -ENOMEM;
		strncpy(val, node->value, max);
		return 0;
	}
	return -ENOENT;
}

int rauc_nvram_set(const char *key, const char *val)
{
	int r = nvram_list_set(&nv_list, key, val);
	if (r < 0) {
		pr_err("Failed setting key %s (%d)\n", key, r);
	}
	return r;
}

int rauc_nvram_del(const char *key)
{
	return nvram_list_remove(&nv_list, key);
}

int nvram_clear(void)
{
	destroy_nvram_list(&nv_list);
	nvram_interface_clear(rauc_nvram.priv);
	nv_list.dirty = 0;
	return 0;
}

static int rauc_defaults(void)
{
	int r;
	printf("Initializing RAUC variables\n");
	char *bp = env_get("boot_part");
	if (!strncmp(bp, "2", 2)) {
		r = rauc_nvram_set("BOOT_ORDER", "B A");
	} else {
		r = rauc_nvram_set("BOOT_ORDER", "A B");
	}
	r += rauc_nvram_set("BOOT_A_LEFT", "3");
	r += rauc_nvram_set("BOOT_B_LEFT", "3");
	if (r < 0)
		return -EIO;
	printf("Initialized RAUC variables\n");
	return rauc_nvram_commit();
}

int rauc_boot(void)
{
	nvram_init();
	struct nvram_node *boot_order, *boot_left_a, *boot_left_b;
	bool rauc_ready = false;
	int retries = 0;
	do {
		boot_order = nvram_list_get(&nv_list, "BOOT_ORDER");
		boot_left_a = nvram_list_get(&nv_list, "BOOT_A_LEFT");
		boot_left_b = nvram_list_get(&nv_list, "BOOT_B_LEFT");
		if (boot_order == 0 || boot_left_a == 0 || boot_left_b == 0) {
			printf("bo=%d, bla=%d, blb=%d\n", boot_order ? 1 : 0, boot_left_a ? 1 : 0,
			       boot_left_b ? 1 : 0);
			int r = rauc_defaults();
			if (r < 0) {
				pr_err("Rauc not operating\n");
				return -EIO;
			}
			retries++;
			continue;
		}
		rauc_ready = true;
		printf("%s=%s\n", boot_order->key, boot_order->value);
		printf("%s=%s\n", boot_left_a->key, boot_left_a->value);
		printf("%s=%s\n", boot_left_b->key, boot_left_b->value);
	} while (!rauc_ready && retries < 2);
	if (!rauc_ready)
		return -EINVAL;
	/* Check valid values */
	unsigned long a_left, b_left;
	int r = strict_strtoul(boot_left_a->value, 10, &a_left);
	r += strict_strtoul(boot_left_b->value, 10, &b_left);
	printf("a_left=%ld, b_left=%ld\n", a_left, b_left);
	if (r < 0) {
		r = rauc_defaults();
		if (r < 0) {
			pr_err("%s: panic, failed to initialize (%d)\n", __func__, r);
			return r;
		}
	}

	/* Determine boot */
	if (boot_order->value[0] == 'A') {
		if (a_left > 0) {
			rauc_nvram_set("BOOT_A_LEFT", simple_itoa(a_left - 1));
			env_set("boot_part", "1");
			env_set("rauc_slot", "A");
		} else if (b_left > 0) {
			rauc_nvram_set("BOOT_B_LEFT", simple_itoa(b_left - 1));
			env_set("boot_part", "2");
			env_set("rauc_slot", "B");
		} else {
			pr_err("%s: No valid slot - giving up\n", __func__);
			return -EINVAL;
		}
		return rauc_nvram_commit();
	}
	if (boot_order->value[0] == 'B') {
		if (b_left > 0) {
			rauc_nvram_set("BOOT_B_LEFT", simple_itoa(b_left - 1));
			env_set("boot_part", "2");
			env_set("rauc_slot", "B");
		} else if (a_left > 0) {
			rauc_nvram_set("BOOT_A_LEFT", simple_itoa(a_left - 1));
			env_set("boot_part", "1");
			env_set("rauc_slot", "A");
		} else {
			pr_err("%s: No valid slot - giving up\n", __func__);
			return -EINVAL;
		}
		return rauc_nvram_commit();
	}

	return EINVAL;
}

#ifdef CONFIG_ENV_RAUC_CMD

static int do_rauc_test(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	uint32_t len = 379;
	len = simple_strtoul(argv[0], 0, 10);
	nvram_interface_test(rauc_nvram.priv, len);
	return 0;
}

static int do_rauc_list(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	rauc_nvram_list(1);
	return 0;
}

static int do_rauc_get(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	char val[256];
	if (argc == 0)
		return -EINVAL;
	int r = rauc_nvram_get(argv[0], val, 255);
	if (r < 0)
		return -r;
	printf("%s\n", val);
	return 0;
}

static int do_rauc_clear(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	int r = nvram_clear();
	if (r < 0)
		pr_err("%s failed (%d)\n", __func__, r);
	return 0;
}

static int do_rauc_set(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	if (argc < 2)
		return -EINVAL;
	int r = rauc_nvram_set(argv[0], argv[1]);
	if (r < 0)
		return r;
	return rauc_nvram_commit();
}

static int do_rauc_del(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	if (argc < 1)
		return -EINVAL;
	int r = rauc_nvram_del(argv[0]);
	if (r < 0)
		return r;
	return rauc_nvram_commit();
}

static int do_rauc_commit(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	int r = nvram_commit(&nv_list);
	return r;
}

static int do_rauc_boot(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	int r = rauc_boot();
	return r;
}

static int do_rauc_init(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	int r = rauc_defaults();
	return r;
}

static struct cmd_tbl rauc_commands[] = {
	U_BOOT_CMD_MKENT(list, 0, 1, do_rauc_list, "", ""),
	U_BOOT_CMD_MKENT(get, 1, 1, do_rauc_get, "", ""),
	U_BOOT_CMD_MKENT(set, 2, 1, do_rauc_set, "", ""),
	U_BOOT_CMD_MKENT(clear, 0, 1, do_rauc_clear, "", ""),
	U_BOOT_CMD_MKENT(test, 1, 1, do_rauc_test, "", ""),
	U_BOOT_CMD_MKENT(commit, 0, 1, do_rauc_commit, "", ""),
	U_BOOT_CMD_MKENT(boot, 0, 1, do_rauc_boot, "", ""),
	U_BOOT_CMD_MKENT(init, 0, 1, do_rauc_init, "", ""),
	U_BOOT_CMD_MKENT(delete, 1, 1, do_rauc_del, "", ""),
};

static int do_rauc(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	struct cmd_tbl *rauc_cmd;
	int ret = CMD_RET_USAGE;

	if (argc < 2)
		return CMD_RET_USAGE;
	rauc_cmd = find_cmd_tbl(argv[1], rauc_commands, ARRAY_SIZE(rauc_commands));
	if (rauc_cmd) {
		nvram_init();
		argc -= 2;
		argv += 2;
		ret = rauc_cmd->cmd(rauc_cmd, flag, argc, argv);
		nvram_deinit();
	}
	return cmd_process_error(rauc_cmd, ret);
}
U_BOOT_CMD(rauc, 4, 1, do_rauc, "NVRAM commands\n",
	   "rauc list\n"
	   "rauc get\n"
	   "rauc set\n"
	   "rauc commit\n"
	   "rauc clear\n"
	   "rauc boot\n"
	   "rauc test\n"
	   "rauc delete\n"
	   "rauc init\n")
#endif
