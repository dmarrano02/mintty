// config.c (part of mintty)
// Copyright 2008-11 Andy Koppe
// Based on code from PuTTY-0.60 by Simon Tatham and team.
// Licensed under the terms of the GNU General Public License v3 or later.

#include "config.h"
#include "ctrls.h"
#include "print.h"
#include "charset.h"
#include "win.h"

#include <termios.h>
#include <sys/cygwin.h>

string log_file = 0;
bool utmp_enabled = false;
hold_t hold = HOLD_DEFAULT;

#if CYGWIN_VERSION_API_MINOR >= 222
static wstring rc_filename = 0;
#else
static string rc_filename = 0;
#endif

const config default_cfg = {
  // Looks
  .fg_colour = 0xBFBFBF,
  .bg_colour = 0x000000,
  .cursor_colour = 0xBFBFBF,
  .transparency = 0,
  .opaque_when_focused = false,
  .cursor_type = CUR_LINE,
  .cursor_blinks = true,
  // Text
  .font = {.name = "Lucida Console", .isbold = false, .size = 9},
  .font_quality = FQ_DEFAULT,
  .bold_as_font = -1,  // -1 means "the opposite of bold_as_colour"
  .bold_as_colour = true,
  .allow_blinking = false,
  .locale = "",
  .charset = "",
  // Keys
  .backspace_sends_bs = CERASE == '\b',
  .ctrl_alt_is_altgr = false,
  .clip_shortcuts = true,
  .window_shortcuts = true,
  .switch_shortcuts = true,
  .zoom_shortcuts = true,
  .alt_fn_shortcuts = true,
  .ctrl_shift_shortcuts = false,
  // Mouse
  .copy_on_select = true,
  .copy_as_rtf = true,
  .clicks_place_cursor = false,
  .right_click_action = RC_SHOWMENU,
  .clicks_target_app = true,
  .click_target_mod = MDK_SHIFT,
  // Window
  .cols = 80,
  .rows = 24,
  .scrollbar = 1,
  .scrollback_lines = 10000,
  .scroll_mod = MDK_SHIFT,
  .pgupdn_scroll = false,
  // Terminal
  .term = "xterm",
  .answerback = "",
  .bell_sound = false,
  .bell_flash = false,
  .bell_taskbar = true,
  .printer = "",
  .confirm_exit = true,
  // Hidden
  .col_spacing = 0,
  .row_spacing = 0,
  .word_chars = "",
  .use_system_colours = false,
  .ime_cursor_colour = DEFAULT_COLOUR,
  .ansi_colours = {
    0x000000, 0x0000BF, 0x00BF00, 0x00BFBF,
    0xBF0000, 0xBF00BF, 0xBFBF00, 0xBFBFBF,
    0x404040, 0x4040FF, 0x40FF40, 0x40FFFF,
    0xFF4040, 0xFF40FF, 0xFFFF40, 0xFFFFFF
  }
};

config cfg, new_cfg;

typedef enum {
  OPT_STRING, OPT_BOOL, OPT_INT, OPT_COLOUR,
  OPT_COMPAT = 8
} opt_type;

static const uchar opt_type_sizes[] = {
  [OPT_STRING] = sizeof(string),
  [OPT_BOOL] = sizeof(bool),
  [OPT_INT] = sizeof(int),
  [OPT_COLOUR] = sizeof(colour)
};

#define offcfg(option) offsetof(config, option)

