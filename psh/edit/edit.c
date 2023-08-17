/*
 * Phoenix-RTOS
 *
 * edit - Text Editor
 *
 * Copyright 2021 Phoenix Systems
 * Author: Gerard Swiderski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <termios.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../psh.h"

/* Tabulator stop width */
#define EDIT_TAB_STOP 4


typedef struct {
	char *chars;
	int len;
	char updated;
} row_t;


typedef struct {
	int what;
	union {
		int key;
		int errcode;
	} data;
} event_t;


/* Editor globals */
static struct {
	char *filename;
	row_t *row;
	int rows, cols, hbar;
	int cx, cy;
	int vx, vy, rx;
	int nrows;
	const char *msg;
	unsigned char color;
	/* flags */
	unsigned char dirty : 1;
	unsigned char replace : 1;
} edit_common;


/* Terminal globals */
static struct {
	struct termios orig;
	int evCnt, cols, rows;
	event_t ev;
} term_common;


/* clang-format off */

enum { colorDefault = 0x70, colorInfo = 0x60, colorWarn = 0xb0, colorError = 0x10 };

enum { evNone, evRedraw, evQuit, evFatalError, evKey, evKeyDead };

/* clang-format on */


enum {
	keyEsc = 0x1b,
	keyEnter = 0x0d,

	/* edit and cursor movement */
	keyEditFirst = 0x100,
	keyBksp = keyEditFirst + '%',
	keyTab = keyEditFirst + '-',
	keyShiftTab = keyEditFirst + '=',
	keyUp = keyEditFirst + 'A',
	keyDown = keyEditFirst + 'B',
	keyRight = keyEditFirst + 'C',
	keyLeft = keyEditFirst + 'D',
	keyIns = keyEditFirst + '2',
	keyDel = keyEditFirst + '3',
	keyPgup = keyEditFirst + '5',
	keyPgdn = keyEditFirst + '6',
	keyHome = keyEditFirst + '7',
	keyEnd = keyEditFirst + '8',

	/* control + letter */
	keyCtrlFirst = 0x200,
	keyCtrlC = keyCtrlFirst + 'C',
	keyCtrlD = keyCtrlFirst + 'D',
	keyCtrlL = keyCtrlFirst + 'L',
	keyCtrlQ = keyCtrlFirst + 'Q',
	keyCtrlS = keyCtrlFirst + 'S',
	keyCtrlX = keyCtrlFirst + 'X'
};


static void term_print(const char *str)
{
	(void)psh_write(STDOUT_FILENO, str, strlen(str));
}


static int term_getCursor(int *col, int *row)
{
	char buf[16];
	struct termios prev, curr;
	int ret, i, e, lcol, lrow;

	if ((ret = tcgetattr(STDIN_FILENO, &prev)) < 0)
		return ret;

	curr = prev;
	cfmakeraw(&curr);

	curr.c_cc[VTIME] = 0;
	curr.c_cc[VMIN] = 1;

	if ((ret = tcsetattr(STDIN_FILENO, TCSAFLUSH, &curr)) < 0)
		return ret;

	term_print("\033[6n");

	for (i = 0, e = 0; i < sizeof(buf) - 1; i++) {
		if ((ret = read(STDIN_FILENO, &buf[i], 1)) < 1)
			break;

		if (buf[i] == '\033')
			e = i;

		if ((ret = ((i - e) >= 5 && buf[e] == '\033' && buf[e + 1] == '[' && buf[i] == 'R')))
			break;
	}

	buf[i] = '\0';

	if (ret > 0 && sscanf(&buf[e], "\033[%d;%dR", &lrow, &lcol) == 2) {
		*col = lcol;
		*row = lrow;
	}

	if ((ret = tcsetattr(STDIN_FILENO, TCSAFLUSH, &prev)) < 0)
		return ret;

	return 0;
}


static int term_getSize(int *col, int *row)
{
	int ret, lcol = -1, lrow = -1;

	if ((ret = term_getCursor(&lcol, &lrow)) < 0)
		return ret;

	if (lcol < 0 || lrow < 0)
		return -1;

	term_print("\0337\033[999B\033[999C");

	if ((ret = term_getCursor(&lcol, &lrow)) < 0)
		return ret;

	term_print("\0338");

	*col = lcol;
	*row = lrow;

	return 0;
}


