/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license in the file COPYING
 * or http://www.opensource.org/licenses/CDDL-1.0.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file COPYING.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2017 Saso Kiselkov. All rights reserved.
 */

#include <string.h>

#include <acfutils/dsf.h>
#include <acfutils/log.h>

static dsf_cmd_cb_t cmd_cbs[NUM_DSF_CMDS];

static void
logfunc(const char *str)
{
	puts(str);
}

static void
cmd_cb(dsf_cmd_t cmd, const void *arg, const dsf_cmd_parser_t *parser)
{
	UNUSED(arg);
	UNUSED(parser);
	printf("cmd: %s (%x)\n", dsf_cmd2str(cmd), (int)parser->cmd_file_off);
}

int
main(int argc, char *argv[])
{
	dsf_t *dsf;
	char *dump;
	char reason[DSF_REASON_SZ];

	memset(cmd_cbs, 0, sizeof (cmd_cbs));
	for (int i = 0; i < NUM_DSF_CMDS; i++)
		cmd_cbs[i] = cmd_cb;

	log_init(logfunc, "dsfdump");

	if (argc != 2) {
		fprintf(stderr, "Usage: %s <dsf-file>\n", argv[0]);
		return (1);
	}

	dsf = dsf_init(argv[1]);
	if (dsf == NULL)
		return (1);

	dump = dsf_dump(dsf);
	puts(dump);
	free(dump);

	if (!dsf_parse_cmds(dsf, cmd_cbs, NULL, reason))
		fprintf(stderr, "Error parsing DSF commands: %s\n", reason);

	dsf_fini(dsf);

	return (0);
}