static const struct {
  string name;
  uchar type;
  uchar offset;
}
options[] = {
  // Looks
  {"ForegroundColour", OPT_COLOUR, offcfg(fg_colour)},
  {"BackgroundColour", OPT_COLOUR, offcfg(bg_colour)},
  {"CursorColour", OPT_COLOUR, offcfg(cursor_colour)},
  {"Transparency", OPT_INT, offcfg(transparency)},
  {"OpaqueWhenFocused", OPT_BOOL, offcfg(opaque_when_focused)},
  {"CursorType", OPT_INT, offcfg(cursor_type)},
  {"CursorBlinks", OPT_BOOL, offcfg(cursor_blinks)},

  // Text
  {"Font", OPT_STRING, offcfg(font.name)},
  {"FontIsBold", OPT_BOOL, offcfg(font.isbold)},
  {"FontHeight", OPT_INT, offcfg(font.size)},
  {"FontQuality", OPT_INT, offcfg(font_quality)},
  {"BoldAsFont", OPT_BOOL, offcfg(bold_as_font)},
  {"BoldAsColour", OPT_BOOL, offcfg(bold_as_colour)},
  {"AllowBlinking", OPT_BOOL, offcfg(allow_blinking)},
  {"Locale", OPT_STRING, offcfg(locale)},
  {"Charset", OPT_STRING, offcfg(charset)},

  // Keys
  {"BackspaceSendsBS", OPT_BOOL, offcfg(backspace_sends_bs)},
  {"CtrlAltIsAltGr", OPT_BOOL, offcfg(ctrl_alt_is_altgr)},
  {"ClipShortcuts", OPT_BOOL, offcfg(clip_shortcuts)},
  {"WindowShortcuts", OPT_BOOL, offcfg(window_shortcuts)},
  {"SwitchShortcuts", OPT_BOOL, offcfg(switch_shortcuts)},
  {"ZoomShortcuts", OPT_BOOL, offcfg(zoom_shortcuts)},
  {"AltFnShortcuts", OPT_BOOL, offcfg(alt_fn_shortcuts)},
  {"CtrlShiftShortcuts", OPT_BOOL, offcfg(ctrl_shift_shortcuts)},

  // Mouse
  {"CopyOnSelect", OPT_BOOL, offcfg(copy_on_select)},
  {"CopyAsRTF", OPT_BOOL, offcfg(copy_as_rtf)},
  {"ClicksPlaceCursor", OPT_BOOL, offcfg(clicks_place_cursor)},
  {"RightClickAction", OPT_INT, offcfg(right_click_action)},
  {"ClicksTargetApp", OPT_INT, offcfg(clicks_target_app)},
  {"ClickTargetMod", OPT_INT, offcfg(click_target_mod)},

  // Window
  {"Columns", OPT_INT, offcfg(cols)},
  {"Rows", OPT_INT, offcfg(rows)},
  {"Scrollbar", OPT_INT, offcfg(scrollbar)},
  {"ScrollbackLines", OPT_INT, offcfg(scrollback_lines)},
  {"ScrollMod", OPT_INT, offcfg(scroll_mod)},
  {"PgUpDnScroll", OPT_BOOL, offcfg(pgupdn_scroll)},

  // Terminal
  {"Term", OPT_STRING, offcfg(term)},
  {"Answerback", OPT_STRING, offcfg(answerback)},
  {"BellSound", OPT_BOOL, offcfg(bell_sound)},
  {"BellFlash", OPT_BOOL, offcfg(bell_flash)},
  {"BellTaskbar", OPT_BOOL, offcfg(bell_taskbar)},
  {"Printer", OPT_STRING, offcfg(printer)},
  {"ConfirmExit", OPT_BOOL, offcfg(confirm_exit)},

  // Hidden
  
  // Character spacing
  {"ColSpacing", OPT_INT, offcfg(col_spacing)},
  {"RowSpacing", OPT_INT, offcfg(row_spacing)},
  
  // Word selection characters
  {"WordChars", OPT_STRING, offcfg(word_chars)},
  
  // IME cursor colour
  {"IMECursorColour", OPT_COLOUR, offcfg(ime_cursor_colour)},
  
  // ANSI colours
  {"Black", OPT_COLOUR, offcfg(ansi_colours[BLACK_I])},
  {"Red", OPT_COLOUR, offcfg(ansi_colours[RED_I])},
  {"Green", OPT_COLOUR, offcfg(ansi_colours[GREEN_I])},
  {"Yellow", OPT_COLOUR, offcfg(ansi_colours[YELLOW_I])},
  {"Blue", OPT_COLOUR, offcfg(ansi_colours[BLUE_I])},
  {"Magenta", OPT_COLOUR, offcfg(ansi_colours[MAGENTA_I])},
  {"Cyan", OPT_COLOUR, offcfg(ansi_colours[CYAN_I])},
  {"White", OPT_COLOUR, offcfg(ansi_colours[WHITE_I])},
  {"BoldBlack", OPT_COLOUR, offcfg(ansi_colours[BOLD_BLACK_I])},
  {"BoldRed", OPT_COLOUR, offcfg(ansi_colours[BOLD_RED_I])},
  {"BoldGreen", OPT_COLOUR, offcfg(ansi_colours[BOLD_GREEN_I])},
  {"BoldYellow", OPT_COLOUR, offcfg(ansi_colours[BOLD_YELLOW_I])},
  {"BoldBlue", OPT_COLOUR, offcfg(ansi_colours[BOLD_BLUE_I])},
  {"BoldMagenta", OPT_COLOUR, offcfg(ansi_colours[BOLD_MAGENTA_I])},
  {"BoldCyan", OPT_COLOUR, offcfg(ansi_colours[BOLD_CYAN_I])},
  {"BoldWhite", OPT_COLOUR, offcfg(ansi_colours[BOLD_WHITE_I])},

  // Backward compatibility
  {"UseSystemColours", OPT_BOOL | OPT_COMPAT, offcfg(use_system_colours)},
  {"BoldAsBright", OPT_BOOL | OPT_COMPAT, offcfg(bold_as_colour)},
};