static int term_setup(int cols, int rows)
{
	struct termios curr;
	int ret;

	if (cols < 0 || rows < 0) {
		if (term_getSize(&cols, &rows) < 0) {
			cols = 80;
			rows = 25;
		}
	}

	if (cols)
		term_common.rows = cols;

	if (rows)
		term_common.cols = rows;

	if ((ret = tcgetattr(STDIN_FILENO, &curr)) < 0)
		return ret;

	term_common.orig = curr;

	cfmakeraw(&curr);
	curr.c_cc[VTIME] = 0;
	curr.c_cc[VMIN] = 1;

	return tcsetattr(STDIN_FILENO, TCSAFLUSH, &curr);
}


static int term_restore(void)
{
	term_print("\033[H\033[2J");

	return tcsetattr(STDIN_FILENO, TCSAFLUSH, &term_common.orig);
}


static int term_handleCtrl(char c, event_t *ev)
{
	ev->what = evKey;
	switch (c) {
		case 0x03: /* ctrl+C */
			ev->data.key = keyCtrlC;
			return 0;

		case 0x04: /* ctrl+D */
			ev->data.key = keyCtrlD;
			return 0;

		case 0x08: /* backspace */
		case 0x7f: /* backspace */
			ev->data.key = keyBksp;
			return 0;

		case '\t': /* tab */
			ev->data.key = keyTab;
			return 0;

		case 0x0c: /* ctrl+L */
			ev->data.key = keyCtrlL;
			return 0;

		case '\n': /* enter */
		case '\r': /* enter */
			ev->data.key = keyEnter;
			return 0;

		case 0x11: /* ctrl+Q */
			ev->data.key = keyCtrlQ;
			return 0;

		case 0x13: /* ctrl+S */
			ev->data.key = keyCtrlS;
			return 0;

		case 0x18: /* ctrl+X */
			ev->data.key = keyCtrlX;
			return 0;

		case 0x1b: /* escape */
			ev->data.key = keyEsc;
			return 0;

		default:
			break;
	}

	ev->what = evKeyDead;
	return 0;
}


static int term_handleEscape(char buf[16], int len, event_t *ev)
{
	char c;
	ev->what = evKey;

	if (len == 4 && buf[len - 1] == '~') {
		switch (c = buf[len - 2]) {
			case '1': /* home (tmux/screen) */
				ev->data.key = keyHome;
				return 0;

			case '4': /* end (tmux/screen) */
				ev->data.key = keyEnd;
				return 0;

			case '2': /* insert */
			case '3': /* delete */
			case '5': /* page up */
			case '6': /* page down */
			case '7': /* home */
			case '8': /* end */
				ev->data.key = keyEditFirst + c;
				return 0;

			default:
				break;
		}
	}
	else if (len == 3 && (buf[1] == '[' || buf[1] == 'O')) {
		switch (c = buf[len - 1]) {
			case 'A': /* arrow up */
			case 'B': /* arrow down */
			case 'C': /* arrow right */
			case 'D': /* arrow left */
				ev->data.key = keyEditFirst + c;
				return 0;

			case 'H': /* home */
				ev->data.key = keyHome;
				return 0;

			case 'F': /* end */
				ev->data.key = keyEnd;
				return 0;

			case 'Z': /* shift+tab */
				ev->data.key = keyShiftTab;
				return 0;

			case 'P':
			case 'Q':
			case 'R':
			case 'S':
				ev->what = evKeyDead;
				return 0;

			default:
				break;
		}
	}

	ev->what = evKeyDead;
	return 1;
}


static inline void term_clearEvent(event_t *ev)
{
	ev->what = evNone;
}


static inline void term_sendEvent(event_t ev)
{
	term_common.ev = ev;
}


static int term_getEvent(event_t *ev)
{
	int rlen = -1, pos = 0;
	char c, buf[16] = { 0 };

	if (term_common.ev.what != evNone) {
		*ev = term_common.ev;
		term_clearEvent(&term_common.ev);
	}

	if (ev->what == evQuit)
		return 0;
	else if (ev->what == evFatalError)
		return -ev->data.errcode;
	else if (ev->what != evNone && term_common.evCnt < 3)
		return ++term_common.evCnt;

	term_common.evCnt = 0;
	term_clearEvent(ev);

	while (pos < sizeof(buf)) {
		c = 0;
		if ((rlen = read(STDIN_FILENO, &c, 1)) <= 0)
			break;

		if (iscntrl(c)) {
			if (c != '\033')
				return term_handleCtrl(c, ev) == 0;

			if (pos > 0 && buf[pos - 1] == '\033')
				return term_handleCtrl(c, ev) == 0;

			pos = 0;
			buf[pos++] = '\033';

			continue;
		}
		else if (pos == 0) {
			ev->what = evKey;
			ev->data.key = (unsigned char)c;
			return 1;
		}

		buf[pos++] = c;

		if (pos > 2 && (buf[1] == '[' || buf[1] == 'O')) {
			if (term_handleEscape(buf, pos, ev) == 0)
				return 1;

			if (buf[pos - 1] == '~') {
				ev->what = evKeyDead;
				return 1;
			}
		}
	}

	if (pos == 1 && buf[0] == '\033')
		return term_handleCtrl(c, ev) == 0;

	if (rlen < 0 && errno != EAGAIN)
		return rlen;

	return 1;
}


