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
 * Copyright 2023 Saso Kiselkov. All rights reserved.
 */

#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

#if	IBM
#include <io.h>
#include <fcntl.h>
#else
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <curl/curl.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <png.h>

#if	IBM
#include <windows.h>
#elif	APL
#include <sys/sysctl.h>
#endif	/* APL */

#include "acfutils/assert.h"
#include "acfutils/avl.h"
#include "acfutils/chartdb.h"
#include "acfutils/helpers.h"
#include "acfutils/list.h"
#include "acfutils/mt_cairo_render.h"
#include "acfutils/png.h"
#include "acfutils/stat.h"
#include "acfutils/thread.h"
#include "acfutils/worker.h"

#include "chartdb_impl.h"
#include "chart_prov_common.h"
#include "chart_prov_faa.h"
#include "chart_prov_autorouter.h"
#include "chart_prov_navigraph.h"

#define	MAX_METAR_AGE	60	/* seconds */
#define	MAX_TAF_AGE	300	/* seconds */
#define	RETRY_INTVAL	30	/* seconds */
#define	WRITE_BUFSZ	4096	/* bytes */
#define	READ_BUFSZ	4096	/* bytes */

#define	DESTROY_HANDLE(__handle__)	\
	do { \
		if ((__handle__) != NULL) { \
			CloseHandle((__handle__)); \
			(__handle__) = NULL; \
		} \
	} while (0)

static chart_prov_t prov[NUM_PROVIDERS] = {
    {
	.name = "aeronav.faa.gov",
	.init = chart_faa_init,
	.fini = chart_faa_fini,
	.get_chart = chart_faa_get_chart
    },
    {
	.name = "autorouter.aero",
	.init = chart_autorouter_init,
	.fini = chart_autorouter_fini,
	.get_chart = chart_autorouter_get_chart,
	.arpt_lazyload = chart_autorouter_arpt_lazyload,
	.test_conn = chart_autorouter_test_conn
    },
    {
	.name = "navigraph.com",
	.init = chart_navigraph_init,
	.fini = chart_navigraph_fini,
	.get_chart = chart_navigraph_get_chart,
	.watermark_chart = chart_navigraph_watermark_chart,
	.arpt_lazy_discover = chart_navigraph_arpt_lazy_discover,
	.pending_ext_account_setup = chart_navigraph_pending_ext_account_setup
    }
};

static chart_arpt_t *arpt_find(chartdb_t *cdb, const char *icao);
static char *download_metar(chartdb_t *cdb, const char *icao);
static char *download_taf(chartdb_t *cdb, const char *icao);

#if	IBM

static uint64_t
physmem(void)
{
	MEMORYSTATUSEX status;
	status.dwLength = sizeof(status);
	VERIFY(GlobalMemoryStatusEx(&status));
	return (status.ullTotalPhys);
}

#elif	APL

static uint64_t
physmem(void)
{
	int mib[2] = { CTL_HW, HW_MEMSIZE };
	int64_t mem;
	size_t length = sizeof(int64_t);
	sysctl(mib, 2, &mem, &length, NULL, 0);
	return (mem);
}

#else	/* LIN */

static uint64_t
physmem(void)
{
	uint64_t pages = sysconf(_SC_PHYS_PAGES);
	uint64_t page_size = sysconf(_SC_PAGE_SIZE);
	return (pages * page_size);
}

#endif	/* LIN */

static int
chart_name_compar(const void *a, const void *b)
{
	const chart_t *ca = a, *cb = b;
	int res = strcmp(ca->name, cb->name);

	if (res < 0)
		return (-1);
	if (res == 0)
		return (0);
	return (1);
}

static int
arpt_compar(const void *a, const void *b)
{
	const chart_arpt_t *ca = a, *cb = b;
	int res = strcmp(ca->icao, cb->icao);

	if (res < 0)
		return (-1);
	if (res == 0)
		return (0);
	return (1);
}

void
chartdb_chart_destroy(chart_t *chart)
{
	ASSERT(chart != NULL);

	if (chart->surf != NULL)
		cairo_surface_destroy(chart->surf);
	if (chart->png_data != NULL) {
		memset(chart->png_data, 0, chart->png_data_len);
		free(chart->png_data);
	}
	free(chart->name);
	free(chart->codename);
	free(chart->filename);
	free(chart->filename_night);
	ZERO_FREE(chart);
}

static void
arpt_destroy(chart_arpt_t *arpt)
{
	void *cookie;
	chart_t *chart;

	cookie = NULL;
	while ((chart = avl_destroy_nodes(&arpt->charts, &cookie)) != NULL)
		chartdb_chart_destroy(chart);
	avl_destroy(&arpt->charts);

	free(arpt->name);
	free(arpt->city);
	free(arpt->metar);
	free(arpt->taf);
	free(arpt->codename);

	free(arpt);
}

static void
remove_old_airacs(chartdb_t *cdb)
{
	char *dpath = mkpathname(cdb->path, cdb->prov_name, NULL);
	DIR *dp;
	struct dirent *de;
	time_t now = time(NULL);

	if (!file_exists(dpath, NULL))
		goto out;

	dp = opendir(dpath);
	if (dp == NULL) {
		logMsg("Error accessing directory %s: %s", dpath,
		    strerror(errno));
		goto out;
	}
	while ((de = readdir(dp)) != NULL) {
		unsigned nr = 0;
		char *subpath;
		struct stat st;

		if (strlen(de->d_name) != 4 ||
		    sscanf(de->d_name, "%u", &nr) != 1 ||
		    nr < 1000 || nr >= cdb->airac) {
			continue;
		}
		subpath = mkpathname(dpath, de->d_name, NULL);
		if (stat(subpath, &st) == 0) {
			if (now - st.st_mtime > 30 * 86400)
				remove_directory(subpath);
		}
		free(subpath);
	}

	closedir(dp);
out:
	free(dpath);
}

static bool_t
loader_init(void *userinfo)
{
	chartdb_t *cdb = userinfo;

	ASSERT(cdb != NULL);
	ASSERT3U(cdb->prov, <, NUM_PROVIDERS);

	/* Expunge outdated AIRACs */
	remove_old_airacs(cdb);

	if (!prov[cdb->prov].init(cdb))
		return (B_FALSE);

	mutex_enter(&cdb->lock);
	cdb->init_complete = B_TRUE;
	mutex_exit(&cdb->lock);

	return (B_TRUE);
}

static void
loader_purge(chartdb_t *cdb)
{
	for (chart_arpt_t *arpt = avl_first(&cdb->arpts); arpt != NULL;
	    arpt = AVL_NEXT(&cdb->arpts, arpt)) {
		for (chart_t *chart = avl_first(&arpt->charts); chart != NULL;
		    chart = AVL_NEXT(&arpt->charts, chart)) {
			if (chart->surf != NULL) {
				cairo_surface_destroy(chart->surf);
				chart->surf = NULL;
			}
			if (chart->png_data != NULL) {
				free(chart->png_data);
				chart->png_data = NULL;
				chart->png_data_len = 0;
			}
		}
	}
	while (list_remove_head(&cdb->load_seq) != NULL)
		;
}

