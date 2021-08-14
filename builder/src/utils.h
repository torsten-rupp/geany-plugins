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

#define LOCAL static

/****************** Conditional compilation switches *******************/

/***************************** Constants *******************************/

/***************************** Datatypes *******************************/

typedef void(*OutputHandler)(GString *line, GIOCondition ioCondition, gpointer data);
typedef gboolean(*InputValidator)(const gchar *value, gpointer data);

typedef GPtrArray StringStack;

/***************************** Variables *******************************/

/****************************** Macros *********************************/
#define INLINE inline
#define LOCAL_INLINE static inline
#define UNUSED_VARIABLE(name) (void)name

#define ARRAY_SIZE(array) (sizeof(array)/sizeof(array[0]))

/***************************** Forwards ********************************/

/***************************** Functions *******************************/

/**
 * @brief new string stack
 *
 * @return      string stack
 */

StringStack *string_stack_new();

/**
 * @brief free string stack
 *
 * @param [in]  stringStack string stack to free
 */

void string_stack_free(StringStack *stringStack);

/**
 * @brief push string on string stack
 *
 * @param [in]  stringStack string stack
 * @param [in]  string      string
 */

void string_stack_push(StringStack *stringStack, const gchar *string);

/**
 * @brief pop string from string stack
 *
 * @param [in]  stringStack string stack
 * @return      string
 */

void string_stack_pop(StringStack *stringStack);

/**
 * @brief clear string stack
 *
 * @param [in]  stringStack string stack
 */

void string_stack_clear(StringStack *stringStack);

/**
 * @brief get top string on stack or NULL
 *
 * @param [in]  stringStack string stack
 * @return      top string
 */

gchar *string_stack_top(StringStack *stringStack);

/**
 * @brief get absolute directory with current working directory or
 *        (sub-)directiry
 *
 * @param [in]  workingDirectory working directory
 * @param [in]  directory        absolute or relative directory
 * @return      absolute directory (when possible)
 */

gchar *getAbsoluteDirectory(const gchar *directory,
                            ...
                           );

// ---------------------------------------------------------------------

/**
 * @brief set indicator
 *
 * @param [in]  filePath       file path
 * @param [in]  lineNumber     line number [1..n]
 * @param [in]  color          indicator color
 * @param [in]  indicatorIndex indciator index
 */

void setIndicator(const gchar *filePath, guint lineNumber, const GdkRGBA *color, guint indicatorIndex);

/**
 * @brief clear indicator
 *
 * @param [in]  document       document
 * @param [in]  indicatorIndex indicator index
 */

void clearIndicator(GeanyDocument *document, guint indicatorIndex);

/**
 * @brief adjust tree view to show end
 *
 * @param [in]  data scrolled window widget
 */

gboolean adjustTreeViewToEnd(gpointer data);

/**
 * @brief show last line
 *
 * @param [in]  widget scrolled window widget
 */

void showLastLine(GtkScrolledWindow *widget);

/**
 * @brief add widget to box
 *
 * @param [in]  box    box
 * @param [in]  expand TRUE to fill+expand
 * @param [in]  widget widget to add
 * @return      widget
 */

GtkWidget *addBox(GtkBox *box, gboolean expand, GtkWidget *widget);

/**
 * @brief add widget to grid
 *
 * @param [in]  grid        grid
 * @param [in]  row, column row/column
 * @param [in]  columnSpan  column span [1..n]
 * @param [in]  widget      widget to add
 * @return      widget
 */

GtkWidget *addGrid(GtkGrid *grid, guint row, guint column, guint columnSpan, GtkWidget *widget);

/**
 * @brief add label widget
 *
 * @param [in]  rootObject  root object
 * @param [in]  name        property value name
 * @param [in]  text        entry label text
 * @param [in]  tooltipText entry tooltip text
 */

GtkWidget *newLabel(GObject     *rootObject,
                    const gchar *name,
                    const gchar *text,
                    const gchar *tooltipText
                   );

/**
 * @brief add text view widget
 *
 * @param [in]  rootObject  root object
 * @param [in]  name        property value name
 * @param [in]  text        entry label text
 * @param [in]  tooltipText entry tooltip text
 */