static void edit_status(const char *msg, unsigned char color)
{
	edit_common.color = color;
	edit_common.msg = msg;
}


static int edit_error(int errcode)
{
	event_t ev = { .what = evFatalError, .data = { .errcode = errcode } };
	term_sendEvent(ev);
	return -errcode;
}


static int edit_updateScroll(void)
{
	unsigned int i;
	row_t *row;
	int vy = edit_common.vy, vx = edit_common.vx;

	edit_common.rx = 0;

	edit_common.cols = term_common.cols;
	edit_common.rows = term_common.rows - edit_common.hbar;

	if (edit_common.cy < edit_common.nrows) {
		row = &edit_common.row[edit_common.cy];
		for (i = 0; i < edit_common.cx; i++) {
			if (row->chars[i] == '\t') {
				edit_common.rx += (EDIT_TAB_STOP - 1) - (edit_common.rx % EDIT_TAB_STOP);
			}
			edit_common.rx++;
		}
	}

	if (edit_common.cy < edit_common.vy)
		edit_common.vy = edit_common.cy;

	if (edit_common.cy >= edit_common.vy + edit_common.rows)
		edit_common.vy = edit_common.cy - edit_common.rows + 1;

	if (edit_common.rx < edit_common.vx)
		edit_common.vx = edit_common.rx;

	if (edit_common.rx >= edit_common.vx + edit_common.cols)
		edit_common.vx = edit_common.rx - edit_common.cols + 1;

	if (vx != edit_common.vx || vy != edit_common.vy)
		return 1;

	return 0;
}


static void edit_rowRender(row_t *row, int offset, int rx)
{
	unsigned int i, j, end = offset + edit_common.cols;
	char ch;

	for (i = 0, j = 0; i < row->len; i++) {
		if (row->chars[i] == '\t')
			j++;
	}

	j = row->len + j * (EDIT_TAB_STOP - 1) + 1;

	if (offset > j)
		offset = j;

	for (i = 0, j = 0; i < row->len; i++) {
		ch = row->chars[i];
		if (ch == '\t') {
			do {
				if (j >= offset && j < end)
					fputc(' ', stdout);
				j++;
			} while ((j % EDIT_TAB_STOP) != 0);
			continue;
		}
		else if (isprint(ch) == 0) {
			ch = '?';
		}

		if (j >= offset && j < end)
			fputc(ch, stdout);
		j++;
	}
}


static int edit_rowInsert(int pos, char *line, size_t len)
{
	int i;
	char *buf;
	row_t *row;

	if (pos < 0 || pos > edit_common.nrows) {
		return 0;
	}

	if ((buf = malloc(len + 1)) == NULL) {
		return edit_error(ENOMEM);
	}

	if ((row = realloc(edit_common.row, sizeof(*row) * (edit_common.nrows + 1))) == NULL) {
		free(buf);
		return edit_error(ENOMEM);
	}

	if (edit_common.nrows - pos > 0)
		memmove(&row[pos + 1], &row[pos], sizeof(*row) * (edit_common.nrows - pos));

	row[pos].len = len;
	row[pos].chars = buf;
	memcpy(buf, line, len);

	edit_common.row = row;
	edit_common.nrows++;
	edit_common.dirty = 1;

	for (i = pos; i < edit_common.nrows; i++) {
		row[i].updated = 1;
	}

	return 1;
}


static void edit_rowDelete(int cury)
{
	int i;

	if (cury < 0 || cury >= edit_common.nrows)
		return;

	free(edit_common.row[cury].chars);

	if (edit_common.nrows > 1) {
		memmove(&edit_common.row[cury], &edit_common.row[cury + 1], sizeof(row_t) * (edit_common.nrows - cury - 1));

		for (i = cury; i < edit_common.nrows; i++)
			edit_common.row[i].updated = 1;
	}

	edit_common.nrows--;
	edit_common.dirty = 1;
}


