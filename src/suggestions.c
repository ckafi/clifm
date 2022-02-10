/* suggestions.c -- functions to manage the suggestions system */

/*
 * This file is part of CliFM
 * 
 * Copyright (C) 2016-2022, L. Abramovich <johndoe.arch@outlook.com>
 * All rights reserved.

 * CliFM is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * CliFM is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
*/
#ifndef _NO_SUGGESTIONS

#include "helpers.h"

#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#ifdef __OpenBSD__
#include <strings.h>
#endif
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <termios.h>

#include <wchar.h>

#ifdef __linux__
#include <sys/capability.h>
#endif

#ifdef __OpenBSD__
typedef char *rl_cpvfunc_t;
#include <ereadline/readline/readline.h>
#else
#include <readline/readline.h>
#endif

#include "aux.h"
#include "checks.h"
#include "colors.h"
#include "jump.h"
#include "readline.h"
#include "builtins.h"
#include "prompt.h"

#ifndef _NO_HIGHLIGHT
#include "highlight.h"
#endif

#define NO_MATCH      0
#define PARTIAL_MATCH 1
#define FULL_MATCH    2

#define CHECK_MATCH 0
#define PRINT_MATCH 1

#define BAEJ_OFFSET 2

char *last_word = (char *)NULL;
int last_word_offset = 0;

#ifndef _NO_HIGHLIGHT
/* Change the color of the word _LAST_WORD, at offset OFFSET, to COLOR
 * in the current input string */
/*static void
change_word_color(const char *_last_word, const int offset, const char *color)
{
	int bk = rl_point;
	fputs("\x1b[?25l", stdout);
	rl_delete_text(offset, rl_end);
	rl_point = rl_end = offset;
	rl_redisplay();
	fputs(color, stdout);
	rl_insert_text(_last_word);
	rl_point = bk;
	fputs("\x1b[?25h", stdout);
} */
#endif

int
recover_from_wrong_cmd(void)
{
	/* Check rl_dispathing to know whether we are called from a keybind,
	 * in which case we should skip this check */
	if (rl_line_buffer && (rl_dispatching == 0 || nwords > 1)) {
		char *p = (strrchr(rl_line_buffer, ' '));
		if (p && p != rl_line_buffer && *(p - 1) != '\\' && *(p + 1) != ' ')
			return EXIT_FAILURE;
	}

	fputs(NC, stdout);
	rl_restore_prompt();
	rl_clear_message();

#ifndef _NO_HIGHLIGHT
	if (highlight)
		recolorize_line();
#endif
	wrong_cmd = 0;
	return EXIT_SUCCESS;
}

/* This function is only used before running a keybind command. We don't
 * want the suggestion buffer after running a keybind */
void
free_suggestion(void)
{
	free(suggestion_buf);
	suggestion_buf = (char *)NULL;
	suggestion.printed = 0;
	suggestion.nlines = 0;
}

void
clear_suggestion(const int free_sug)
{
	/* Delete everything in the current line starting from the current
	 * cursor position */
	if (write(STDOUT_FILENO, DLFC, DLFC_LEN) <= 0) {/* Avoid compiler warning */}

	if (suggestion.nlines > 1) {
		/* Save cursor position */
		get_cursor_position(STDIN_FILENO, STDOUT_FILENO);

		int i = (int)suggestion.nlines;
		while (--i > 0) {
			/* Move the cursor to the beginning of the next line */
			if (write(STDOUT_FILENO, "\x1b[1E", 4) <= 0) {/* Avoid compiler warning */}
			/* Delete the line */
			if (write(STDOUT_FILENO, "\x1b[0K", 4) <= 0) {/* Avoid compiler warning */}
		}
		/* Restore cursor position */
		printf("\x1b[%d;%dH", currow, curcol);
		fflush(stdout);
		suggestion.nlines = 0;
	}

	suggestion.printed = 0;
	if (free_sug) {
		free(suggestion_buf);
		suggestion_buf = (char *)NULL;
	}
}

void
remove_suggestion_not_end(void)
{
	printf("\x1b[%dC", rl_end - rl_point);
	fflush(stdout);
	clear_suggestion(CS_FREEBUF);
	printf("\x1b[%dD", rl_end - rl_point);
	fflush(stdout);
}

static inline void
restore_cursor_position(const size_t slines)
{
	/* Update the row number, if needed */
	/* If the cursor is in the last row, printing a multi-line suggestion
	 * will move the beginning of the current line up the number of
	 * lines taken by the suggestion, so that we need to update the
	 * value to move the cursor back to the correct row (the beginning
	 * of the line) */
	int old_currow = currow;
	/* extra_rows: amount of extra rows we need to print the suggestion
	 * (excluding the current row) */
	int extra_rows = (int)slines - 1;
	if (extra_rows && old_currow + extra_rows >= term_rows)
		currow -= extra_rows - (term_rows - old_currow);

	/* Restore cursor position */
	printf("\x1b[%d;%dH", currow, curcol);
}

static inline void
correct_offset(size_t *offset)
{
	if (wrong_cmd && !recover_from_wrong_cmd()
#ifndef _NO_HIGHLIGHT
	&& (rl_point == rl_end || !highlight))
#else
	&& rl_point == rl_end)
#endif
		(*offset)++;

#ifndef _NO_HIGHLIGHT
	/* The highlight function modifies the terminal's idea of the current
	 * cursor position: let's correct it */
	if (highlight && rl_point != rl_end) {
		printf("\x1b[%dD", rl_end - rl_point);
		fflush(stdout);
		(*offset)++;
	}
#endif
}

static inline size_t
calculate_suggestion_lines(int *baej, const size_t suggestion_len)
{
	size_t cuc = (size_t)curcol; /* Current cursor column position*/

	if (suggestion.type == BOOKMARK_SUG || suggestion.type == ALIAS_SUG
	|| suggestion.type == ELN_SUG || suggestion.type == JCMD_SUG
	|| suggestion.type == JCMD_SUG_NOACD || suggestion.type == BACKDIR_SUG) {
		/* 3 = 1 (one char forward) + 2 (" >") */
		cuc += suggestion.type == ELN_SUG ? 3 : 4;
		*baej = 1;
	}

	size_t cucs = cuc + suggestion_len;
	/* slines: amount of lines we need to print the suggestion, including
	 * the current line */
	size_t slines = 1;

	if (cucs > term_cols) {
		slines = cucs / (size_t)term_cols;
		fflush(stdout);
		int cucs_rem = (int)cucs % term_cols;
		if (cucs_rem > 0)
			slines++;
	}

	return slines;
}