chart_arpt_t *
chartdb_add_arpt(chartdb_t *cdb, const char *icao, const char *name,
    const char *city_name, const char *state_id)
{
	chart_arpt_t *arpt, srch;
	avl_index_t where;

	ASSERT(cdb != NULL);

	lacf_strlcpy(srch.icao, icao, sizeof (srch.icao));

	mutex_enter(&cdb->lock);
	arpt = avl_find(&cdb->arpts, &srch, &where);
	if (arpt == NULL) {
		arpt = safe_calloc(1, sizeof (*arpt));
		avl_create(&arpt->charts, chart_name_compar, sizeof (chart_t),
		    offsetof(chart_t, node));
		lacf_strlcpy(arpt->icao, icao, sizeof (arpt->icao));
		arpt->name = safe_strdup(name);
		arpt->city = safe_strdup(city_name);
		lacf_strlcpy(arpt->state, state_id, sizeof (arpt->state));
		arpt->db = cdb;
		avl_insert(&cdb->arpts, arpt, where);
	}
	mutex_exit(&cdb->lock);

	return (arpt);
}

bool_t
chartdb_add_chart(chart_arpt_t *arpt, chart_t *chart)
{
	avl_index_t where;
	chartdb_t *cdb = arpt->db;

	ASSERT(cdb != NULL);

	mutex_enter(&cdb->lock);
	if (avl_find(&arpt->charts, chart, &where) != NULL) {
		mutex_exit(&cdb->lock);
		return (B_FALSE);
	}
	avl_insert(&arpt->charts, chart, where);
	chart->arpt = arpt;
	chart->num_pages = -1;
	arpt->load_complete = B_TRUE;
	mutex_exit(&cdb->lock);

	return (B_TRUE);
}

char *
chartdb_mkpath(chart_t *chart)
{
	chart_arpt_t *arpt = chart->arpt;
	chartdb_t *cdb;
	char airac_nr[8];

	ASSERT(arpt != NULL);
	cdb = arpt->db;
	ASSERT(cdb != NULL);

	snprintf(airac_nr, sizeof (airac_nr), "%d", cdb->airac);
	if (cdb->flat_db) {
		return (mkpathname(cdb->path, prov[cdb->prov].name, airac_nr,
		    chart->filename, NULL));
	} else {
		return (mkpathname(cdb->path, prov[cdb->prov].name, airac_nr,
		    arpt->icao, chart->filename, NULL));
	}
}

int
chartdb_pdf_count_pages_direct(const char *pdfinfo_path, const uint8_t *buf,
    size_t len)
{
	int num_pages = -1;
	char *dpath = lacf_dirname(pdfinfo_path);
	int fd_in = -1, fd_out = -1;
	char *line = NULL;
	size_t cap = 0, fill = 0;
	size_t written = 0;
	char *out_buf = NULL;
	char **lines = NULL;
	size_t num_lines;

#if	IBM
	SECURITY_ATTRIBUTES sa;
	HANDLE stdin_rd_handle = NULL;
	HANDLE stdin_wr_handle = NULL;
	HANDLE stdout_rd_handle = NULL;
	HANDLE stdout_wr_handle = NULL;
	PROCESS_INFORMATION pi;
	STARTUPINFO si;
	char cmd[3 * MAX_PATH];
	TCHAR cmdT[3 * MAX_PATH];

	memset(&pi, 0, sizeof (pi));

	snprintf(cmd, sizeof (cmd), "\"%s\" fd://0", pdfinfo_path);
	MultiByteToWideChar(CP_UTF8, 0, cmd, -1, cmdT, 3 * MAX_PATH);

	sa.nLength = sizeof(sa);
	sa.bInheritHandle = TRUE;
	sa.lpSecurityDescriptor = NULL;

	if (!CreatePipe(&stdout_rd_handle, &stdout_wr_handle, &sa, 0) ||
	    !SetHandleInformation(stdout_rd_handle, HANDLE_FLAG_INHERIT, 0) ||
	    !CreatePipe(&stdin_rd_handle, &stdin_wr_handle, &sa, 0) ||
	    !SetHandleInformation(stdin_wr_handle, HANDLE_FLAG_INHERIT, 0)) {
		win_perror(GetLastError(), "Error creating pipe");
		goto errout;
	}
	ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));
	ZeroMemory(&si, sizeof(STARTUPINFO));
	si.cb = sizeof(STARTUPINFO);
	si.hStdInput = stdin_rd_handle;
	si.hStdOutput = stdout_wr_handle;
	si.dwFlags |= STARTF_USESTDHANDLES;

	fd_in = _open_osfhandle((intptr_t)stdin_wr_handle, _O_WRONLY);
	fd_out = _open_osfhandle((intptr_t)stdout_rd_handle, _O_RDONLY);
	if (fd_in == -1 || fd_out == -1) {
		win_perror(GetLastError(), "Error opening pipe as fd");
		goto errout;
	}
	/*
	 * OSF open take over the handle!
	 */
	stdin_wr_handle = NULL;
	stdout_rd_handle = NULL;

	if (!CreateProcess(NULL, cmdT, NULL, NULL, TRUE,
	    CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS,
	    NULL, NULL, &si, &pi)) {
		win_perror(GetLastError(), "Error invoking %s", pdfinfo_path);
		goto errout;
	}
#else	/* !IBM */
	int child_pid;
	int stdin_pipe[2] = { -1, -1 };
	int stdout_pipe[2] = { -1, -1 };

	if (pipe(stdin_pipe) < 0 || pipe(stdout_pipe) < 0) {
		logMsg("Error counting PDF pages: pipe failed: %s\n",
		    strerror(errno));
		goto errout;
	}

	child_pid = fork();
	switch (child_pid) {
	case -1:
		logMsg("Error counting PDF pages: fork failed: %s\n",
		    strerror(errno));
		goto errout;
	case 0:
		dup2(stdin_pipe[0], STDIN_FILENO);
		dup2(stdout_pipe[1], STDOUT_FILENO);
		for (int i = 0; i < 2; i++) {
			close(stdin_pipe[i]);
			close(stdout_pipe[i]);
		}
#if	APL
		setenv("DYLD_LIBRARY_PATH", dpath, 1);
#else
		setenv("LD_LIBRARY_PATH", dpath, 1);
#endif
		execl(pdfinfo_path, pdfinfo_path, "fd://0", NULL);
		logMsg("Error counting PDF pages: execl failed: %s\n",
		    strerror(errno));
		exit(EXIT_FAILURE);
	default:
		fd_in = dup(stdin_pipe[1]);
		fd_out = dup(stdout_pipe[0]);
		VERIFY(fd_in != -1);
		VERIFY(fd_out != -1);
		for (int i = 0; i < 2; i++) {
			close(stdin_pipe[i]);
			stdin_pipe[i] = -1;
			close(stdout_pipe[i]);
			stdout_pipe[i] = -1;
		}
		break;
	}
#endif	/* !IBM */

	while (written < len) {
		int n = write(fd_in, &buf[written], len - written);
		if (n == -1) {
			logMsg("write error: %s", strerror(errno));
			goto errout;
		}
		if (n == 0)
			break;
		written += n;
	}
	close(fd_in);
	fd_in = -1;

	for (;;) {
		int n, to_read;

		if (cap - fill < READ_BUFSZ) {
			cap += READ_BUFSZ;
			out_buf = safe_realloc(out_buf, cap);
		}
		to_read = cap - fill;
		n = read(fd_out, &out_buf[fill], to_read);
		if (n == -1) {
			logMsg("read error: %s", strerror(errno));
			goto errout;
		}
		if (n == 0) {
			/* EOF */
			break;
		}
		fill += n;
#if	IBM
		/*
		 * On windows, a short byte count indicates an EOF, so
		 * we need to exit now, or else we'll block indefinitely.
		 */
		if (n < to_read)
			break;
#endif	/* IBM */
	}
	close(fd_out);
	fd_out = -1;

	lines = strsplit(out_buf, "\n", B_TRUE, &num_lines);
	for (size_t i = 0; i < num_lines; i++) {
		char *line = lines[i];
		if (strncmp(line, "Pages:", 6) == 0) {
			size_t n_comps;
			char **comps;

			strip_space(line);
			comps = strsplit(line, " ", B_TRUE, &n_comps);
			if (n_comps >= 2)
				num_pages = atoi(comps[1]);
			free_strlist(comps, n_comps);
			break;
		}
	}

