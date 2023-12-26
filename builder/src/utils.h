/***********************************************************************\
*
* $Revision: 11537 $
* $Date: 2021-06-20 08:45:38 +0200 (Sun, 20 Jun 2021) $
* $Author: torsten $
* Contents: Geany builder plugin utility functions
* Systems: all
*
\***********************************************************************/

#ifndef UTILS_H
#define UTILS_H

/****************************** Includes *******************************/
#include <string.h>
#include <locale.h>
#include <errno.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <glib-object.h>

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include <geany.h>
#include <geanyplugin.h>
#include <sciwrappers.h>

#include <gdk/gdkkeysyms.h>

#include "builder.h"

/****************** Conditional compilation switches *******************/

/***************************** Constants *******************************/

/***************************** Datatypes *******************************/

typedef void(*OutputHandler)(GString *line, GIOCondition ioCondition, gpointer data);
typedef gboolean(*InputValidator)(const gchar *value, gpointer data);

typedef GPtrArray StringStack;

/***************************** Variables *******************************/

/****************************** Macros *********************************/

/***************************** Forwards ********************************/

/***************************** Functions *******************************/

/***********************************************************************\
* Name   : stringEquals
* Purpose: compare if strings are equal
* Input  : string1,string2 - strings
* Output : -
* Return : TRUE iff equals
* Notes  : -
\***********************************************************************/

static INLINE gboolean stringEquals(const gchar *string1, const gchar *string2)
{
  return (string1 == string2) || ((string1 != NULL) && (string2 != NULL) && (strcmp(string1,string2) == 0));
}

/***********************************************************************\
* Name   : stringIsEmpty
* Purpose: check if string is empty
* Input  : string - string
* Output : -
* Return : TRUE iff empty
* Notes  : -
\***********************************************************************/

static INLINE gboolean stringIsEmpty(const gchar *string)
{
  return (string == NULL) || (string[0] == '\0');
}

/***********************************************************************\
* Name   : stringClear
* Purpose: clear string
* Input  : string - string
* Output : -
* Return : string
* Notes  : -
\***********************************************************************/

static INLINE gchar* stringClear(gchar *string)
{
  if (string != NULL) string[0] = '\0';

  return string;
}

/***********************************************************************\
* Name   : stringEscape
* Purpose: escape characters in string
* Input  : string     - string
*          toEscape   - characters to escape
*          escapeChar - escape character
* Output : -
* Return : escaped string
* Notes  : free string with g_free()!
\***********************************************************************/

gchar *stringEscape(const gchar *string, const char *toEscape, char escapeChar);

/***********************************************************************\
* Name   : stringUnescape
* Purpose: unescape characters in string
* Input  : string     - string
*          toEscape   - characters to escape
*          escapeChar - escape character
* Output : -
* Return : unescaped string
* Notes  : free string with g_free()!
\***********************************************************************/

gchar *stringUnescape(const gchar *string, char escapeChar);

/***********************************************************************\
* Name   : stringSplit
* Purpose: split string into tokens
* Input  : string - string
*          delimiters - delimiters charachters
*          escapeChar - escape character or NUL
*          maxTokens  - max. number of tokens or -1
* Output : -
* Return : string array or NULL if string is NULL
* Notes  : -
\***********************************************************************/

gchar **stringSplit(const gchar *string,
                    const gchar *delimiters,
                    char        escapeChar,
                    gint        maxTokens
                   );

/***********************************************************************\
* Name   : getAbsolutePath
* Purpose: get absolute path from directory and file path
* Input  : directory - directory (can be NULL)
*          filePath  - file path
* Output : -
* Return : absolute file path
* Notes  : -
\***********************************************************************/

gchar *getAbsolutePath(const gchar *directory,
                       const gchar *filePath
                      );

/***********************************************************************\
* Name   : string_stack_new
* Purpose: new string stack
* Input  : -
* Output : -
* Return : string stack
* Notes  : -
\***********************************************************************/

StringStack *string_stack_new();

/***********************************************************************\
* Name   : string_stack_free
* Purpose: free string stack
* Input  : stringStack - string stack to free
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void string_stack_free(StringStack *stringStack);

/***********************************************************************\
* Name   : string_stack_push
* Purpose: push string on string stack
* Input  : stringStack - string stack
*          string      - string
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void string_stack_push(StringStack *stringStack, const gchar *string);

/***********************************************************************\
* Name   : string_stack_pop
* Purpose: pop string from string stack
* Input  : stringStack - string stack
* Output : -
* Return : string
* Notes  : -
\***********************************************************************/

void string_stack_pop(StringStack *stringStack);

/***********************************************************************\
* Name   : string_stack_clear
* Purpose: clear string stack
* Input  : stringStack - string stack
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void string_stack_clear(StringStack *stringStack);

/***********************************************************************\
* Name   : string_stack_top
* Purpose: get top string on stack or NULL
* Input  : stringStack - string stack
* Output : -
* Return : top string
* Notes  : -
\***********************************************************************/