static uchar option_order[lengthof(options)];
static uint option_order_len;

static int
find_option(string name)
{
  for (uint i = 0; i < lengthof(options); i++) {
    if (!strcasecmp(name, options[i].name))
      return i;
  }
  return -1;
}

int
parse_option(string option)
{
  string eq = strchr(option, '=');
  if (!eq)
    return -1;
  
  uint name_len = eq - option;
  char name[name_len + 1];
  memcpy(name, option, name_len);
  name[name_len] = 0;
  
  int i = find_option(name);
  if (i < 0)
    return i;
  
  string val = eq + 1;
  uint offset = options[i].offset;
  switch (options[i].type & ~OPT_COMPAT) {
    when OPT_STRING:
      strset(&atoffset(string, &cfg, offset), val);
    when OPT_BOOL:
      atoffset(bool, &cfg, offset) = atoi(val);
    when OPT_INT:
      atoffset(int, &cfg, offset) = atoi(val);
    when OPT_COLOUR: {
      uint r, g, b;
      if (sscanf(val, "%u,%u,%u", &r, &g, &b) == 3)
        atoffset(colour, &cfg, offset) = make_colour(r, g, b);
    }
  }
  return i;
}

static void
remember_option(int i)
{
  if (!memchr(option_order, i, option_order_len))
    option_order[option_order_len++] = i;
}

void
load_config(string filename)
{
  option_order_len = 0;

  delete(rc_filename);
#if CYGWIN_VERSION_API_MINOR >= 222
  rc_filename = cygwin_create_path(CCP_POSIX_TO_WIN_W, filename);
#else
  rc_filename = strdup(filename);
#endif

  FILE *file = fopen(filename, "r");
  if (file) {
    char line[256];
    while (fgets(line, sizeof line, file)) {
      line[strcspn(line, "\r\n")] = 0;  /* trim newline */
      int i = parse_option(line);
      if (i >= 0)
        remember_option(i);
    }
    fclose(file);
  }
}

void
copy_config(config *dst, const config *src)
{
  for (uint i = 0; i < lengthof(options); i++) {
    uint offset = options[i].offset;
    opt_type type = options[i].type;
    if (options[i].type & OPT_COMPAT)
      ; // skip
    else if (options[i].type == OPT_STRING)
      strset(&atoffset(string, dst, offset), atoffset(string, src, offset));
    else
      memcpy((void *)dst + offset, (void *)src + offset, opt_type_sizes[type]);
  }
}

