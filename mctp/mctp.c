/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2020 Aspeed Technology Inc.
 */
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>
#include <getopt.h>
#include <string.h>
#include <termios.h>
#include <sys/mman.h>
#include "libmctp.h"

static void
usage(FILE *fp, int argc, char **argv)
{
	fprintf(fp,
		"Usage: %s [options]\n\n"
		"Options:\n"
		" -h | --help                 Print this message\n"
		" -t | --xfer                 mctp xfer\n"
		" -r | --recv                 mctp recv\n"
		" -i | --dest_id              destination endpoint ID\n"
		" -b | --bus                  destination pcie bus number\n"
		" -d | --dev                  destination pcie dev number\n"
		" -f | --fun                  destination pcie fun number\n"
		" -o | --routing              pcie routing type 0: route to RC, 2: route by ID, 3: Broadcase from RC\n"
		" -l | --length               bytes length >= 64\n"
		" -c | --continue             continue (0: loop, n: count number)\n"
		" -s | --som                  set som\n"
		" -e | --eom                  set eom\n"
		" example: tx : mctp -t -b 7 -d 0 -f 0 -o 2 -l 1024, rx : mctp -r\n"
		"",
		argv[0]);
}

static const char short_options [] = "htri:b:d:f:o:l:c:s:e:n:";

static const struct option
	long_options [] = {
	{ "help",	no_argument,		NULL,	'h' },
	{ "xfer",	no_argument,		NULL,	't' },
	{ "recv",	no_argument,		NULL,	'r' },
	{ "id",		required_argument,	NULL,	'i' },
	{ "bus",	required_argument,	NULL,	'b' },
	{ "dev",	required_argument,	NULL,	'd' },
	{ "fun",	required_argument,	NULL,	'f' },
	{ "routing",	required_argument,	NULL,	'o' },
	{ "length",	required_argument,	NULL,	'l' },
	{ "som",	required_argument,	NULL,	's' },
	{ "eom",	required_argument,	NULL,	'e' },
	{ "node",	required_argument,	NULL,	'n' },
	{ "continue",	required_argument,	NULL,	'c' },
	{ 0, 0, 0, 0 }
};

int aspeed_mctp_tx(int length, int bus, int dev, int fun, int eid, int routing, int som, int eom)
{
	struct aspeed_mctp_xfer xfer;
	struct pcie_vdm_header *header = &xfer.header;
	unsigned char *mctp_header = (unsigned char *) &xfer.header;
	unsigned int data_len = 0;
	unsigned char pattern = 0;
	unsigned char sed = 0;
	int ret;
	int i;
	int debug = 1;


	data_len = length - 16;
	pattern = rand();
	sed = 0x01;

	memset(&xfer, 0, sizeof(struct aspeed_mctp_xfer));
	//dwords align
	if (length > 16)
		xfer.xfer_buff = malloc((length + 3) & ~3);
	else
		xfer.xfer_buff = malloc(16);


	//prepare default vdm header
	header->length = ((length + 3) & ~3) / 4;
	header->attr = 0x1;
	header->ep = 0x0;
	header->td = 0x0;
	header->tc = 0x0;
	header->type_routing = 0x10 | (routing & 0x7);
	header->fmt =  0x3;
	header->message_code = 0x7f;
	header->vdm_code = 0x0;

	header->pad_len = ((length + 3) & ~3) - length;

	header->vender_id = 0x1ab4;

	header->pcie_target_id = ((bus & 0xff) << 8) |
				 ((dev & 0x1f) << 3) |
				 ((fun & 0x7));

	header->msg_tag = 0;
	header->to = 0;
	header->pkt_seq = 0;
	header->eom = eom;
	header->som = som;
	header->src_epid = 0;
	header->dest_epid = eid & 0xff;
	header->header_ver = 0x1;
	/* 0 ~ 13 : fix header, 14: sed, 15 : pattern, data pad load */
	//generate data
	xfer.xfer_buff[0] = 0x1;
	xfer.xfer_buff[1] = 0x2;
	xfer.xfer_buff[2] = 0x3;
	xfer.xfer_buff[3] = 0x4;

	//header pattern;
	for (i = 4; i < 10; i++) {
		xfer.xfer_buff[i] = 0x5a;
	}

	xfer.xfer_buff[10] = 0;
	xfer.xfer_buff[11] = 0;
	xfer.xfer_buff[12] = 0;
	xfer.xfer_buff[13] = 0;
	xfer.xfer_buff[14] = sed;
	xfer.xfer_buff[15] = pattern;

	for (i = 16; i < (data_len + 16); i++) {
		xfer.xfer_buff[i] = (xfer.xfer_buff[i - 1] + sed) % 0xff;
	}
	// for (i = 0; i < length; i++) {
	// 	xfer.xfer_buff[i] = i % 0xff;
	// }
	if (debug) {
		printf("mctp_tx pattern : 0x%02x \n   sed : 0x%02x \n   len : 0x%02x \n", xfer.xfer_buff[15], xfer.xfer_buff[14], xfer.xfer_buff[13]);

		printf("mctp header -- \n");
		for (i = 0; i < 16; i++)
			printf("%02x ", mctp_header[i]);

		printf("\n");
		printf("mctp buff  -- \n");
		printf("----- TX PADLOAD - HEADER ---- \n");
		for (i = 0; i < 16; i++) {
			printf("%02x ", xfer.xfer_buff[i]);
		}
		printf("\n");

		printf("----- PADLOAD - DATA ---- \n");
		for (i = 16; i < (data_len + 16); i++) {
			if (i % 16 == 0)
				printf("\n");
			printf("%02x ", xfer.xfer_buff[i]);
		}

		printf("\n");
	}

	ret = aspeed_mctp_send(&xfer);
	free(xfer.xfer_buff);

	if (ret < 0)
		return 1;
	else
		return 0;
}

