/***********************************************************************\
*
* $Revision: 11537 $
* $Date: 2021-06-20 08:45:38 +0200 (Sun, 20 Jun 2021) $
* $Author: torsten $
* Contents: Geany builder plugin utility functions
* Systems: all
*
\***********************************************************************/

/****************************** Includes *******************************/
#include <stdlib.h>
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

#include "utils.h"

/****************** Conditional compilation switches *******************/

/***************************** Constants *******************************/

/***************************** Datatypes *******************************/

typedef void(*OutputHandler)(GString *line, GIOCondition ioCondition, gpointer userData);
typedef gboolean(*InputValidator)(const gchar *value, gpointer userData);

typedef GPtrArray StringStack;

/***************************** Variables *******************************/

/****************************** Macros *********************************/
#define LOCAL_INLINE static inline
#define UNUSED_VARIABLE(name) (void)name

#define ARRAY_SIZE(array) (sizeof(array)/sizeof(array[0]))

/***************************** Forwards ********************************/

/***************************** Functions *******************************/

gchar *stringEscape(const gchar *string, const char *toEscape, char escapeChar)
{
  GString *escapedString;

  g_assert(toEscape != NULL);

  escapedString = g_string_new(NULL);
  if (string != NULL)
  {
    while ((*string) != '\0')
    {
      if (   ((*string) == escapeChar)
          || (strchr(toEscape,*string) != NULL)
         )
      {
        g_string_append_c(escapedString, escapeChar);
      }
      g_string_append_c(escapedString, *string);
      string++;
    }
  }

  return g_string_free(escapedString,FALSE);
}

gchar *stringUnescape(const gchar *string, char escapeChar)
{
  g_assert(string != NULL);

  GString *unescapedString;

  unescapedString = g_string_new(NULL);
  while ((*string) != '\0')
  {
    if ((*string) == escapeChar)
    {
      string++;
      if ((*string) != '\0')
      {
        g_string_append_c(unescapedString, *string);
      }
    }
    else
    {
      g_string_append_c(unescapedString, *string);
    }
    string++;
  }

  return g_string_free(unescapedString,FALSE);
}

gchar **stringSplit(const gchar *string,
                    const gchar *delimiters,
                    char        escapeChar,
                    gint        maxTokens
                   )
{
  GPtrArray  *tokens;
  const char *tokenStart;
  char       *token;
  gint       n;

  g_assert(delimiters != NULL);
  g_assert(maxTokens >= 1);

  if (string != NULL)
  {
    tokens = g_ptr_array_new();
    g_assert(tokens != NULL);
    n      = 0;

    // get token 1..n-1
    tokenStart = string;
    while (   ((*string) != '\0')
           && (   (maxTokens == -1)
               || ((n+1) < maxTokens)
              )
          )
    {
      // get next token
      if      ((*string) == escapeChar)
      {
        string++;
        if ((*string) != '\0') string++;
      }
      else if (strchr(delimiters,*string) != NULL)
      {
        token = g_strndup(tokenStart, string-tokenStart);

        string++;
        tokenStart = string;

        g_ptr_array_add(tokens, token);
        n++;
      }
      else
      {
        string++;
      }
    }

    // get last token
    if ((*string) != '\0')
    {
      token = g_strdup(string);
      g_ptr_array_add(tokens, token);
    }
    g_ptr_array_add(tokens, NULL);

    return (gchar **)g_ptr_array_free(tokens, FALSE);
  }
  else
  {
    return NULL;
  }
}

gchar *getAbsolutePath(const gchar *directory,
                       const gchar *filePath
                      )
{
  g_assert(filePath != NULL);

  gchar *absoluteFilePath;
  if (g_path_is_absolute(filePath) || stringIsEmpty(directory))
  {
    absoluteFilePath = g_strdup(filePath);
  }
  else
  {
    absoluteFilePath = g_strconcat(directory, G_DIR_SEPARATOR_S, filePath, NULL);
  }

  return absoluteFilePath;
}

StringStack *string_stack_new()
{
  return g_ptr_array_new_with_free_func(g_free);
}

void string_stack_free(StringStack *stringStack)
{
  g_assert(stringStack != NULL);

  g_ptr_array_free(stringStack, TRUE);
}

void string_stack_push(StringStack *stringStack, const gchar *string)
{
  g_assert(stringStack != NULL);

  g_ptr_array_add(stringStack, g_strdup(string));
}

void string_stack_pop(StringStack *stringStack)
{
  g_assert(stringStack != NULL);

  if (stringStack->len > 0)
  {
    g_ptr_array_set_size(stringStack, stringStack->len-1);
  }
}

void string_stack_clear(StringStack *stringStack)
{
  g_assert(stringStack != NULL);

  g_ptr_array_set_size(stringStack, 0);
}

gchar *string_stack_top(StringStack *stringStack)
{
  g_assert(stringStack != NULL);

  if (stringStack->len > 0)
  {
    return (gchar*)stringStack->pdata[stringStack->len-1];
  }
  else
  {
    return NULL;
  }
}

