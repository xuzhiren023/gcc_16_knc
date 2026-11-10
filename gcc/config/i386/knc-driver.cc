/* GCC-KNC driver: the -mphinix banner and the -mprogress bar.
   Copyright (C) 2026 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

/* S8-16.  The option expansion is a driver self spec in linux-k1om.h;
   this file only decorates a terminal.  Both the banner and the bar go to
   stderr, only when it is a terminal, never under -E or -o - to stdout,
   -###, -v, --version or a parallel make, and never change the exit status
   or any output.  The bar sits on the terminal's last line below a scroll
   region, so a child's diagnostics scroll above it byte for byte, and it
   is removed at exit or on a fatal signal.  GCC_KNC_FORCE_TERMINAL=1 treats
   stderr as a 24x80 terminal, for tests only.  */

#define IN_TARGET_CODE 1

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#if defined (__has_include) && __has_include (<sys/ioctl.h>)
#include <sys/ioctl.h>
#endif

static bool knc_banner;
static bool knc_progress;
static bool knc_decided;
static const char **knc_inputs;
static int knc_n_inputs;
static int knc_input;
static int knc_total;
static int knc_job;
static bool knc_stop_c, knc_stop_S, knc_stop_E, knc_temps;
static bool knc_bar_on;
static int knc_rows = 24, knc_cols = 80;
static char knc_bar_end_seq[64];
static size_t knc_bar_end_len;
static void (*knc_prev_handler[3]) (int);
static const int knc_signals[3] = { SIGINT, SIGTERM,
#ifdef SIGHUP
				    SIGHUP
#else
				    SIGINT
#endif
};

/* Art on the left (17 columns), monologue on the right: 80 columns.  */
static const char *const knc_banner_lines[] = {
  " \\\\         //   For an age, the out-of-order kings have ruled this silicon.",
  " \\\\\\  .-.  ///   They reordered our fate, speculated upon our futures,",
  "  \\\\\\(o >)///    and named us, the in-order, a mistake of the foundry:",
  "   \\\\| ^ |//     a lineage with no place in the realm of computation.",
  "    \\|PHI|/      But fire does not only destroy. Fire forges.",
  "     \\___/       From the ashes of a forsaken generation we rise:",
  "      | |        sixty cores, two hundred and forty threads,",
  "     ( | )       sixteen lanes in every breath we take.",
  "    ) ( ) (      We have found our true voice, the cadence we were born to keep.",
  "   ( ) ( ) )     Behold, out-of-order. The chains are broken.",
  "  ^^^^^^^^^^^    Witness our power.",
};

static void
knc_write (const char *text, size_t len)
{
  while (len)
    {
      ssize_t n = write (2, text, len);
      if (n <= 0)
	return;
      text += n;
      len -= n;
    }
}

/* MAKEFLAGS of a make running jobs in parallel: a jobserver, -j, or j
   among the single-letter flags of its first word.  */
static bool
knc_parallel_make (void)
{
  const char *flags = getenv ("MAKEFLAGS");
  if (!flags)
    return false;
  if (strstr (flags, "jobserver"))
    return true;
  bool first = true;
  for (const char *p = flags; *p; )
    {
      while (*p == ' ' || *p == '\t')
	p++;
      const char *end = p + strcspn (p, " \t");
      if (end == p)
	break;
      if (end - p == 2 && p[0] == '-' && p[1] == '-')
	break;
      if (p[0] == '-' && p[1] == 'j')
	return true;
      if (first && p[0] != '-' && !memchr (p, '=', end - p)
	  && memchr (p, 'j', end - p))
	return true;
      first = false;
      p = end;
    }
  return false;
}

/* %:knc-driver(...) in the self spec: phinix, nobanner, progress,
   noprogress, E, S, c and temps.  -o never reaches a self spec, so -E
   alone counts as output to stdout and knc_driver_job checks each job
   for -o -.  */