static int edit_rowAppend(int cury, char *chars, size_t len)
{
	row_t *row = &edit_common.row[cury];
	char *tmp = realloc(row->chars, row->len + len + 1);

	if (tmp == NULL)
		return edit_error(ENOMEM);

	memcpy(&tmp[row->len], chars, len);
	row->chars = tmp;
	row->len += len;
	row->updated = 1;
	edit_common.dirty = 1;

	return 0;
}


static int edit_rowInsertChar(int curx, int cury, char ch)
{
	char *tmp;
	row_t *row = &edit_common.row[cury];

	if (curx < 0 || curx > row->len)
		curx = row->len;

	if (edit_common.replace == 0 || curx == row->len) {
		if ((tmp = realloc(row->chars, row->len + 1)) == NULL)
			return edit_error(ENOMEM);

		row->chars = tmp;

		if (edit_common.replace == 0)
			memmove(&row->chars[curx + 1], &row->chars[curx], row->len - curx);

		row->len++;
	}

	row->chars[curx] = ch;

	row->updated = 1;
	edit_common.dirty = 1;

	return 0;
}


static void edit_rowDeleteChar(int curx, int cury)
{
	row_t *row = &edit_common.row[cury];

	if (curx < 0 || curx >= row->len)
		return;

	row->len--;
	memmove(&row->chars[curx], &row->chars[curx + 1], row->len - curx);
	row->updated = 1;
	edit_common.dirty = 1;
}


static void edit_insertChar(char ch)
{
	if (edit_common.cy == edit_common.nrows) {
		if (edit_rowInsert(edit_common.nrows, "", 0) < 0)
			return;
	}

	if (edit_rowInsertChar(edit_common.cx, edit_common.cy, ch) == 0)
		edit_common.cx++;
}


static void edit_deleteChar(void)
{
	row_t *row;

	if (edit_common.nrows == 1 && edit_common.row[0].len == 0) {
		edit_common.cx = edit_common.cy = 0;
		edit_rowDelete(0);
		return;
	}

	if (edit_common.cx == 0 && edit_common.cy == 0)
		return;

	if (edit_common.cx > 0) {
		edit_rowDeleteChar(edit_common.cx - 1, edit_common.cy);
		edit_common.cx--;
	}
	else {
		edit_common.cx = edit_common.row[edit_common.cy - 1].len;
		if (edit_common.cy < edit_common.nrows) {
			row = &edit_common.row[edit_common.cy];
			if (edit_rowAppend(edit_common.cy - 1, row->chars, row->len) < 0)
				return;
		}
		edit_rowDelete(edit_common.cy);
		edit_common.cy--;
	}
}


static void edit_insertNewLine(void)
{
	row_t *row;

	if (edit_common.cx == 0) {
		if (edit_rowInsert(edit_common.cy, "", 0) < 0)
			return;
	}
	else {
		row = &edit_common.row[edit_common.cy];
		if (edit_rowInsert(edit_common.cy + 1, &row->chars[edit_common.cx], row->len - edit_common.cx) < 0)
			return;
		row = &edit_common.row[edit_common.cy];
		row->len = edit_common.cx;
		row->updated = 1;
	}
	edit_common.cy++;
	edit_common.cx = 0;
}


static void edit_drawRows(int force)
{
	unsigned int y, ry;

	for (y = 0; y < edit_common.rows; y++) {
		ry = y + edit_common.vy;

		if (ry < edit_common.nrows) {
			if (force || edit_common.row[ry].updated != 0) {
				fprintf(stdout, "\033[%dH", y + 1);
				edit_rowRender(&edit_common.row[ry], edit_common.vx, edit_common.rx);
				edit_common.row[ry].updated = 0;
				fprintf(stdout, "\033[0K");
			}
		}
		else if (force || ry == edit_common.nrows) {
			fprintf(stdout, "\033[%dH~\033[0K", y + 1);
		}
	}
}