#if	IBM
	WaitForSingleObject(pi.hProcess, INFINITE);
#else	/* !IBM */
	/* reap child process */
	waitpid(child_pid, NULL, 0);
#endif	/* !IBM */

errout:
	if (lines != NULL)
		free_strlist(lines, num_lines);
	if (fd_in != -1)
		close(fd_in);
	if (fd_out != -1)
		close(fd_out);
#if	IBM
	DESTROY_HANDLE(stdin_rd_handle);
	DESTROY_HANDLE(stdin_wr_handle);
	DESTROY_HANDLE(stdout_rd_handle);
	DESTROY_HANDLE(stdout_wr_handle);
	DESTROY_HANDLE(pi.hProcess);
	DESTROY_HANDLE(pi.hThread);
#else	/* !IBM */
	if (stdin_pipe[0] != -1) {
		close(stdin_pipe[0]);
		close(stdin_pipe[1]);
	}
	if (stdout_pipe[0] != -1) {
		close(stdout_pipe[0]);
		close(stdout_pipe[1]);
	}
#endif	/* !IBM */

	free(line);
	free(dpath);
	free(out_buf);
	if (num_pages == -1)
		logMsg("Unable to read page count");

	return (num_pages);
}

int
chartdb_pdf_count_pages_file(const char *pdfinfo_path, const char *path)
{
	uint8_t *buf = NULL;
	size_t len = 0, fill = 0;
	FILE *infp = fopen(path, "rb");
	int pages = -1;

	if (infp == NULL) {
		logMsg("Error counting PDF pages %s: can't read input: %s",
		    path, strerror(errno));
		return (-1);
	}
	for (;;) {
		int ret;

		if (len - fill < READ_BUFSZ) {
			len += READ_BUFSZ;
			buf = safe_realloc(buf, len);
		}
		ret = fread(&buf[fill], 1, len - fill, infp);
		if (ret > 0)
			fill += ret;
		if (ret < READ_BUFSZ) {
			if (ferror(infp)) {
				logMsg("Error counting PDF pages %s: "
				    "error reading input", path);
				goto errout;
			}
			break;
		}
	}
	pages = chartdb_pdf_count_pages_direct(pdfinfo_path, buf, fill);
errout:
	fclose(infp);
	free(buf);

	return (pages);
}

char *
chartdb_pdf_convert_file(const char *pdftoppm_path, char *old_path, int page,
    double zoom)
{
	char *ext = NULL, *new_path = NULL;
	uint8_t *pdf_buf = NULL;
	size_t pdf_len = 0, pdf_fill = 0;
	uint8_t *png_buf = NULL;
	size_t png_len;
	FILE *infp = NULL, *outfp = NULL;

	infp = fopen(old_path, "rb");
	if (infp == NULL) {
		logMsg("Error converting chart %s: can't read input: %s",
		    old_path, strerror(errno));
		return (NULL);
	}
	for (;;) {
		int ret;

		if (pdf_len - pdf_fill < READ_BUFSZ) {
			pdf_len += READ_BUFSZ;
			pdf_buf = safe_realloc(pdf_buf, pdf_len);
		}
		ret = fread(&pdf_buf[pdf_fill], 1, pdf_len - pdf_fill, infp);
		if (ret > 0)
			pdf_fill += ret;
		if (ret < READ_BUFSZ) {
			if (ferror(infp)) {
				logMsg("Error converting chart %s: "
				    "error reading input", old_path);
				goto errout;
			}
			break;
		}
	}
	fclose(infp);
	infp = NULL;

	png_buf = chartdb_pdf_convert_direct(pdftoppm_path, pdf_buf, pdf_fill,
	    page, zoom, &png_len);
	if (png_buf == NULL)
		goto errout;

	new_path = safe_strdup(old_path);
	ext = strrchr(new_path, '.');
	VERIFY(ext != NULL);
	lacf_strlcpy(&ext[1], "png", strlen(&ext[1]) + 1);

	outfp = fopen(new_path, "wb");
	if (outfp == NULL) {
		logMsg("Error converting chart %s: can't write output "
		    "file %s: %s", old_path, new_path, strerror(errno));
		goto errout;
	}
	if (fwrite(png_buf, 1, png_len, outfp) < png_len) {
		logMsg("Error converting chart %s: can't write output to "
		    "file %s", old_path, new_path);
		goto errout;
	}
	fclose(outfp);
	outfp = NULL;

	free(pdf_buf);
	free(png_buf);

	return (new_path);
errout:
	if (infp != NULL)
		fclose(infp);
	if (outfp != NULL)
		fclose(outfp);
	free(new_path);
	free(pdf_buf);
	free(png_buf);
	return (NULL);
}

uint8_t *
chartdb_pdf_convert_direct(const char *pdftoppm_path, const uint8_t *pdf_data,
    size_t len, int page, double zoom, size_t *out_len)
{
	char *dpath;
	int fd_in = -1, fd_out = -1;
	int exit_code;
#if	!IBM
	int child_pid;
#endif
	size_t written = 0;
	uint8_t *png_buf = NULL;
	int png_buf_sz = 0, png_buf_fill = 0;

	zoom = clamp(zoom, 0.1, 10.0);
	dpath = lacf_dirname(pdftoppm_path);

	/*
	 * As the PDF conversion process can be rather CPU-intensive, run
	 * with reduced priority to avoid starving the sim, even if that
	 * means taking longer.
	 */
#if	IBM
	SECURITY_ATTRIBUTES sa;
	HANDLE stdin_rd_handle = NULL;
	HANDLE stdin_wr_handle = NULL;
	HANDLE stdout_rd_handle = NULL;
	HANDLE stdout_wr_handle = NULL;
	PROCESS_INFORMATION pi;
	STARTUPINFO si;
	char cmd[3 * MAX_PATH];
	TCHAR cmdT[3 * MAX_PATH];
	DWORD exit_code_win;

	sa.nLength = sizeof(sa);
	sa.bInheritHandle = TRUE;
	sa.lpSecurityDescriptor = NULL;

	if (!CreatePipe(&stdout_rd_handle, &stdout_wr_handle, &sa, 0) ||
	    !SetHandleInformation(stdout_rd_handle, HANDLE_FLAG_INHERIT, 0) ||
	    !CreatePipe(&stdin_rd_handle, &stdin_wr_handle, &sa, 0) ||
	    !SetHandleInformation(stdin_wr_handle, HANDLE_FLAG_INHERIT, 0)) {
		win_perror(GetLastError(), "Error creating pipes");
		goto errout;
	}
	ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));
	ZeroMemory(&si, sizeof(STARTUPINFO));
	si.cb = sizeof(STARTUPINFO);
	si.hStdInput = stdin_rd_handle;
	si.hStdOutput = stdout_wr_handle;
	si.dwFlags |= STARTF_USESTDHANDLES;

	fd_in = _open_osfhandle((intptr_t)stdin_wr_handle, _O_WRONLY);
	fd_out = _open_osfhandle((intptr_t)stdout_rd_handle, _O_RDONLY);
	if (fd_in == -1 || fd_out == -1) {
		win_perror(GetLastError(), "Error opening pipe as fd");
		goto errout;
	}
	/*
	 * OSF open take over the handle!
	 */
	stdin_wr_handle = NULL;
	stdout_rd_handle = NULL;

	snprintf(cmd, sizeof (cmd), "\"%s\" -png -f %d -l %d -r %d -cropbox",
	    pdftoppm_path, page + 1, page + 1, (int)(100 * zoom));
	MultiByteToWideChar(CP_UTF8, 0, cmd, -1, cmdT, 3 * MAX_PATH);

	if (!CreateProcess(NULL, cmdT, NULL, NULL, TRUE,
	    CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS,
	    NULL, NULL, &si, &pi)) {
		win_perror(GetLastError(), "Error converting chart to PNG");
		goto errout;
	}