void
start_config(void)
{
  copy_config(&cfg, &default_cfg);
}

void
finish_config(void)
{
  // Ignore charset setting if we haven't got a locale.
  if (!*cfg.locale)
    strset(&cfg.charset, "");
  
  if (cfg.use_system_colours) {
    // Translate 'UseSystemColours' to colour settings.
    cfg.fg_colour = cfg.cursor_colour = win_get_sys_colour(true);
    cfg.bg_colour = win_get_sys_colour(false);

    // Make sure they're written to the config file.
    // This assumes that the colour options are the first three in options[].
    remember_option(0);
    remember_option(1);
    remember_option(2);
  }
  
  // bold_as_font used to be implied by !bold_as_colour.
  if (cfg.bold_as_font == -1) {
    cfg.bold_as_font = !cfg.bold_as_colour;
    remember_option(find_option("BoldAsFont"));
  }
}

static void
save_config(void)
{
  string filename;

#if CYGWIN_VERSION_API_MINOR >= 222
  filename = cygwin_create_path(CCP_WIN_W_TO_POSIX, rc_filename);
#else
  filename = rc_filename;
#endif

  FILE *file = fopen(filename, "w");

  if (!file) {
    char *msg;
    int len = asprintf(&msg, "Could not save options to '%s':\n%s.",
                       filename, strerror(errno));
    if (len > 0) {
      wchar wmsg[len + 1];
      if (cs_mbstowcs(wmsg, msg, lengthof(wmsg)) >= 0)
        win_show_error(wmsg);
      delete(msg);
    }
  }
  else {
    for (uint j = 0; j < option_order_len; j++) {
      uint i = option_order[j];
      if (!(options[i].type & OPT_COMPAT)) {
        fprintf(file, "%s=", options[i].name);
        uint offset = options[i].offset;
        switch (options[i].type) {
          when OPT_STRING:
            fprintf(file, "%s\n", atoffset(string, &cfg, offset));
          when OPT_BOOL:
            fprintf(file, "%i\n", atoffset(bool, &cfg, offset));
          when OPT_INT:
            fprintf(file, "%i\n", atoffset(int, &cfg, offset));
          when OPT_COLOUR: {
            colour c = atoffset(colour, &cfg, offset);
            fprintf(file, "%u,%u,%u\n", red(c), green(c), blue(c));
          }
        }
      }
    }
    fclose(file);
  }

#if CYGWIN_VERSION_API_MINOR >= 222
  delete(filename);
#endif
}


static control *cols_box, *rows_box, *locale_box, *charset_box;

static void
apply_config(void)
{
  // Record what's changed
  for (uint i = 0; i < lengthof(options); i++) {
    opt_type type = options[i].type;
    uint size = opt_type_sizes[type], off = options[i].offset;
    bool changed =
      type == OPT_STRING
      ? strcmp(atoffset(string, &cfg, off), atoffset(string, &new_cfg, off))
      : memcmp((void *)&cfg + off, (void *)&new_cfg + off, size);
    if (changed && !memchr(option_order, i, option_order_len))
      option_order[option_order_len++] = i;
  }
  
  win_reconfig();
  save_config();
}

static void
ok_handler(control *unused(ctrl), void *unused(data), int event)
{
  if (event == EVENT_ACTION) {
    apply_config();
    dlg_end();
  }
}

static void
cancel_handler(control *unused(ctrl), void *unused(data), int event)
{
  if (event == EVENT_ACTION)
    dlg_end();
}

static void
apply_handler(control *unused(ctrl), void *unused(data), int event)
{
  if (event == EVENT_ACTION)
    apply_config();
}

static void
about_handler(control *unused(ctrl), void *unused(data), int event)
{
  if (event == EVENT_ACTION)
    win_show_about();
}

static void
current_size_handler(control *unused(ctrl), void *unused(data), int event)
{
  if (event == EVENT_ACTION) {
    new_cfg.cols = term.cols;
    new_cfg.rows = term.rows;
    dlg_refresh(cols_box);
    dlg_refresh(rows_box);
  }
}

