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

#define HALT_INTERNAL_ERROR_UNHANDLED_SWITCH_CASE() \
  do \
  { \
     __abort("Unhandled switch case"); \
  } \
  while (0)


/***************************** Forwards ********************************/

/***************************** Functions *******************************/

/***********************************************************************\
* Name   : __abort
* Purpose: avirt program immediately
* Input  : message - abort message to print
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void __abort(const gchar *message);

// ---------------------------------------------------------------------

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
* Name   : stringCompare
* Purpose: compare strings
* Input  : string1,string2 - strings
* Output : -
* Return : -1 iff string1 <  string2
*           0 iff string1 == string2
*           1 iff string2 >  string2
* Notes  : -
\***********************************************************************/

static INLINE int stringCompare(const gchar *string1, const gchar *string2)
{
  if      ((string1 != NULL) && (string2 != NULL)) return strcmp(string1,string2);
  else if ((string1 != NULL) && (string2 == NULL)) return -1;
  else if ((string1 == NULL) && (string2 != NULL)) return  1;
  else                                             return  0;
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

// ---------------------------------------------------------------------

/***********************************************************************\
* Name   : stringListClear
* Purpose: clear string list
* Input  : stringList - string list
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

static INLINE void stringListClear(GList **stringList)
{
  g_assert(stringList != NULL);

  g_list_free_full(g_steal_pointer(stringList), g_free);
}

/***********************************************************************\
* Name   : stringAppend
* Purpose: append string to string list
* Input  : stringList - string list
*          string     - string to append
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

static INLINE void stringAppend(GList **stringList, const gchar *string)
{
  g_assert(stringList != NULL);
  g_assert(string != NULL);

  (*stringList) = g_list_append(*stringList, strdup(string));
}

/***********************************************************************\
* Name   : stringListLength
* Purpose: get string list length
* Input  : stringList - string list
* Output : -
* Return : length of string list
* Notes  : -
\***********************************************************************/

static INLINE size_t stringListLength(const GList *stringList)
{
  g_assert(stringList != NULL);

  return g_list_length((GList*)stringList);
}

/***********************************************************************\
* Name   : stringListIterate
* Purpose: string list iterate with offset+length
* Input  : stringList - string list
*          offset     - offset [0..n-1]
*          length     - length or -1
*          function   - function to call
*          userData   - user data for function
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void stringListIterate(const GList *stringList,
                       guint       offset,
                       gint        length,
                       GFunc       function,
                       gpointer    userData
                      );

/***********************************************************************\
* Name   : stringListToString
* Purpose: convert string list to string
* Input  : string     - string variable (can be NULL)
*          stringList - string list
*          separator  - separator (can be NULL)
* Output : -
* Return : string
* Notes  : -
\***********************************************************************/

GString *stringListToString(GString *string, const GList *stringList, const gchar *separator);

// ---------------------------------------------------------------------

/***********************************************************************\
* Name   : stringStackNew
* Purpose: new string stack
* Input  : -
* Output : -
* Return : string stack
* Notes  : -
\***********************************************************************/

static INLINE StringStack *stringStackNew()
{
  return g_ptr_array_new_with_free_func(g_free);
}

/***********************************************************************\
* Name   : stringStackDelete
* Purpose: free string stack
* Input  : stringStack - string stack to free
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

static INLINE void stringStackDelete(StringStack *stringStack)
{
  g_assert(stringStack != NULL);

  g_ptr_array_free(stringStack, TRUE);
}

/***********************************************************************\
* Name   : stringStackPush
* Purpose: push string on string stack
* Input  : stringStack - string stack
*          string      - string
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

static INLINE void stringStackPush(StringStack *stringStack, const gchar *string)
{
  g_assert(stringStack != NULL);

  g_ptr_array_add(stringStack, g_strdup(string));
}

/***********************************************************************\
* Name   : stringStackPop
* Purpose: pop string from string stack
* Input  : stringStack - string stack
* Output : -
* Return : string
* Notes  : -
\***********************************************************************/

void stringStackPop(StringStack *stringStack);

/***********************************************************************\
* Name   : stringStackClear
* Purpose: clear string stack
* Input  : stringStack - string stack
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

static INLINE void stringStackClear(StringStack *stringStack)
{
  g_assert(stringStack != NULL);

  g_ptr_array_set_size(stringStack, 0);
}

/***********************************************************************\
* Name   : stringStackPeek
* Purpose: peek top string on stack or NULL
* Input  : stringStack - string stack
* Output : -
* Return : top string
* Notes  : -
\***********************************************************************/

gchar *stringStackPeek(StringStack *stringStack);

// ----------------------------------------------------------------------

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
* Name   : getAbsoluteDirectory
* Purpose: get absolute directory with current working directory or
*          (sub-)directiry
* Input  : directory - absolute or relative directory
*          ...       - optional addition directories
* Output : -
* Return : absolute directory (when possible)
* Notes  : last optional directory must be NULL!
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
*          text        - label text
*          alignTop    - TRUE to align text top
*          tooltipText - label tooltip text
* Output : -
* Return : widget
* Notes  : -
\***********************************************************************/

GtkWidget *newLabel(GtkWidget   **widget,
                    GObject     *rootObject,
                    const gchar *name,
                    const gchar *text,
                    gboolean    alignTop,
                    const gchar *tooltipText
                   );

/***********************************************************************\
* Name   : newView
* Purpose: new text view widget
* Input  : rootObject  - root object
*          name        - property value name
*          text        - view text
*          tooltipText - view tooltip text
* Output : -
* Return : widget
* Notes  : -
\***********************************************************************/

GtkWidget *newView(GtkWidget   **widget,
                   GObject     *rootObject,
                   const gchar *name,
                   const gchar *text,
                   const gchar *tooltipText
                  );

/***********************************************************************\
* Name   : newCheckButton
* Purpose: new check button widget
* Input  : rootObject  - root object
*          name        - property value name
*          text        - button label text
*          tooltipText - button tooltip text
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
*          text            - button label text
*          tooltipText     - button tooltip text
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
*          text        - entry text
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
* Name   : newTextEntry
* Purpose: new text entry widget (multiline entry)
* Input  : widget      - widget variable or NULL
*          rootObject  - root object
*          name        - property value name
*          text        - entry text
*          tooltipText - entry tooltip text
* Output : -
* Return : widget
* Notes  : -
\***********************************************************************/

GtkWidget *newTextEntry(GtkWidget   **widget,
                        GObject     *rootObject,
                        const gchar *name,
                        const gchar *text,
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