#else	/* !IBM */
	int stdin_pipe[2] = { -1, -1 };
	int stdout_pipe[2] = { -1, -1 };
	char page_nr[8], zoom_nr[8];

	if (pipe(stdin_pipe) < 0 || pipe(stdout_pipe) < 0) {
		logMsg("Error converting chart to PNG: "
		    "error creating pipes: %s", strerror(errno));
		goto errout;
	}
	snprintf(page_nr, sizeof (page_nr), "%d", page + 1);
	snprintf(zoom_nr, sizeof (zoom_nr), "%d", (int)(100 * zoom));

	child_pid = fork();
	switch (child_pid) {
	case -1:
		logMsg("Error converting chart to PNG: fork failed: %s",
		    strerror(errno));
		goto errout;
	case 0:
		dup2(stdin_pipe[0], STDIN_FILENO);
		dup2(stdout_pipe[1], STDOUT_FILENO);
		for (int i = 0; i < 2; i++) {
			close(stdin_pipe[i]);
			close(stdout_pipe[i]);
		}
#if	APL
		setenv("DYLD_LIBRARY_PATH", dpath, 1);
#else	/* !APL */
		setenv("LD_LIBRARY_PATH", dpath, 1);
#endif	/* !APL */
		/* drop exec priority so the sim doesn't stutter */
		if (nice(10)) { /*shut up GCC */ }
		execl(pdftoppm_path, pdftoppm_path, "-png", "-f", page_nr,
		    "-l", page_nr, "-r", zoom_nr, "-cropbox", NULL);
		logMsg("Error converting chart to PNG: execv failed: %s",
		    strerror(errno));
		exit(EXIT_FAILURE);
	default:
		fd_in = dup(stdin_pipe[1]);
		fd_out = dup(stdout_pipe[0]);
		for (int i = 0; i < 2; i++) {
			close(stdin_pipe[i]);
			stdin_pipe[i] = -1;
			close(stdout_pipe[i]);
			stdout_pipe[i] = -1;
		}
		break;
	}
#endif	/* !IBM */

	while (written < len) {
		int n = write(fd_in, &pdf_data[written], len - written);
		if (n == -1) {
			logMsg("write error: %s", strerror(errno));
			goto errout;
		}
		if (n == 0)
			break;
		written += n;
	}
	close(fd_in);
	fd_in = -1;

	for (;;) {
		int n, to_read;

		if (png_buf_sz - png_buf_fill < READ_BUFSZ) {
			png_buf_sz += READ_BUFSZ;
			png_buf = safe_realloc(png_buf, png_buf_sz);
		}
		to_read = png_buf_sz - png_buf_fill;
		n = read(fd_out, &png_buf[png_buf_fill], to_read);
		if (n <= 0)
			break;
		png_buf_fill += n;
#if	IBM
		/*
		 * On windows, a short byte count indicates an EOF, so
		 * we need to exit now, or else we'll block indefinitely.
		 */
		if (n < to_read)
			break;
#endif	/* IBM */
	}
	close(fd_out);
	fd_out = -1;

#if	IBM
	DESTROY_HANDLE(stdin_rd_handle);
	DESTROY_HANDLE(stdin_wr_handle);
	DESTROY_HANDLE(stdout_rd_handle);
	DESTROY_HANDLE(stdout_wr_handle);
	stdin_rd_handle = NULL;
	stdin_wr_handle = NULL;
	stdout_rd_handle = NULL;
	stdout_wr_handle = NULL;

	WaitForSingleObject(pi.hProcess, INFINITE);
	VERIFY(GetExitCodeProcess(pi.hProcess, &exit_code_win));
	exit_code = exit_code_win;
	DESTROY_HANDLE(pi.hProcess);
	DESTROY_HANDLE(pi.hThread);
#else	/* !IBM */
	while (waitpid(child_pid, &exit_code, 0) < 0) {
		if (errno != EINTR) {
			logMsg("Error converting chart to PNG: "
			    "waitpid failed: %s", strerror(errno));
			goto errout;
		}
	}
	/* chunk out the exit code only */
	exit_code = WEXITSTATUS(exit_code);
#endif	/* !IBM */

	if (exit_code != 0) {
		logMsg("Error converting chart to PNG. Command returned "
		    "error code %d", exit_code);
		goto errout;
	}

	free(dpath);
	*out_len = png_buf_fill;
	return (png_buf);
errout:
	if (fd_in != -1)
		close(fd_in);
	if (fd_out != -1)
		close(fd_out);
#if	IBM
	DESTROY_HANDLE(stdout_rd_handle);
	DESTROY_HANDLE(stdout_wr_handle);
	DESTROY_HANDLE(stdin_rd_handle);
	DESTROY_HANDLE(stdin_wr_handle);
#else	/* !IBM */
	if (stdin_pipe[0] != -1) {
		close(stdin_pipe[0]);
		close(stdin_pipe[1]);
	}
	if (stdout_pipe[0] != -1) {
		close(stdout_pipe[0]);
		close(stdout_pipe[1]);
	}
#endif	/* !IBM */
	free(dpath);
	free(png_buf);
	return (NULL);
}

static void
invert_surface(cairo_surface_t *surf)
{
	uint8_t *data = cairo_image_surface_get_data(surf);
	int stride = cairo_image_surface_get_stride(surf);
	int width = cairo_image_surface_get_width(surf);
	int height = cairo_image_surface_get_height(surf);

	cairo_surface_flush(surf);

	switch (cairo_image_surface_get_format(surf)) {
	case CAIRO_FORMAT_ARGB32:
	case CAIRO_FORMAT_RGB24:
		for (int y = 0; y < height; y++) {
			uint8_t *p = data + y * stride;

			for (int x = 0; x < width; x++) {
#if	__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
				p[1] = 255 - p[1];
				p[2] = 255 - p[2];
				p[3] = 255 - p[3];
#else
				p[0] = 255 - p[0];
				p[1] = 255 - p[1];
				p[2] = 255 - p[2];
#endif
				p += 4;
			}
		}
		break;
	default:
		logMsg("Unable to invert surface colors: unsupported "
		    "format %x", cairo_image_surface_get_format(surf));
		break;
	}
	cairo_surface_mark_dirty(surf);
}

static cairo_surface_t *
chart_get_surface_nocache(chartdb_t *cdb, chart_t *chart)
{
	int width, height;
	uint8_t *png_pixels;
	cairo_surface_t *surf;
	uint32_t *surf_data;

	ASSERT(cdb != NULL);
	ASSERT(chart != NULL);

	if (chart->png_data == NULL)
		return (NULL);
	png_pixels = png_load_from_buffer_cairo_argb32(chart->png_data,
	    chart->png_data_len, &width, &height);
	if (png_pixels == NULL)
		return (NULL);
	surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
	surf_data = (uint32_t *)cairo_image_surface_get_data(surf);
	ASSERT(surf_data != NULL);
	memcpy(surf_data, png_pixels, width * height * 4);
	cairo_surface_mark_dirty(surf);

	ZERO_FREE_N(png_pixels, width * height * 4);

	return (surf);
}