GtkWidget *newView(GObject     *rootObject,
                   const gchar *name,
                   const gchar *tooltipText
                  );

/**
 * @brief add check button widget
 *
 * @param [in]  rootObject  root object
 * @param [in]  name        property value name
 * @param [in]  text        entry label text
 * @param [in]  tooltipText entry tooltip text
 * @return      widget
 */

GtkWidget *newCheckButton(GObject     *rootObject,
                          const gchar *name,
                          const gchar *text,
                          const gchar *tooltipText
                         );

/**
 * @brief add radio button widget
 *
 * @param [in]  rootObject      root object
 * @param [in]  prevRadioButton previouos radio button or NULL
 * @param [in]  name            property value name
 * @param [in]  text            entry label text
 * @param [in]  tooltipText     entry tooltip text
 */

GtkWidget *newRadioButton(GObject     *rootObject,
                          GtkWidget   *prevRadioButton,
                          const gchar *name,
                          const gchar *text,
                          const gchar *tooltipText
                         );

/**
 * @brief add combo widget
 *
 * @param [in]  rootObject  root object
 * @param [in]  name        property value name
 * @param [in]  tooltipText entry tooltip text
 */

GtkWidget *newCombo(GObject     *rootObject,
                    const gchar *name,
                    const gchar *tooltipText
                   );

/**
 * @brief add combo widget with entry
 *
 * @param [in]  rootObject  root object
 * @param [in]  name        property value name
 * @param [in]  tooltipText entry tooltip text
 */

GtkWidget *newComboEntry(GObject     *rootObject,
                         const gchar *name,
                         const gchar *tooltipText
                        );

/**
 * @brief add text entry widget
 *
 * @param [in]  rootObject  root object
 * @param [in]  name        property value name
 * @param [in]  text        entry label text
 * @param [in]  tooltipText entry tooltip text
 */

GtkWidget *newEntry(GObject     *rootObject,
                    const gchar *name,
                    const gchar *tooltipText
                   );

/**
 * @brief add color chooser widget
 *
 * @param [in]  rootObject  root object
 * @param [in]  name        property value name
 * @param [in]  tooltipText entry tooltip text
 */

GtkWidget *newColorChooser(GObject     *rootObject,
                           const gchar *name,
                           const gchar *tooltipText
                          );

/**
 * @brief add color chooser widget
 *
 * @param [in]  rootObject  root object
 * @param [in]  entry       entry widget to get/set directory path
 * @param [in]  tooltipText entry tooltip text
 */

GtkWidget *newDirectoryChooser(GObject     *rootObject,
                               GtkEntry    *entry,
                               const gchar *tooltipText
                              );

/**
 * @brief add working directory chooser with directory select menu
 *
 * @param [in]  rootObject  root object
 * @param [in]  name        property value name
 * @param [in]  tooltipText entry tooltip text
 * @return      entry widget
 */

GtkWidget *newWorkingDirectoryChooser(GObject     *rootObject,
                                      const gchar *name,
                                      const gchar *tooltipText
                                     );

/**
 * @brief input text line dialog
 *
 * @param [in]  parentWindow parent window
 * @param [in]  title        dialog title
 * @param [in]  text         dialog entry text
 * @param [in]  tooltipText  dialog tooltip text
 * @param [in]  value        text entry value or NULL
 * @param [in]  validator    validator callback or NULL
 * @param [in]  data         user data
 * @param [out] string input string
 * @return      TRUE on "ok", FALSE otherwise
 */

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

/**
 * @brief expand macros %%, %d, %e, %f, %p, %t
 *
 * @param [in]  project    project or NULL
 * @param [in]  document   current document or NULL
 * @param [in]  template   template with macros to expand
 * @param [in]  customText custom text
 * @return      expanded string
 */

gchar *expandMacros(const GeanyProject  *project,
                    const GeanyDocument *document,
                    const gchar         *template,
                    const gchar *wrapperCommand,
                    const gchar         *customTarget
                   );

#endif // UTILS_H

/* end of file */