static void
printerbox_handler(control *ctrl, void *unused(data), int event)
{
  static const char NONE[] = "None (printing disabled)";
  string printer = new_cfg.printer;
  if (event == EVENT_REFRESH) {
    dlg_listbox_clear(ctrl);
    dlg_listbox_add(ctrl, NONE);
    uint num = printer_start_enum();
    for (uint i = 0; i < num; i++)
      dlg_listbox_add(ctrl, printer_get_name(i));
    printer_finish_enum();
    dlg_editbox_set(ctrl, *printer ? printer : NONE);
  }
  else if (event == EVENT_VALCHANGE || event == EVENT_SELCHANGE) {
    dlg_editbox_get(ctrl, &printer);
    if (!strcmp(printer, NONE))
      strset(&printer, "");
    new_cfg.printer = printer;
  }
}

static void
set_charset(string charset)
{
  strset(&new_cfg.charset, charset);
  dlg_editbox_set(charset_box, charset);
}

static void
locale_handler(control *ctrl, void *unused(data), int event)
{
  string locale = new_cfg.locale;
  switch (event) {
    when EVENT_REFRESH:
      dlg_listbox_clear(ctrl);
      string l;
      for (int i = 0; (l = locale_menu[i]); i++)
        dlg_listbox_add(ctrl, l);
      dlg_editbox_set(ctrl, locale);
    when EVENT_UNFOCUS:
      dlg_editbox_set(ctrl, locale);
      if (!*locale)
        set_charset("");
    when EVENT_VALCHANGE:
      dlg_editbox_get(ctrl, &new_cfg.locale);
    when EVENT_SELCHANGE:
      dlg_editbox_get(ctrl, &new_cfg.locale);
      if (*locale == '(')
        strset(&locale, "");
      if (!*locale)
        set_charset("");
#if HAS_LOCALES
      else if (!*new_cfg.charset)
        set_charset("UTF-8");
#endif
      new_cfg.locale = locale;
  }
}

static void
check_locale(void)
{
  if (!*new_cfg.locale) {
    strset(&new_cfg.locale, "C");
    dlg_editbox_set(locale_box, "C");
  }
}

static void
charset_handler(control *ctrl, void *unused(data), int event)
{
  string charset = new_cfg.charset;
  switch (event) {
    when EVENT_REFRESH:
      dlg_listbox_clear(ctrl);
      string cs;
      for (int i = 0; (cs = charset_menu[i]); i++)
        dlg_listbox_add(ctrl, cs);
      dlg_editbox_set(ctrl, charset);
    when EVENT_UNFOCUS:
      dlg_editbox_set(ctrl, charset);
      if (*charset)
        check_locale();
    when EVENT_VALCHANGE:
      dlg_editbox_get(ctrl, &new_cfg.charset);
    when EVENT_SELCHANGE:
      dlg_editbox_get(ctrl, &charset);
      if (*charset == '(')
        strset(&charset, "");
      else {
        *strchr(charset, ' ') = 0;
        check_locale();
      }
      new_cfg.charset = charset;
  }
}

static void
colour_handler(control *ctrl, void *unused(data), int event)
{
  colour *colour_p = ctrl->context.p;
  if (event == EVENT_ACTION) {
   /*
    * Start a colour selector, which will send us an
    * EVENT_CALLBACK when it's finished and allow us to
    * pick up the results.
    */
    dlg_coloursel_start(*colour_p);
  }
  else if (event == EVENT_CALLBACK) {
   /*
    * Collect the results of the colour selector. Will
    * return nonzero on success, or zero if the colour
    * selector did nothing (user hit Cancel, for example).
    */
    colour result;
    if (dlg_coloursel_results(&result))
      *colour_p = result;
  }
}