static bool_t
chart_needs_get(chartdb_t *cdb, chart_t *chart)
{
	ASSERT(cdb != NULL);
	ASSERT(chart != NULL);
	if (!cdb->disallow_caching) {
		/*
		 * If we use caching, try to redownload the chart once,
		 * or redownload if the file doesn't exist on disk.
		 */
		char *path = chartdb_mkpath(chart);
		bool_t result = (!chart->refreshed || !file_exists(path, NULL));
		free(path);
		return (result);
	} else {
		/*
		 * If we are not allowed to cache the chart, look for the
		 * png_data pointer. The chart provider will populate it.
		 * Also refresh the data if the day/night status changed
		 * and the chart provider can do day/night specific charts.
		 */
		return (chart->png_data == NULL ||
		    (chart->filename_night != NULL &&
		    chart->night_prev != chart->night));
	}
}

static cairo_surface_t *
chart_get_surface(chartdb_t *cdb, chart_t *chart)
{
	char *path = NULL, *ext = NULL;
	cairo_surface_t *surf = NULL;

	ASSERT(cdb != NULL);
	ASSERT(chart != NULL);

	if (chart->load_cb != NULL)
		return (chart->load_cb(chart));
	if (chart_needs_get(cdb, chart)) {
		chart->refreshed = B_TRUE;
		if (!prov[cdb->prov].get_chart(chart)) {
			mutex_enter(&cdb->lock);
			chart->load_error = B_TRUE;
			mutex_exit(&cdb->lock);
			goto out;
		}
	}
	chart->night_prev = chart->night;
	if (cdb->disallow_caching) {
		surf = chart_get_surface_nocache(cdb, chart);
		goto out;
	}
	path = chartdb_mkpath(chart);
	ext = strrchr(path, '.');
	if (ext != NULL &&
	    (strcmp(&ext[1], "pdf") == 0 || strcmp(&ext[1], "PDF") == 0)) {
		if (cdb->pdfinfo_path == NULL ||
		    cdb->pdftoppm_path == NULL) {
			logMsg("Attempted to load PDF chart, but this chart "
			    "DB instance doesn't support PDF conversion");
			mutex_enter(&cdb->lock);
			chart->load_error = B_TRUE;
			mutex_exit(&cdb->lock);
			goto out;
		}
		if (chart->num_pages == -1) {
			chart->num_pages = chartdb_pdf_count_pages_file(
			    cdb->pdfinfo_path, path);
		}
		if (chart->num_pages == -1) {
			mutex_enter(&cdb->lock);
			chart->load_error = B_TRUE;
			mutex_exit(&cdb->lock);
			goto out;
		}
		path = chartdb_pdf_convert_file(cdb->pdftoppm_path, path,
		    chart->load_page, chart->zoom);
		if (path == NULL) {
			mutex_enter(&cdb->lock);
			chart->load_page = chart->cur_page;
			chart->load_error = B_TRUE;
			mutex_exit(&cdb->lock);
			goto out;
		}
	}
	surf = cairo_image_surface_create_from_png(path);
out:
	free(path);
	return (surf);
}

static void
loader_load(chartdb_t *cdb, chart_t *chart)
{
	cairo_surface_t *surf = chart_get_surface(cdb, chart);
	cairo_status_t st;

	if (surf == NULL)
		return;
	if ((st = cairo_surface_status(surf)) == CAIRO_STATUS_SUCCESS) {
		/*
		 * If night mode was selected and this provider doesn't
		 * explicitly support supplying night charts, simply invert
		 * the surface's colors.
		 */
		if (chart->night && chart->filename_night == NULL)
			invert_surface(surf);
		if (prov[cdb->prov].watermark_chart != NULL)
			prov[cdb->prov].watermark_chart(chart, surf);
		mutex_enter(&cdb->lock);
		CAIRO_SURFACE_DESTROY(chart->surf);
		chart->surf = surf;
		chart->cur_page = chart->load_page;
		mutex_exit(&cdb->lock);
	} else {
		logMsg("Can't load chart %s PNG file %s", chart->name,
		    cairo_status_to_string(st));
		mutex_enter(&cdb->lock);
		chart->load_error = B_TRUE;
		mutex_exit(&cdb->lock);
	}
}

static uint64_t
chart_mem_usage(chartdb_t *cdb)
{
	uint64_t total = 0;

	for (chart_t *c = list_head(&cdb->load_seq); c != NULL;
	    c = list_next(&cdb->load_seq, c)) {
		if (c->surf != NULL) {
			unsigned w = cairo_image_surface_get_stride(c->surf);
			unsigned h = cairo_image_surface_get_height(c->surf);

			total += w * h * 4;
		}
		total += c->png_data_len;
	}

	return (total);
}

static bool_t
loader(void *userinfo)
{
	chartdb_t *cdb = userinfo;
	chart_t *chart;
	chart_arpt_t *arpt;

	mutex_enter(&cdb->lock);
	while ((arpt = list_remove_head(&cdb->loader_arpt_queue)) != NULL) {
		if (arpt->load_complete ||
		    prov[cdb->prov].arpt_lazyload == NULL)
			continue;
		mutex_exit(&cdb->lock);
		prov[cdb->prov].arpt_lazyload(arpt);
		mutex_enter(&cdb->lock);
	}
	while ((chart = list_remove_head(&cdb->loader_queue)) != NULL) {
		if (chart == &cdb->loader_cmd_purge) {
			loader_purge(cdb);
		} else if (chart == &cdb->loader_cmd_metar) {
			char *metar;

			chart->arpt->metar_load_t = time(NULL);
			mutex_exit(&cdb->lock);
			metar = download_metar(cdb, chart->arpt->icao);
			mutex_enter(&cdb->lock);
			if (chart->arpt->metar != NULL)
				free(chart->arpt->metar);
			chart->arpt->metar = metar;
			if (metar == NULL) {
				chart->arpt->metar_load_t = time(NULL) -
				    (MAX_METAR_AGE - RETRY_INTVAL);
			}
		} else if (chart == &cdb->loader_cmd_taf) {
			char *taf;

			chart->arpt->taf_load_t = time(NULL);
			mutex_exit(&cdb->lock);
			taf = download_taf(cdb, chart->arpt->icao);
			mutex_enter(&cdb->lock);
			if (chart->arpt->taf != NULL)
				free(chart->arpt->taf);
			chart->arpt->taf = taf;
			if (taf == NULL) {
				chart->arpt->taf_load_t = time(NULL) -
				    (MAX_TAF_AGE - RETRY_INTVAL);
			}
		} else {
			mutex_exit(&cdb->lock);
			loader_load(cdb, chart);
			mutex_enter(&cdb->lock);
			/* Move to the head of the load sequence list */
			if (list_link_active(&chart->load_seq_node))
				list_remove(&cdb->load_seq, chart);
			list_insert_head(&cdb->load_seq, chart);

			while (list_count(&cdb->load_seq) > 1 &&
			    chart_mem_usage(cdb) > cdb->load_limit) {
				chart_t *c = list_tail(&cdb->load_seq);

				if (c->surf != NULL) {
					cairo_surface_destroy(c->surf);
					c->surf = NULL;
				}
				if (c->png_data != NULL) {
					free(c->png_data);
					c->png_data = NULL;
					c->png_data_len = 0;
				}
				list_remove(&cdb->load_seq, c);
			}
		}
	}
	mutex_exit(&cdb->lock);

	return (B_TRUE);
}