static inline char *
truncate_name(const char *str)
{
	char *wname = (char *)NULL;

	if (suggestion.type == ELN_SUG || suggestion.type == COMP_SUG
	|| suggestion.type == FILE_SUG) {
		size_t wlen = wc_xstrlen(str);
		if (wlen == 0)
			wname = truncate_wname(str);
	}

	return wname;
}

static inline void
set_cursor_position(const int baej)
{
	/* If not at the end of the line, move the cursor there */
	if (rl_end > rl_point) {
		printf("\x1b[%dC", rl_end - rl_point);
		fflush(stdout);
	}

	/* rl_end and rl_point are not updated: they do not include
	 * the last typed char. However, since we only care here about
	 * the difference between them, it doesn't matter: the result
	 * is the same (7 - 4 == 6 - 3 == 1) */

	/* Erase everything after the current cursor position */
	if (write(STDOUT_FILENO, DLFC, DLFC_LEN) <= 0) {/* Avoid compiler warning */}

	if (baej == 1)
		/* Move the cursor %d columns to the right and print "> " */
		printf("\x1b[%dC%s> \x1b[0m", BAEJ_OFFSET, sp_c);
}

static inline int
check_conditions(const char *str, const size_t offset, const size_t str_len,
		int *baej, size_t *slines)
{
	if (offset > str_len)
		return EXIT_FAILURE;

	/* Do not print suggestions bigger than what the current terminal
	 * window size can hold */
	size_t suggestion_len = wc_xstrlen(str + offset);
	if ((int)suggestion_len > (term_cols * term_rows) - curcol)
		return EXIT_FAILURE;

	*slines = calculate_suggestion_lines(baej, suggestion_len);
	if (*slines > (size_t)term_rows)
		return EXIT_FAILURE;

	return EXIT_SUCCESS;
}

static inline void
_print_suggestion(const char *str, const size_t offset, const char *color)
{
	char *wname = truncate_name(str);
	printf("%s%s", color, (wname ? wname : str) + offset - (offset ? 1 : 0));
	fflush(stdout);
	free(wname);
}

/* Clear the line, print the suggestion (STR) at OFFSET in COLOR, and
 * move the cursor back to the original position.
 * OFFSET marks the point in STR that is already typed: the suggestion
 * will be printed starting from this point */
void
print_suggestion(const char *str, size_t offset, char *color)
{
	if (!str || !*str)
		return;

	if (suggestion.printed && str != suggestion_buf)
		clear_suggestion(CS_FREEBUF);

	correct_offset(&offset);

	/* Store current cursor position in CURROW and CURCOL (globals) */
	get_cursor_position(STDIN_FILENO, STDOUT_FILENO);

	int baej = 0; /* Bookmark/backdir, alias, ELN, or jump */
	size_t str_len = strlen(str), slines = 0;

	if (check_conditions(str, offset, str_len, &baej, &slines) == EXIT_FAILURE)
		return;

	/* In some cases (accepting first suggested word), we might want to
	 * reprint the suggestion buffer, in which case it is already stored */
	if (str != suggestion_buf)
		/* Store the suggestion (used later by rl_accept_suggestion (keybinds.c) */
		suggestion_buf = savestring(str, str_len);

	set_cursor_position(baej);
	_print_suggestion(str, offset, color);
	restore_cursor_position(slines);

	/* Store the amount of lines taken by the current command line
	 * (plus the suggestion's length) to be able to correctly
	 * remove it later (via the clear_suggestion function) */
	suggestion.nlines = slines;
	/* Store the suggestion color, in case we need to reprint it */
	suggestion.color = color;
}

static inline char *
get_reg_file_color(const char *filename, const struct stat *attr, int *free_color)
{
	if (light_mode)	return fi_c;
	if (access(filename, R_OK) == -1) return nf_c;
	if (attr->st_mode & S_ISUID) return su_c;
	if (attr->st_mode & S_ISGID) return sg_c;

#ifdef _LINUX_CAP
	cap_t cap = cap_get_file(filename);
	if (cap) {
		cap_free(cap);
		return ca_c;
	}
#endif
	if (attr->st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) {
		if (FILE_SIZE_PTR == 0)
			return ee_c;
		return ex_c;
	}

	if (FILE_SIZE_PTR == 0)	return ef_c;
	if (attr->st_nlink > 1)	return mh_c;

	char *ext = strrchr(filename, '.');
	if (ext && ext != filename) {
		char *extcolor = get_ext_color(ext);
		if (!extcolor)
			return fi_c;

		char *ext_color = (char *)xnmalloc(strlen(extcolor) + 4, sizeof(char));
		sprintf(ext_color, "\x1b[%sm", extcolor);
		*free_color = 1;
		return ext_color;
	}

	return fi_c;
}

/* Used by the check_completions function to get file names color
 * according to file type */
static char *
get_comp_color(const char *filename, const struct stat *attr, int *free_color)
{
	char *color = no_c; 

	switch(attr->st_mode & S_IFMT) {
	case S_IFDIR:
		if (light_mode) return di_c;
		if (access(filename, R_OK | X_OK) != 0)
			return nd_c;
		color = get_dir_color(filename, attr->st_mode);
		break;

	case S_IFREG:
		color = get_reg_file_color(filename, attr, free_color);
		break;

	case S_IFLNK: {
		if (light_mode)	return ln_c;
		char *linkname = realpath(filename, (char *)NULL);
		if (linkname)
			return ln_c;
		return or_c;
		}
		break;

	case S_IFSOCK: return so_c;
	case S_IFBLK: return bd_c;
	case S_IFCHR: return cd_c;
	case S_IFIFO: return pi_c;
	default: return no_c;
	}

	return color;
}

static inline int
skip_leading_dot_slash(char **str, size_t *len)
{
	int dot_slash = 0;

	if (*len >= 2 && *(*str) == '.' && *(*str + 1) == '/') {
		dot_slash = 1;
		(*str) += 2;
		(*len) -= 2;
	}

	return dot_slash;
}

