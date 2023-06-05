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
 * Copyright 2023 Saso Kiselkov. All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>

#if	APL || LIN
#include <signal.h>
#include <err.h>
#else	/* !APL && !LIN */
#include <windows.h>
#endif	/* !APL && !LIN */

#include "acfutils/assert.h"
#include "acfutils/except.h"
#include "acfutils/helpers.h"
#include "acfutils/log.h"

static bool_t inited = B_FALSE;

#if	APL || LIN

static struct sigaction old_sigsegv = {};
static struct sigaction old_sigabrt = {};
static struct sigaction old_sigfpe = {};
static struct sigaction old_sigint = {};
static struct sigaction old_sigill = {};
static struct sigaction old_sigterm = {};

static const char *
sigfpe2str(int si_code)
{
	switch (si_code) {
	case FPE_INTDIV:
		return ("integer divide by zero");
	case FPE_INTOVF:
		return ("integer overflow");
	case FPE_FLTDIV:
		return ("floating-point divide by zero");
	case FPE_FLTOVF:
		return ("floating-point overflow");
	case FPE_FLTUND:
		return ("floating-point underflow");
	case FPE_FLTRES:
		return ("floating-point inexact result");
	case FPE_FLTINV:
		return ("floating-point invalid operation");
	case FPE_FLTSUB:
		return ("subscript out of range");
	default:
		return ("general arithmetic exception");
	}
}

static const char *
sigill2str(int si_code)
{
	switch(si_code) {
	case ILL_ILLOPC:
		return ("illegal opcode");
	case ILL_ILLOPN:
		return ("illegal operand");
	case ILL_ILLADR:
		return ("illegal addressing mode");
	case ILL_ILLTRP:
		return ("illegal trap");
	case ILL_PRVOPC:
		return ("privileged opcode");
	case ILL_PRVREG:
		return ("privileged register");
	case ILL_COPROC:
		return ("coprocessor error");
	case ILL_BADSTK:
		return ("internal stack error");
	default:
		return ("unknown error");
	}
}

static void
handle_posix_sig(int sig, siginfo_t *siginfo, void *context)
{
#define	SIGNAL_FORWARD(sigact) \
	do { \
		if ((sigact)->sa_sigaction != NULL && \
		    ((sigact)->sa_flags & SA_SIGINFO)) { \
			(sigact)->sa_sigaction(sig, siginfo, context); \
		} else if ((sigact)->sa_handler != NULL) { \
			(sigact)->sa_handler(sig); \
		} \
	} while (0)
	switch (sig) {
	case SIGSEGV:
		logMsg("Caught SIGSEGV: segmentation fault (%p)",
		    siginfo->si_addr);
		break;
	case SIGABRT:
		logMsg("Caught SIGABORT: abort (%p)", siginfo->si_addr);
		break;
	case SIGFPE:
		logMsg("Caught SIGFPE: floating point exception (%s)",
		    sigfpe2str(siginfo->si_code));
		break;
	case SIGILL:
		logMsg("Caught SIGILL: illegal instruction (%s)",
		    sigill2str(siginfo->si_code));
		break;
	case SIGTERM:
		logMsg("Caught SIGTERM: terminated");
		break;
	default:
		logMsg("Caught signal %d", sig);
		break;
	}

	log_backtrace(1);

	switch (sig) {
	case SIGSEGV:
		SIGNAL_FORWARD(&old_sigsegv);
		break;
	case SIGABRT:
		SIGNAL_FORWARD(&old_sigabrt);
		break;
	case SIGFPE:
		SIGNAL_FORWARD(&old_sigfpe);
		break;
	case SIGILL:
		SIGNAL_FORWARD(&old_sigill);
		break;
	case SIGTERM:
		SIGNAL_FORWARD(&old_sigterm);
		break;
	}

	exit(EXIT_FAILURE);
}

#if	LIN
static uint8_t *alternate_stack = NULL;
#endif

static void
signal_handler_init(void)
{
	struct sigaction sig_action = { .sa_sigaction = handle_posix_sig };

	sigemptyset(&sig_action.sa_mask);

#if	LIN
	/*
	 * Since glibc 2.34, SIGSTKSZ is no longer constant, so we need to
	 * heap storage for the stack snapshot.
	 */
	alternate_stack = malloc(SIGSTKSZ);
	stack_t ss = {
	    .ss_sp = (void*)alternate_stack,
	    .ss_size = SIGSTKSZ,
	    .ss_flags = 0
	};

	VERIFY0(sigaltstack(&ss, NULL));
	sig_action.sa_flags = SA_SIGINFO | SA_ONSTACK;
#else	/* !LIN */
	sig_action.sa_flags = SA_SIGINFO;
#endif	/* !LIN */

	VERIFY0(sigaction(SIGSEGV, &sig_action, &old_sigsegv));
	VERIFY0(sigaction(SIGABRT, &sig_action, &old_sigabrt));
	VERIFY0(sigaction(SIGFPE, &sig_action, &old_sigfpe));
	VERIFY0(sigaction(SIGINT, &sig_action, &old_sigint));
	VERIFY0(sigaction(SIGILL, &sig_action, &old_sigill));
	VERIFY0(sigaction(SIGTERM, &sig_action, &old_sigterm));
}