static void
loader_fini(void *userinfo)
{
	chartdb_t *cdb = userinfo;

	ASSERT(cdb != NULL);
	ASSERT3U(cdb->prov, <, NUM_PROVIDERS);
	prov[cdb->prov].fini(cdb);
}

chartdb_t *
chartdb_init(const char *cache_path, const char *pdftoppm_path,
    const char *pdfinfo_path, unsigned airac, const char *provider_name,
    const chart_prov_info_login_t *provider_login)
{
	chartdb_t *cdb;
	chart_prov_id_t pid;

	ASSERT(cache_path != NULL);
	/* pdftoppm_path can be NULL */
	/* pdfinfo_path can be NULL */
	ASSERT(provider_name != NULL);
	/* provider_login can be NULL */

	for (pid = 0; pid < NUM_PROVIDERS; pid++) {
		if (strcmp(provider_name, prov[pid].name) == 0)
			break;
	}
	if (pid >= NUM_PROVIDERS)
		return (NULL);

	cdb = safe_calloc(1, sizeof (*cdb));
	mutex_init(&cdb->lock);
	avl_create(&cdb->arpts, arpt_compar, sizeof (chart_arpt_t),
	    offsetof(chart_arpt_t, node));
	cdb->path = safe_strdup(cache_path);
	if (pdftoppm_path != NULL)
		cdb->pdftoppm_path = safe_strdup(pdftoppm_path);
	if (pdfinfo_path != NULL)
		cdb->pdfinfo_path = safe_strdup(pdfinfo_path);
	cdb->airac = airac;
	cdb->prov = pid;
	/* Deep-copy the login information */
	if (provider_login != NULL) {
		cdb->prov_login = safe_malloc(sizeof (*cdb->prov_login));
		if (provider_login->username != NULL) {
			cdb->prov_login->username = safe_strdup(
			    provider_login->username);
		}
		if (provider_login->password != NULL) {
			cdb->prov_login->password = safe_strdup(
			    provider_login->password);
		}
		if (provider_login->cainfo != NULL) {
			cdb->prov_login->cainfo = safe_strdup(
			    provider_login->cainfo);
		}
	}
	cdb->normalize_non_icao = B_TRUE;
	/* Default to 1/32 of physical memory, but no more than 256MB */
	cdb->load_limit = MIN(physmem() >> 5, 256 << 20);
	lacf_strlcpy(cdb->prov_name, provider_name, sizeof (cdb->prov_name));

	list_create(&cdb->loader_queue, sizeof (chart_t),
	    offsetof(chart_t, loader_node));
	list_create(&cdb->loader_arpt_queue, sizeof (chart_arpt_t),
	    offsetof(chart_arpt_t, loader_node));
	list_create(&cdb->load_seq, sizeof (chart_t),
	    offsetof(chart_t, load_seq_node));

	worker_init2(&cdb->loader, loader_init, loader, loader_fini, 0, cdb,
	    "chartdb");

	return (cdb);
}

void
chartdb_fini(chartdb_t *cdb)
{
	void *cookie;
	chart_arpt_t *arpt;

	worker_fini(&cdb->loader);

	while(list_remove_head(&cdb->load_seq) != NULL)
		;
	list_destroy(&cdb->load_seq);
	while(list_remove_head(&cdb->loader_queue) != NULL)
		;
	list_destroy(&cdb->loader_queue);
	while(list_remove_head(&cdb->loader_arpt_queue) != NULL)
		;
	list_destroy(&cdb->loader_arpt_queue);

	cookie = NULL;
	while ((arpt = avl_destroy_nodes(&cdb->arpts, &cookie)) != NULL)
		arpt_destroy(arpt);
	avl_destroy(&cdb->arpts);
	mutex_destroy(&cdb->lock);

	free(cdb->proxy);
	free(cdb->path);
	free(cdb->pdftoppm_path);
	free(cdb->pdfinfo_path);
	if (cdb->prov_login != NULL) {
		if (cdb->prov_login->username != NULL) {
			ZERO_FREE_N(cdb->prov_login->username,
			    strlen(cdb->prov_login->username));
		}
		if (cdb->prov_login->password != NULL) {
			ZERO_FREE_N(cdb->prov_login->password,
			    strlen(cdb->prov_login->password));
		}
		if (cdb->prov_login->cainfo != NULL) {
			ZERO_FREE_N(cdb->prov_login->cainfo,
			    strlen(cdb->prov_login->cainfo));
		}
		ZERO_FREE(cdb->prov_login);
	}
	ZERO_FREE(cdb);
}

bool_t
chartdb_test_connection(const char *provider_name,
    const chart_prov_info_login_t *creds)
{
	return (chartdb_test_connection2(provider_name, creds, NULL));
}

bool_t
chartdb_test_connection2(const char *provider_name,
    const chart_prov_info_login_t *creds, const char *proxy)
{
	ASSERT(provider_name != NULL);
	/* creds can be NULL */
	/* proxy can be NULL */

	for (chart_prov_id_t pid = 0; pid < NUM_PROVIDERS; pid++) {
		if (strcmp(provider_name, prov[pid].name) == 0) {
			if (prov[pid].test_conn == NULL)
				return (B_TRUE);
			return (prov[pid].test_conn(creds, proxy));
		}
	}
	return (B_FALSE);
}

void
chartdb_set_load_limit(chartdb_t *cdb, uint64_t bytes)
{
	bytes = MAX(bytes, 16 << 20);
	if (cdb->load_limit != bytes) {
		cdb->load_limit = bytes;
		worker_wake_up(&cdb->loader);
	}
}

void
chartdb_purge(chartdb_t *cdb)
{
	mutex_enter(&cdb->lock);

	/* purge the queue */
	while (list_remove_head(&cdb->loader_queue) != NULL)
		;
	list_insert_tail(&cdb->loader_queue, &cdb->loader_cmd_purge);
	worker_wake_up(&cdb->loader);

	mutex_exit(&cdb->lock);
}

void
chartdb_set_proxy(chartdb_t *cdb, const char *proxy)
{
	ASSERT(cdb != NULL);

	mutex_enter(&cdb->lock);
	LACF_DESTROY(cdb->proxy);
	if (proxy != NULL)
		cdb->proxy = safe_strdup(proxy);
	mutex_exit(&cdb->lock);
}

size_t
chartdb_get_proxy(chartdb_t *cdb, char *proxy, size_t cap)
{
	size_t len;

	ASSERT(cdb != NULL);
	ASSERT(proxy != NULL || cap == 0);

	mutex_enter(&cdb->lock);
	if (cdb->proxy != NULL) {
		len = strlen(cdb->proxy) + 1;
		lacf_strlcpy(proxy, cdb->proxy, cap);
	} else {
		len = 0;
		lacf_strlcpy(proxy, "", cap);
	}
	mutex_exit(&cdb->lock);

	return (len);
}

