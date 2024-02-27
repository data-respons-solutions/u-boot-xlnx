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

#include <linux/sizes.h>

#define xstr(s) str(s)
#define str(s) #s

/* #define CONFIG_ARMV8_SWITCH_TO_EL1 */
/* Generic Interrupt Controller Definitions */
#define GICD_BASE	0xF9010000
#define GICC_BASE	0xF9020000

/* Generic Timer Definitions - setup in EL3. Setup by ATF for other cases */
#if !defined(COUNTER_FREQUENCY)
# define COUNTER_FREQUENCY		100000000
#endif


/* Serial setup */
#define CONFIG_ARM_DCC

#define CONFIG_SYS_BAUDRATE_TABLE \
	{ 4800, 9600, 19200, 38400, 57600, 115200 }

/* BOOTP options */
#define CONFIG_BOOTP_BOOTFILESIZE
#define CONFIG_BOOTP_MAY_FAIL

#ifdef CONFIG_NAND_ARASAN
# define CONFIG_SYS_MAX_NAND_DEVICE	1
# define CONFIG_SYS_NAND_ONFI_DETECTION
#endif

#if defined(CONFIG_ZYNQMP_USB)
#define DFU_DEFAULT_POLL_TIMEOUT	300
#define CONFIG_THOR_RESET_OFF
#endif
/* Monitor Command Prompt */
/* Console I/O Buffer Size */
#define CONFIG_SYS_CBSIZE		2048
#define CONFIG_SYS_BARGSIZE		CONFIG_SYS_CBSIZE

/* Ethernet driver */
#if defined(CONFIG_ZYNQ_GEM)
# define CONFIG_SYS_FAULT_ECHO_LINK_DOWN
# define PHY_ANEG_TIMEOUT       20000
#endif

#define CONFIG_SYS_I2C_RTC_ADDR 0x51

#define CONFIG_BOARD_EARLY_INIT_F

#define CONFIG_IPADDR			20.20.5.2
#define CONFIG_NETMASK			255.255.255.252
#define CONFIG_SERVERIP			20.20.5.1

#define ALT_DFU "dfu_alt_info=ram 0=fitimage ram 0x10000000 0x10000000&sf 0:0:3000000:0=boot-pri raw 0x0 0x1000000;boot-sec raw 0x1000000 0x1000000\0"

#define XX "VAL="filnavn