static inline void
remove_trailing_slash(char **str, size_t *len)
{
	if (*len == 0)
		return;

	if ((*str)[*len - 1] == '/') {
		(*len)--;
		(*str)[*len] = '\0';
	}
}

static inline void
skip_leading_spaces(char **str, size_t *len)
{
	if (*len == 0)
		return;

	while ((*str)[*len - 1] == ' ') {
		(*len)--;
		(*str)[*len] = '\0';
	}
}

static inline void
skip_leading_backslashes(char **str, size_t *len)
{
	if (*len == 0)
		return;

	while (*(*str) == '\\') {
		++(*str);
		--(*len);
	}
}

static void
match_print(char *match, size_t len, char *color, int append_slash)
{
	char t[NAME_MAX + 2];
	*t = '\0';

	if (append_slash == 1)
		snprintf(t, NAME_MAX + 2, "%s/", match);

	char *tmp = escape_str(*t ? t : match);
	if (!tmp || !*tmp) {
		print_suggestion(match, len, color);
		return;
	}

	char *q;
	if (cur_comp_type == TCMP_PATH && *tmp == '\\'
	&& *(tmp + 1) == '~')
		q = tmp + 1;
	else
		q = tmp;

	print_suggestion(q, len, color);
	free(tmp);
}

static inline int
print_match(char *match, const size_t len, const unsigned char c)
{
	int append_slash = 0, free_color = 0;

	char *p = (char *)NULL, *_color = (char *)NULL;
	char *color = (suggest_filetype_color == 1) ? no_c : sf_c;

	if (*match == '~')
		p = tilde_expand(match);

	struct stat attr;
	if (lstat(p ? p : match, &attr) != -1) {
		if (S_ISDIR(attr.st_mode)) {
			append_slash = 1;
			suggestion.filetype = DT_DIR;
		}

		if (suggest_filetype_color) {
			_color = get_comp_color(p ? p : match, &attr, &free_color);
			if (_color)
				color = _color;
		}
	} else {
		suggestion.filetype = DT_DIR;
	}

	free(p);

	if (c != BS)
		suggestion.type = COMP_SUG;

	match_print(match, len, color, append_slash);

	if (free_color)
		free(color);

	return PARTIAL_MATCH;
}

static inline int
get_print_status(const char *str, const char *match, const size_t len)
{
	if (suggestion.printed && suggestion_buf)
		clear_suggestion(CS_FREEBUF);

	if (strlen(match) == len || str[len - 1] == '/')
		return FULL_MATCH;

	return PARTIAL_MATCH;
}

static inline void
free_matches(char ***matches)
{
	size_t i;
	for (i = 0; (*matches)[i]; i++)
		free((*matches)[i]);
	free(*matches);
}

static int
check_completions(char *str, size_t len, const unsigned char c, const int print)
{
	if (!str || !*str)
		return NO_MATCH;

	skip_leading_spaces(&str, &len);
	skip_leading_backslashes(&str, &len);

	char **_matches = rl_completion_matches(str, rl_completion_entry_function);
	if (!_matches)
		return NO_MATCH;

	int printed = NO_MATCH;
	suggestion.filetype = DT_REG;

	if (len == 0)
		goto FREE;

	/* If only one match */
	if (!_matches[1] || !*_matches[1]) {
		if (!print) {
			printed = get_print_status(str, _matches[0], len);
			goto FREE;
		}
		printed = print_match(_matches[0], len, c);
		goto FREE;
	}

	/* If multiple matches, suggest the first one */
	if (!print) {
		printed = get_print_status(str, _matches[1], len);
		goto FREE;
	}
	printed = print_match(_matches[1], len, c);

FREE:
	free_matches(&_matches);
	return printed;
}

static inline void
print_directory_suggestion(const int i, const size_t len, char *color)
{
	if (suggest_filetype_color == 1)
		color = file_info[i].color;

	suggestion.filetype = DT_DIR;

	char tmp[NAME_MAX + 2];
	snprintf(tmp, NAME_MAX + 2, "%s/", file_info[i].name);

	char *_tmp = escape_str(tmp);
	if (_tmp) {
		print_suggestion(_tmp, len, color);
		free(_tmp);
		return;
	}

	print_suggestion(tmp, len, color);
}

static inline void
print_reg_file_suggestion(char *str, const int i, size_t len,
                          char *color, const int dot_slash)
{
	if (suggest_filetype_color)
		color = file_info[i].color;

	suggestion.filetype = DT_REG;

	char *tmp = escape_str(file_info[i].name);
	if (tmp) {
		char *s = str;
		while(*s) {
			if (is_quote_char(*s))
				len++;
			s++;
		}

		if (dot_slash) {
			/* Reinsert './', removed to check file name*/
			char t[NAME_MAX + 2];
			snprintf(t, NAME_MAX + 1, "./%s", tmp);
			print_suggestion(t, len + 2, color);
		} else {
			print_suggestion(tmp, len, color);
		}

		free(tmp);
		return;
	}

	if (dot_slash) {
		char t[NAME_MAX + 2];
		snprintf(t, NAME_MAX + 1, "./%s", file_info[i].name);
		print_suggestion(t, len + 2, color);
		return;
	}

	print_suggestion(file_info[i].name, len, color);
}

static int
check_filenames(char *str, size_t len, const unsigned char c,
				const int first_word, const size_t full_word)
{
	int i = (int)files;
	char *color = (suggest_filetype_color == 1) ? no_c : sf_c;

	skip_leading_backslashes(&str, &len);
	int dot_slash = skip_leading_dot_slash(&str, &len);
	skip_leading_spaces(&str, &len);
	remove_trailing_slash(&str, &len);

	while (--i >= 0) {
		if (!file_info[i].name || TOUPPER(*str) != TOUPPER(*file_info[i].name))
			continue;

		if (full_word) {
			if ((case_sens_path_comp ? strcmp(str, file_info[i].name)
			: strcasecmp(str, file_info[i].name)) == 0)
				return FULL_MATCH;
			continue;
		}

		if (len > 0 && (case_sens_path_comp ? strncmp(str, file_info[i].name, len)
		: strncasecmp(str, file_info[i].name, len)) == 0) {
			if (file_info[i].len == len) return FULL_MATCH;
			if (first_word && !auto_open) continue;
			if (c != BS) suggestion.type = FILE_SUG;

			if (file_info[i].dir)
				print_directory_suggestion(i, len, color);
			else
				print_reg_file_suggestion(str, i, len, color, dot_slash);

			return PARTIAL_MATCH;
		}
	}

	return NO_MATCH;
}