char **
chartdb_get_chart_names(chartdb_t *cdb, const char *icao, chart_type_t type,
    size_t *num_charts)
{
	chart_arpt_t *arpt;
	char **charts;
	chart_t *chart;
	size_t i, num;

	mutex_enter(&cdb->lock);

	arpt = arpt_find(cdb, icao);
	if (arpt == NULL) {
		mutex_exit(&cdb->lock);
		*num_charts = 0;
		return (NULL);
	}
	if (!arpt->load_complete) {
		if (!list_link_active(&arpt->loader_node)) {
			list_insert_tail(&cdb->loader_arpt_queue, arpt);
			/*
			 * If an airport change has been detected, dump
			 * everything the loader is doing and try and get
			 * the airport load in as quickly as possible.
			 */
			while (list_remove_head(&cdb->loader_queue) != NULL)
				;
			worker_wake_up(&cdb->loader);
		}
		mutex_exit(&cdb->lock);
		*num_charts = 0;
		return (NULL);
	}
	for (chart = avl_first(&arpt->charts), num = 0; chart != NULL;
	    chart = AVL_NEXT(&arpt->charts, chart)) {
		if ((chart->type & type) != 0)
			num++;
	}
	if (num == 0) {
		mutex_exit(&cdb->lock);
		*num_charts = 0;
		return (NULL);
	}
	charts = safe_calloc(num, sizeof (*charts));
	for (chart = avl_first(&arpt->charts), i = 0; chart != NULL;
	    chart = AVL_NEXT(&arpt->charts, chart)) {
		if ((chart->type & type) != 0) {
			ASSERT3U(i, <, num);
			ASSERT(chart->name[0] != '\0');
			charts[i] = safe_strdup(chart->name);
			i++;
		}
	}
	if (cdb->chart_sort_func != NULL) {
		lacf_qsort_r(charts, num, sizeof (*charts),
		    cdb->chart_sort_func, cdb);
	}

	mutex_exit(&cdb->lock);

	*num_charts = num;

	return (charts);
}

void
chartdb_free_str_list(char **l, size_t num)
{
	free_strlist(l, num);
}

static chart_arpt_t *
arpt_find(chartdb_t *cdb, const char *icao)
{
	chart_arpt_t srch = {};
	chart_arpt_t *arpt;

	ASSERT(icao != NULL);

	if (cdb->normalize_non_icao) {
		switch (strlen(icao)) {
		case 3:
			/*
			 * In the US it's common to omit the leading 'K',
			 * especially for non-ICAO airports. Adapt to them.
			 */
			snprintf(srch.icao, sizeof (srch.icao), "K%s", icao);
			break;
		case 4:
			lacf_strlcpy(srch.icao, icao, sizeof (srch.icao));
			break;
		default:
			return (NULL);
		}
	} else {
		lacf_strlcpy(srch.icao, icao, sizeof (srch.icao));
	}
	arpt = avl_find(&cdb->arpts, &srch, NULL);
	if (arpt == NULL && prov[cdb->prov].arpt_lazy_discover != NULL)
		arpt = prov[cdb->prov].arpt_lazy_discover(cdb, icao);
	return (arpt);
}

static chart_t *
chart_find(chartdb_t *cdb, const char *icao, const char *chart_name)
{
	chart_arpt_t *arpt = arpt_find(cdb, icao);
	chart_t srch_chart = { .name = (char *)chart_name };
	if (arpt == NULL)
		return (NULL);
	return (avl_find(&arpt->charts, &srch_chart, NULL));
}

char *
chartdb_get_chart_codename(chartdb_t *cdb, const char *icao,
    const char *chart_name)
{
	chart_t *chart;
	char *codename = NULL;

	ASSERT(cdb != NULL);
	ASSERT(icao != NULL);
	ASSERT(chart_name != NULL);

	mutex_enter(&cdb->lock);

	chart = chart_find(cdb, icao, chart_name);
	if (chart == NULL || chart->load_error) {
		mutex_exit(&cdb->lock);
		return (NULL);
	}
	if (chart->codename != NULL)
		codename = safe_strdup(chart->codename);
	mutex_exit(&cdb->lock);

	return (codename);
}

chart_type_t
chartdb_get_chart_type(chartdb_t *cdb, const char *icao, const char *chart_name)
{
	chart_t *chart;
	chart_type_t type;

	ASSERT(cdb != NULL);
	ASSERT(icao != NULL);
	ASSERT(chart_name != NULL);

	mutex_enter(&cdb->lock);
	chart = chart_find(cdb, icao, chart_name);
	if (chart == NULL || chart->load_error) {
		mutex_exit(&cdb->lock);
		return (CHART_TYPE_UNKNOWN);
	}
	type = chart->type;
	mutex_exit(&cdb->lock);

	return (type);
}

chart_georef_t
chartdb_get_chart_georef(chartdb_t *cdb, const char *icao,
    const char *chart_name)
{
	chart_t *chart;
	chart_georef_t georef;

	ASSERT(cdb != NULL);
	ASSERT(icao != NULL);
	ASSERT(chart_name != NULL);

	mutex_enter(&cdb->lock);
	chart = chart_find(cdb, icao, chart_name);
	if (chart == NULL || chart->load_error) {
		mutex_exit(&cdb->lock);
		return ((chart_georef_t){});
	}
	georef = chart->georef;
	mutex_exit(&cdb->lock);

	return (georef);
}

chart_bbox_t
chartdb_get_chart_view(chartdb_t *cdb, const char *icao,
    const char *chart_name, chart_view_t view)
{
	chart_t *chart;
	chart_bbox_t bbox;

	ASSERT(cdb != NULL);
	ASSERT(icao != NULL);
	ASSERT(chart_name != NULL);
	ASSERT3U(view, <, ARRAY_NUM_ELEM(chart->views));

	mutex_enter(&cdb->lock);
	chart = chart_find(cdb, icao, chart_name);
	if (chart == NULL || chart->load_error) {
		mutex_exit(&cdb->lock);
		return ((chart_bbox_t){});
	}
	bbox = chart->views[view];
	mutex_exit(&cdb->lock);

	return (bbox);
}

chart_procs_t
chartdb_get_chart_procs(chartdb_t *cdb, const char *icao,
    const char *chart_name)
{
	chart_t *chart;
	chart_procs_t procs;

	ASSERT(cdb != NULL);
	ASSERT(icao != NULL);
	ASSERT(chart_name != NULL);

	mutex_enter(&cdb->lock);
	chart = chart_find(cdb, icao, chart_name);
	if (chart == NULL || chart->load_error) {
		mutex_exit(&cdb->lock);
		return ((chart_procs_t){});
	}
	procs = chart->procs;
	mutex_exit(&cdb->lock);

	return (procs);
}

bool_t
chartdb_get_chart_surface(chartdb_t *cdb, const char *icao,
    const char *chart_name, int page, double zoom, bool_t night,
    cairo_surface_t **surf, int *num_pages)
{
	chart_t *chart;

	ASSERT(cdb != NULL);
	ASSERT(icao != NULL);
	ASSERT(chart_name != NULL);
	ASSERT(surf != NULL);
	/* num_pages can be NULL */

	*surf = NULL;
	if (num_pages != NULL)
		*num_pages = 0;

	mutex_enter(&cdb->lock);

	chart = chart_find(cdb, icao, chart_name);
	if (chart == NULL || chart->load_error) {
		mutex_exit(&cdb->lock);
		return (B_FALSE);
	}

	if ((chart->surf == NULL || chart->zoom != zoom ||
	    chart->night != night || chart->cur_page != page) &&
	    !list_link_active(&chart->loader_node)) {
		chart->zoom = zoom;
		chart->load_page = page;
		chart->night = night;
		CAIRO_SURFACE_DESTROY(chart->surf);
		/*
		 * Dump everything else in the queue so we get in first.
		 */
		while (list_remove_head(&cdb->loader_queue) != NULL)
			;
		list_insert_tail(&cdb->loader_queue, chart);
		worker_wake_up(&cdb->loader);
	}

	if (chart->surf != NULL && page == chart->cur_page &&
	    chart->night == night) {
		*surf = cairo_surface_reference(chart->surf);
	} else {
		*surf = NULL;
	}
	if (num_pages != NULL)
		*num_pages = chart->num_pages;

	mutex_exit(&cdb->lock);

	return (B_TRUE);
}