static void
signal_handler_fini(void)
{
	VERIFY0(sigaction(SIGSEGV, &old_sigsegv, NULL));
	VERIFY0(sigaction(SIGABRT, &old_sigabrt, NULL));
	VERIFY0(sigaction(SIGFPE, &old_sigfpe, NULL));
	VERIFY0(sigaction(SIGINT, &old_sigint, NULL));
	VERIFY0(sigaction(SIGILL, &old_sigill, NULL));
	VERIFY0(sigaction(SIGTERM, &old_sigterm, NULL));
#if	LIN
	free(alternate_stack);
	alternate_stack = NULL;
#endif	/* LIN */
}

#else	/* APL || LIN */

static LPTOP_LEVEL_EXCEPTION_FILTER prev_windows_except_handler = NULL;

LONG WINAPI
handle_windows_exception(EXCEPTION_POINTERS *ei)
{
	switch(ei->ExceptionRecord->ExceptionCode) {
	case EXCEPTION_ASSERTION_FAILED:
		/* No need to print anything, there's already a log message */
		break;
	case EXCEPTION_ACCESS_VIOLATION:
		logMsg("Caught EXCEPTION_ACCESS_VIOLATION");
		break;
	case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
		logMsg("Caught EXCEPTION_ARRAY_BOUNDS_EXCEEDED");
		break;
	case EXCEPTION_BREAKPOINT:
		logMsg("Caught EXCEPTION_BREAKPOINT");
		break;
	case EXCEPTION_DATATYPE_MISALIGNMENT:
		logMsg("Caught EXCEPTION_DATATYPE_MISALIGNMENT");
		break;
	case EXCEPTION_FLT_DENORMAL_OPERAND:
		logMsg("Caught EXCEPTION_FLT_DENORMAL_OPERAND");
		break;
	case EXCEPTION_FLT_DIVIDE_BY_ZERO:
		logMsg("Caught EXCEPTION_FLT_DIVIDE_BY_ZERO");
		break;
	case EXCEPTION_FLT_INEXACT_RESULT:
		logMsg("Caught EXCEPTION_FLT_INEXACT_RESULT");
		break;
	case EXCEPTION_FLT_INVALID_OPERATION:
		logMsg("Caught EXCEPTION_FLT_INVALID_OPERATION");
		break;
	case EXCEPTION_FLT_OVERFLOW:
		logMsg("Caught EXCEPTION_FLT_OVERFLOW");
		break;
	case EXCEPTION_FLT_STACK_CHECK:
		logMsg("Caught EXCEPTION_FLT_STACK_CHECK");
		break;
	case EXCEPTION_FLT_UNDERFLOW:
		logMsg("Caught EXCEPTION_FLT_UNDERFLOW");
		break;
	case EXCEPTION_ILLEGAL_INSTRUCTION:
		logMsg("Caught EXCEPTION_ILLEGAL_INSTRUCTION");
		break;
	case EXCEPTION_IN_PAGE_ERROR:
		logMsg("Caught EXCEPTION_IN_PAGE_ERROR");
		break;
	case EXCEPTION_INT_DIVIDE_BY_ZERO:
		logMsg("Caught EXCEPTION_INT_DIVIDE_BY_ZERO");
		break;
	case EXCEPTION_INT_OVERFLOW:
		logMsg("Caught EXCEPTION_INT_OVERFLOW");
		break;
	case EXCEPTION_INVALID_DISPOSITION:
		logMsg("Caught EXCEPTION_INVALID_DISPOSITION");
		break;
	case EXCEPTION_NONCONTINUABLE_EXCEPTION:
		logMsg("Caught EXCEPTION_NONCONTINUABLE_EXCEPTION");
		break;
	case EXCEPTION_PRIV_INSTRUCTION:
		logMsg("Caught EXCEPTION_PRIV_INSTRUCTION");
		break;
	case EXCEPTION_SINGLE_STEP:
		logMsg("Caught EXCEPTION_SINGLE_STEP");
		break;
	case EXCEPTION_STACK_OVERFLOW:
		logMsg("Caught EXCEPTION_STACK_OVERFLOW");
		break;
	default:
		logMsg("Caught unknown exception %lx",
		    ei->ExceptionRecord->ExceptionCode);
		break;
	}
	log_backtrace_sw64(ei->ContextRecord);

	if (prev_windows_except_handler != NULL)
		return (prev_windows_except_handler(ei));

	return (EXCEPTION_CONTINUE_SEARCH);
}

#endif	/* APL || LIN */

/**
 * Installs a custom crash handler and initializes the system.
 * This should be called near the top of your `XPluginStart`.
 */
void
except_init(void)
{
	ASSERT(!inited);
	inited = B_TRUE;

#if	LIN || APL
	signal_handler_init();
#else	/* !LIN && !APL */
	prev_windows_except_handler =
	    SetUnhandledExceptionFilter(handle_windows_exception);
#endif	/* !LIN && !APL */
}

/**
 * Uninstalls a custom crash handler and deinitializes the system.
 * This should be called near the bottom of your `XPluginStop`.
 */
void
except_fini(void)
{
	if (!inited)
		return;
	inited = B_FALSE;

#if	LIN || APL
	signal_handler_fini();
#else	/* !LIN && !APL */
	SetUnhandledExceptionFilter(prev_windows_except_handler);
#endif	/* !LIN && !APL */
}