static void
term_handler(control *ctrl, void *unused(data), int event)
{
  switch (event) {
    when EVENT_REFRESH:
      dlg_listbox_clear(ctrl);
      dlg_listbox_add(ctrl, "xterm");
      dlg_listbox_add(ctrl, "xterm-256color");
      dlg_listbox_add(ctrl, "xterm-vt220");
      dlg_listbox_add(ctrl, "vt100");
      dlg_listbox_add(ctrl, "vt220");
      dlg_editbox_set(ctrl, new_cfg.term);
    when EVENT_VALCHANGE or EVENT_SELCHANGE:
      dlg_editbox_get(ctrl, &new_cfg.term);
  }
}

static void
int_handler(control *ctrl, void *data, int event)
{
  int offset = ctrl->context.i;
  int *field = &atoffset(int, data, offset);
  switch (event) {
    when EVENT_VALCHANGE: {
      string val = 0;
      dlg_editbox_get(ctrl, &val);
      *field = max(0, atoi(val));
      delete(val);
    }
    when EVENT_REFRESH: {
      char buf[16];
      sprintf(buf, "%i", *field);
      dlg_editbox_set(ctrl, buf);
    }
  }
}

static void
string_handler(control *ctrl, void *data, int event)
{
  int offset = ctrl->context.i;
  string *ps = &atoffset(string, data, offset);
  switch (event) {
    when EVENT_VALCHANGE:
      dlg_editbox_get(ctrl, ps);
    when EVENT_REFRESH:
      dlg_editbox_set(ctrl, *ps);
  }
}

