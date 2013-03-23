/*-
 * Copyright (c) 2002 Tim J. Robbins.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
/* commented as FBSDID not needed for Tiger ......
__FBSDID("$FreeBSD: src/usr.bin/who/who.c,v 1.20 2003/10/26 05:05:48 peter Exp $");
*/

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <langinfo.h>
#include <limits.h>
#include <locale.h>
#include <paths.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <timeconv.h>
#include <unistd.h>
#include <utmp.h>

static void	heading(void);
static void	process_utmp(FILE *);
static void	process_wtmp(FILE *);
static void	quick(FILE *);
static void	row(struct utmp *);
static int	ttywidth(void);
static void	usage(void);
static void	whoami(FILE *);

static int	bflag;			/* date & time of last reboot */
static int	dflag;			/* dead processes */
static int	Hflag;			/* Write column headings */
static int	lflag;			/* waiting to login */
static int	mflag;			/* Show info about current terminal */
static int	pflag;			/* Processes active & spawned by init */
static int	qflag;			/* "Quick" mode */
static int	rflag;			/* run-level of the init process */
static int	sflag;			/* Show name, line, time */
static int	tflag;			/* time of change to system clock */
static int	Tflag;			/* Show terminal state */
static int	uflag;			/* Show idle time */

int
main(int argc, char *argv[])
{
	int ch;
	const char *file;
	FILE *fp;
	FILE *wtmp_fp;

	setlocale(LC_TIME, "");

	while ((ch = getopt(argc, argv, "abdHlmpqrstTu")) != -1) {
		switch (ch) {

		case 'a':		/* -b, -d, -l, -p, -r, -t, -T and -u */
			bflag = dflag = lflag = pflag = 1;
			rflag = tflag = Tflag = uflag = 1;
			break;
		case 'b':		/* date & time of last reboot */
			bflag = 1;
			break;
		case 'd':		/* dead processes */
			dflag = 1;
			break;
		case 'H':		/* Write column headings */
			Hflag = 1;
			break;
		case 'l':		/* waiting to login */
			lflag = 1;
			break;
		case 'm':		/* Show info about current terminal */
			mflag = 1;
			break;
		case 'p':		/* Processes active & spawned by init */
			pflag = 1;
			break;
		case 'q':		/* "Quick" mode */
			qflag = 1;
			break;
		case 'r':		/* run-level of the init process */
			rflag = 1;
			break;
		case 's':		/* Show name, line, time */
			sflag = 1;
			break;
		case 't':		/* time of change to system clock */
			tflag = 1;
			break;
		case 'T':		/* Show terminal state */
			Tflag = 1;
			break;
		case 'u':		/* Show idle time */
			uflag = 1;
			break;
		default:
			usage();
			/*NOTREACHED*/
		}
	}
	argc -= optind;
	argv += optind;

	if (argc >= 2 && strcmp(argv[0], "am") == 0 &&
	    (strcmp(argv[1], "i") == 0 || strcmp(argv[1], "I") == 0)) {
		/* "who am i" or "who am I", equivalent to -m */
		mflag = 1;
		argc -= 2;
		argv += 2;
	}
	if (argc > 1)
		usage();

	if (*argv != NULL)
		file = *argv;
	else
		file = _PATH_UTMP;
	if ((fp = fopen(file, "r")) == NULL)
		err(1, "%s", file);

	if (qflag)
		quick(fp);
	else {
		if (sflag)
			Tflag = uflag = 0;
		if (Hflag)
			heading();
		if (mflag)
			whoami(fp);
		else
			/* read and process utmp file for relevant options */
			if( Tflag || uflag || !(bflag || dflag || lflag || pflag || rflag) )
				process_utmp(fp);
	}

	fclose(fp);

	/* read and process wtmp file for relevant options */
	if (bflag || dflag || lflag || pflag || rflag ) {

		/* Open the wtmp file */
		if ((wtmp_fp = fopen(_PATH_WTMP, "r")) == NULL)
                	err(1, "%s", _PATH_WTMP);
		else {
			process_wtmp(wtmp_fp);
			fclose(wtmp_fp);
		}
	}

	exit(0);
}

static void
usage(void)
{

	fprintf(stderr, "usage: who [-abdHlmpqrstTu] [am I] [file]\n");
	exit(1);
}

static void
heading(void)
{

	printf("%-*s ", UT_NAMESIZE, "NAME");
	if (Tflag)
		printf("S ");
	printf("%-*s ", UT_LINESIZE, "LINE");
	printf("%-*s ", 12, "TIME");
	if (uflag)
		printf("IDLE  ");
	printf("%-*s", UT_HOSTSIZE, "FROM");
	putchar('\n');
}