gchar *getAbsoluteDirectory(const gchar *directory,
                            ...
                           )
{
  gchar **tokens;

  g_assert(directory != NULL);

  // get initial root (if possible)
  GString *root = g_string_new_len(directory, g_path_skip_root(directory)-directory);
  if (root->len <= 0)
  {
    #if   defined(G_OS_UNIX)
      g_string_assign(root, G_DIR_SEPARATOR_S);
    #elif defined(G_OS_WIN32)
      gchar *currentDirectory = g_get_current_dir();
      g_string_append_len(root, directory, g_path_skip_root(currentDirectory)-currentDirectory);
      g_free(currentDirectory);
    #else
      #error still not implemented
    #endif
  }

  GPtrArray *pathTokens = g_ptr_array_new_with_free_func(g_free);
  va_list arguments;
  va_start(arguments, directory);
  do
  {
    if (g_path_is_absolute(directory))
    {
      // get new root
      g_string_truncate(root, 0);
      g_string_append_len(root, directory, g_path_skip_root(directory)-directory);
      if (root->len <= 0)
      {
        #if   defined(G_OS_UNIX)
          g_string_assign(root, G_DIR_SEPARATOR_S);
        #elif defined(G_OS_WIN32)
          gchar *currentDirectory = g_get_current_dir();
          g_string_append_len(root, directory, g_path_skip_root(currentDirectory)-currentDirectory);
          g_free(currentDirectory);
        #else
          #error still not implemented
        #endif
      }

      // reset path
      g_ptr_array_set_size(pathTokens, 0);
    }

    tokens = g_strsplit(directory, G_DIR_SEPARATOR_S, -1);
    for (uint i = 0; tokens[i] != NULL; i++)
    {
      if      ((strcmp(tokens[i], "") == 0) || (strcmp(tokens[i], ".") == 0))
      {
        // skip
      }
      else if (strcmp(tokens[i], "..") == 0)
      {
        // remove previous directory token
        if (pathTokens->len > 0)
        {
          g_ptr_array_set_size(pathTokens, pathTokens->len-1);
        }
      }
      else
      {
        // append directory token
        g_ptr_array_add(pathTokens, g_strdup(tokens[i]));
      }
    }
    g_strfreev(tokens);

    directory = va_arg(arguments, const gchar*);
  }
  while (directory != NULL);
  va_end(arguments);
  g_ptr_array_add(pathTokens, NULL);

  // create absolute path
  gchar *path = g_build_pathv(G_DIR_SEPARATOR_S, (char**)pathTokens->pdata);
  gchar *absoluteDirectory = g_build_path(G_DIR_SEPARATOR_S, root->str, path, NULL);
  g_free(path);

  // free resources
  g_ptr_array_free(pathTokens, TRUE);

  return absoluteDirectory;
}

// ---------------------------------------------------------------------

/***********************************************************************\
* Name   : colorRGBAToBGR
* Purpose: convert RGBA color to 32bit Scintilla color BGR
* Input  : color - RGBA color
* Output : -
* Return : Scintilla color BGR
* Notes  : -
\***********************************************************************/

LOCAL_INLINE uint32_t colorRGBAToBGR(const GdkRGBA *color)
{
  g_assert(color != NULL);

  return   (((uint32_t)(color->red   * 255.0) & 0xFF) <<  0)
         | (((uint32_t)(color->green * 255.0) & 0xFF) <<  8)
         | (((uint32_t)(color->blue  * 255.0) & 0xFF) << 16);
}

void setIndicator(const gchar *filePath, guint lineNumber, const GdkRGBA *color, guint indicatorIndex)
{
  g_assert(filePath != NULL);
  g_assert(color != NULL);

  gchar *filePathLocale = utils_get_locale_from_utf8(filePath);
  GeanyDocument *document = document_find_by_filename(filePathLocale);
  if (document != NULL)
  {
    uint32_t color32 = colorRGBAToBGR(color);

    scintilla_send_message(document->editor->sci, SCI_INDICSETSTYLE, indicatorIndex, INDIC_SQUIGGLE);
    scintilla_send_message(document->editor->sci, SCI_INDICSETFORE, indicatorIndex, color32);
    editor_indicator_set_on_line(document->editor, indicatorIndex, (lineNumber > 0) ? lineNumber-1 : 0);
  }
  g_free(filePathLocale);
}

void clearIndicator(GeanyDocument *document, guint indicatorIndex)
{
  g_assert(document != NULL);

  editor_indicator_clear(document->editor, indicatorIndex);
}

gboolean adjustTreeViewToEnd(gpointer userData)
{
  GtkScrolledWindow *widget = (GtkScrolledWindow*)userData;

  g_assert(widget != NULL);

  GtkAdjustment *adjustment = gtk_scrolled_window_get_vadjustment(widget);
  g_assert(adjustment != NULL);
  gtk_adjustment_set_value(adjustment,
                           gtk_adjustment_get_upper(adjustment)+gtk_adjustment_get_page_size(adjustment)+gtk_adjustment_get_step_increment(adjustment)
                          );
  gtk_scrolled_window_set_vadjustment(widget, adjustment);

  return G_SOURCE_REMOVE;
}

void showLastLine(GtkScrolledWindow *widget)
{
  g_assert(widget != NULL);

  g_idle_add(adjustTreeViewToEnd, widget);
}

GtkWidget *addBox(GtkBox *box, gboolean expand, GtkWidget *widget)
{
  g_assert(box != NULL);
  g_assert(widget != NULL);

  gtk_box_pack_start(box, widget, expand, expand, 0);

  return widget;
}

