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
 * Copyright 2017 Saso Kiselkov. All rights reserved.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#if	IBM
#include <windows.h>
#include <dbghelp.h>
#include <psapi.h>
#else
#include <execinfo.h>   /* used for stack tracing */
#endif	/* !IBM */

#include <acfutils/assert.h>
#include <acfutils/helpers.h>
#include <acfutils/log.h>

#define	DATE_FMT	"%Y-%m-%d %H:%M:%S"
#define	PREFIX_FMT	"%s %s[%s:%d]: ", timedate, log_prefix, filename, line

static logfunc_t log_func = NULL;
static const char *log_prefix = NULL;

void
log_init(logfunc_t func, const char *prefix)
{
	/* Can't use VERIFY here, since it uses this logging interface. */
	if (func == NULL || prefix == NULL)
		abort();
	log_func = func;
	log_prefix = prefix;
}


void
log_impl(const char *filename, int line, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	log_impl_v(filename, line, fmt, ap);
	va_end(ap);
}

void
log_impl_v(const char *filename, int line, const char *fmt, va_list ap)
{
	va_list ap_copy;
	char timedate[32];
	char *buf;
	size_t prefix_len, len;
	struct tm *tm;
	time_t t;

	t = time(NULL);
	tm = localtime(&t);
	VERIFY(strftime(timedate, sizeof (timedate), DATE_FMT, tm) != 0);

	/* Can't use VERIFY here, since it uses this logging interface. */
	if (log_func == NULL || log_prefix == NULL)
		abort();

	prefix_len = snprintf(NULL, 0, PREFIX_FMT);
	va_copy(ap_copy, ap);
	len = vsnprintf(NULL, 0, fmt, ap_copy);

	buf = (char *)malloc(prefix_len + len + 2);

	(void) snprintf(buf, prefix_len + 1, PREFIX_FMT);
	(void) vsnprintf(&buf[prefix_len], len + 1, fmt, ap);
	(void) sprintf(&buf[strlen(buf)], "\n");
	log_func(buf);

	free(buf);
}

#define	MAX_STACK_FRAMES	128
#define	MAX_MODULES		1024
#define	BACKTRACE_STR		"Backtrace is:\n"
#if	defined(__GNUC__) || defined(__clang__)
#define	BACKTRACE_STRLEN	__builtin_strlen(BACKTRACE_STR)
#else	/* !__GNUC__ && !__clang__ */
#define	BACKTRACE_STRLEN	strlen(BACKTRACE_STR)
#endif	/* !__GNUC__ && !__clang__ */

#if	IBM
/*
 * Since while dumping stack we are most likely in a fairly compromised
 * state, we statically pre-allocate these buffers to try and avoid having
 * to call into the VM subsystem.
 */
#define	MAX_SYM_NAME_LEN	1024
#define	MAX_BACKTRACE_LEN	(64 * 1024)
static char backtrace_buf[MAX_BACKTRACE_LEN] = { 0 };
static char symbol_buf[sizeof (SYMBOL_INFO) +
    MAX_SYM_NAME_LEN * sizeof (TCHAR)];
static char line_buf[sizeof (IMAGEHLP_LINE64)];
#endif	/* IBM */

#if	IBM

/*
 * Given a module path in `filename' and a relative module address in `addr',
 * attempts to resolve the symbol name and relative symbol address. This is
 * done by looking for a syms.txt file in the same directory as the module's
 * filename.
 * If found, the symbol name + symbol relative address is placed into
 * `symname' in the "symbol+offset" format.
 */
static void
find_symbol(const char *filename, void *addr, char *symname, size_t symname_cap)
{
	static char symstxtname[MAX_PATH];
	static char prevsym[128];
	const char *sep;
	FILE *fp;
	void *prevptr = NULL;
	void *image_base = NULL;

	*symname = 0;
	*prevsym = 0;

	sep = strrchr(filename, DIRSEP);
	if (sep == NULL)
		return;
	strlcpy(symstxtname, filename, MIN((uintptr_t)(sep - filename) + 1,
	    sizeof (symstxtname)));
	strncat(symstxtname, DIRSEP_S "syms.txt", sizeof (symstxtname));
	fp = fopen(symstxtname, "rb");
	if (fp == NULL)
		return;
	while (!feof(fp)) {
		char unused_c;
		void *ptr;
		static char sym[128];

		if (fscanf(fp, "%p %c %127s", &ptr, &unused_c, sym) != 3)
			break;
		if (strcmp(sym, "__image_base__") == 0) {
			image_base = ptr;
			continue;
		}
		ptr = (void *)(ptr - image_base);
		if (addr >= prevptr && addr < ptr) {
			snprintf(symname, symname_cap, "%s+%x", prevsym,
			    (unsigned)(addr - prevptr));
			break;
		}
		prevptr = ptr;
		strlcpy(prevsym, sym, sizeof (prevsym));
	}
	fclose(fp);
}