#define CONFIG_EXTRA_ENV_SETTINGS \
	"ethaddr=00:30:64:34:00:01\0" \
	"loadaddr=0x10000000\0" \
	"kernel_addr=0x80000\0" \
	"initrd_addr=0xa00000\0" \
	"initrd_size=0x2000000\0" \
	"fdt_addr=4000000\0" \
	"fdt_high=0x10000000\0" \
	"bitstream_qspi=0x1000000\0" \
	"bitstream_load=0x1000000\0" \
	"bitstream_file=/boot/fpga.bit\0" \
	"loadbootenv_addr=0x100000\0" \
	"sdbootdev=0\0" \
	"partid=1\0" \
	"bootenv_part=3\0" \
	"boot_part=1\0" \
	"rauc_slot=A\0" \
	"tftp_file=initrd-installer.itb\0" \
	"dtb_file=/boot/ftkn-zynqmp.dtb\0" \
	"loadkernel=ext4load mmc 0:${boot_part} ${kernel_addr} /boot/Image\0" \
	"loaddtb=ext4load mmc 0:${boot_part} ${fdt_addr} ${dtb_file}\0" \
	CONFIG_XTKN_FIT_CONF \
	ALT_DFU \
	"mtdparts=" CONFIG_MTDPARTS_DEFAULT "\0" \
	"default_bootargs=earlycon clk_ignore_unused root=/dev/mmcblk0p${boot_part} ro rootwait rauc.slot=${rauc_slot}\0" \
	"bootenv=/boot/uEnv.txt\0" \
	"divert_flag=/boot/divert\0" \
	"loadbootenv=load mmc ${sdbootdev}:${bootenv_part} ${loadbootenv_addr} ${bootenv}\0" \
	"importbootenv=echo Importing environment from SD ...; env import -t ${loadbootenv_addr} $filesize\0" \
	"prog_fpga_ext4=ext4load mmc ${sdbootdev}:${boot_part} ${bitstream_load} ${bitstream_file}; " \
		"if fpga image_size 0 ${bitstream_load}; then " \
			"echo Loading fpga from MMC part ${boot_part}, size ${fpga_image_size}; " \
			"fpga load 0 ${bitstream_load} ${fpga_image_size}; " \
		"fi\0" \
	"prog_fpga_qspi=sf probe; sf read ${bitstream_load} ${bitstream_qspi} 0x1000; " \
		"if fpga image_size 0 ${bitstream_load}; then " \
			"echo Loading fpga from QSPI, size ${fpga_image_size}; " \
			"sf read ${bitstream_load} ${fpga_image_start} ${fpga_image_size}; " \
			"fpga load 0 ${bitstream_load} ${fpga_image_size}; " \
		"fi\0" \
	"prog_fpga=run prog_fpga_ext4 || run prog_fpga_qspi;\0" \
	"sd_uEnvtxt_existence_test=test -e mmc ${sdbootdev}:${bootenv_part} ${bootenv}\0" \
	"netboot=" \
		"setenv bootargs clk_ignore_unused rauc.external; " \
		"for n in 1 2 3 4; do " \
			"sleep 30; echo attempt $n on tftp boot; " \
			"tftpboot ${loadaddr} initrd-installer.itb && bootm ${loadaddr}#${bootconf} " \
		"done\0" \
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
	"check_part_legacy=" \
		"if run sd_uEnvtxt_existence_test; then " \
			"if run loadbootenv; then " \
				"echo Loaded environment from mmc ${sdbootdev}:${bootenv_part}${bootenv}; " \
				"env import -t ${loadbootenv_addr} $filesize mmc_boot_part && setenv boot_part ${mmc_boot_part}; " \
				"echo legacy boot part init is ${boot_part}; " \
				"rauc init; " \
			"fi; " \
		"fi\0" \
	"tftptimeout=50000\0" \
	"toggle_slots=" \
		"if test ${rauc_slot} = \"A\"; then " \
			"setenv rauc_slot B; setenv boot_part 2; " \
		"else " \
			"setenv rauc_slot A; setenv boot_part 1; " \
		"fi\0" \
	"setboot=setenv bootargs earlycon clk_ignore_unused root=/dev/mmcblk0p${boot_part} ro rootwait rauc.slot=${rauc_slot}\0" \
	"setdfuboot=setenv bootargs earlycon clk_ignore_unused rauc.external\0" \
	"boot_mode=0\0" \
	"check_boot_mode=gpio input 16 && setenv boot_mode 1\0"

#define BOOTCMD_UENV \
	"bootcmd_uenv=" \
	"if test -e mmc ${sdbootdev}:${bootenv_part} ${divert_flag}; then " \
		"echo diverting to tftpboot ...; " \
		"run bootcmd_netboot; " \
	"else " \
		"run uenvboot; " \
	"fi\0"

#undef CONFIG_BOOTCOMMAND
#define CONFIG_BOOTCOMMAND \
	"if test -e mmc ${sdbootdev}:${bootenv_part} ${divert_flag}; then " \
		"echo diverting to tftpboot ...; run netboot; " \
	"else " \
		"if test -e mmc ${sdbootdev}:1 /boot/initrd-installer.itb; then " \
			"echo booting from eMMC; " \
			"setenv bootargs clk_ignore_unused; load mmc 0: ${loadaddr} /boot/initrd-installer.itb; bootm ${loadaddr}#${bootconf}; " \
		"fi; " \
	"fi; " \
	"echo netboot; run netboot; " \
	"echo Failed, enter DFU mode; rauc init; run setdfuboot; dfu 0 && bootm ${loadaddr}#${bootconf}; " \
	"echo PANIC and reset; reset"

#endif /* __CONFIG_ZYNQMP_FTKN_H */