gchar *string_stack_top(StringStack *stringStack);

/***********************************************************************\
* Name   : getAbsoluteDirectory
* Purpose: get absolute directory with current working directory or
*          (sub-)directiry
* Input  : workingDirectory - working directory
*          directory        - absolute or relative directory
* Output : -
* Return : absolute directory (when possible)
* Notes  : -
\***********************************************************************/

gchar *getAbsoluteDirectory(const gchar *directory,
                            ...
                           );

// ---------------------------------------------------------------------

/***********************************************************************\
* Name   : setIndicator
* Purpose: set indicator
* Input  : filePath       - file path
*          lineNumber     - line number [1..n]
*          color          - indicator color
*          indicatorIndex - indciator index
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void setIndicator(const gchar *filePath, guint lineNumber, const GdkRGBA *color, guint indicatorIndex);

/***********************************************************************\
* Name   : clearIndicator
* Purpose: clear indicator
* Input  : document       - document
*          indicatorIndex - indicator index
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void clearIndicator(GeanyDocument *document, guint indicatorIndex);

/***********************************************************************\
* Name   : adjustTreeView
* Purpose: adjust tree view to show end
* Input  : data - scrolled window widget
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

gboolean adjustTreeViewToEnd(gpointer data);

/***********************************************************************\
* Name   : showLastLine
* Purpose: show last line
* Input  : widget - scrolled window widget
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void showLastLine(GtkScrolledWindow *widget);

/***********************************************************************\
* Name   : addBox
* Purpose: add widget to box
* Input  : box    - box
*          expand - TRUE to fill+expand
*          widget - widget to add
* Output : -
* Return : widget
* Notes  : -
\***********************************************************************/

GtkWidget *addBox(GtkBox *box, gboolean expand, GtkWidget *widget);

/***********************************************************************\
* Name   : addGrid
* Purpose: add widget to grid
* Input  : grid        - grid
*          row, column - row/column
*          columnSpan  - column span [1..n]
*          widget      - widget to add
* Output : -
* Return : widget
* Return : -
* Notes  : -
\***********************************************************************/

GtkWidget *addGrid(GtkGrid *grid, guint row, guint column, guint columnSpan, GtkWidget *widget);

/***********************************************************************\
* Name   : addTab
* Purpose: add tab widget to notebook
* Input  : notebook - notebook
*          title    - tab title
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

GtkWidget *addTab(GtkWidget *notebook, const char *title);

/***********************************************************************\
* Name   : newLabel
* Purpose: new label widget
* Input  : widget      - widget variable or NULL
*          rootObject  - root object
*          name        - property value name
*          text        - entry label text
*          tooltipText - entry tooltip text
* Output : -
* Return : widget
* Notes  : -
\***********************************************************************/

GtkWidget *newLabel(GtkWidget   **widget,
                    GObject     *rootObject,
                    const gchar *name,
                    const gchar *text,
                    const gchar *tooltipText
                   );

/***********************************************************************\
* Name   : newView
* Purpose: new text view widget
* Input  : rootObject  - root object
*          name        - property value name
*          text        - entry label text
*          tooltipText - entry tooltip text
* Output : -
* Return : widget
* Notes  : -
\***********************************************************************/

GtkWidget *newView(GtkWidget   **widget,
                   GObject     *rootObject,
                   const gchar *name,
                   const gchar *tooltipText
                  );

/***********************************************************************\
* Name   : newCheckButton
* Purpose: new check button widget
* Input  : rootObject  - root object
*          name        - property value name
*          text        - entry label text
*          tooltipText - entry tooltip text
* Output : -
* Return : widget
* Notes  : -
\***********************************************************************/

GtkWidget *newCheckButton(GtkWidget   **widget,
                          GObject     *rootObject,
                          const gchar *name,
                          const gchar *text,
                          const gchar *tooltipText
                         );

/***********************************************************************\
* Name   : newRadioButton
* Purpose: new radio button widget
* Input  : rootObject      - root object
*          prevRadioButton - previouos radio button or NULL
*          name            - property value name
*          text            - entry label text
*          tooltipText     - entry tooltip text
* Output : -
* Return : widget
* Notes  : -
\***********************************************************************/

GtkWidget *newRadioButton(GtkWidget   **widget,
                          GObject     *rootObject,
                          GtkWidget   *prevRadioButton,
                          const gchar *name,
                          const gchar *text,
                          const gchar *tooltipText
                         );

/***********************************************************************\
* Name   : newSpinButton
* Purpose: new spin button widget
* Input  : widget      - widget variable or NULL
*          rootObject  - root object
*          name        - property value name
*          text        - entry label text
*          tooltipText - entry tooltip text
*          min,max     - min./max. value
* Output : -
* Return : widget
* Notes  : -
\***********************************************************************/

GtkWidget *newSpinButton(GtkWidget   **widget,
                         GObject     *rootObject,
                         const gchar *name,
                         const gchar *tooltipText,
                         size_t      min,
                         size_t      max
                        );

