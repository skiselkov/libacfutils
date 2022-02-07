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
 * Copyright 2022 Saso Kiselkov. All rights reserved.
 */

#include <errno.h>
#include <string.h>
#include <stdio.h>

#if	IBM
#include <windows.h>
#include <wincrypt.h>
#include <bcrypt.h>
#include <ntstatus.h>
#endif	/* !IBM */

#include <acfutils/core.h>
#include <acfutils/helpers.h>
#include <acfutils/log.h>
#include <acfutils/osrand.h>

/*
 * Generates high-quality random numbers using the OS'es own PRNG. Returns
 * B_TRUE iff `buf' has been successfully populated with `len' bytes.
 * Use this to generate high-quality encryption key material.
 *
 * Be sparing with this function. High-quality random numbers are expensive
 * and/or slow to generate in large quantities. For a low-quality but cheap
 * PRNG, see crc64.c.
 */
bool_t
osrand(void *buf, size_t len)
{
#if	IBM
	HCRYPTPROV prov;
	/*
	 * First try the New-New RNG API. MS, will you ever stop making new
	 * APIs for old functionality?
	 */
	if (BCryptGenRandom(NULL, buf, len, BCRYPT_USE_SYSTEM_PREFERRED_RNG) ==
	    STATUS_SUCCESS) {
		return (B_TRUE);
	}
	/*
	 * If that fails, try the crypto interface.
	 */
	if (!CryptAcquireContext(&prov, NULL, NULL, PROV_RSA_FULL, 0) &&
	    !CryptAcquireContext(&prov, NULL, NULL, PROV_RSA_FULL,
	    CRYPT_NEWKEYSET) &&
	    !CryptAcquireContext(&prov, NULL, NULL, PROV_RSA_FULL,
	    CRYPT_NEWKEYSET | CRYPT_MACHINE_KEYSET)) {
		win_perror(GetLastError(), "Error generating random data: "
		    "error during CryptAcquireContext");
		return (B_FALSE);
	}
	if (!CryptGenRandom(prov, len, buf)) {
		win_perror(GetLastError(), "Error generating random data: "
		    "error during CryptGenRandom");
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