static void edit_drawStatusBar(int force)
{
	int len;
	char buf[32];
	int color = colorDefault;

	if (edit_common.msg != NULL)
		color = edit_common.color;

	fprintf(stdout, "\0337\033[%dH\033[%u;%um\033[0K",
		edit_common.rows + 1,
		(color & 0x80 ? 100 : 40) + ((color >> 4) & 7),
		(color & 0x8 ? 90 : 30) + (color & 7));

	if (edit_common.msg == NULL) {
		if (edit_common.filename)
			fprintf(stdout, " [%s]", edit_common.filename);
		else
			fprintf(stdout, " [No name]");

		fprintf(stdout, " - %d lines ", edit_common.nrows);
	}
	else {
		fprintf(stdout, " %s", edit_common.msg);
		edit_common.msg = NULL;
	}

	len = snprintf(buf, sizeof(buf), "[%d:%d] %c%c",
		edit_common.cy + 1, edit_common.cx + 1,
		edit_common.replace ? 'R' : 'I',
		edit_common.dirty ? '*' : ' ');

	fprintf(stdout, "\033[%d;%dH%s", edit_common.rows + 1, edit_common.cols - len, buf);

	fprintf(stdout, "\0338\033[0m");
}


static void edit_drawKeys(int force)
{
	unsigned int n, len;
	static const struct {
		char *key, *descr;
	} keys[] = {
		{ "^S", "Save" },
		{ "^Q", "Quit" },
		{ "^D", "Delete row" },
		{ "^L", "Redraw" },
		{ "Ins", "Insert/Replace" },
	};

	if (force == 0)
		return;

	fprintf(stdout, "\0337\033[%dH\033[37;40m\033[0K\033[0m", term_common.rows);

	for (n = 0, len = 0; n < sizeof(keys) / sizeof(keys[0]); n++) {
		if (keys[n].descr) {
			len += strlen(keys[n].key) + strlen(keys[n].descr) + 3;

			if (len >= edit_common.cols)
				break;

			fprintf(stdout, " \033[1;40;33m%s\033[21;0m %s ", keys[n].key, keys[n].descr);
		}
		else
			len += fprintf(stdout, "|");
	}

	fprintf(stdout, "\033[0K\0338\033[0m");
}


static void edit_draw(int force)
{
	int changed = edit_updateScroll();

	fprintf(stdout, "\033[?25l");

	edit_drawStatusBar(force);
	edit_drawKeys(force);
	edit_drawRows(force || changed);

	fprintf(stdout, "\033[?25h\033[%d;%dH", (edit_common.cy - edit_common.vy) + 1, (edit_common.rx - edit_common.vx) + 1);

	fflush(stdout);
}


static void edit_cursorMove(unsigned int key, int count)
{
	row_t *row;

	while (count-- > 0) {
		if (edit_common.cy < edit_common.nrows)
			row = &edit_common.row[edit_common.cy];
		else
			row = NULL;

		switch (key) {
			case keyUp:
				if (edit_common.cy > 0)
					edit_common.cy--;
				break;

			case keyDown:
				if (edit_common.cy < edit_common.nrows)
					edit_common.cy++;
				break;

			case keyLeft:
				if (edit_common.cx > 0) {
					edit_common.cx--;
				}
				else if (edit_common.cy > 0) {
					edit_common.cy--;
					edit_common.cx = edit_common.row[edit_common.cy].len;
				}
				break;

			case keyRight:
				if (row == NULL)
					break;

				if (edit_common.cx < row->len) {
					edit_common.cx++;
				}
				else if (edit_common.cx == row->len) {
					edit_common.cy++;
					edit_common.cx = 0;
				}

				break;
		}

		if (edit_common.cy < edit_common.nrows) {
			row = &edit_common.row[edit_common.cy];
			if (edit_common.cx > row->len)
				edit_common.cx = row->len;
		}
		else {
			edit_common.cx = 0;
		}
	}
}


static int edit_open(char *filename)
{
	FILE *f;
	ssize_t len;
	size_t n = 0;
	char *line = NULL;
	struct stat st;

	edit_common.filename = strdup(filename);
	if (edit_common.filename == NULL)
		return edit_error(ENOMEM);

	if (stat(filename, &st) < 0) {
		edit_status("Editing new empty file", colorDefault);
		return 0;
	}

	/* Load file if exist */
	if ((f = fopen(filename, "r")) == NULL)
		return -1;

	while ((len = getline(&line, &n, f)) != -1) {
		while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
			len--;
		}
		if (edit_rowInsert(edit_common.nrows, line, len) < 0) {
			break;
		}
	}

	free(line);
	fclose(f);

	edit_common.dirty = 0;

	return edit_common.nrows;
}