/***********************************************************************\
* Name   : newCombo
* Purpose: new combo widget
* Input  : rootObject  - root object
*          name        - property value name
*          tooltipText - entry tooltip text
* Output : -
* Return : widget
* Notes  : -
\***********************************************************************/

GtkWidget *newCombo(GtkWidget   **widget,
                    GObject     *rootObject,
                    const gchar *name,
                    const gchar *tooltipText
                   );

/***********************************************************************\
* Name   : newComboEntry
* Purpose: new combo widget with entry
* Input  : rootObject  - root object
*          name        - property value name
*          tooltipText - entry tooltip text
* Output : -
* Return : widget
* Notes  : -
\***********************************************************************/

GtkWidget *newComboEntry(GtkWidget   **widget,
                         GObject     *rootObject,
                         const gchar *name,
                         const gchar *tooltipText
                        );

/***********************************************************************\
* Name   : newEntry
* Purpose: new text entry widget
* Input  : widget      - widget variable or NULL
*          rootObject  - root object
*          name        - property value name
*          text        - entry label text
*          tooltipText - entry tooltip text
* Output : -
* Return : widget
* Notes  : -
\***********************************************************************/

GtkWidget *newEntry(GtkWidget   **widget,
                    GObject     *rootObject,
                    const gchar *name,
                    const gchar *tooltipText
                   );

/***********************************************************************\
* Name   : newPasswordEntry
* Purpose: new password entry widget
* Input  : widget      - widget variable or NULL
*          rootObject  - root object
*          name        - property value name
*          text        - entry label text
*          tooltipText - entry tooltip text
* Output : -
* Return : widget
* Notes  : -
\***********************************************************************/

GtkWidget *newPasswordEntry(GtkWidget   **widget,
                            GObject     *rootObject,
                            const gchar *name,
                            const gchar *tooltipText
                           );

/***********************************************************************\
* Name   : newColorChooser
* Purpose: new color chooser widget
* Input  : rootObject  - root object
*          name        - property value name
*          tooltipText - entry tooltip text
* Output : -
* Return : widget
* Notes  : -
\***********************************************************************/

GtkWidget *newColorChooser(GtkWidget   **widget,
                           GObject     *rootObject,
                           const gchar *name,
                           const gchar *tooltipText
                          );

/***********************************************************************\
* Name   : newDirectoryChooser
* Purpose: new directory chooser widget
* Input  : rootObject  - root object
*          name        - property value name
*          tooltipText - entry tooltip text
* Output : -
* Return : widget
* Notes  : -
\***********************************************************************/

GtkWidget *newDirectoryChooser(GtkWidget   **widget,
                               GObject     *rootObject,
                               const gchar *name,
                               const gchar *tooltipText
                              );

/***********************************************************************\
* Name   : newFileChooser
* Purpose: new file chooser widget
* Input  : rootObject  - root object
*          name        - property value name
*          tooltipText - entry tooltip text
* Output : -
* Return : widget
* Notes  : -
\***********************************************************************/

GtkWidget *newFileChooser(GtkWidget   **widget,
                          GObject     *rootObject,
                          const gchar *name,
                          const gchar *tooltipText
                         );

/***********************************************************************\
* Name   : newWorkingDirectoryChooser
* Purpose: new working directory chooser with directory select menu
* Input  : rootObject  - root object
*          name        - property value name
*          tooltipText - entry tooltip text
* Output : -
* Return : grid widget
* Notes  : -
\***********************************************************************/

GtkWidget *newWorkingDirectoryChooser(GtkWidget   **widget,
                                      GObject     *rootObject,
                                      const gchar *name,
                                      const gchar *tooltipText
                                     );

/***********************************************************************\
* Name   : inputDialog
* Purpose: input text line dialog
* Input  : parentWindow - parent window
*          title        - dialog title
*          text         - dialog entry text
*          tooltipText  - dialog tooltip text
*          value        - text entry value or NULL
*          validator    - validator callback or NULL
*          data         - user data
* Output : string - input string
* Return : TRUE on "ok", FALSE otherwise
* Notes  : -
\***********************************************************************/

gboolean inputDialog(GtkWindow      *parentWindow,
                     const gchar    *title,
                     const char     *text,
                     const gchar    *tooltipText,
                     const gchar    *value,
                     InputValidator validator,
                     gpointer       data,
                     GString        *string
                    );

// ---------------------------------------------------------------------

/***********************************************************************\
* Name   : expandMacros
* Purpose: expand macros %%, %d, %e, %f, %p, %t
* Input  : project    - project or NULL
*          document   - current document or NULL
*          template   - template with macros to expand
*          customText - custom text
* Output : -
* Return : expanded string
* Notes  : -
\***********************************************************************/

gchar *expandMacros(const GeanyProject  *project,
                    const GeanyDocument *document,
                    const gchar         *template,
                    const gchar         *customTarget
                   );

#endif // UTILS_H

/* end of file */