static int
check_history(const char *str, const size_t len)
{
	if (!str || !*str || !len)
		return NO_MATCH;

	int i = (int)current_hist_n;
	while (--i >= 0) {
		if (!history[i] || TOUPPER(*str) != TOUPPER(*history[i]))
			continue;

		if ((case_sens_path_comp ? strncmp(str, history[i], len)
		: strncasecmp(str, history[i], len)) == 0) {
			if (strlen(history[i]) > len) {
				suggestion.type = HIST_SUG;
				print_suggestion(history[i], len, sh_c);
				return PARTIAL_MATCH;
			}
			return FULL_MATCH;
		}
	}

	return NO_MATCH;
}

static int
check_builtins(const char *str, const size_t len, const int print)
{
	char **b = (char **)NULL;

	switch(shell) {
	case SHELL_NONE: return NO_MATCH;
	case SHELL_BASH: b = bash_builtins; break;
	case SHELL_DASH: b = dash_builtins; break;
	case SHELL_KSH: b = ksh_builtins; break;
	case SHELL_TCSH: b = tcsh_builtins; break;
	case SHELL_ZSH: b = zsh_builtins; break;
	default: return NO_MATCH;
	}

	size_t i = 0;
	for(; b[i]; i++) {
		if (*str != *b[i])
			continue;

		if (!print) {
			if (strcmp(str, b[i]) == 0)
				return FULL_MATCH;
			continue;
		}

		if (strncmp(b[i], str, len) != 0)
			continue;

		size_t blen = strlen(b[i]);
		if (blen > len) {
			suggestion.type = CMD_SUG;
			print_suggestion(b[i], len, sb_c);
			return PARTIAL_MATCH;
		}
		return FULL_MATCH;
	}

	return NO_MATCH;
}

static inline int
print_cmd_suggestion(int i, size_t len)
{
	if (is_internal_c(bin_commands[i])) {
		if (strlen(bin_commands[i]) > len) {
			suggestion.type = CMD_SUG;
			print_suggestion(bin_commands[i], len, sx_c);
			return PARTIAL_MATCH;
		}
		return FULL_MATCH;
	}

	if (ext_cmd_ok) {
		if (strlen(bin_commands[i]) > len) {
			suggestion.type = CMD_SUG;
			print_suggestion(bin_commands[i], len, sc_c);
			return PARTIAL_MATCH;
		}
		return FULL_MATCH;
	}

	return (-1);
}

static inline int
print_internal_cmd_suggestion(char *str, size_t len, const int print)
{
	/* Check internal command with fused parameter */
	char *p = (char *)NULL;
	size_t j;
	for (j = 0; str[j]; j++) {
		if (str[j] >= '1' && str[j] <= '9') {
			p = str + j;
			break;
		}
	}

	if (!p || p == str)
		return check_builtins(str, len, print);
	
	*p = '\0';
	if (!is_internal_c(str))
		return NO_MATCH;

	return FULL_MATCH;
}

int
check_cmds(char *str, const size_t len, const int print)
{
	if (len == 0)
		return NO_MATCH;

	int i = (int)path_progsn;
	while (--i >= 0) {
		if (!bin_commands[i] || *str != *bin_commands[i])
			continue;

		if (!print) {
			if (strcmp(str, bin_commands[i]) == 0)
				return FULL_MATCH;
			continue;
		}

		if (strncmp(str, bin_commands[i], len) != 0)
			continue;

		int ret = print_cmd_suggestion(i, len);
		if (ret == -1)
			continue;
		return ret;
	}

	return print_internal_cmd_suggestion(str, len, print);
}

static int
check_jumpdb(const char *str, const size_t len, const int print)
{
	char *color = (suggest_filetype_color == 1) ? di_c : sf_c;

	int i = (int)jump_n;
	while (--i >= 0) {
		if (!jump_db[i].path || TOUPPER(*str) != TOUPPER(*jump_db[i].path))
			continue;

		if (!print) {
			if ((case_sens_path_comp ? strcmp(str, jump_db[i].path)
			: strcasecmp(str, jump_db[i].path)) == 0)
				return FULL_MATCH;
			continue;
		}

		if (len && (case_sens_path_comp ? strncmp(str, jump_db[i].path, len)
		: strncasecmp(str, jump_db[i].path, len)) == 0) {
			size_t db_len = strlen(jump_db[i].path);
			if (db_len <= len)
				return FULL_MATCH;

			suggestion.type = FILE_SUG;
			suggestion.filetype = DT_DIR;
			char tmp[PATH_MAX + 2];
			*tmp = '\0';

			if (jump_db[i].path[db_len - 1] != '/')
				snprintf(tmp, PATH_MAX + 2, "%s/", jump_db[i].path);

			print_suggestion(*tmp ? tmp : jump_db[i].path, len, color);
			return PARTIAL_MATCH;
		}
	}

	return NO_MATCH;
}

static inline void
print_bookmark_dir_suggestion(const int i)
{
	suggestion.type = BOOKMARK_SUG;
	suggestion.filetype = DT_DIR;

	char tmp[PATH_MAX + 2];
	size_t path_len = strlen(bookmarks[i].path);
	if (bookmarks[i].path[path_len - 1] != '/')
		snprintf(tmp, PATH_MAX + 2, "%s/", bookmarks[i].path);
	else
		xstrsncpy(tmp, bookmarks[i].path, PATH_MAX + 2);

	char *color = suggest_filetype_color == 1 ? di_c : sf_c;

	char *_tmp = escape_str(tmp);
	print_suggestion(_tmp ? _tmp : tmp, 1, color);
	free(_tmp);
}

static inline void
print_bookmark_file_suggestion(const int i, struct stat *attr)
{
	suggestion.type = BOOKMARK_SUG;
	suggestion.filetype = DT_REG;

	int free_color = 0;
	char *color = (!suggest_filetype_color) ? sf_c : (char *)NULL;

	if (suggest_filetype_color)
		color = get_comp_color(bookmarks[i].path, attr, &free_color);

	char *_tmp = escape_str(bookmarks[i].path);
	print_suggestion(_tmp ? _tmp : bookmarks[i].path, 1, color);
	free(_tmp);

	if (free_color == 1)
		free(color);
}