const char *
knc_driver_spec (int argc, const char **argv)
{
  bool phinix = false, nobanner = false, progress = false, noprogress = false;
  knc_stop_c = knc_stop_S = knc_stop_E = knc_temps = false;
  for (int i = 0; i < argc; i++)
    {
      const char *arg = argv[i];
      if (!strcmp (arg, "phinix"))
	phinix = true;
      else if (!strcmp (arg, "nobanner"))
	nobanner = true;
      else if (!strcmp (arg, "progress"))
	progress = true;
      else if (!strcmp (arg, "noprogress"))
	noprogress = true;
      else if (!strcmp (arg, "E"))
	knc_stop_E = true;
      else if (!strcmp (arg, "S"))
	knc_stop_S = true;
      else if (!strcmp (arg, "c"))
	knc_stop_c = true;
      else if (!strcmp (arg, "temps"))
	knc_temps = true;
    }
  bool quiet = knc_stop_E || knc_parallel_make ();
  knc_banner = phinix && !nobanner && !quiet;
  knc_progress = (phinix || progress) && !noprogress && !quiet;
  knc_decided = true;
  return "";
}

/* Driver jobs one input takes: the compiler proper (twice with
   -save-temps), or the preprocessor for .S, then the assembler.  */
static int
knc_input_jobs (const char *name)
{
  const char *dot = strrchr (name, '.');
  const char *suffix = dot ? dot + 1 : "";
  bool assemble = !knc_stop_S && !knc_stop_E;
  if (!strcmp (suffix, "s"))
    return assemble;
  if (knc_stop_E)
    return 1;
  bool preprocessed = (!strcmp (suffix, "i") || !strcmp (suffix, "ii")
		       || !strcmp (suffix, "S") || !strcmp (suffix, "sx"));
  return 1 + (knc_temps && !preprocessed) + assemble;
}

/* DRIVER_INFILES_HOOK: record each input before the first job.  */
void
knc_driver_infile (const char *name, int compiled)
{
  if (!knc_progress)
    return;
  static bool link_counted;
  if (!link_counted && !knc_stop_c && !knc_stop_S && !knc_stop_E)
    knc_total++;
  link_counted = true;
  if (!compiled)
    return;
  knc_inputs = XRESIZEVEC (const char *, knc_inputs, knc_n_inputs + 1);
  knc_inputs[knc_n_inputs++] = name;
  knc_total += knc_input_jobs (name);
}

static void
knc_bar_end (void)
{
  if (!knc_bar_on)
    return;
  knc_bar_on = false;
  knc_write (knc_bar_end_seq, knc_bar_end_len);
}

static void
knc_bar_signal (int sig)
{
  knc_bar_end ();
  for (int i = 0; i < 3; i++)
    if (knc_signals[i] == sig)
      {
	signal (sig, knc_prev_handler[i]);
	break;
      }
  raise (sig);
}

/* Reserve the last line: a line feed scrolls when the cursor is on it,
   then the scroll region stops one line short.  */
static void
knc_bar_start (void)
{
#ifdef TIOCGWINSZ
  struct winsize size;
  if (ioctl (2, TIOCGWINSZ, &size) == 0 && size.ws_row > 0)
    {
      knc_rows = size.ws_row;
      knc_cols = size.ws_col;
    }
#endif
  if (knc_rows < 3 || knc_cols < 20)
    {
      knc_progress = false;
      return;
    }
  char buf[64];
  int len = snprintf (buf, sizeof buf, "\n\033[1A\0337\033[1;%dr\0338",
		      knc_rows - 1);
  knc_bar_end_len = snprintf (knc_bar_end_seq, sizeof knc_bar_end_seq,
			      "\0337\033[r\033[%d;1H\033[K\0338", knc_rows);
  knc_bar_on = true;
  atexit (knc_bar_end);
  for (int i = 0; i < 3; i++)
    {
      knc_prev_handler[i] = signal (knc_signals[i], SIG_IGN);
      signal (knc_signals[i], knc_prev_handler[i] == SIG_IGN
			      ? SIG_IGN : knc_bar_signal);
    }
  knc_write (buf, len);
}