static char *
get_metar_taf_common(chartdb_t *cdb, const char *icao, bool_t metar)
{
	char *result = NULL;
	time_t now = time(NULL);
	chart_arpt_t *arpt;

	mutex_enter(&cdb->lock);

	arpt = arpt_find(cdb, icao);
	if (arpt == NULL) {
		mutex_exit(&cdb->lock);
		return (NULL);
	}

	/*
	 * We could have NULLs in the cache here if the download
	 * failed. In that case, wait a little before retrying
	 * another download.
	 */
	if (metar && now - arpt->metar_load_t < MAX_METAR_AGE) {
		/* Fresh METAR still cached, return that. */
		if (arpt->metar != NULL)
			result = safe_strdup(arpt->metar);
	} else if (!metar && now - arpt->taf_load_t < MAX_TAF_AGE) {
		/* Fresh TAF still cached, return that. */
		if (arpt->taf != NULL)
			result = safe_strdup(arpt->taf);
	} else {
		if (metar) {
			if (!list_link_active(
			    &cdb->loader_cmd_metar.loader_node)) {
				/* Initiate async download of METAR */
				cdb->loader_cmd_metar.arpt = arpt;
				list_insert_tail(&cdb->loader_queue,
				    &cdb->loader_cmd_metar);
				worker_wake_up(&cdb->loader);
			}
			/* If we have an old METAR, return that for now */
			if (arpt->metar != NULL)
				result = safe_strdup(arpt->metar);
		} else {
			if (!list_link_active(
			    &cdb->loader_cmd_taf.loader_node)) {
				/* Initiate async download of TAF */
				cdb->loader_cmd_taf.arpt = arpt;
				list_insert_tail(&cdb->loader_queue,
				    &cdb->loader_cmd_taf);
				worker_wake_up(&cdb->loader);
			}
			/* If we have an old TAF, return that for now */
			if (arpt->taf != NULL)
				result = safe_strdup(arpt->taf);
		}
	}

	mutex_exit(&cdb->lock);
	return (result);
}

bool_t
chartdb_is_ready(chartdb_t *cdb)
{
	bool_t init_complete;
	mutex_enter(&cdb->lock);
	init_complete = cdb->init_complete;
	mutex_exit(&cdb->lock);
	return (init_complete);
}

bool_t
chartdb_is_arpt_known(chartdb_t *cdb, const char *icao)
{
	chart_arpt_t *arpt;
	mutex_enter(&cdb->lock);
	arpt = arpt_find(cdb, icao);
	mutex_exit(&cdb->lock);
	return (arpt != NULL);
}

#define	ARPT_GET_COMMON(field_name) \
	do { \
		chart_arpt_t *arpt; \
		char *field_name; \
		mutex_enter(&cdb->lock); \
		arpt = arpt_find(cdb, icao); \
		if (arpt == NULL) { \
			mutex_exit(&cdb->lock); \
			return (NULL); \
		} \
		field_name = safe_strdup(arpt->field_name); \
		mutex_exit(&cdb->lock); \
		return (field_name); \
	} while (0)

char *
chartdb_get_arpt_name(chartdb_t *cdb, const char *icao)
{
	ARPT_GET_COMMON(name);
}

char *
chartdb_get_arpt_city(chartdb_t *cdb, const char *icao)
{
	ARPT_GET_COMMON(city);
}

char *
chartdb_get_arpt_state(chartdb_t *cdb, const char *icao)
{
	ARPT_GET_COMMON(state);
}

char *
chartdb_get_metar(chartdb_t *cdb, const char *icao)
{
	return (get_metar_taf_common(cdb, icao, B_TRUE));
}

char *
chartdb_get_taf(chartdb_t *cdb, const char *icao)
{
	return (get_metar_taf_common(cdb, icao, B_FALSE));
}

static char *
download_metar_taf_common(chartdb_t *cdb, const char *icao, const char *source,
    const char *node_name)
{
	chart_dl_info_t info;
	char url[256];
	char error_reason[128];
	xmlDoc *doc = NULL;
	xmlXPathContext *xpath_ctx = NULL;
	xmlXPathObject *xpath_obj = NULL;
	char query[128];
	char *result;
	chart_prov_info_login_t login = { .username = NULL };

	snprintf(url, sizeof (url), "https://aviationweather.gov/adds/"
	    "dataserver_current/httpparam?dataSource=%s&requestType=retrieve&"
	    "format=xml&stationString=%s&hoursBeforeNow=2",
	    source, icao);
	snprintf(error_reason, sizeof (error_reason), "Error downloading %s",
	    node_name);
	snprintf(query, sizeof (query), "/response/data/%s/raw_text",
	    node_name);

	if (cdb->prov_login != NULL) {
		/*
		 * If the caller supplied a CAINFO path, we ONLY want to use
		 * that for the METAR download. We do NOT want to send in any
		 * user credentials, which might be meant for the main chart
		 * data provider.
		 */
		login.cainfo = cdb->prov_login->cainfo;
	}

	if (!chart_download(cdb, url, NULL, &login, error_reason, &info))
		return (NULL);
	doc = xmlParseMemory((char *)info.buf, info.bufsz);
	if (doc == NULL) {
		logMsg("Error parsing %s: XML parsing error", node_name);
		goto errout;
	}
	xpath_ctx = xmlXPathNewContext(doc);
	if (xpath_ctx == NULL) {
		logMsg("Error creating XPath context for XML");
		goto errout;
	}
	xpath_obj = xmlXPathEvalExpression((xmlChar *)query, xpath_ctx);
	if (xpath_obj->nodesetval->nodeNr == 0 ||
	    xpath_obj->nodesetval->nodeTab[0]->children == NULL ||
	    xpath_obj->nodesetval->nodeTab[0]->children->content == NULL) {
		char *path = mkpathname(cdb->path, "metar.xml", NULL);

		logMsg("Error parsing %s, valid but incorrect XML structure. "
		    "For debugging purposes, I'm going to dump the raw data "
		    "into a file named %s.", node_name, path);
		FILE *fp = fopen(path, "wb");
		if (fp != NULL) {
			fwrite(info.buf, 1, info.bufsz, fp);
			fclose(fp);
		}
		free(path);
		goto errout;
	}
	result = safe_strdup(
	    (char *)xpath_obj->nodesetval->nodeTab[0]->children->content);
	xmlXPathFreeObject(xpath_obj);
	xmlXPathFreeContext(xpath_ctx);
	xmlFreeDoc(doc);
	free(info.buf);
	return (result);
errout:
	if (xpath_obj != NULL)
		xmlXPathFreeObject(xpath_obj);
	if (xpath_ctx != NULL)
		xmlXPathFreeContext(xpath_ctx);
	if (doc != NULL)
		xmlFreeDoc(doc);
	free(info.buf);

	return (NULL);
}

static char *
download_metar(chartdb_t *cdb, const char *icao)
{
	return (download_metar_taf_common(cdb, icao, "metars", "METAR"));
}

static char *
download_taf(chartdb_t *cdb, const char *icao)
{
	return (download_metar_taf_common(cdb, icao, "tafs", "TAF"));
}

bool_t
chartdb_pending_ext_account_setup(chartdb_t *cdb)
{
	ASSERT(cdb != NULL);
	if (prov[cdb->prov].pending_ext_account_setup != NULL)
		return (prov[cdb->prov].pending_ext_account_setup(cdb));
	else
		return (B_FALSE);
}