static int
check_bookmarks(const char *str, const size_t len, const int print)
{
	if (bm_n == 0)
		return NO_MATCH;

	int i = (int)bm_n;
	while (--i >= 0) {
		if (!bookmarks[i].name || TOUPPER(*str) != TOUPPER(*bookmarks[i].name))
			continue;

		if (!print) {
			if ((case_sens_path_comp ? strcmp(str, bookmarks[i].name)
			: strcasecmp(str, bookmarks[i].name)) == 0)
				return FULL_MATCH;
			continue;
		}

		if (len && (case_sens_path_comp ? strncmp(str, bookmarks[i].name, len)
		: strncasecmp(str, bookmarks[i].name, len)) == 0) {
			struct stat attr;
			if (lstat(bookmarks[i].path, &attr) == -1)
				continue;
			else if ((attr.st_mode & S_IFMT) == S_IFDIR)
				print_bookmark_dir_suggestion(i);
			else
				print_bookmark_file_suggestion(i, &attr);

			return PARTIAL_MATCH;
		}
	}

	return NO_MATCH;
}

static int
check_int_params(const char *str, const size_t len)
{
	size_t i;
	for (i = 0; param_str[i]; i++) {
		if (*str != *param_str[i])
			continue;
		if (len && strncmp(str, param_str[i], len) == 0
		&& strlen(param_str[i]) > len) {
			suggestion.type = INT_CMD;
			print_suggestion(param_str[i], len, sx_c);
			return PARTIAL_MATCH;
		}
	}

	return NO_MATCH;
}

static int
check_eln(const char *str, const int print)
{
	if (!str || !*str)
		return NO_MATCH;

	int n = atoi(str);
	if (n < 1 || n > (int)files || !file_info[n - 1].name)
		return NO_MATCH;

	if (!print)
		return FULL_MATCH;

	n--;
	char *color = sf_c;
	suggestion.type = ELN_SUG;
	if (suggest_filetype_color)
		color = file_info[n].color;

	char tmp[NAME_MAX + 1];
	*tmp = '\0';
	if (file_info[n].dir) {
		snprintf(tmp, NAME_MAX + 1, "%s/", file_info[n].name);
		suggestion.filetype = DT_DIR;
	} else {
		suggestion.filetype = DT_REG;
	}

	print_suggestion(!*tmp ? file_info[n].name : tmp, 0, color);

	return PARTIAL_MATCH;
}

static int
check_aliases(const char *str, const size_t len, const int print)
{
	if (!aliases_n)
		return NO_MATCH;

	char *color = sc_c;

	int i = (int)aliases_n;
	while (--i >= 0) {
		if (!aliases[i].name)
			continue;
		char *p = aliases[i].name;
		if (TOUPPER(*p) != TOUPPER(*str))
			continue;

		if (!print) {
			if ((case_sens_path_comp ? strcmp(p, str)
			: strcasecmp(p, str)) == 0)
				return FULL_MATCH;
			continue;
		}

		if ((case_sens_path_comp ? strncmp(p, str, len)
		: strncasecmp(p, str, len)) != 0)
			continue;
		if (!aliases[i].cmd || !*aliases[i].cmd)
			continue;

		suggestion.type = ALIAS_SUG;
		print_suggestion(aliases[i].cmd, 1, color);
		return PARTIAL_MATCH;
	}

	return NO_MATCH;
}

/* Get a match from the jump database and print the suggestion */
static int
check_jcmd(char *line)
{
	if (suggestion_buf)
		clear_suggestion(CS_FREEBUF);

	/* Split line into an array of substrings */
	char **substr = get_substr(line, ' ');
	if (!substr)
		return NO_MATCH;

	/* Check the jump database for a match. If a match is found, it will
	 * be stored in jump_suggestion (global) */
	dirjump(substr, SUG_JUMP);

	size_t i;
	for (i = 0; substr[i]; i++)
		free(substr[i]);
	free(substr);

	if (!jump_suggestion)
		return NO_MATCH;

	suggestion.type = JCMD_SUG;
	suggestion.filetype = DT_DIR;
	if (!autocd) {
		char *tmp = xnmalloc(strlen(jump_suggestion) + 4, sizeof(char));
		sprintf(tmp, "cd %s", jump_suggestion);
		print_suggestion(tmp, 1, suggest_filetype_color ? di_c : sf_c);
		suggestion.type = JCMD_SUG_NOACD;
		free(tmp);
	} else {
		print_suggestion(jump_suggestion, 1,
			suggest_filetype_color ? di_c : sf_c);
	}

	free(jump_suggestion);
	jump_suggestion = (char *)NULL;
	return PARTIAL_MATCH;
}

/* Check if we must suggest --help for internal commands */
static int
check_help(char *full_line, const char *_last_word)
{
	size_t len = strlen(_last_word);
	if (strncmp(_last_word, "--help", len) != 0)
		return NO_MATCH;

	char *ret = strchr(full_line, ' ');
	if (!ret)
		return NO_MATCH;

	*ret = '\0';
	int retval = is_internal_c(full_line);
	*ret = ' ';

	if (!retval)
		return NO_MATCH;

	suggestion.type = CMD_SUG;
	print_suggestion("--help", len, sx_c);
	return PARTIAL_MATCH;
}

static int
check_variables(const char *str, const size_t len)
{
	size_t i;

	for (i = 0; environ[i]; i++) {
		if (TOUPPER(*environ[i]) != TOUPPER(*str)
		|| strncasecmp(str, environ[i], len) != 0)
			continue;

		char *ret = strchr(environ[i], '=');
		*ret = '\0';
		suggestion.type = VAR_SUG;
		char t[NAME_MAX + 1];
		snprintf(t, NAME_MAX + 1, "$%s", environ[i]);
		print_suggestion(t, len + 1, sh_c);
		*ret = '=';
		return PARTIAL_MATCH;
	}

	if (usrvar_n == 0)
		return NO_MATCH;

	for (i = 0; usr_var[i].name; i++) {
		if (TOUPPER(*str) != TOUPPER(*usr_var[i].name)
		|| strncasecmp(str, usr_var[i].name, len) != 0)
			continue;

		suggestion.type = CMD_SUG;
		char t[NAME_MAX + 1];
		snprintf(t, NAME_MAX + 1, "$%s", usr_var[i].name);
		print_suggestion(t, len + 1, sh_c);
		return PARTIAL_MATCH;
	}

	return NO_MATCH;
}