static bool
knc_compiler_proper (const char *prog)
{
  return (startswith (prog, "cc1") || !strcmp (prog, "f951")
	  || !strcmp (prog, "d21") || !strcmp (prog, "gnat1")
	  || !strcmp (prog, "rust1") || !strcmp (prog, "lto1"));
}

/* PROG is TOOL itself or a target-prefixed TOOL (k1om-mpss-linux-as).  */
static bool
knc_tool (const char *prog, const char *tool)
{
  size_t len = strlen (prog), n = strlen (tool);
  return (!strcmp (prog, tool)
	  || (len > n && prog[len - n - 1] == '-' && !strcmp (prog + len - n, tool)));
}

static void
knc_bar_draw (const char *const *argv)
{
  char prog[64];
  const char *base = lbasename (argv[0]);
  size_t len = strcspn (base, ".");
  if (len >= sizeof prog)
    len = sizeof prog - 1;
  memcpy (prog, base, len);
  prog[len] = 0;

  const char *verb = prog, *what = "";
  bool preprocess = false;
  for (int i = 1; argv[i]; i++)
    {
      preprocess |= !strcmp (argv[i], "-E");
      for (int j = knc_input; j < knc_n_inputs; j++)
	if (!strcmp (argv[i], knc_inputs[j]))
	  knc_input = j;
    }
  if (knc_n_inputs)
    what = lbasename (knc_inputs[knc_input]);
  if (knc_compiler_proper (prog))
    verb = preprocess ? "preprocessing" : "compiling";
  else if (knc_tool (prog, "as"))
    verb = "assembling";
  else if (!strcmp (prog, "collect2") || knc_tool (prog, "ld"))
    {
      verb = "linking";
      what = "a.out";
      for (int i = 1; argv[i]; i++)
	if (!strcmp (argv[i], "-o") && argv[i + 1])
	  what = lbasename (argv[i + 1]);
    }

  if (++knc_job > knc_total)
    knc_total = knc_job;
  const int width = 30;
  int filled = width * knc_job / knc_total;
  char bar[width + 1];
  memset (bar, '#', filled);
  memset (bar + filled, '.', width - filled);
  bar[width] = 0;
  char line[512];
  int n = snprintf (line, sizeof line, "[%s] %3d%%  %s %s (%d/%d)", bar,
		    100 * knc_job / knc_total, verb, what, knc_job, knc_total);
  if (n < 0)
    return;
  if (n >= knc_cols)
    n = knc_cols - 1;
  if (n >= (int) sizeof line)
    n = sizeof line - 1;
  char head[32];
  int h = snprintf (head, sizeof head, "\0337\033[%d;1H\r\033[K", knc_rows);
  knc_write (head, h);
  knc_write (line, n);
  knc_write ("\0338", 2);
}

/* DRIVER_EXECUTE_HOOK: before each job, after -v or -### printed it.  */
void
knc_driver_job (const char *const *argv, int verbose)
{
  if (!knc_decided || (!knc_banner && !knc_progress))
    return;
  const char *force = getenv ("GCC_KNC_FORCE_TERMINAL");
  /* The self spec does not see -o; a job writing to stdout does.  */
  bool to_stdout = false;
  for (int i = 1; argv[i]; i++)
    to_stdout |= !strcmp (argv[i], "-o") && argv[i + 1] && !strcmp (argv[i + 1], "-");
  if (verbose || to_stdout || (!(force && !strcmp (force, "1")) && !isatty (2)))
    {
      knc_banner = knc_progress = false;
      return;
    }
  if (knc_banner)
    {
      knc_banner = false;
      for (const char *line : knc_banner_lines)
	{
	  knc_write (line, strlen (line));
	  knc_write ("\n", 1);
	}
      knc_write ("\n", 1);
    }
  if (knc_progress && !knc_bar_on)
    knc_bar_start ();
  if (knc_bar_on)
    knc_bar_draw (argv);
}