int aspeed_mctp_rx(struct aspeed_mctp_xfer *xfer)
{
	struct pcie_vdm_header *header;
	unsigned char *vmd_header;
	int recv_length;
	unsigned char sed = 0;
	unsigned char pattern = 0;
	int i;
	int ret;
	int cmp_err = 0;
	int seq_rx = 0;
	int mctp_seq_num = 0;
	int mctp_tag = 0;
	int debug = 1;
//
next_mctp:
	xfer->header.length = 0;
	vmd_header = (unsigned char *)xfer;
	memset(&xfer->header, 0, sizeof(struct pcie_vdm_header));

	ret = aspeed_mctp_recv(xfer);
	if (ret < 0) {
		goto rx_out;
		return -1;
	}

	if (!xfer->header.length)
		return -1;

	if (debug) printf("MCTP Header :som [%d], eom [%d], tag [%d], seq [%d]  \n", xfer->header.som, xfer->header.eom, xfer->header.msg_tag, xfer->header.pkt_seq);

	if (xfer->header.som) {
		mctp_tag = xfer->header.msg_tag;
		mctp_seq_num = xfer->header.pkt_seq;
		if (!xfer->header.eom) {
			mctp_seq_num++;
			mctp_seq_num &= 0x3;
		}
	} else {
		if (mctp_tag != xfer->header.msg_tag) {
			printf("mctp tag error ");
			cmp_err = 1;
			return 1;
		}
		if (mctp_seq_num != xfer->header.pkt_seq) {
			printf("mctp seq error mctp_seq_num %d , xfer->header.pkt_seq %d \n", mctp_seq_num, xfer->header.pkt_seq);
			cmp_err = 1;
			return 1;
		} else {
			mctp_seq_num++;
			mctp_seq_num &= 0x3;
		}
	}

	header = &xfer->header;
	vmd_header = (unsigned char *)&xfer->header;

	if (debug) {
		for (i = 0; i < 16; i++)
			printf("%02x ", vmd_header[i]);

		printf("\n=========================================\n");
	}

	recv_length = (header->length * 4) - header->pad_len;

	if (!xfer->header.som) {
		goto padload_dump;
	}

	if (xfer->xfer_buff[0] != 0x1 ||
	    xfer->xfer_buff[1] != 0x2 ||
	    xfer->xfer_buff[2] != 0x3 ||
	    xfer->xfer_buff[3] != 0x4)
		cmp_err = 1;

	for (i = 4; i < 10; i++)
		if (xfer->xfer_buff[i] != 0x5a)
			cmp_err = 1;

	for (i = 10; i < 12; i++)
		if (xfer->xfer_buff[i] != 0x0)
			cmp_err = 1;

	if (cmp_err)
		printf("\nPadload HEADER Error !!\n");

	sed = xfer->xfer_buff[14];
	pattern = xfer->xfer_buff[15];

	if (debug) printf("----- RX PADLOAD len [%d] - HEADER ---- \n", recv_length);

padload_dump:

	if (debug) {

		printf("\n");
		for (i = 0; i < 16; i++) {
			printf("%02x ", xfer->xfer_buff[i]);
		}

		if (xfer->header.som)
			printf("\n----- PADLOAD - DATA ---- ");
	}

	for (i = 16; i < recv_length; i++) {
		if (xfer->header.som) {
			if (xfer->xfer_buff[i] != ((xfer->xfer_buff[i - 1] + sed) % 0xff)) {
				//printf("CMP Error idx %d : !! %x  != %x \n", i, xfer->xfer_buff[i], (xfer->xfer_buff[i - 1] + sed) % 0xff);
				cmp_err = 1;
			}
		}
		if (debug) {

			if (i % 16 == 0)
				printf("\n");

			printf("%02x ", xfer->xfer_buff[i]);
		}
	}

	if (debug) printf("\n");

	if (!xfer->header.eom) {
		seq_rx = 1;
		goto next_mctp;
	}

	if (cmp_err == 1) {
		return 1;
	}

rx_out:
	return 0;

}

