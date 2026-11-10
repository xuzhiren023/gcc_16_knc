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
#include "diagnostic-core.h"
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

/* S9-13: -mknc-card=MODEL.  Every KNC card has the same core, so a model
   changes only what follows the memory system.  The L2 prefetch distance
   covers memory latency x bandwidth per core, about equal latency and 4.2,
   5.3 and 5.8 GB/s per core on the 3100, 5100 and 7100: 32 lines measured
   on a 7120P (S8-03), scaled for the others (derived, not measured).  The
   L1 distance covers an L2 hit in core cycles, the same on every model; it
   is written out so -### shows the whole setting.  generic takes the
   longest distance: one too short stalls on every line, one too long only
   holds a few more lines in L2.  docs/gcc-knc-flags.md has the table.  */

struct knc_card_model
{
  const char *name;
  int l2, l1;
};

static const knc_card_model knc_card_models[] = {
  { "generic", 32, 16 }, { "3100", 23, 16 }, { "5100", 29, 16 },
  { "7100", 32, 16 }
};

enum { KNC_CARD_GENERIC, KNC_CARD_3100, KNC_CARD_5100, KNC_CARD_7100 };

/* The model of a family or card name (3100, 3120P, 7120, ...: the family
   or card number and at most one of P, A, X, D), or -1.  */
static int
knc_card_lookup (const char *name)
{
  static const char *const numbers[] = {
    "3100", "3110", "3115", "3120", "5100", "5110", "5120", "7100", "7110",
    "7120"
  };
  if (!strcmp (name, "generic"))
    return KNC_CARD_GENERIC;
  for (const char *number : numbers)
    if (!strncmp (name, number, 4)
	&& (name[4] == '\0'
	    || (strchr ("PAXDpaxd", name[4]) && name[5] == '\0')))
      return (name[0] == '3' ? KNC_CARD_3100
	      : name[0] == '5' ? KNC_CARD_5100 : KNC_CARD_7100);
  return -1;
}

/* Open ROOT/PATH; ROOT is GCC_KNC_CARD_ROOT (a test tree) or empty.  */
static FILE *
knc_card_open (const char *root, const char *path)
{
  char *name = concat (root, path, NULL);
  FILE *f = fopen (name, "r");
  free (name);
  return f;
}

/* Running on a card (the native compiler): the distinct core ids in
   /proc/cpuinfo count every core, the one MPSS keeps for the OS included,
   and a MemTotal over 12 GiB means a 7100, the only 16 GB model, even if a
   kernel hides the OS core.  -1 if this is not a KNC (cpu family 11);
   -2 for a KNC whose core count names no model.  */
static int
knc_card_on_card (const char *root, int *cores_out)
{
  FILE *f = knc_card_open (root, "/proc/cpuinfo");
  if (!f)
    return -1;
  char line[512];
  bool knc = false, seen[256] = { false };
  int cores = 0, value;
  while (fgets (line, sizeof line, f))
    if (sscanf (line, "cpu family : %d", &value) == 1)
      knc |= value == 11;
    else if (sscanf (line, "core id : %d", &value) == 1
	     && value >= 0 && value < 256 && !seen[value])
      seen[value] = true, cores++;
  fclose (f);
  if (!knc)
    return -1;
  unsigned long mem_kb = 0;
  if ((f = knc_card_open (root, "/proc/meminfo")))
    {
      while (fgets (line, sizeof line, f))
	if (sscanf (line, "MemTotal: %lu", &mem_kb) == 1)
	  break;
      fclose (f);
    }
  *cores_out = cores;
  if (cores >= 61 || mem_kb > 12UL * 1024 * 1024)
    return KNC_CARD_7100;
  if (cores == 59 || cores == 60)
    return KNC_CARD_5100;
  if (cores == 56 || cores == 57)
    return KNC_CARD_3100;
  return -2;
}

/* A Linux host with MPSS 3.x: /sys/class/mic/micN/family is "x100" for a
   KNC and micN/sku its SKU name, such as "C0-7120 P/A/X/D" or
   "B1PRQ-5110P/5120D" (mpss-modules host/linsysfs.c show_family and
   show_sku): the first digit after the dash is the family.  The model
   of every card found, or -1 if none; fatal if they differ.  */
static int
knc_card_on_host (const char *root)
{
  int model = -1;
  bool mixed = false;
  char found[512] = "";
  for (int n = 0; n < 64; n++)
    {
      char path[64], line[128];
      snprintf (path, sizeof path, "/sys/class/mic/mic%d/family", n);
      FILE *f = knc_card_open (root, path);
      if (!f)
	continue;
      bool knc = fgets (line, sizeof line, f) && !strncmp (line, "x100", 4);
      fclose (f);
      snprintf (path, sizeof path, "/sys/class/mic/mic%d/sku", n);
      if (!knc || !(f = knc_card_open (root, path)))
	continue;
      const char *dash = fgets (line, sizeof line, f) ? strchr (line, '-') : NULL;
      fclose (f);
      int this_model = (!dash ? -1 : dash[1] == '3' ? KNC_CARD_3100
			: dash[1] == '5' ? KNC_CARD_5100
			: dash[1] == '7' ? KNC_CARD_7100 : -1);
      if (this_model < 0)
	continue;
      mixed |= model >= 0 && model != this_model;
      model = this_model;
      size_t len = strlen (found);
      snprintf (found + len, sizeof found - len, "%smic%d %s", len ? ", " : "",
		n, knc_card_models[this_model].name);
    }
  if (mixed)
    fatal_error (UNKNOWN_LOCATION,
		 "%<-mknc-card=native%> found different card models (%s); "
		 "use %<-mknc-card=MODEL%> to pick one", found);
  return model;
}

/* %:knc-card(VALUE) in the self spec: the model's --params, each only
   where the user did not set that param.  */
const char *
knc_card_spec (int argc, const char **argv)
{
  if (argc != 1)
    return "";
  int model = knc_card_lookup (argv[0]);
  if (!strcmp (argv[0], "native"))
    {
      const char *root = getenv ("GCC_KNC_CARD_ROOT");
      root = root ? root : "";
      int cores = 0;
      model = knc_card_on_card (root, &cores);
      if (model == -2)
	inform (UNKNOWN_LOCATION, "%<-mknc-card=native%>: a KNC with %d "
		"cores is no known model; using %<-mknc-card=generic%>", cores);
      else if (model == -1 && (model = knc_card_on_host (root)) < 0)
	inform (UNKNOWN_LOCATION, "%<-mknc-card=native%>: no KNC card found; "
		"using %<-mknc-card=generic%>");
      if (model < 0)
	model = KNC_CARD_GENERIC;
    }
  else if (model < 0)
    fatal_error (UNKNOWN_LOCATION, "unknown %<-mknc-card=%> value %qs; use "
		 "generic, 3100, 5100, 7100, native or a card name such as "
		 "7120P", argv[0]);
  const knc_card_model &m = knc_card_models[model];
  return xasprintf ("%%{!-param=knc-prefetch-l2-distance=*:"
		    "--param=knc-prefetch-l2-distance=%d} "
		    "%%{!-param=knc-prefetch-l1-distance=*:"
		    "--param=knc-prefetch-l1-distance=%d}", m.l2, m.l1);
}