static void edit_save(void)
{
	int len, n = -1;
	size_t total = 0;
	row_t *row;

	if (edit_common.filename == NULL)
		return;

	FILE *f = fopen(edit_common.filename, "w");

	if (f != NULL) {
		for (n = 0; n < edit_common.nrows; n++) {
			row = &edit_common.row[n];

			if ((len = fprintf(f, "%.*s\n", row->len, row->chars)) < 0) {
				if (ferror(f) || feof(f)) {
					clearerr(f);
					n = -1;
					break;
				}
			}

			if (len != row->len + 1) {
				n = -1;
				break;
			}

			total += len;
		}

		fclose(f);
	}
	fflush(stdout);

	if (n < 0) {
		edit_status("Unable to write file to disk!", colorError);
		return;
	}

	edit_common.dirty = 0;

	edit_status("File saved.", colorInfo);
}


static void edit_handleEvent(event_t *ev)
{
	if (ev->what == evKey) {
		switch (ev->data.key) {
			case keyCtrlL:
				ev->what = evRedraw;
				return;

			case keyCtrlX:
				break;

			case keyCtrlC:
				ev->what = evQuit;
				return;

			case keyCtrlQ:
				if (edit_common.dirty == 0) {
					ev->what = evQuit;
					return;
				}

				edit_status("Please save file or use ^C to abandon editing.", colorWarn);
				break;

			case keyCtrlS:
				edit_save();
				break;

			case keyEnter:
				edit_insertNewLine();
				break;

			case keyCtrlD:
				if (edit_common.nrows > 0 && edit_common.cy == edit_common.nrows)
					edit_common.cy--;
				edit_rowDelete(edit_common.cy);
				edit_common.cx = 0;
				break;

			case keyDel:
				edit_cursorMove(keyRight, 1);
				/* fallthrough */

			case keyBksp:
				edit_deleteChar();
				break;

			case keyIns:
				edit_common.replace ^= 1;
				break;

			case keyHome:
				edit_common.cx = 0;
				break;

			case keyEnd:
				if (edit_common.cy < edit_common.nrows)
					edit_common.cx = edit_common.row[edit_common.cy].len;
				break;

			case keyPgup:
				edit_common.cy = edit_common.vy;
				edit_cursorMove(keyUp, edit_common.rows);
				break;

			case keyPgdn:
				edit_common.cy = edit_common.vy + edit_common.rows - 1;

				if (edit_common.cy > edit_common.nrows)
					edit_common.cy = edit_common.nrows;

				edit_cursorMove(keyDown, edit_common.rows);

				break;

			case keyUp:
			case keyDown:
			case keyLeft:
			case keyRight:
				edit_cursorMove(ev->data.key, 1);
				break;

			case keyTab:
				edit_insertChar('\t');
				break;

			default:
				edit_insertChar(ev->data.key);
				break;
		}

		term_clearEvent(ev);
		edit_draw(0);
		return;
	}

	if (ev->what == evRedraw) {
		term_clearEvent(ev);
		term_getSize(&term_common.cols, &term_common.rows);
		edit_draw(1);
	}
}


static void edit_free(void)
{
	if (edit_common.nrows > 0) {
		while (edit_common.nrows-- > 0) {
			free(edit_common.row[edit_common.nrows].chars);
		}
	}

	free(edit_common.row);
	free(edit_common.filename);
}


static int edit_init(int argc, char **argv)
{
	int ret;

	memset(&edit_common, 0, sizeof(edit_common));

	if (argc != 2) {
		printf("Usage: %s <file name>\n", argv[0]);
		return -EINVAL;
	}

	if ((ret = edit_open(argv[1])) < 0) {
		printf("Unable to open file '%s'\n", argv[1]);
	}

	edit_common.hbar = 2;

	return ret;
}


static void edit_info(void)
{
	printf("text editor");
}


static int edit_main(int argc, char **argv)
{
	event_t ev = { .what = evRedraw };
	int ret = -1;

	do {
		if ((ret = edit_init(argc, argv)) < 0)
			break;

		if (term_setup(-1, -1) < 0)
			break;

		while ((ret = term_getEvent(&ev)) > 0) {
			if (ret > 1) {
				term_clearEvent(&ev);
				term_print("\007");
			}

			edit_handleEvent(&ev);
		}

		if (term_restore() < 0)
			break;

	} while (0);

	edit_free();

	if (ret == -ENOMEM)
		printf("Out of memory.\n");

	return ret;
}


void __attribute__((constructor)) edit_registerapp(void)
{
	static psh_appentry_t app = { .name = "edit", .run = edit_main, .info = edit_info };
	psh_registerapp(&app);
}