GtkWidget *addGrid(GtkGrid *grid, guint row, guint column, guint columnSpan, GtkWidget *widget)
{
  g_assert(grid != NULL);
  g_assert(widget != NULL);

  gtk_grid_attach(grid, widget, column, row, columnSpan, 1);

  return widget;
}

GtkWidget *addTab(GtkWidget *notebook, const char *title)
{
  g_assert(notebook != NULL);
  g_assert(title != NULL);

  GtkWidget *label = gtk_label_new(title);
  g_assert(label != NULL);

  GtkBox *vbox = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 6));
  g_assert(vbox != NULL);

  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), GTK_WIDGET(vbox), label);

  return GTK_WIDGET(vbox);
}

GtkWidget *newLabel(GtkWidget   **widget,
                    GObject     *rootObject,
                    const gchar *name,
                    const gchar *text,
                    const gchar *tooltipText
                   )
{
  GtkWidget *label;

  label = gtk_label_new(text);
  gtk_widget_set_tooltip_text(label, tooltipText);
  //gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_LEFT);
  gtk_widget_set_halign(label, GTK_ALIGN_START);

  if (widget != NULL)
  {
    (*widget) = label;
  }
  if (name != NULL)
  {
    g_object_set_data(G_OBJECT(rootObject), name, label);
  }

  return label;
}

GtkWidget *newView(GtkWidget   **widget,
                   GObject     *rootObject,
                   const gchar *name,
                   const gchar *tooltipText
                  )
{
  GtkWidget *entry;

  entry = gtk_entry_new();
  g_assert(entry != NULL);
  gtk_widget_set_tooltip_text(entry, tooltipText);
  g_object_set(entry, "editable", FALSE, "can_focus", FALSE, NULL);
  gtk_widget_set_hexpand(entry, TRUE);

  if (widget != NULL)
  {
    (*widget) = entry;
  }
  if (name != NULL)
  {
    g_object_set_data(G_OBJECT(rootObject), name, entry);
  }

  return entry;
}

GtkWidget *newCheckButton(GtkWidget   **widget,
                          GObject     *rootObject,
                          const gchar *name,
                          const gchar *text,
                          const gchar *tooltipText
                         )
{
  GtkWidget *checkButton;

  checkButton = (text != NULL) ? gtk_check_button_new_with_label(text) : gtk_check_button_new();
  g_assert(checkButton != NULL);
  gtk_widget_set_tooltip_text(checkButton, tooltipText);
  gtk_widget_set_halign(checkButton, GTK_ALIGN_START);

  if (widget != NULL)
  {
    (*widget) = checkButton;
  }
  if (name != NULL)
  {
    g_object_set_data(G_OBJECT(rootObject), name, checkButton);
  }

  return checkButton;
}

GtkWidget *newRadioButton(GtkWidget   **widget,
                          GObject     *rootObject,
                          GtkWidget   *prevRadioButton,
                          const gchar *name,
                          const gchar *text,
                          const gchar *tooltipText
                         )
{
  GtkWidget *radioButton;

  radioButton = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(prevRadioButton), text);
  gtk_widget_set_tooltip_text(radioButton, tooltipText);
  gtk_widget_set_halign(radioButton, GTK_ALIGN_START);

  if (widget != NULL)
  {
    (*widget) = radioButton;
  }
  if (name != NULL)
  {
    g_object_set_data(G_OBJECT(rootObject), name, radioButton);
  }

  return radioButton;
}

GtkWidget *newSpinButton(GtkWidget   **widget,
                         GObject     *rootObject,
                         const gchar *name,
                         const gchar *tooltipText,
                         size_t      min,
                         size_t      max
                        )
{
  GtkWidget *entry;

  entry = gtk_spin_button_new_with_range((gdouble)min, (gdouble)max, 1.0);
  g_assert(entry != NULL);
  gtk_widget_set_tooltip_text(entry, tooltipText);
  gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(entry), TRUE);
  gtk_widget_set_hexpand(entry, FALSE);

  if (widget != NULL)
  {
    (*widget) = entry;
  }
  if (name != NULL)
  {
    g_object_set_data(G_OBJECT(rootObject), name, entry);
  }

  return entry;
}

GtkWidget *newCombo(GtkWidget   **widget,
                    GObject     *rootObject,
                    const gchar *name,
                    const gchar *tooltipText
                   )
{
  GtkWidget *entry;

  entry = gtk_combo_box_text_new();
  g_assert(entry != NULL);
  gtk_widget_set_tooltip_text(entry, tooltipText);
  gtk_widget_set_hexpand(entry, TRUE);

  if (widget != NULL)
  {
    (*widget) = entry;
  }
  if (name != NULL)
  {
    g_object_set_data(G_OBJECT(rootObject), name, entry);
  }

  return entry;
}

GtkWidget *newComboEntry(GtkWidget   **widget,
                         GObject     *rootObject,
                         const gchar *name,
                         const gchar *tooltipText
                        )
{
  GtkWidget *entry;

  entry = gtk_combo_box_text_new_with_entry();
  gtk_widget_set_tooltip_text(entry, tooltipText);
  gtk_widget_set_hexpand(entry, TRUE);

  if (widget != NULL)
  {
    (*widget) = entry;
  }
  if (name != NULL)
  {
    g_object_set_data(G_OBJECT(rootObject), name, entry);
  }

  return entry;
}