static char*
get_last_word(const char *last_space, size_t buflen)
{
	if (last_space) {
		char *rl = rl_line_buffer;
		int j = rl_end;
		while (--j >= 0) {
			if (j + 1 && rl[j] == ' ' && rl[j + 1] && rl[j + 1] != ' ')
				break;
		}
		last_word_offset = j + 1;
		buflen = strlen(last_space);

		if (*(++last_space)) {
			buflen--;
			last_word = (char *)xrealloc(last_word, (buflen + 2) * sizeof(char));
			strcpy(last_word, last_space);
		} else {
			last_word = (char *)xrealloc(last_word, 2 * sizeof(char));
			*last_word = '\0';
		}
	} else {
		last_word = (char *)xrealloc(last_word, (buflen + 2) * sizeof(char));
		strcpy(last_word, rl_line_buffer);
	}

	return last_word;
}

static int
is_last_word(void)
{
	int lw = 1;

	if (rl_point >= rl_end)
		return lw;

	char *p = strchr(rl_line_buffer + rl_point, ' ');
	if (!p)
		return lw;
	
	while (*(++p)) {
		if (*p != ' ') {
			lw = 0;
			break;
		}
	}

	return lw;
}

static size_t
count_words(size_t *start_word, size_t *full_word)
{
	rl_last_word_start = 0;
	size_t words = 0, w = 0, first_non_space = 0;
	char q = 0;
	char *b = rl_line_buffer;
	for (; b[w]; w++) {
		/* Keep track of open quotes */
		if (b[w] == '\'' || b[w] == '"')
			q = q == b[w] ? 0 : b[w];

		if (!first_non_space && b[w] != ' ') {
			words = 1;
			*start_word = w;
			first_non_space = 1;
			continue;
		}
		if (w && b[w] == ' ' && b[w - 1] != '\\') {
			if (b[w + 1] && b[w + 1] != ' ')
				rl_last_word_start = (int)w + (b + 1 ? 1 : 0);
			if (!*full_word && b[w - 1] != '|'
			&& b[w - 1] != ';' && b[w - 1] != '&')
				*full_word = w; /* Index of the end of the first full word (cmd) */
			if (b[w + 1] && b[w + 1] != ' ') {
				words++;
			}
		}
		/* If a process separator char is found, reset variables so that we
		 * can start counting again for the new command */
		if (!q && cur_color != hq_c && w && b[w - 1] != '\\'
		&& ((b[w] == '&' && b[w - 1] == '&') || b[w] == '|' || b[w] == ';')) {
			words = first_non_space = *full_word = 0;
		}
	}

	return words;
}

static void
print_warning_prompt(const char c)
{
	if (warning_prompt == 1 && !wrong_cmd
	&& c != ';' && c != ':' && c != '#'
	&& c != '$' && c != '\'' && c != '"') {
		if (suggestion.printed)
			clear_suggestion(CS_FREEBUF);
		wrong_cmd = 1;

		rl_save_prompt();

		char *decoded_prompt = decode_prompt(wprompt_str);
		/* Print the warning prompt */
		rl_set_prompt(decoded_prompt);
		free(decoded_prompt);
	}
}

/* Check for available suggestions. Returns zero if true, one if not,
 * and -1 if C was inserted before the end of the current line.
 * If a suggestion is found, it will be printed by print_suggestion() */