int main(int argc, char *argv[])
{
	char dev_name[100] = "/dev/aspeed-mctp0";
	int xfer_dir = 0;
	int cont = 1;
	int times = 0;
	int length = 0;
	int ret;
	int som = 1;
	int eom = 1;
	int BUS = 0;
	int DEV = 0;
	int FUN = 0;
	int EID = 0;
	int routing = 2;
	char option;
	int infinity_loop = 0;
	int rx_flag = 0;
	struct aspeed_mctp_xfer mctp_xfer;

	if (!argv[1]) {
		usage(stdout, argc, argv);
		exit(EXIT_FAILURE);
	}

	while ((option = getopt_long(argc, argv, short_options, long_options, NULL)) != (char) -1) {
		switch (option) {
		case 'h':
			usage(stdout, argc, argv);
			exit(EXIT_SUCCESS);
			break;
		case 'n':
			strcpy(dev_name, optarg);
			if (!strcmp(dev_name, "")) {
				printf("No dev file name!\n");
				usage(stdout, argc, argv);
				exit(EXIT_FAILURE);
			}
			break;
		case 't':
			xfer_dir = 1;
			break;
		case 'r':
			xfer_dir = 0;
			break;
		case 'i':
			EID = strtoul(optarg, 0, 0);
			break;
		case 'b':
			BUS = strtoul(optarg, 0, 0);
			break;
		case 'd':
			DEV = strtoul(optarg, 0, 0);
			break;
		case 'f':
			FUN = strtoul(optarg, 0, 0);
			break;
		case 'o':
			routing = strtoul(optarg, 0, 0);
			break;
		case 'l':
			length = strtoul(optarg, 0, 0);
			break;
		case 'c':
			cont = strtoul(optarg, 0, 0);
			if (cont == 0) infinity_loop = 1;
			break;
		case 's':
			som = strtoul(optarg, 0, 0);
			break;
		case 'e':
			eom = strtoul(optarg, 0, 0);
			break;
		default:
			usage(stdout, argc, argv);
			exit(EXIT_FAILURE);
			break;
		}
	}

	if ((!length) && xfer_dir) {
		usage(stdout, argc, argv);
		return -1;
	}

	ret = aspeed_mctp_init(dev_name);
	if (ret < 0) {
		printf("Please check mctp driver \n");
		return ret;
	}

	srand(time(NULL));

	mctp_xfer.xfer_buff = malloc(ASPEED_MCTP_XFER_SIZE);

	if (!xfer_dir)
		times = 0;

	while (1) {
		if (xfer_dir) {
			if (aspeed_mctp_tx(length, BUS, DEV, FUN, EID, routing, som, eom))
				break;
			if (!infinity_loop) {
				cont--;
				if (cont == 0)
					break;
			}
		} else {
			rx_flag = aspeed_mctp_rx(&mctp_xfer);
			if (rx_flag == 1) {
				printf("[%d] : Fail \n", times);
				break;
			} else if (rx_flag == 0) {
				printf("[%d] : PASS \n", times);
				times++;
			} else {
				if (times >= cont && cont != 0)
					break;
			}
		}

	}

	aspeed_mctp_exit();

	free(mctp_xfer.xfer_buff);
	return 0;
}