GtkWidget *newEntry(GtkWidget   **widget,
                    GObject     *rootObject,
                    const gchar *name,
                    const gchar *tooltipText
                   )
{
  GtkWidget *entry;

  entry = gtk_entry_new();
  g_assert(entry != NULL);
  gtk_widget_set_tooltip_text(entry, tooltipText);
  ui_entry_add_clear_icon(GTK_ENTRY(entry));
  gtk_widget_set_hexpand(entry, TRUE);

  if (widget != NULL)
  {
    (*widget) = entry;
  }
  if (name != NULL)
  {
    g_object_set_data(G_OBJECT(rootObject), name, entry);
  }

  return entry;
}

GtkWidget *newPasswordEntry(GtkWidget   **widget,
                            GObject     *rootObject,
                            const gchar *name,
                            const gchar *tooltipText
                           )
{
  GtkWidget *entry;

  entry = gtk_entry_new();
  g_assert(entry != NULL);
  gtk_entry_set_visibility(GTK_ENTRY(entry), FALSE);
  gtk_widget_set_tooltip_text(entry, tooltipText);
  ui_entry_add_clear_icon(GTK_ENTRY(entry));
  gtk_widget_set_hexpand(entry, TRUE);

  if (widget != NULL)
  {
    (*widget) = entry;
  }
  if (name != NULL)
  {
    g_object_set_data(G_OBJECT(rootObject), name, entry);
  }

  return entry;
}

GtkWidget *newColorChooser(GtkWidget   **widget,
                           GObject     *rootObject,
                           const gchar *name,
                           const gchar *tooltipText
                          )
{
  GtkWidget *colorChooser;

  colorChooser = gtk_color_button_new();
  g_assert(colorChooser != NULL);
//  colorChooser = gtk_color_button_new_with_rgba(&pluginData.configuration.errorIndicatorColor);
  gtk_widget_set_tooltip_text(colorChooser, tooltipText);
  gtk_widget_set_halign(colorChooser, GTK_ALIGN_START);

  if (widget != NULL)
  {
    (*widget) = colorChooser;
  }
  if (name != NULL)
  {
    g_object_set_data(G_OBJECT(rootObject), name, colorChooser);
  }

  return colorChooser;
}

