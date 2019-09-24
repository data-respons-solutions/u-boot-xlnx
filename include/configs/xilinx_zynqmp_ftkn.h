/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Configuration for Xilinx ZynqMP Flash utility
 *
 * (C) Copyright 2018 Xilinx, Inc.
 * Michal Simek <michal.simek@xilinx.com>
 * Siva Durga Prasad Paladugu <sivadur@xilinx.com>
 */

#ifndef __CONFIG_ZYNQMP_FTKN_H
#define __CONFIG_ZYNQMP_FTKN_H

#define xstr(s) str(s)
#define str(s) #s

#define CONFIG_IPADDR			20.20.5.2
#define CONFIG_NETMASK			255.255.255.252
#define CONFIG_SERVERIP			20.20.5.1


#define CONFIG_EXTRA_ENV_BOARD_SETTINGS \
	"ethaddr=00:30:64:34:00:01\0" \
	"kernel_addr=0x80000\0" \
	"initrd_addr=0xa00000\0" \
	"initrd_size=0x2000000\0" \
	"fdt_addr=4000000\0" \
	"fdt_high=0x10000000\0" \
	"loadbootenv_addr=0x100000\0" \
	"sdbootdev=0\0" \
	"partid=1\0" \
	"bootenv_part=3\0" \
	"kernel_offset=0x280000\0" \
	"fdt_offset=0x200000\0" \
	"kernel_size=0x1e00000\0" \
	"fdt_size=0x80000\0" \
	"tftp_file=initrd-installer.itb\0" \
	"bootenv=/boot/uEnv.txt\0" \
	"divert_flag=/boot/divert\0" \
	"loadbootenv=load mmc ${sdbootdev}:${bootenv_part} ${loadbootenv_addr} ${bootenv}\0" \
	"importbootenv=echo Importing environment from SD ...; " \
		"env import -t ${loadbootenv_addr} $filesize\0" \
	"sd_uEnvtxt_existence_test=test -e mmc ${sdbootdev}:${bootenv_part} ${bootenv}\0" \
	"netboot=setenv bootargs clk_ignore_unused; sleep 60; tftpboot 10000000 initrd-installer.itb && bootm\0" \
	"qspiboot=sf probe 0 0 0 && sf read $fdt_addr $fdt_offset $fdt_size && " \
		  "sf read $kernel_addr $kernel_offset $kernel_size && " \
		  "booti $kernel_addr - $fdt_addr\0" \
	"uenvboot=" \
		"run sd_uEnvtxt_existence_test || setenv bootenv_part 1; " \
		"if run sd_uEnvtxt_existence_test; then " \
			"run loadbootenv; " \
			"echo Loaded environment from mmc ${sdbootdev}:${bootenv_part}${bootenv}; " \
			"run importbootenv; " \
		"fi; " \
		"if test -n ${uenvcmd}; then " \
			"echo Running uenvcmd ...; " \
			"run uenvcmd; " \
		"fi\0" \
	"jtagboot=tftpboot 80000 Image && tftpboot $fdt_addr system.dtb && " \
		 "tftpboot 6000000 rootfs.cpio.ub && booti 80000 6000000 $fdt_addr\0" \
	"nosmp=setenv bootargs $bootargs maxcpus=1\0" \
	"qspiboot=sf probe 0 0 0 && sf read $fdt_addr $fdt_offset $fdt_size && " \
	  "sf read $kernel_addr $kernel_offset $kernel_size && " \
	  "booti $kernel_addr - $fdt_addr\0" \
	"tftptimeout=50000\0"

#include <configs/xilinx_zynqmp.h>


#undef BOOT_TARGET_DEVICES
#define BOOTENV_DEV_NETBOOT(devtypeu, devtypel, instance) \
	"bootcmd_netboot=" \
	"while true ; do " \
		"echo Trying TFTP boot from ${serverip} using ${tftp_file}; " \
		"run netboot; " \
	"done\0"
#define BOOTENV_DEV_NAME_NETBOOT(devtypeu, devtypel, instance) "netboot "
#define BOOT_TARGET_DEVICES_NETBOOT(func)	func(NETBOOT, na, na)

#define BOOTENV_DEV_UENV(devtypeu, devtypel, instance) \
	"bootcmd_uenv=" \
	"if test -e mmc ${sdbootdev}:${bootenv_part} ${divert_flag}; then " \
		"echo diverting to tftpboot ...; " \
		"run bootcmd_netboot; " \
	"else " \
		"run uenvboot; " \
	"fi\0"

#define BOOTENV_DEV_NAME_UENV(devtypeu, devtypel, instance) "uenv "
#define BOOT_TARGET_DEVICES_UENV(func)	func(UENV, uenvboot, na)

#define BOOT_TARGET_DEVICES(func) \
	BOOT_TARGET_DEVICES_UENV(func) \
	BOOT_TARGET_DEVICES_NETBOOT(func) \
	BOOT_TARGET_DEVICES_QSPI(func)

#endif /* __CONFIG_ZYNQMP_FTKN_H */
