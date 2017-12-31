/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
*/
/*
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include <errno.h>
#include <string.h>
#include <stdio.h>

#if	IBM
#include <windows.h>
#include <wincrypt.h>
#endif	/* !IBM */

#include <acfutils/log.h>
#include <acfutils/osrand.h>

/*
 * Generates high-quality random numbers using the OS'es own RNG. Returns
 * B_TRUE iff `buf' has been successfully populated with `len' bytes.
 */
bool_t
osrand(uint8_t *buf, size_t len)
{
#if	IBM
	HCRYPTPROV prov;

	if (!CryptAcquireContext(&prov, NULL, NULL, PROV_RSA_FULL, 0)) {
		logMsg("Error generating random data: error during "
		    "CryptAcquireContext");
		return (B_FALSE);
	}
	if (!CryptGenRandom(prov, len, buf)) {
		logMsg("Error generating random data: error during "
		    "CryptGenRandom");
		CryptReleaseContext(prov, 0);
		return (B_FALSE);
	}
	CryptReleaseContext(prov, 0);
	return (B_TRUE);
#else	/* !IBM */
	FILE *fp = fopen("/dev/random", "rb");

	if (fp == NULL) {
		logMsg("Error reading /dev/random to generate random data: %s",
		    strerror(errno));
		return (B_FALSE);
	}
	if (fread(buf, 1, len, fp) != len) {
		logMsg("Error reading /dev/random to generate random data: %s",
		    strerror(errno));
		fclose(fp);
		return (B_FALSE);
	}
	fclose(fp);
	return (B_TRUE);
#endif	/* !IBM */
}