/***********************************************************************\
* Name   : newFileChooserOnSelectDirectory
* Purpose: select directory
* Input  : widget   - widget
*          userData - user data: entry widget
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void newFileChooserOnSelectDirectory(GtkWidget *widget,
                                           gpointer  userData
                                          )
{
  GtkEntry *entry = GTK_ENTRY(userData);
  g_assert(entry != NULL);

  g_assert(widget != NULL);

  GtkWidget *dialog = gtk_file_chooser_dialog_new(_("Select file"),
                                                  GTK_WINDOW(gtk_widget_get_parent_window(GTK_WIDGET(widget))),
                                                  GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                                                  _("_Cancel"),
                                                  GTK_RESPONSE_CANCEL,
                                                  _("_Open"),
                                                  GTK_RESPONSE_ACCEPT,
                                                  NULL
                                                 );
  gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dialog), gtk_entry_get_text(GTK_ENTRY(entry)));
  gtk_file_chooser_set_create_folders(GTK_FILE_CHOOSER(dialog), TRUE);

  if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
  {
    gchar *directoryPath = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
    g_assert(directoryPath != NULL);
    gtk_entry_set_text(entry, directoryPath);
    g_free(directoryPath);
  }
  gtk_widget_destroy(dialog);
}

GtkWidget *newDirectoryChooser(GtkWidget   **widget,
                               GObject     *rootObject,
                               const gchar *name,
                               const gchar *tooltipText
                              )
{
// TODO: remove old
#if 0
  GtkWidget *directoryChooser;

  UNUSED_VARIABLE(rootObject);

  directoryChooser = gtk_file_chooser_button_new(_("Select a directory"),
                                                 GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER
                                                );
//  gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(directoryChooser), gtk_entry_get_text(entry));
  gtk_widget_set_tooltip_text(directoryChooser, tooltipText);
  gtk_widget_set_hexpand(directoryChooser, TRUE);

  if (widget != NULL)
  {
    (*widget) = directoryChooser;
  }
  if (name != NULL)
  {
    g_object_set_data(G_OBJECT(rootObject), name, directoryChooser);
  }

  return directoryChooser;
#else
  GtkWidget *entry;

  GtkWidget *subGrid = gtk_grid_new();
  g_assert(subGrid != NULL);
  gtk_grid_set_column_spacing(GTK_GRID(subGrid), 6);
  gtk_widget_set_hexpand(GTK_WIDGET(subGrid), TRUE);
  {
    entry = gtk_entry_new();
    g_assert(entry != NULL);
    gtk_widget_set_tooltip_text(entry, tooltipText);
    ui_entry_add_clear_icon(GTK_ENTRY(entry));
    gtk_widget_set_hexpand(entry, TRUE);
    addGrid(GTK_GRID(subGrid), 0, 0, 1, entry);

    GtkWidget *button = gtk_button_new();
    gtk_button_set_image(GTK_BUTTON(button), gtk_image_new_from_icon_name("document-open",0));
    gtk_widget_set_tooltip_text(button, tooltipText);
    gtk_widget_set_halign(button, GTK_ALIGN_START);
    addGrid(GTK_GRID(subGrid), 0, 1, 1, button);
    plugin_signal_connect(geany_plugin,
                          G_OBJECT(button),
                          "clicked",
                          FALSE,
                          G_CALLBACK(newFileChooserOnSelectDirectory),
                          entry
                         );
  }
  gtk_widget_show_all(subGrid);

  if (widget != NULL)
  {
    (*widget) = entry;
  }
  if (name != NULL)
  {
    g_object_set_data(G_OBJECT(rootObject), name, entry);
  }

  return subGrid;
#endif
}

/***********************************************************************\
* Name   : newFileChooserOnSelectFile
* Purpose: select file
* Input  : widget   - widget
*          userData - user data: entry widget
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void newFileChooserOnSelectFile(GtkWidget *widget,
                                      gpointer  userData
                                     )
{
  GtkEntry *entry = GTK_ENTRY(userData);
  g_assert(entry != NULL);

  g_assert(widget != NULL);

  GtkWidget *dialog = gtk_file_chooser_dialog_new(_("Select file"),
                                                  GTK_WINDOW(gtk_widget_get_parent_window(GTK_WIDGET(widget))),
                                                  GTK_FILE_CHOOSER_ACTION_OPEN,
                                                  _("_Cancel"),
                                                  GTK_RESPONSE_CANCEL,
                                                  _("_Open"),
                                                  GTK_RESPONSE_ACCEPT,
                                                  NULL
                                                 );
  gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dialog), gtk_entry_get_text(GTK_ENTRY(entry)));
  gtk_file_chooser_set_create_folders(GTK_FILE_CHOOSER(dialog), TRUE);

  if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
  {
    gchar *filePath = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
    g_assert(filePath != NULL);
    gtk_entry_set_text(entry, filePath);
    g_free(filePath);
  }
  gtk_widget_destroy(dialog);
}

GtkWidget *newFileChooser(GtkWidget   **widget,
                          GObject     *rootObject,
                          const gchar *name,
                          const gchar *tooltipText
                         )
{
// TODO: remove old
#if 0
  GtkWidget *fileChooser;

  UNUSED_VARIABLE(rootObject);

  fileChooser = gtk_file_chooser_button_new(_("Select a file"),
                                            GTK_FILE_CHOOSER_ACTION_OPEN
                                           );
//  gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(fileChooser), gtk_entry_get_text(entry));
  gtk_widget_set_tooltip_text(fileChooser, tooltipText);
  gtk_widget_set_hexpand(fileChooser, TRUE);

  if (widget != NULL)
  {
    (*widget) = fileChooser;
  }
  if (name != NULL)
  {
    g_object_set_data(G_OBJECT(rootObject), name, fileChooser);
  }

  return fileChooser;
#else
  GtkWidget *entry;

  GtkWidget *subGrid = gtk_grid_new();
  g_assert(subGrid != NULL);
  gtk_grid_set_column_spacing(GTK_GRID(subGrid), 6);
  gtk_widget_set_hexpand(GTK_WIDGET(subGrid), TRUE);
  {
    entry = gtk_entry_new();
    g_assert(entry != NULL);
    gtk_widget_set_tooltip_text(entry, tooltipText);
    ui_entry_add_clear_icon(GTK_ENTRY(entry));
    gtk_widget_set_hexpand(entry, TRUE);
    addGrid(GTK_GRID(subGrid), 0, 0, 1, entry);

    GtkWidget *button = gtk_button_new();
    gtk_button_set_image(GTK_BUTTON(button), gtk_image_new_from_icon_name("document-open",0));
    gtk_widget_set_tooltip_text(button, tooltipText);
    gtk_widget_set_halign(button, GTK_ALIGN_START);
    addGrid(GTK_GRID(subGrid), 0, 1, 1, button);
    plugin_signal_connect(geany_plugin,
                          G_OBJECT(button),
                          "clicked",
                          FALSE,
                          G_CALLBACK(newFileChooserOnSelectFile),
                          entry
                         );
  }
  gtk_widget_show_all(subGrid);

  if (widget != NULL)
  {
    (*widget) = entry;
  }
  if (name != NULL)
  {
    g_object_set_data(G_OBJECT(rootObject), name, entry);
  }

  return subGrid;
#endif
}

/***********************************************************************\
* Name   : newDirectoryToolMenuOnPopupMenu
* Purpose: open directory selector popup menu
* Input  : button   - button widget
*          userData - user data: popup menu to open
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void newDirectoryToolMenuOnPopupMenu(GtkButton *button, gpointer userData)
{
  GtkMenu *menu = GTK_MENU(userData);
  g_assert(menu != NULL);

  g_assert(button != NULL);
  g_assert(menu != NULL);

  gtk_menu_popup_at_widget(menu, GTK_WIDGET(button), GDK_GRAVITY_SOUTH_EAST, GDK_GRAVITY_NORTH_EAST, NULL);
}

/***********************************************************************\
* Name   : newDirectoryToolMenuOnSelectDirectory
* Purpose: select directory
* Input  : widget   - widget
*          userData - user data: entry widget
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void newDirectoryToolMenuOnSelectDirectory(GtkWidget *widget,
                                                 gpointer  userData
                                                )
{
  GtkEntry *entry = GTK_ENTRY(userData);
  g_assert(entry != NULL);

  g_assert(widget != NULL);

  GtkWidget *dialog = gtk_file_chooser_dialog_new(_("Select directory"),
                                                  GTK_WINDOW(gtk_widget_get_parent_window(GTK_WIDGET(widget))),
                                                  GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                                                  _("_Cancel"),
                                                  GTK_RESPONSE_CANCEL,
                                                  _("_Open"),
                                                  GTK_RESPONSE_ACCEPT,
                                                  NULL
                                                 );
  gtk_file_chooser_set_create_folders(GTK_FILE_CHOOSER(dialog), TRUE);

  if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
  {
    gchar *filePath = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
    g_assert(filePath != NULL);
    gtk_entry_set_text(entry, filePath);
    g_free(filePath);
  }
  gtk_widget_destroy(dialog);
}

/***********************************************************************\
* Name   : newDirectoryToolMenuOnSelectDirectory
* Purpose: current file directory
* Input  : widget   - widget (not used)
*          userData - user data: entry widget
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void newDirectoryToolMenuOnCurrentFileDirectory(GtkWidget *widget,
                                                      gpointer  userData
                                                     )
{
  GtkEntry *entry = GTK_ENTRY(userData);
  g_assert(entry != NULL);

  UNUSED_VARIABLE(widget);

  gtk_entry_set_text(entry, "%d");
}

/***********************************************************************\
* Name   : newDirectoryToolMenuOnCurrentFileBase
* Purpose: curent file basename
* Input  : menuItem - button widget
*          userData - user data: entry widget
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void newDirectoryToolMenuOnCurrentFileBase(GtkMenuItem *menuItem,
                                                 gpointer    userData
                                                )
{
  GtkEntry *entry = GTK_ENTRY(userData);
  g_assert(entry != NULL);

  UNUSED_VARIABLE(menuItem);

  gtk_entry_set_text(entry, "%e");
}

/***********************************************************************\
* Name   : newDirectoryToolMenuOnCurrentFile
* Purpose: current file name
* Input  : menuItem - button widget
*          userData - user data: entry widget
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void newDirectoryToolMenuOnCurrentFile(GtkMenuItem *menuItem,
                                             gpointer    userData
                                            )
{
  GtkEntry *entry = GTK_ENTRY(userData);
  g_assert(entry != NULL);

  UNUSED_VARIABLE(menuItem);

  gtk_entry_set_text(entry, "%f");
}

/***********************************************************************\
* Name   : newDirectoryToolMenuOnSelectDirectory
* Purpose: project base directory
* Input  : widget   - widget (not used)
*          userData - user data: entry widget
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void newDirectoryToolMenuOnProjectDirectory(GtkWidget *widget,
                                                  gpointer  userData
                                                 )
{
  GtkEntry *entry = GTK_ENTRY(userData);
  g_assert(entry != NULL);

  UNUSED_VARIABLE(widget);

  gtk_entry_set_text(entry, "%p");
}

/***********************************************************************\
* Name   : newDirectoryToolMenuOnCurrentLineNumber
* Purpose: current line number
* Input  : menuItem - button widget
*          userData - user data: entry widget
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void newDirectoryToolMenuOnCurrentLineNumber(GtkMenuItem *menuItem,
                                                   gpointer    userData
                                                  )
{
  GtkEntry *entry = GTK_ENTRY(userData);
  g_assert(entry != NULL);

  UNUSED_VARIABLE(menuItem);

  gtk_entry_set_text(entry, "%l");
}

/***********************************************************************\
* Name   : newDirectoryToolMenuOnText
* Purpose: text
* Input  : menuItem - button widget
*          userData - user data: entry widget
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void newDirectoryToolMenuOnText(GtkMenuItem *menuItem,
                                      gpointer    userData
                                     )
{
  GtkEntry *entry = GTK_ENTRY(userData);
  g_assert(entry != NULL);

  UNUSED_VARIABLE(menuItem);

  gtk_entry_set_text(entry, "%t");
}

/***********************************************************************\
* Name   : newDirectoryToolMenu
* Purpose: add directory select menu widget
* Input  : rootObject  - root object
*          grid        - grid container widget
*          row, column - row/column in grid
*          name        - property value name
*          text        - entry label text
*          tooltipText - entry tooltip text
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL GtkWidget *newDirectoryToolMenu(GtkWidget   **widget,
                                      GObject     *rootObject,
                                      GtkEntry    *entry,
                                      const gchar *tooltipText
                                     )
{
  g_assert(rootObject != NULL);
  g_assert(entry != NULL);

  UNUSED_VARIABLE(rootObject);
  UNUSED_VARIABLE(entry);

  GtkWidget *button = gtk_button_new();
  g_assert(button != NULL);
  gtk_widget_set_tooltip_text(button, tooltipText);
  gtk_button_set_image(GTK_BUTTON(button), gtk_image_new_from_icon_name("folder", GTK_ICON_SIZE_BUTTON));
  {
    GtkWidget *menu = gtk_menu_new();
    g_assert(menu != NULL);
    {
      GtkWidget *menuItem;

      menuItem = gtk_menu_item_new_with_label("Select");
      gtk_container_add(GTK_CONTAINER(menu), menuItem);
      plugin_signal_connect(geany_plugin,
                            G_OBJECT(menuItem),
                            "activate",
                            FALSE,
                            G_CALLBACK(newDirectoryToolMenuOnSelectDirectory),
                            entry
                           );

      menuItem = gtk_menu_item_new_with_label("Current file directory");
      gtk_container_add(GTK_CONTAINER(menu), menuItem);
      plugin_signal_connect(geany_plugin,
                            G_OBJECT(menuItem),
                            "activate",
                            FALSE,
                            G_CALLBACK(newDirectoryToolMenuOnCurrentFileDirectory),
                            entry
                           );

      menuItem = gtk_menu_item_new_with_label("Current file base");
      gtk_container_add(GTK_CONTAINER(menu), menuItem);
      plugin_signal_connect(geany_plugin,
                            G_OBJECT(menuItem),
                            "activate",
                            FALSE,
                            G_CALLBACK(newDirectoryToolMenuOnCurrentFileBase),
                            entry
                           );

      menuItem = gtk_menu_item_new_with_label("Current file");
      gtk_container_add(GTK_CONTAINER(menu), menuItem);
      plugin_signal_connect(geany_plugin,
                            G_OBJECT(menuItem),
                            "activate",
                            FALSE,
                            G_CALLBACK(newDirectoryToolMenuOnCurrentFile),
                            entry
                           );

      menuItem = gtk_menu_item_new_with_label("Project directory");
      gtk_container_add(GTK_CONTAINER(menu), menuItem);
      plugin_signal_connect(geany_plugin,
                            G_OBJECT(menuItem),
                            "activate",
                            FALSE,
                            G_CALLBACK(newDirectoryToolMenuOnProjectDirectory),
                            entry
                           );

      menuItem = gtk_menu_item_new_with_label("Current line number");
      gtk_container_add(GTK_CONTAINER(menu), menuItem);
      plugin_signal_connect(geany_plugin,
                            G_OBJECT(menuItem),
                            "activate",
                            FALSE,
                            G_CALLBACK(newDirectoryToolMenuOnCurrentLineNumber),
                            entry
                           );

      menuItem = gtk_menu_item_new_with_label("Text");
      gtk_container_add(GTK_CONTAINER(menu), menuItem);
      plugin_signal_connect(geany_plugin,
                            G_OBJECT(menuItem),
                            "activate",
                            FALSE,
                            G_CALLBACK(newDirectoryToolMenuOnText),
                            entry
                           );
    }
    gtk_widget_show_all(menu);

    plugin_signal_connect(geany_plugin,
                          G_OBJECT(button),
                          "clicked",
                          FALSE,
                          G_CALLBACK(newDirectoryToolMenuOnPopupMenu),
                          menu
                         );
  }
  gtk_widget_show_all(button);

  if (widget != NULL)
  {
    (*widget) = button;
  }

  return button;
}

GtkWidget *newWorkingDirectoryChooser(GtkWidget   **widget,
                                      GObject     *rootObject,
                                      const gchar *name,
                                      const gchar *tooltipText
                                     )
{
  GtkWidget *entry;

  GtkWidget *subGrid = gtk_grid_new();
  g_assert(subGrid != NULL);
  gtk_grid_set_column_spacing(GTK_GRID(subGrid), 12);
  gtk_widget_set_hexpand(GTK_WIDGET(subGrid), TRUE);
  {
    entry = gtk_entry_new();
    g_assert(entry != NULL);
    gtk_widget_set_tooltip_text(entry, tooltipText);
//  gtk_entry_set_max_length (GTK_ENTRY (entry), 50);
    ui_entry_add_clear_icon(GTK_ENTRY(entry));
    gtk_widget_set_hexpand(entry, TRUE);
    addGrid(GTK_GRID(subGrid), 0, 0, 1, entry);
    addGrid(GTK_GRID(subGrid), 0, 1, 1, newDirectoryToolMenu(NULL, rootObject, GTK_ENTRY(entry), _("Select working directory.")));
  }
  gtk_widget_show_all(subGrid);

  if (widget != NULL)
  {
    (*widget) = entry;
  }
  if (name != NULL)
  {
    g_object_set_data(G_OBJECT(rootObject), name, entry);
  }

  return subGrid;
}

/***********************************************************************\
* Name   : onInputDialogChanged
* Purpose: input entry text changed callback
* Input  : entry    - entry
*          userData - user data: ok-button widget
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onInputDialogChanged(GtkEntry *entry,
                                gpointer userData
                               )
{
  InputValidator validator     = (InputValidator)g_object_get_data(G_OBJECT(entry), "validator_function");
  g_assert(validator != NULL);
  gpointer       validatorData = g_object_get_data(G_OBJECT(entry), "validator_data");
  GtkWidget      *okButton     = GTK_WIDGET(userData);
  g_assert(okButton != NULL);

  // call validator and enable/disable ok-button
  gtk_widget_set_sensitive(okButton,
                           validator(gtk_entry_get_text(entry), validatorData)
                          );
}

gboolean inputDialog(GtkWindow      *parentWindow,
                     const gchar    *title,
                     const char     *text,
                     const gchar    *tooltipText,
                     const gchar    *value,
                     InputValidator validator,
                     gpointer       userData,
                     GString        *string
                    )
{
  GtkWidget *entry, *okButton;

  g_assert(parentWindow != NULL);
  g_assert(title != NULL);
  g_assert(text != NULL);
  g_assert(string != NULL);

  // create dialog
  GtkWidget *dialog = gtk_dialog_new_with_buttons(title,
                                                  parentWindow,
                                                  GTK_DIALOG_DESTROY_WITH_PARENT,
                                                  _("_Cancel"),
                                                  GTK_RESPONSE_CANCEL,
                                                  NULL
                                                 );
//  gtk_box_set_spacing(GTK_BOX(vbox), 6);
  okButton = gtk_dialog_add_button(GTK_DIALOG(dialog), _("_OK"), GTK_RESPONSE_ACCEPT);

  GtkBox *vbox = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 6));
  gtk_widget_set_margin_top(GTK_WIDGET(vbox), 6);
  gtk_widget_set_margin_bottom(GTK_WIDGET(vbox), 6);
  {
    GtkGrid *grid = GTK_GRID(gtk_grid_new());
    g_assert(grid != NULL);
    gtk_grid_set_column_spacing(grid, 12);
    gtk_widget_set_hexpand(GTK_WIDGET(grid), TRUE);
    g_object_set(GTK_GRID(grid), "margin", 6, NULL);
    {
      addGrid(grid, 0, 0, 1, newLabel(NULL, G_OBJECT(dialog), NULL, text, tooltipText));
      addGrid(grid, 0, 1, 1, newEntry(&entry, G_OBJECT(dialog), "entry", tooltipText));

      if (value != NULL)
      {
        gtk_entry_set_text(GTK_ENTRY(entry), value);
      }
      if (validator != NULL)
      {
        g_object_set_data(G_OBJECT(entry), "validator_function", validator);
        g_object_set_data(G_OBJECT(entry), "validator_data", userData);
        plugin_signal_connect(geany_plugin,
                              G_OBJECT(entry),
                              "changed",
                              FALSE,
                              G_CALLBACK(onInputDialogChanged),
                              okButton
                             );
      }
    }
    addBox(vbox, FALSE, GTK_WIDGET(grid));
  }
  addBox(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), TRUE, GTK_WIDGET(vbox));
  gtk_widget_show_all(dialog);

  // initially validate
  if (validator != NULL)
  {
    gtk_widget_set_sensitive(okButton,
                             validator(gtk_entry_get_text(GTK_ENTRY(entry)),userData)
                            );
  }

  // run dialog
  gint result = gtk_dialog_run(GTK_DIALOG(dialog));
  if (result == GTK_RESPONSE_ACCEPT)
  {
    g_string_assign(string, gtk_entry_get_text(GTK_ENTRY(entry)));
  }

  // free resources
  gtk_widget_destroy(dialog);

  return (result == GTK_RESPONSE_ACCEPT);
}

// ---------------------------------------------------------------------

gchar *expandMacros(const GeanyProject  *project,
                    const GeanyDocument *document,
                    const gchar         *template,
                    const gchar         *customTarget
                   )
{
  gboolean customTargetSet = FALSE;
  gchar    *result;

  GString *expandedString = g_string_new(NULL);
  if (template != NULL)
  {
    while ((*template) != '\0')
    {
      if ((*template) == '%')
      {
        template++;
        switch (*template)
        {
          case '%':
            // insert single %
            g_string_append_c(expandedString,*template);
            template++;
            break;
          case 'd':
            // insert directory of current document
            if (document != NULL)
            {
              gchar *directory = g_path_get_dirname(document->file_name);
              g_string_append(expandedString, directory);
              g_free(directory);
            }
            template++;
            break;
          case 'e':
            // insert current document base name (without extension)
            if (document != NULL)
            {
              gchar *executableName = utils_remove_ext_from_filename(document->file_name);
              gchar *baseName = g_path_get_basename(executableName);
              g_string_append(expandedString, baseName);
              g_free(baseName);
              g_free(executableName);
            }
            template++;
            break;
          case 'f':
            // insert current document name
            if (document != NULL)
            {
              gchar *baseName = g_path_get_basename(document->file_name);
              g_string_append(expandedString, baseName);
              g_free(baseName);
            }
            template++;
            break;
          case 'p':
            // insert project path
            if (project != NULL)
            {
              if (g_path_is_absolute(project->base_path))
              {
                g_string_append(expandedString, project->base_path);
              }
              else
              {
                gchar *directoryPath = g_path_get_dirname(project->file_name);
                gchar *basePath      = getAbsoluteDirectory(directoryPath, project->base_path, NULL);
                g_string_append(expandedString, basePath);
                g_free(basePath);
                g_free(directoryPath);
              }
            }
            template++;
            break;
          case 'l':
            // insert current line number
            if (document != NULL)
            {
              gint lineNumber = sci_get_current_line(document->editor->sci) + 1;
              g_string_append_printf(expandedString, "%d", lineNumber);
            }
            template++;
            break;
          case 't':
            // insert custom text
            if (customTarget != NULL)
            {
              g_string_append(expandedString, customTarget);
              customTargetSet = TRUE;
            }
            template++;
            break;
          default:
            g_string_append_c(expandedString, *template);
            template++;
            break;
        }
      }
      else
      {
        g_string_append_c(expandedString, *template);
        template++;
      }
    }
    if ((customTarget != NULL) && !customTargetSet)
    {
      g_string_append(expandedString, customTarget);
    }
  }

  // get result
  result = expandedString->str;

  // free resources
  g_string_free(expandedString,FALSE);

  return result;
}

/* end of file */