void
setup_config_box(controlbox * b)
{
  controlset *s;
  control *c;

 /*
  * The standard panel that appears at the bottom of all panels:
  * Open, Cancel, Apply etc.
  */
  s = ctrl_new_set(b, "", "");
  ctrl_columns(s, 5, 20, 20, 20, 20, 20);
  c = ctrl_pushbutton(s, "About...", about_handler, P(0));
  c->column = 0;
  c = ctrl_pushbutton(s, "OK", ok_handler, P(0));
  c->button.isdefault = true;
  c->column = 2;
  c = ctrl_pushbutton(s, "Cancel", cancel_handler, P(0));
  c->button.iscancel = true;
  c->column = 3;
  c = ctrl_pushbutton(s, "Apply", apply_handler, P(0));
  c->column = 4;

 /*
  * The Looks panel.
  */
  s = ctrl_new_set(b, "Looks", "Colours");
  ctrl_columns(s, 3, 33, 33, 33);
  ctrl_pushbutton(
    s, "&Foreground...", colour_handler, P(&new_cfg.fg_colour)
  )->column = 0;
  ctrl_pushbutton(
    s, "&Background...", colour_handler, P(&new_cfg.bg_colour)
  )->column = 1;
  ctrl_pushbutton(
    s, "&Cursor...", colour_handler, P(&new_cfg.cursor_colour)
  )->column = 2;
  
  s = ctrl_new_set(b, "Looks", "Transparency");
  bool with_glass = win_is_glass_available();
  ctrl_radiobuttons(
    s, null, 4 + with_glass,
    dlg_stdradiobutton_handler, I(offcfg(transparency)),
    "&Off", I(0),
    "&Low", I(1),
    with_glass ? "&Med." : "&Medium", I(2), 
    "&High", I(3), 
    with_glass ? "Gla&ss" : null, I(-1), 
    null
  );
  ctrl_checkbox(
    s, "Opa&que when focused",
    dlg_stdcheckbox_handler, I(offcfg(opaque_when_focused))
  );

  s = ctrl_new_set(b, "Looks", "Cursor");
  ctrl_radiobuttons(
    s, null, 4 + with_glass,
    dlg_stdradiobutton_handler, I(offcfg(cursor_type)),
    "Li&ne", I(CUR_LINE), 
    "Bloc&k", I(CUR_BLOCK),
    "&Underscore", I(CUR_UNDERSCORE),
    null
  );
  ctrl_checkbox(
    s, "Blinkin&g", dlg_stdcheckbox_handler, I(offcfg(cursor_blinks))
  );

 /*
  * The Text panel.
  */
  s = ctrl_new_set(b, "Text", "Font");
  ctrl_fontsel(
    s, null, dlg_stdfontsel_handler, I(offcfg(font))
  );

  s = ctrl_new_set(b, "Text", null);
  ctrl_columns(s, 2, 50, 50);
  ctrl_radiobuttons(
    s, "Font smoothing", 2,
    dlg_stdradiobutton_handler, I(offcfg(font_quality)),
    "&Default", I(FQ_DEFAULT),
    "&None", I(FQ_NONANTIALIASED),
    "&Partial", I(FQ_ANTIALIASED),
    "&Full", I(FQ_CLEARTYPE),
    null
  )->column = 1;

  ctrl_checkbox(
    s, "Sho&w bold as font",
    dlg_stdcheckbox_handler, I(offcfg(bold_as_font))
  )->column = 0;
  ctrl_checkbox(
    s, "Show &bold as colour",
    dlg_stdcheckbox_handler, I(offcfg(bold_as_colour))
  )->column = 0;
  ctrl_checkbox(
    s, "&Allow blinking",
    dlg_stdcheckbox_handler, I(offcfg(allow_blinking))
  )->column = 0;

  s = ctrl_new_set(b, "Text", null);
  ctrl_columns(s, 2, 29, 71);
  (locale_box = ctrl_combobox(
    s, "&Locale", 100, locale_handler, P(0), P(0)
  ))->column = 0;
  (charset_box = ctrl_combobox(
    s, "&Character set", 100, charset_handler, P(0), P(0)
  ))->column = 1;

 /*
  * The Keys panel.
  */
  s = ctrl_new_set(b, "Keys", null);
  ctrl_checkbox(
    s, "&Backspace sends ^H",
    dlg_stdcheckbox_handler, I(offcfg(backspace_sends_bs))
  );
  ctrl_checkbox(
    s, "Ctrl+LeftAlt is Alt&Gr",
    dlg_stdcheckbox_handler, I(offcfg(ctrl_alt_is_altgr))
  );

  s = ctrl_new_set(b, "Keys", "Shortcuts");
  ctrl_checkbox(
    s, "Cop&y and Paste (Ctrl/Shift+Ins)",
    dlg_stdcheckbox_handler, I(offcfg(clip_shortcuts))
  );
  ctrl_checkbox(
    s, "&Menu and Full Screen (Alt+Space/Enter)",
    dlg_stdcheckbox_handler, I(offcfg(window_shortcuts))
  );
  ctrl_checkbox(
    s, "&Switch window (Ctrl+[Shift+]Tab)",
    dlg_stdcheckbox_handler, I(offcfg(switch_shortcuts))
  );
  ctrl_checkbox(
    s, "&Zoom (Ctrl+plus/minus/zero)",
    dlg_stdcheckbox_handler, I(offcfg(zoom_shortcuts))
  );
  ctrl_checkbox(
    s, "&Alt+Fn shortcuts",
    dlg_stdcheckbox_handler, I(offcfg(alt_fn_shortcuts))
  );
  ctrl_checkbox(
    s, "&Ctrl+Shift+letter shortcuts",
    dlg_stdcheckbox_handler, I(offcfg(ctrl_shift_shortcuts))
  );

 /*
  * The Mouse panel.
  */
  s = ctrl_new_set(b, "Mouse", null);
  ctrl_columns(s, 2, 50, 50);
  ctrl_checkbox(
    s, "Cop&y on select",
    dlg_stdcheckbox_handler, I(offcfg(copy_on_select))
  )->column = 0;
  ctrl_checkbox(
    s, "Copy as &rich text",
    dlg_stdcheckbox_handler, I(offcfg(copy_as_rtf))
  )->column = 1;
  ctrl_checkbox(
    s, "Clic&ks place command line cursor",
    dlg_stdcheckbox_handler, I(offcfg(clicks_place_cursor))
  );

  s = ctrl_new_set(b, "Mouse", "Right click action");
  ctrl_radiobuttons(
    s, null, 4,
    dlg_stdradiobutton_handler, I(offcfg(right_click_action)),
    "&Paste", I(RC_PASTE),
    "E&xtend", I(RC_EXTEND),
    "Show &menu", I(RC_SHOWMENU),
    null
  );
  
  s = ctrl_new_set(b, "Mouse", "Application mouse mode");
  ctrl_radiobuttons(
    s, "Default click target", 4,
    dlg_stdradiobutton_handler, I(offcfg(clicks_target_app)),
    "&Window", I(0),
    "Applicatio&n", I(1),
    null
  );
  ctrl_radiobuttons(
    s, "Modifier for overriding default", 4,
    dlg_stdradiobutton_handler, I(offcfg(click_target_mod)),
    "&Shift", I(MDK_SHIFT),
    "&Ctrl", I(MDK_CTRL),
    "&Alt", I(MDK_ALT),
    "&Off", I(0),
    null
  );
  
 /*
  * The Window panel.
  */
  s = ctrl_new_set(b, "Window", "Default size");
  ctrl_columns(s, 5, 35, 3, 28, 4, 30);
  (cols_box = ctrl_editbox(
    s, "Colu&mns", 44, int_handler, I(offcfg(cols)), I(256)
  ))->column = 0;
  (rows_box = ctrl_editbox(
    s, "Ro&ws", 55, int_handler, I(offcfg(rows)), I(256)
  ))->column = 2;
  ctrl_pushbutton(
    s, "C&urrent size", current_size_handler, P(0)
  )->column = 4;

  s = ctrl_new_set(b, "Window", null);
  ctrl_columns(s, 2, 66, 34);
  ctrl_editbox(
    s, "Scroll&back lines", 50,
    int_handler, I(offcfg(scrollback_lines)), I(1000000)
  )->column = 0;
  ctrl_radiobuttons(
    s, "Scrollbar", 4,
    dlg_stdradiobutton_handler, I(offcfg(scrollbar)),
    "&Left", I(-1),
    "&None", I(0),
    "&Right", I(1),
    null
  );
  ctrl_radiobuttons(
    s, "Modifier for scrolling", 4,
    dlg_stdradiobutton_handler, I(offcfg(scroll_mod)),
    "&Shift", I(MDK_SHIFT),
    "&Ctrl", I(MDK_CTRL),
    "&Alt", I(MDK_ALT),
    "&Off", I(0),
    null
  );
  ctrl_checkbox(
    s, "&PgUp and PgDn scroll without modifier",
    dlg_stdcheckbox_handler, I(offcfg(pgupdn_scroll))
  );

 /*
  * The Terminal panel.
  */
  s = ctrl_new_set(b, "Terminal", null);
  ctrl_columns(s, 2, 50, 50);
  ctrl_combobox(
    s, "&Type", 100, term_handler, P(0), P(0)
  )->column = 0;
  ctrl_editbox(
    s, "&Answerback", 100, string_handler, I(offcfg(answerback)), I(0)
  )->column = 1;

  s = ctrl_new_set(b, "Terminal", "Bell");
  ctrl_columns(s, 3, 25, 25, 50);
  ctrl_checkbox(
    s, "&Sound", dlg_stdcheckbox_handler, I(offcfg(bell_sound))
  )->column = 0;
  ctrl_checkbox(
    s, "&Flash", dlg_stdcheckbox_handler, I(offcfg(bell_flash))
  )->column = 1;
  ctrl_checkbox(
    s, "&Highlight in taskbar", dlg_stdcheckbox_handler, I(offcfg(bell_taskbar))
  )->column = 2;

  s = ctrl_new_set(b, "Terminal", "Printer");
  ctrl_combobox(
    s, null, 100, printerbox_handler, P(0), P(0)
  );

  s = ctrl_new_set(b, "Terminal", null);
  ctrl_checkbox(
    s, "&Prompt about running processes on close",
    dlg_stdcheckbox_handler, I(offcfg(confirm_exit))
  );
}