int
rl_suggestions(const unsigned char c)
{
	int printed = 0, zero_offset = 0;
	last_word_offset = 0;

	if (rl_end == 0 && rl_point == 0) {
		free(suggestion_buf);
		suggestion_buf = (char *)NULL;
		if (wrong_cmd)
			recover_from_wrong_cmd();
		return EXIT_SUCCESS;
	}

	size_t buflen = (size_t)rl_end;
	suggestion.full_line_len = buflen + 1;
	char *last_space = strrchr(rl_line_buffer, ' ');
	if (last_space && last_space != rl_line_buffer
	&& *(last_space - 1) == '\\')
		last_space = (char *)NULL;

	/* Reset the wrong cmd flag whenever we have a new word or a new line */
	if (rl_end == 0 || c == '\n') {
		if (wrong_cmd)
			recover_from_wrong_cmd();
		wrong_cmd_line = 0;
	}

	/* We need a copy of the complete line */
	char *full_line = rl_line_buffer;

	/* A copy of the last entered word */
	last_word = get_last_word(last_space, buflen);

	/* Count words */
	size_t full_word = 0, start_word = 0;
	nwords = count_words(&start_word, &full_word);

	/* And a copy of the first word as well */
	char *first_word = (char *)NULL;
	if (full_word) {
		rl_line_buffer[full_word] = '\0';
		char *q = rl_line_buffer + start_word;
		first_word = savestring(q, strlen(q));
		rl_line_buffer[full_word] = ' ';
	}

	char *word = (nwords == 1 && c != ' ' && first_word) ? first_word : last_word;
	size_t wlen = strlen(word);

	/* If more than one word and the cursor is on the first word,
	 * jump to the check command name section */
	int point_is_first_word = 0;
	if (nwords >= 2 && rl_point <= (int)full_word + 1) {
		point_is_first_word = 1;
		goto CHECK_CMD;
	}

	/* If not on the first word and not at the end of the last word,
	 * do nothing */
	int lw = is_last_word();
	if (!lw)
		goto SUCCESS;

		/* ######################################
		 * #	    Search for suggestions		#
		 * ######################################*/

	char *lb = rl_line_buffer;
	/* 3.a) Let's suggest non-fixed parameters for internal commands */

	switch(*lb) {
	case 'b': /* Bookmarks names */
		if (lb[1] == 'm' && lb[2] == ' ' && strncmp(lb + 3, "add", 3) != 0) {
			size_t i;
			for (i = 0; bookmark_names[i]; i++) {
				if (*word == *bookmark_names[i]
				&& strncmp(bookmark_names[i], word, wlen) == 0) {
					suggestion.type = CMD_SUG;
					print_suggestion(bookmark_names[i], wlen, sx_c);
					printed = 1;
					break;
				}
			}
			if (printed) {
				goto SUCCESS;
			}
		}
		/* Backdir function (bd) */
		else {
			if (lb[1] == 'd' && lb[2] == ' ' && lb[3]) {
				if (*(lb + 3) == '/' && !*(lb + 4)) {
					/* The query string is a single slash: do nothing */
					if (suggestion.printed)
						clear_suggestion(CS_FREEBUF);
					goto FAIL;
				}
				/* Remove the last component of the current path name (CWD):
				 * we want to match only PARENT directories */
				char bk_cwd[PATH_MAX + 1];
				xstrsncpy(bk_cwd, workspaces[cur_ws].path, PATH_MAX);
				char *q = strrchr(bk_cwd, '/');
				if (q)
					*q = '\0';
				/* Find the query string in the list of parent directories */
				char *p = strstr(bk_cwd, lb + 3);
				if (p) {
					char *pp = strchr(p, '/');
					if (pp)
						*pp = '\0';
					suggestion.type = BACKDIR_SUG;
					print_suggestion(bk_cwd, 1, sf_c);
					printed = 1;
					goto SUCCESS;
				}
			}
		}
		break;

	case 'c': /* Color schemes */
		if (lb[1] == 's' && lb[2] == ' ') {
			size_t i;
			for (i = 0; color_schemes[i]; i++) {
				if (*last_word == *color_schemes[i]
				&& strncmp(color_schemes[i], word, wlen) == 0) {
					suggestion.type = CMD_SUG;
					print_suggestion(color_schemes[i], wlen, sx_c);
					printed = 1;
					break;
				}
			}
			if (printed) {
				goto SUCCESS;
			}
		}
		break;

	case 'j': /* j command */
		if (lb[1] == ' ' && lb[2] == '-' && (lb[3] == 'h' || strncmp(lb + 2, "--help", strlen(lb + 2)) == 0))
			break;
		if (lb[1] == ' '  || ((lb[1] == 'c'	|| lb[1] == 'o'
		|| lb[1] == 'p') && lb[2] == ' ')) {
			printed = check_jcmd(full_line);
			if (printed) {
				zero_offset = 1;
				goto SUCCESS;
			} else {
				goto FAIL;
			}
		}
		break;

	case 'n': /* Remotes */
		if (lb[1] == 'e' && lb[2] == 't' && lb[3] == ' ') {
			size_t i;
			for (i = 0; remotes[i].name; i++) {
				if (*word == *remotes[i].name
				&& strncmp(remotes[i].name, word, wlen) == 0) {
					suggestion.type = CMD_SUG;
					print_suggestion(remotes[i].name, wlen, sx_c);
					printed = 1;
					break;
				}
			}
			if (printed)
				goto SUCCESS;
		}
		break;

	case 'p': /* Profiles */
		if (lb[1] == 'f' && lb[2] == ' ' && (strncmp(lb + 3, "set", 3) == 0
		|| strncmp(lb + 3, "del", 3) == 0)) {
			size_t i;
			for (i = 0; profile_names[i]; i++) {
				if (*word == *profile_names[i]
				&& strncmp(profile_names[i], word, wlen) == 0) {
					suggestion.type = CMD_SUG;
					print_suggestion(profile_names[i], wlen, sx_c);
					printed = 1;
					break;
				}
			}
			if (printed) {
				goto SUCCESS;
			} else {
				goto FAIL;
			}
		}
		break;

	default: break;
	}

	/* 3.b) Check already suggested string */
	if (suggestion_buf && suggestion.printed && !_ISDIGIT(c)
	&& strncmp(full_line, suggestion_buf, (size_t)rl_end) == 0) {
		printed = zero_offset = 1;
		goto SUCCESS;
	}

	/* 3.c) Check CliFM internal parameters */
	char *ret = strchr(full_line, ' ');
	if (ret) {
		/* 3.c.1) Suggest the sel keyword only if not first word */
		if (sel_n > 0 && *word == 's' && strncmp(word, "sel", wlen) == 0) {
			suggestion.type = SEL_SUG;
			printed = 1;
			print_suggestion("sel", wlen, sx_c);
			goto SUCCESS;
		}

		/* 3.c.2) Check commands fixed parameters */
		printed = check_int_params(full_line, (size_t)rl_end);
		if (printed) {
			zero_offset = 1;
			goto SUCCESS;
		}
	}

	/* 3.c.3) Let's suggest --help for internal commands */
	if (*word == '-') {
		printed = check_help(full_line, word);
		if (printed)
			goto SUCCESS;
	}

	/* 3.d) Execute the following check in the order specified by
	 * suggestion_strategy (the value is taken form the configuration
	 * file) */
	size_t st;
	int flag = 0;

	for (st = 0; st < SUG_STRATS; st++) {
		switch(suggestion_strategy[st]) {

		case 'a': /* 3.d.1) Aliases */
			flag = c == ' ' ? CHECK_MATCH : PRINT_MATCH;
			if (flag == CHECK_MATCH && suggestion.printed)
				clear_suggestion(CS_FREEBUF);

			printed = check_aliases(word, wlen, flag);
			if (printed)
				goto SUCCESS;
			break;

		case 'b': /* 3.d.2) Bookmarks */
			if (last_space || autocd || auto_open) {
				flag = c == ' ' ? CHECK_MATCH : PRINT_MATCH;
				if (flag == CHECK_MATCH && suggestion.printed)
					clear_suggestion(CS_FREEBUF);

				printed = check_bookmarks(word, wlen, flag);
				if (printed)
					goto SUCCESS;
			}
			break;

		case 'c': /* 3.d.3) Possible completions */
			if (last_space || autocd || auto_open) {
				if (nwords == 1) {
					word = first_word ? first_word : last_word;
					wlen = strlen(word);
				}
				if (wlen && word[wlen - 1] == ' ')
					word[wlen - 1] = '\0';

				flag = c == ' ' ? CHECK_MATCH : PRINT_MATCH;

				char *d = word;
				if (wlen > FILE_URI_PREFIX_LEN && IS_FILE_URI(word)) {
					d += FILE_URI_PREFIX_LEN;
					wlen -= FILE_URI_PREFIX_LEN;
					last_word_offset += FILE_URI_PREFIX_LEN;
				}
				printed = check_completions(d, wlen, c, flag);

				if (printed) {
					if (flag == CHECK_MATCH) {
						if (printed == FULL_MATCH)
							goto SUCCESS;
					} else {
						goto SUCCESS;
					}
				}
			}
			break;

		case 'e': /* 3.d.4) ELN's */
			if (nwords == 1 && first_word) {
				word = first_word;
				wlen = strlen(word);
			}

			if (wlen == 0)
				break;

			int nlen = (int)wlen;
			while (word[nlen - 1] == ' ') {
				nlen--;
				word[nlen] = '\0';
			}

			/* If ELN&, remove ending '&' to check the ELN */
			if (word[nlen - 1] == '&') {
				nlen--;
				word[nlen] = '\0';
			}

			flag = c == ' ' ? CHECK_MATCH : PRINT_MATCH;

			if (flag == CHECK_MATCH && suggestion.printed)
				clear_suggestion(CS_FREEBUF);

			if (*word >= '1' && *word <= '9' && is_number(word)) {
				printed = check_eln(word, flag);

				if (printed)
					goto SUCCESS;
			}
			break;

		case 'f': /* 3.d.5) File names in CWD */
			/* Do not check dirs and filenames if first word and
			 * neither autocd nor auto-open are enabled */
			if (last_space || autocd || auto_open) {
				if (nwords == 1) {
					word = (first_word && *first_word) ? first_word : last_word;
					wlen = strlen(word);
				}
				if (wlen && word[wlen - 1] == ' ')
					word[wlen - 1] = '\0';

				if (c == ' ' && suggestion.printed)
					clear_suggestion(CS_FREEBUF);

				printed = check_filenames(word, wlen,
							c, last_space ? 0 : 1, full_word);

				if (printed)
					goto SUCCESS;
			}
			break;

		case 'h': /* 3.d.6) Commands history */
			printed = check_history(full_line, (size_t)rl_end);
			if (printed) {
				zero_offset = 1;
				goto SUCCESS;
			}
			break;

		case 'j': /* 3.d.7) Jump database */
			/* We don't care about auto-open here: the jump function
			 * deals with directories only */
			if (last_space || autocd) {
				if (nwords == 1) {
					word = (first_word && *first_word) ? first_word : last_word;
					wlen = strlen(word);
				}
				if (wlen && word[wlen - 1] == ' ')
					word[wlen - 1] = '\0';

				flag = (c == ' ' || full_word) ? CHECK_MATCH : PRINT_MATCH;

				if (flag == CHECK_MATCH && suggestion.printed)
					clear_suggestion(CS_FREEBUF);

				printed = check_jumpdb(word, wlen, flag);

				if (printed)
					goto SUCCESS;
			}
			break;

		case '-': break; /* Ignore check */

		default: break;
		}
	}

	/* 3.e) Variable names, both environment and internal */
	if (*word == '$') {
		printed = check_variables(word + 1, wlen - 1);
		if (printed)
			goto SUCCESS;
	}

	/* 3.f) Check commands in PATH and CliFM internals commands, but
	 * only for the first word */

	if (nwords >= 2)
		goto NO_SUGGESTION;

CHECK_CMD:
	word = first_word ? first_word : last_word;
	if (!word || !*word || (c == ' ' && (*word == '\''
	|| *word == '"' || *word == '$' || *word == '#')) || *word == '<'
	|| *word == '>' || *word == '!' || *word == '{' || *word == '['
	|| *word == '(' || strchr(word, '=') || *rl_line_buffer == ' '
	|| *word == '|' || *word == ';' || *word == '&') {
		if (suggestion.printed && suggestion_buf)
			clear_suggestion(CS_FREEBUF);
		goto SUCCESS;
	}

	/* If absolute path */
	if (point_is_first_word && *word == '/' && access(word, X_OK) == 0) {
		printed = 1;
	} else {
		wlen = strlen(word);

		if (wlen && word[wlen - 1] == ' ')
			word[wlen - 1] = '\0';

		flag = (c == ' ' || full_word) ? CHECK_MATCH : PRINT_MATCH;
		printed = check_cmds(word, wlen, flag);
	}

	if (printed) {
		if (wrong_cmd && (nwords == 1 || point_is_first_word)) {
			rl_dispatching = 1;
			recover_from_wrong_cmd();
			rl_dispatching = 0;
		}
		goto SUCCESS;

	/* Let's suppose that two slashes do not constitue a search
	 * expression */
	} else {
	/* There's no suggestion nor any command name matching the
	 * first entered word. So, we assume we have an invalid
	 * command name. Switch to the warning prompt to warn the
	 * user */
		if (*word != '/' || strchr(word + 1, '/'))
			print_warning_prompt(*word);
	}

NO_SUGGESTION:
	/* No suggestion found */

	/* Clear current suggestion, if any, only if no escape char is contained
	 * in the current input sequence. This is mainly to avoid erasing
	 * the suggestion if moving thought the text via the arrow keys */
	if (suggestion.printed) {
		if (!strchr(word, '\x1b')) {
			clear_suggestion(CS_FREEBUF);
			goto FAIL;
		} else {
			/* Go to SUCCESS to avoid removing the suggestion buffer */
			printed = 1;
			goto SUCCESS;
		}
	}

SUCCESS:
	if (printed) {
		suggestion.offset = zero_offset ? 0 : last_word_offset;

		if (printed == FULL_MATCH && suggestion_buf) {
			clear_suggestion(CS_FREEBUF);
		}

		if (wrong_cmd && nwords == 1) {
			rl_dispatching = 1;
			recover_from_wrong_cmd();
			rl_dispatching = 0;
		}

		fputs("\x1b[0m", stdout);
		suggestion.printed = 1;
		/* Restore color */
		if (!wrong_cmd) {
			fputs(cur_color ? cur_color : tx_c, stdout);
		} else {
			fputs(wp_c, stdout);
		}
	} else {
		if (wrong_cmd) {
			fputs("\x1b[0m", stdout);
			fputs(wp_c, stdout);
		}
		suggestion.printed = 0;
	}
	free(first_word);
	free(last_word);
	last_word = (char *)NULL;
	return EXIT_SUCCESS;

FAIL:
	suggestion.printed = 0;
	free(first_word);
	free(last_word);
	last_word = (char *)NULL;
	free(suggestion_buf);
	suggestion_buf = (char *)NULL;
	return EXIT_FAILURE;
}
#else
void *__skip_me_suggestions;
#endif /* _NO_SUGGESTIONS */
