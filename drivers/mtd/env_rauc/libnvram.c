/*
 * Copyright (C) 2019 Data Respons Solutions AB
 *
 * Author: Mikko Salomäki <ms@datarespons.se>
 *
 * SPDX-License-Identifier:	MIT
 */
#ifdef __UBOOT__
#include <common.h>
#include <stdlib.h>
#include <inttypes.h>
#include <linux/types.h>
#include <linux/err.h>
#else
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>
#include <unistd.h>
#endif
#include "libnvram.h"
#include "crc32.h"

#ifdef DEBUG
#define LIBDEBUG 1
#else
#define LIBDEBUG 0
#endif

#define libdebug(fmt, ...)                                                                         \
	do {                                                                                       \
		if (LIBDEBUG)                                                                      \
			printf("DEBUG %s: " fmt, __func__, ##__VA_ARGS__);                         \
	} while (0)

#define NVRAM_ENTRY_KEY_LEN_OFFSET 0
#define NVRAM_ENTRY_VALUE_LEN_OFFSET 4
#define NVRAM_ENTRY_DATA_OFFSET 8
#define NVRAM_ENTRY_MAX_LEN 256
// Size of key_len and value_len
#define NVRAM_ENTRY_HEADER_SIZE sizeof(uint32_t) * 2
//Minimum possible size of an entry where both key and value are 1 char each
#define NVRAM_ENTRY_MIN_SIZE NVRAM_ENTRY_HEADER_SIZE + 2

#define NVRAM_CRC32_INIT 0xffffffff
#define NVRAM_CRC32_XOR 0xffffffff

void dump_header(const nvram_header_t *hdr)
{
	printf("Header:------------------\n");
	printf("cnt=%u, sz=%u, crc=0x%x, hdrcrc=0x%x\n", hdr->counter, hdr->data_size,
	       hdr->data_crc, hdr->hdr_crc);
}

static void nvram_node_free(struct nvram_node *node)
{
	if (node) {
		if (node->key)
			free(node->key);
		if (node->value)
			free(node->value);
		free(node);
	}
}

static int nvram_node_marshal(uint8_t *to, struct nvram_node *node)
{
	uint32_t lens[2];
	lens[0] = strnlen(node->key, NVRAM_ENTRY_MAX_LEN);
	lens[1] = strnlen(node->value, NVRAM_ENTRY_MAX_LEN);
	if (lens[0] == NVRAM_ENTRY_MAX_LEN || lens[1] == NVRAM_ENTRY_MAX_LEN) {
		libdebug("Entry value exceeds max length %d\n", NVRAM_ENTRY_MAX_LEN);
		return -ENOMEM;
	}
	memcpy(to, &lens[0], sizeof(uint32_t));
	memcpy(to + sizeof(uint32_t), &lens[1], sizeof(uint32_t));
	memcpy(to + NVRAM_ENTRY_DATA_OFFSET, node->key, lens[0]);
	memcpy(to + NVRAM_ENTRY_DATA_OFFSET + lens[0], node->value, lens[1]);
	return NVRAM_ENTRY_DATA_OFFSET + lens[0] + lens[1];
}

static int nvram_node_unmarshal(const uint8_t *from, struct nvram_node **node)
{
	int r;
	uint32_t lens[2];
	memcpy(&lens[0], from, sizeof(uint32_t));
	memcpy(&lens[1], from + sizeof(uint32_t), sizeof(uint32_t));
	if (lens[0] == NVRAM_ENTRY_MAX_LEN || lens[1] == NVRAM_ENTRY_MAX_LEN) {
		libdebug("Entry value exceeds max length %d\n", NVRAM_ENTRY_MAX_LEN);
		return -EINVAL;
	}
	int consumed = NVRAM_ENTRY_DATA_OFFSET + lens[0] + lens[1];
	struct nvram_node *new_node = malloc(sizeof(struct nvram_node));
	if (!new_node) {
		r = -ENOMEM;
		goto err;
	}
	memset(new_node, 0, sizeof(struct nvram_node));
	new_node->key = malloc(lens[0] + 1);
	if (!new_node->key) {
		r = -ENOMEM;
		goto err;
	}

	memcpy(new_node->key, from + NVRAM_ENTRY_DATA_OFFSET, lens[0]);
	new_node->key[lens[0]] = '\0';

	new_node->value = malloc(lens[1] + 1);
	if (!new_node->value) {
		r = -ENOMEM;
		goto err;
	}
	memcpy(new_node->value, from + NVRAM_ENTRY_DATA_OFFSET + lens[0], lens[1]);
	new_node->value[lens[1]] = '\0';
	*node = new_node;
	return consumed;
err:
	nvram_node_free(new_node);
	return r;
}

static struct nvram_node *nvram_node_create(const char *key, const char *value)
{
	int r;
	uint32_t lens[2];
	lens[0] = strlen(key);
	lens[1] = strlen(value);
	struct nvram_node *new_node = malloc(sizeof(struct nvram_node));
	if (!new_node) {
		r = -ENOMEM;
		goto err;
	}
	memset(new_node, 0, sizeof(struct nvram_node));
	new_node->key = malloc(lens[0] + 1);
	if (!new_node->key) {
		r = -ENOMEM;
		goto err;
	}
	strcpy(new_node->key, key);
	new_node->value = malloc(lens[1] + 1);
	if (!new_node->value) {
		r = -ENOMEM;
		goto err;
	}
	strcpy(new_node->value, value);
	return new_node;
err:
	nvram_node_free(new_node);
	return ERR_PTR(r);
}

/* Calculate size needed for all entries in linked node */
static uint32_t calc_serialized_entries_size(const struct nvram_node *node)
{
	uint32_t size = 0;
	while (node) {
		size += NVRAM_ENTRY_HEADER_SIZE + strlen(node->key) + strlen(node->value);
		node = node->next;
	}
	return size;
}

static int nvram_header_marshal(uint8_t *to, nvram_header_t *hdr)
{
	memcpy(to, hdr, sizeof(nvram_header_t));
	return sizeof(nvram_header_t);
}

static int nvram_header_unmarshal(const uint8_t *from, nvram_header_t *hdr)
{
	memcpy(hdr, from, sizeof(nvram_header_t));
	return sizeof(nvram_header_t);
}

void destroy_nvram_list(struct nvram_list *list)
{
	if (list->entry) {
		struct nvram_node *cur = list->entry;
		struct nvram_node *next = NULL;
		while (cur) {
			next = cur->next;
			nvram_node_free(cur);
			cur = next;
		}
		list->entry = NULL;
		list->dirty = 1;
	}
}

static void nvram_list_append(struct nvram_list *list, struct nvram_node *node)
{
	if (!list->entry) {
		list->entry = node;
		list->dirty = 1;
		return;
	}
	struct nvram_node *it = list->entry;
	while (it->next) {
		it = it->next;
	}
	it->next = node;
	list->dirty = 1;
}

int nvram_list_delete(struct nvram_list *list, struct nvram_node *node)
{
	if (!list->entry) {
		return -ENOENT;
	}
	if (node == list->entry) {
		list->entry = node->next;
		nvram_node_free(node);
		list->dirty = 1;
		return 0;
	}

	struct nvram_node *it = list->entry;
	while (it->next) {
		if (it->next == node) {
			it->next = node->next;
			nvram_node_free(node);
			list->dirty = 1;
			return 0;
		}
		it = it->next;
	}
	return -ENOENT;
}

int nvram_list_set(struct nvram_list *list, const char *key, const char *value)
{
	libdebug("%s=%s\n", key, value);
	struct nvram_node *node = nvram_list_get(list, key);
	if (node && !strcmp(node->value, value)) {
		libdebug("%s => %s exists - NOP\n", key, value);
		return 0;
	}
	if (node) {
		libdebug("Delete key %s\b", node->key);
		nvram_list_delete(list, node);
	}

	libdebug("Create node\n");
	node = nvram_node_create(key, value);
	if (IS_ERR_OR_NULL(node)) {
		return PTR_ERR(node);
	}
	libdebug("Append node\n");
	nvram_list_append(list, node);
	return 0;
}

struct nvram_node *nvram_list_get(const struct nvram_list *list, const char *key)
{
	struct nvram_node *it = list->entry;
	while (it) {
		if (!strcmp(key, it->key)) {
			return it;
		}
		it = it->next;
	}
	return NULL;
}

int nvram_list_remove(struct nvram_list *list, const char *key)
{
	struct nvram_node *node = nvram_list_get(list, key);
	if (!node)
		return -ENOENT;
	nvram_list_delete(list, node);
	return 0;
}

int is_valid_nvram_section(const uint8_t *data, nvram_header_t *hdr)
{
	nvram_header_unmarshal(data, hdr);
	uint32_t hdr_crc = calc_crc32(NVRAM_CRC32_INIT, (const uint8_t *)hdr,
				      sizeof(nvram_header_t) - sizeof(uint32_t));
	if (hdr->hdr_crc != hdr_crc) {
		libdebug("CRC error in header\n");
		return 0;
	}
	return 1;
}

int nvram_section_deserialize(nvram_header_t *hdr, struct nvram_list *list, const uint8_t *data)
{
	if (!data) {
		libdebug("data empty\n");
		return -EINVAL;
	}
	if (list->entry) {
		libdebug("list not empty\n");
		return -EINVAL;
	}
	uint32_t offset = 0;
	int nodes = 0;
	struct nvram_node *tail = 0;

	while (offset < hdr->data_size) {
		struct nvram_node *node;
		int r = nvram_node_unmarshal(data + offset, &node);
		if (r < 0) {
			libdebug("Failed creating node\n");
			return r;
		}
		if (!tail) {
			list->entry = node;
		} else {
			tail->next = node;
		}
		tail = node;
		libdebug("Found node %s=%s\n", node->key, node->value);
		nodes++;
		offset += r;
	}

	libdebug("Added %d nodes\n", nodes);
	return 0;
}

int nvram_section_serialize_size(const struct nvram_list *list, uint32_t *size)
{
	if (!list) {
		libdebug("list empty\n");
		return -EINVAL;
	}
	const uint32_t data_len = calc_serialized_entries_size(list->entry);
	const uint32_t buf_len = sizeof(nvram_header_t) + data_len;
	*size = buf_len;
	return 0;
}

int nvram_section_serialize(const struct nvram_list *list, uint32_t counter, uint8_t *data,
			    uint32_t size)
{
	libdebug("...\n");
	if (!list) {
		libdebug("list empty\n");
		return -EINVAL;
	}
	nvram_header_t hdr;
	hdr.counter = counter;
	hdr.data_size = calc_serialized_entries_size(list->entry);
	libdebug("Data set is %u, counter is %u\n", hdr.data_size, hdr.counter);
	const uint32_t buf_len = sizeof(nvram_header_t) + hdr.data_size;
	if (size < buf_len) {
		libdebug("data buffer too small\n");
		return -EINVAL;
	}

	struct nvram_node *cur = list->entry;
	for (uint32_t i = sizeof(nvram_header_t); cur; cur = cur->next) {
		i += nvram_node_marshal(data + i, cur);
	}
	hdr.data_crc = calc_crc32(NVRAM_CRC32_INIT, data + sizeof(nvram_header_t), hdr.data_size);
	hdr.hdr_crc = calc_crc32(NVRAM_CRC32_INIT, (const uint8_t *)&hdr,
				 sizeof(nvram_header_t) - sizeof(uint32_t));

	nvram_header_marshal(data, &hdr);
	return 0;
}

int is_valid_nvram_data(nvram_header_t *hdr, uint8_t *data)
{
	uint32_t crc = calc_crc32(NVRAM_CRC32_INIT, data, hdr->data_size);
	return (crc == hdr->data_crc) ? 1 : 0;
}