void
log_backtrace(void)
{
	unsigned frames;
	void *stack[MAX_STACK_FRAMES];
	SYMBOL_INFO *symbol;
	HANDLE process;
	DWORD displacement;
	IMAGEHLP_LINE64 *line;
	static HMODULE modules[MAX_MODULES];
	static MODULEINFO mi[MAX_MODULES];
	DWORD num_modules;
	static TCHAR filenameT[MAX_PATH];
	static char filename[MAX_PATH];

	process = GetCurrentProcess();
	SymInitialize(process, NULL, TRUE);
	EnumProcessModules(process, modules, sizeof (HMODULE) * MAX_MODULES,
	    &num_modules);
	num_modules = MIN(num_modules, MAX_MODULES);

	for (DWORD i = 0; i < num_modules; i++) {
		GetModuleInformation(process, modules[i], &mi[i],
		    sizeof (*mi));
	}

	frames = CaptureStackBackTrace(0, MAX_STACK_FRAMES, stack, NULL);

	memset(symbol_buf, 0, sizeof (symbol_buf));
	memset(line_buf, 0, sizeof (line_buf));

	symbol = (SYMBOL_INFO *)symbol_buf;
	symbol->MaxNameLen = MAX_SYM_NAME_LEN - 1;
	symbol->SizeOfStruct = sizeof (SYMBOL_INFO);

	line = (IMAGEHLP_LINE64 *)line_buf;
	line->SizeOfStruct = sizeof (*line);

	backtrace_buf[0] = '\0';
	strlcpy(backtrace_buf, BACKTRACE_STR, sizeof (backtrace_buf));

	for (unsigned i = 0; i < frames; i++) {
		/*
		 * This is needed because some dunce at Microsoft thought
		 * it'd be a swell idea to design the SymFromAddr function to
		 * always take a DWORD64 rather than a native pointer size.
		 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpointer-to-int-cast"
		DWORD64 address = (DWORD64)(stack[i]);
#pragma GCC diagnostic pop
		int fill = strlen(backtrace_buf);

		memset(symbol_buf, 0, sizeof (symbol_buf));
		/*
		 * Try to grab the symbol name from the stored %rip data.
		 */
		if (!SymFromAddr(process, address, 0, symbol)) {
			for (DWORD j = 0; j < num_modules; j++) {
				LPVOID start = mi[j].lpBaseOfDll;
				LPVOID end = start + mi[j].SizeOfImage;
				if (start <= stack[i] && end > stack[i]) {
					char symname[128];
					GetModuleFileName(modules[j],
					    filenameT, sizeof (filenameT) /
					    sizeof (TCHAR));
					WideCharToMultiByte(CP_UTF8, 0,
					    filenameT, -1, filename,
					    sizeof (filename), NULL, NULL);
					find_symbol(filename,
					    (void *)(stack[i] - start),
					    symname, sizeof (symname));
					fill += snprintf(&backtrace_buf[fill],
					    sizeof (backtrace_buf) - fill,
					    "%u %p %s+%x (%s)\n", i, stack[i],
					    filename,
					    (unsigned)(stack[i] - start),
					    symname);
					goto module_found;
				}
			}
			fill += snprintf(&backtrace_buf[fill],
			    sizeof (backtrace_buf) - fill,
			    "%u %p <unknown module>\n", i, stack[i]);
module_found:
			continue;
		}
		/*
		 * See if we have debug info available with file names and
		 * line numbers.
		 */
		if (SymGetLineFromAddr64(process, address, &displacement,
		    line)) {
			snprintf(&backtrace_buf[fill], sizeof (backtrace_buf) -
			    fill, "%u: %s (0x%x) [%s:%d]\n", i, symbol->Name,
			    symbol->Address, line->FileName, line->LineNumber);
		} else {
			snprintf(&backtrace_buf[fill], sizeof (backtrace_buf) -
			    fill, "%u: %s - 0x%x\n", i, symbol->Name,
			    symbol->Address);
		}
	}

	if (log_func == NULL)
		abort();
	log_func(backtrace_buf);
	fputs(backtrace_buf, stderr);
	fflush(stderr);
}

#else	/* !IBM */

void
log_backtrace(void)
{
	char *msg;
	size_t msg_len;
	void *trace[MAX_STACK_FRAMES];
	size_t i, j, sz;
	char **fnames;

	sz = backtrace(trace, MAX_STACK_FRAMES);
	fnames = backtrace_symbols(trace, sz);

	for (i = 1, msg_len = BACKTRACE_STRLEN; i < sz; i++)
		msg_len += snprintf(NULL, 0, "%s\n", fnames[i]);

	msg = (char *)malloc(msg_len + 1);
	strcpy(msg, BACKTRACE_STR);
	for (i = 1, j = BACKTRACE_STRLEN; i < sz; i++)
		j += sprintf(&msg[j], "%s\n", fnames[i]);

	if (log_func == NULL)
		abort();
	log_func(msg);
	fputs(msg, stderr);

	free(msg);
	free(fnames);
}

#endif	/* !IBM */