static void
row(struct utmp *ut)
{
	char buf[80], tty[sizeof(_PATH_DEV) + UT_LINESIZE];
	struct stat sb;
	time_t idle, t;
	static int d_first = -1;
	struct tm *tm;
	char state;

	if (d_first < 0)
		d_first = (*nl_langinfo(D_MD_ORDER) == 'd');

	if (Tflag || uflag) {
		snprintf(tty, sizeof(tty), "%s%.*s", _PATH_DEV,
			UT_LINESIZE, ut->ut_line);
		state = '?';
		idle = 0;
		if (stat(tty, &sb) == 0) {
			state = sb.st_mode & (S_IWOTH|S_IWGRP) ?
			    '+' : '-';
			idle = time(NULL) - sb.st_mtime;
		}
	}

	printf("%-*.*s ", UT_NAMESIZE, UT_NAMESIZE, ut->ut_name);
	if (Tflag)
		printf("%c ", state);
	printf("%-*.*s ", UT_LINESIZE, UT_LINESIZE, ut->ut_line);
	t = _time32_to_time(ut->ut_time);
	tm = localtime(&t);
	strftime(buf, sizeof(buf), d_first ? "%e %b %R" : "%b %e %R", tm);
	printf("%-*s ", 12, buf);
	if (uflag) {
		if (idle < 60)
			printf("  .   ");
		else if (idle < 24 * 60 * 60)
			printf("%02d:%02d ", (int)(idle / 60 / 60),
			    (int)(idle / 60 % 60));
		else
			printf(" old  ");
	}
	if (*ut->ut_host != '\0')
		printf("(%.*s)", UT_HOSTSIZE, ut->ut_host);
	putchar('\n');

}

static void
process_utmp(FILE *fp)
{
	struct utmp ut;

	while (fread(&ut, sizeof(ut), 1, fp) == 1)
		if (*ut.ut_name != '\0') {
			row(&ut);
		}
}

/* For some options, process the wtmp file to generate output */
static void
process_wtmp(FILE *fp)
{
	struct utmp ut;
	struct utmp lboot_ut = { "", "", "", 0 };
	int num = 0;	/* count of user entries */

	while (fread(&ut, sizeof(ut), 1, fp) == 1)
		if (*ut.ut_name != '\0') {
			if (bflag && (!strcmp(ut.ut_name, "reboot"))) {
				memcpy(&lboot_ut, &ut, sizeof(ut));
			}
			else
				num++;
		};

	if (bflag && (!strcmp(lboot_ut.ut_name, "reboot")))
		row(&lboot_ut);

	/* run level of the init process is unknown in BSD system. If multi
	   user, then display the highest run level. Else, no-op.
	*/
	if (rflag && (num > 1))
		printf("   .       run-level 3\n");
}

static void
quick(FILE *fp)
{
	struct utmp ut;
	int col, ncols, num;

	ncols = ttywidth();
	col = num = 0;
	while (fread(&ut, sizeof(ut), 1, fp) == 1) {
		if (*ut.ut_name == '\0')
			continue;
		printf("%-*.*s", UT_NAMESIZE, UT_NAMESIZE, ut.ut_name);
		if (++col < ncols / (UT_NAMESIZE + 1))
			putchar(' ');
		else {
			col = 0;
			putchar('\n');
		}
		num++;
	}
	if (col != 0)
		putchar('\n');

	printf("# users = %d\n", num);
}

static void
whoami(FILE *fp)
{
	struct utmp ut;
	struct passwd *pwd;
	const char *name, *p, *tty;

	if ((tty = ttyname(STDIN_FILENO)) == NULL)
		tty = "tty??";
	else if ((p = strrchr(tty, '/')) != NULL)
		tty = p + 1;

	/* Search utmp for our tty, dump first matching record. */
	while (fread(&ut, sizeof(ut), 1, fp) == 1)
		if (*ut.ut_name != '\0' && strncmp(ut.ut_line, tty,
		    UT_LINESIZE) == 0) {
			row(&ut);
			return;
		}

	/* Not found; fill the utmp structure with the information we have. */
	memset(&ut, 0, sizeof(ut));
	if ((pwd = getpwuid(getuid())) != NULL)
		name = pwd->pw_name;
	else
		name = "?";
	strncpy(ut.ut_name, name, UT_NAMESIZE);
	strncpy(ut.ut_line, tty, UT_LINESIZE);
	ut.ut_time = _time_to_time32(time(NULL));
	row(&ut);
}

static int
ttywidth(void)
{
	struct winsize ws;
	long width;
	char *cols, *ep;

	if ((cols = getenv("COLUMNS")) != NULL && *cols != '\0') {
		errno = 0;
		width = strtol(cols, &ep, 10);
		if (errno || width <= 0 || width > INT_MAX || ep == cols ||
		    *ep != '\0')
			warnx("invalid COLUMNS environment variable ignored");
		else
			return (width);
	}
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != -1)
		return (ws.ws_col);

	return (80);
}
