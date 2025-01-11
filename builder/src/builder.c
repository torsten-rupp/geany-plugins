/***********************************************************************\
*
* $Revision: 11537 $
* $Date: 2021-06-20 08:45:38 +0200 (Sun, 20 Jun 2021) $
* $Author: torsten $
* Contents: Geany builder plugin
* Systems: all
*
\***********************************************************************/

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

#include "utils.h"
#include "remote.h"
#include "execute.h"

#include "builder.h"

/****************** Conditional compilation switches *******************/

/***************************** Constants *******************************/
const gchar   *VERSION                              = "1.0";

const gchar   *CONFIGURATION_GROUP_BUILDER          = "builder";

const gchar   *KEY_GROUP_BUILDER                    = "builder";

#define MAX_COMMANDS                                   10
const gchar   *DEFAULT_BUILD_COMMAND                = "make";
const gchar   *DEFAULT_CLEAN_COMMAND                = "make clean";

const GdkRGBA DEFAULT_ERROR_INDICATOR_COLOR         = {0xFF,0x00,0x00,0xFF};
const GdkRGBA DEFAULT_WARNING_INDICATOR_COLOR       = {0x00,0xFF,0x00,0xFF};

const guint   ERROR_INDICATOR_INDEX                 = GEANY_INDICATOR_ERROR;
const guint   WARNING_INDICATOR_INDEX               = 1;

const guint   MAX_ERROR_WARNING_INDICATORS          = 16;

const gchar   *COLOR_BUILD_INFO                     = "Blue";
const gchar   *COLOR_BUILD_ERROR                    = "Red";
const gchar   *COLOR_BUILD_MESSAGES                 = "Black";
const gchar   *COLOR_BUILD_MESSAGES_MATCHED_ERROR   = "Magenta";
const gchar   *COLOR_BUILD_MESSAGES_MATCHED_WARNING = "Green";

// command list columns
enum
{
  MODEL_COMMAND_TITLE,
  MODEL_COMMAND_COMMAND_LINE,
  MODEL_COMMAND_WORKING_DIRECTORY,
  MODEL_COMMAND_SHOW_BUTTON,
  MODEL_COMMAND_SHOW_MENU_ITEM,
  MODEL_COMMAND_INPUT_CUSTOM_TEXT,
  MODEL_COMMAND_RUN_IN_DOCKER_CONTAINER,
  MODEL_COMMAND_RUN_REMOTE,
  MODEL_COMMAND_PARSE_OUTPUT,

  MODEL_COMMAND_COUNT
};

// regular expression list columns
enum
{
  MODEL_REGEX_LANGUAGE,
  MODEL_REGEX_GROUP,
  MODEL_REGEX_TYPE,
  MODEL_REGEX_REGEX,

  MODEL_REGEX_COUNT
};

// message columns
enum
{
  MODEL_MESSAGE_COLOR,
  MODEL_MESSAGE_TREE_PATH,

  MODEL_MESSAGE_DIRECTORY,
  MODEL_MESSAGE_FILE_PATH,
  MODEL_MESSAGE_LINE_NUMBER,
  MODEL_MESSAGE_COLUMN_NUMBER,
  MODEL_MESSAGE_MESSAGE,

  MODEL_MESSAGE_COUNT
};

// build error/warning columns
enum
{
  MODEL_ERROR_WARNING_TREE_PATH,

  MODEL_ERROR_WARNING_DIRECTORY,
  MODEL_ERROR_WARNING_FILE_PATH,
  MODEL_ERROR_WARNING_LINE_NUMBER,
  MODEL_ERROR_WARNING_COLUMN_NUMBER,
  MODEL_ERROR_WARNING_MESSAGE,

  MODEL_ERROR_WARNING_COUNT
};

// attach docker container columns
enum
{
  MODEL_ATTACH_DOCKER_CONTAINER_ID,
  MODEL_ATTACH_DOCKER_CONTAINER_IMAGE,

  MODEL_ATTACH_DOCKER_CONTAINER_COUNT
};

const int MODEL_END = -1;

/***************************** Datatypes *******************************/

// key short-cuts
typedef enum
{
  KEY_BINDING_BUILD,
  KEY_BINDING_ABORT,
  KEY_BINDING_PREV_ERROR,
  KEY_BINDING_NEXT_ERROR,
  KEY_BINDING_PREV_WARNING,
  KEY_BINDING_NEXT_WARNING,
  KEY_BINDING_PROJECT_PROPERTIES,
  KEY_BINDING_PLUGIN_CONFIGURATION,

  KEY_BINDING_COUNT
} KeyBindings;

typedef void(*OutputHandler)(GString *line, GIOCondition ioCondition, gpointer data);
typedef gboolean(*InputValidator)(const gchar *value, gpointer data);

typedef enum
{
  REGEX_TYPE_NONE,
  REGEX_TYPE_ENTER,
  REGEX_TYPE_LEAVE,
  REGEX_TYPE_ERROR,
  REGEX_TYPE_WARNING,
  REGEX_TYPE_EXTENSION
} RegexTypes;
RegexTypes REGEX_TYPE_MIN = REGEX_TYPE_NONE;
RegexTypes REGEX_TYPE_MAX = REGEX_TYPE_EXTENSION;

const gchar *REGEX_TYPE_STRINGS[] =
{
  [REGEX_TYPE_NONE     ] = "none",
  [REGEX_TYPE_ENTER    ] = "enter",
  [REGEX_TYPE_LEAVE    ] = "leave",
  [REGEX_TYPE_ERROR    ] = "error",
  [REGEX_TYPE_WARNING  ] = "warning",
  [REGEX_TYPE_EXTENSION] = "extension"
};

#include "regex-list.c"

/***************************** Variables *******************************/

// global Geany data
GeanyPlugin *geany_plugin;
GeanyData   *geany_data;

// plugin data
LOCAL struct
{
  GtkListStore *builtInRegExStore;

  // plugin configuration
  struct
  {
    gchar        *filePath;

    GtkListStore *commandStore;
    GtkListStore *regexStore;

    gboolean     errorIndicators;
    GdkRGBA      errorIndicatorColor;
    gboolean     warningIndicators;
    GdkRGBA      warningIndicatorColor;

    gboolean     addProjectRegExResults;
    gboolean     abortButton;
    gboolean     autoSaveAll;
    gboolean     autoShowFirstError;
    gboolean     autoShowFirstWarning;
  } configuration;

  // project specific properties
  struct
  {
    gchar        *filePath;

    GtkListStore *commandStore;
    GString      *errorRegEx;
    GString      *warningRegEx;

    struct
    {
      GString *hostName;
      guint   hostPort;
      GString *userName;
      GString *publicKey;
      GString *privateKey;
      GString *password;
    } remote;
  } projectProperties;

  // widgets
  struct
  {
    struct
    {
      GtkWidget *commands[MAX_COMMANDS];
      GtkWidget *projectCommands[MAX_COMMANDS];
      GtkWidget *abort;

      GtkWidget *showPrevError;
      GtkWidget *showNextError;
      GtkWidget *showPrevWarning;
      GtkWidget *showNextWarning;
      GtkWidget *dockerContainer;
      GtkWidget *remote;
      GtkWidget *projectProperties;
      GtkWidget *configuration;

      GtkWidget *editRegex;
    } menuItems;

    struct
    {
      GtkToolItem *commands[MAX_COMMANDS];
      GtkToolItem *projectCommands[MAX_COMMANDS];
      GtkToolItem *abort;
    } buttons;

    GtkWidget   *tabs;
    gint        tabIndex;

    GtkBox      *projectProperties;
    gint        projectPropertiesTabIndex;
    gboolean    showProjectPropertiesTab;

    GtkWidget   *messagesTab;
    guint       messagesTabIndex;
    GtkWidget   *messagesList;

    GtkWidget   *errorsTab;
    GtkWidget   *errorsTabLabel;
    guint       errorsTabIndex;
    GtkWidget   *errorsTree;

    GtkWidget   *warningsTab;
    GtkWidget   *warningsTabLabel;
    guint       warningsTabIndex;
    GtkWidget   *warningsTree;

    guint       disableCounter;
  } widgets;

  // attached docker container id
  const gchar *attachedDockerContainerId;

  // build results
  struct
  {
    GtkListStore *lastCommandListStore;
    GString      *lastCommandIteratorString;

    StringStack  *directoryPrefixStack;

    gboolean     showedFirstErrorWarning;

    GtkListStore *messagesStore;
    GtkTreeIter  messageTreeIterator;
    GtkTreePath  *messagesTreePath;
    guint        messageCount;

    GtkTreeStore *errorsStore;
    GtkTreeIter  errorsTreeIterator;
    gboolean     errorsTreeIterorValid;

    GtkTreeStore *warningsStore;
    GtkTreeIter  warningsTreeIterator;
    gboolean     warningsTreeIterorValid;

    GtkTreeIter  insertIterator;
    GtkTreeStore *lastErrorsWarningsInsertStore;
    GtkTreeIter  *lastErrorsWarningsInsertTreeIterator;
    const gchar  *lastErrorsWarningsInsertColor;
    guint        errorWarningIndicatorsCount;
  } build;
} pluginData;

/****************************** Macros *********************************/

/***************************** Forwards ********************************/

LOCAL void updateKeyBinding(GeanyPlugin *plugin, GtkWidget *buildMenuItem);
LOCAL void updateToolbarMenuItems();
LOCAL void updateEnableToolbarButtons();
LOCAL void updateEnableToolbarMenuItems();

/***************************** Functions *******************************/

/***********************************************************************\
* Name   : configurationLoadBoolean
* Purpose: load boolean value from configuration
* Input  : configuration - configuration to load values from
*          value         - value variable
*          name          - value name
* Output : value - value
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void configurationLoadBoolean(GKeyFile    *configuration,
                                    gboolean    *value,
                                    const gchar *name
                                   )
{
  g_assert(configuration != NULL);
  g_assert(value != NULL);
  g_assert(name != NULL);

  (*value) = g_key_file_get_boolean(configuration, CONFIGURATION_GROUP_BUILDER, name, NULL);
}

/***********************************************************************\
* Name   : configurationLoadInteger
* Purpose: load integer value from configuration
* Input  : configuration - configuration to load values from
*          value         - value variable
*          name          - value name
* Output : value - value
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void configurationLoadInteger(GKeyFile    *configuration,
                                    gint        *value,
                                    const gchar *name
                                   )
{
  g_assert(configuration != NULL);
  g_assert(value != NULL);
  g_assert(name != NULL);

  (*value) = g_key_file_get_integer(configuration, CONFIGURATION_GROUP_BUILDER, name, NULL);
}
/***********************************************************************\
* Name   : configurationStringUpdate
* Purpose: load string value from configuration
* Input  : configuration - configuration to load values from
*          value         - value variable
*          name          - value name
* Output : value - value
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void configurationLoadString(GKeyFile    *configuration,
                                   GString     *value,
                                   const gchar *name
                                  )
{
  g_assert(configuration != NULL);
  g_assert(value != NULL);
  g_assert(name != NULL);

  const gchar *data = g_key_file_get_string(configuration, CONFIGURATION_GROUP_BUILDER, name, NULL);
  g_string_assign(value, (data != NULL) ? data : "");
}

/***********************************************************************\
* Name   : configurationLoadCommand
* Purpose: load command from configuration
* Input  : configuration - configuration to load values from
*          listStore     - list store
*          name          - value name
* Output : -
* Return : TRUE iff loaded
* Notes  : -
\***********************************************************************/

LOCAL gboolean configurationLoadCommand(GKeyFile     *configuration,
                                        GtkListStore *listStore,
                                        const gchar  *name
                                       )
{
  g_assert(configuration != NULL);
  g_assert(listStore != NULL);
  g_assert(name != NULL);

  gboolean result = FALSE;

  gchar *string = g_key_file_get_string(configuration, CONFIGURATION_GROUP_BUILDER, name, NULL);
  if (string != NULL)
  {
    gchar **tokens   = stringSplit(string, ":", '\\', 9);
    g_assert(tokens != NULL);
    guint tokenCount = g_strv_length(tokens);

    gtk_list_store_insert_with_values(listStore,
                                      NULL,
                                      -1,
                                      MODEL_COMMAND_TITLE,                   (tokenCount >= 1) ? stringUnescape(tokens[0],'\\') : "",
                                      MODEL_COMMAND_COMMAND_LINE,            (tokenCount >= 2) ? stringUnescape(tokens[1],'\\') : "",
                                      MODEL_COMMAND_WORKING_DIRECTORY,       (tokenCount >= 3) ? stringUnescape(tokens[2],'\\') : "",
                                      MODEL_COMMAND_SHOW_BUTTON,             (tokenCount >= 4) ? stringEquals(tokens[3],"yes")  : FALSE,
                                      MODEL_COMMAND_SHOW_MENU_ITEM,          (tokenCount >= 5) ? stringEquals(tokens[4],"yes")  : FALSE,
                                      MODEL_COMMAND_INPUT_CUSTOM_TEXT,       (tokenCount >= 6) ? stringEquals(tokens[5],"yes")  : FALSE,
                                      MODEL_COMMAND_RUN_IN_DOCKER_CONTAINER, (tokenCount >= 7) ? stringEquals(tokens[6],"yes")  : FALSE,
                                      MODEL_COMMAND_RUN_REMOTE,              (tokenCount >= 8) ? stringEquals(tokens[7],"yes")  : FALSE,
                                      MODEL_COMMAND_PARSE_OUTPUT,            (tokenCount >= 9) ? stringEquals(tokens[8],"yes")  : FALSE,
                                      MODEL_END
                                     );

    g_strfreev(tokens);
    g_free(string);

    result = TRUE;
  }

  return result;
}

/***********************************************************************\
* Name   : configurationLoadCommandList
* Purpose: load commands from configuration
* Input  : configuration - configuration to load values from
* Output : -
* Return : TRUE iff loaded
* Notes  : -
\***********************************************************************/

LOCAL gboolean configurationLoadCommandList(GKeyFile     *configuration,
                                            GtkListStore *listStore
                                           )
{
  g_assert(configuration != NULL);
  g_assert(listStore != NULL);

  gboolean result = TRUE;

  gtk_list_store_clear(listStore);
  guint    i = 0;
  do
  {
    gchar    name[64];
    g_snprintf(name,sizeof(name),"command%u",i);
    result = configurationLoadCommand(configuration, listStore, name);
    i++;
  }
  while (result);

  return result;
}

/***********************************************************************\
* Name   : configurationSaveCommand
* Purpose: save command into configuration
* Input  : configuration - configuration
*          listStore     - list store
*          treeIterator  - iterator to command in store
*          name          - name
* Output : -
* Return : TRUE iff saved
* Notes  : -
\***********************************************************************/

LOCAL gboolean configurationSaveCommand(GKeyFile     *configuration,
                                        GtkListStore *listStore,
                                        GtkTreeIter  *treeIterator,
                                        const gchar  *name
                                       )
{
  g_assert(configuration != NULL);
  g_assert(listStore != NULL);
  g_assert(treeIterator != NULL);
  g_assert(name != NULL);

  gboolean result = TRUE;

  GString *string = g_string_new(NULL);

  gchar    *title;
  gchar    *commandLine;
  gchar    *workingDirectory;
  gboolean showButton;
  gboolean showMenuItem;
  gboolean inputCustomText;
  gboolean runInDockerContainer;
  gboolean runRemote;
  gboolean parseOutput;
  gtk_tree_model_get(GTK_TREE_MODEL(listStore),
                     treeIterator,
                     MODEL_COMMAND_TITLE,                   &title,
                     MODEL_COMMAND_COMMAND_LINE,            &commandLine,
                     MODEL_COMMAND_WORKING_DIRECTORY,       &workingDirectory,
                     MODEL_COMMAND_SHOW_BUTTON,             &showButton,
                     MODEL_COMMAND_SHOW_MENU_ITEM,          &showMenuItem,
                     MODEL_COMMAND_INPUT_CUSTOM_TEXT,       &inputCustomText,
                     MODEL_COMMAND_RUN_IN_DOCKER_CONTAINER, &runInDockerContainer,
                     MODEL_COMMAND_RUN_REMOTE,              &runRemote,
                     MODEL_COMMAND_PARSE_OUTPUT,            &parseOutput,
                     MODEL_END
                    );
  g_assert(title != NULL);
  g_assert(commandLine != NULL);
  g_assert(workingDirectory != NULL);

  gchar *titleEscaped            = stringEscape(title,":",'\\');
  gchar *commandLineEscaped      = stringEscape(commandLine,":",'\\');
  gchar *workingDirectoryEscaped = stringEscape(workingDirectory,":",'\\');
  g_string_printf(string,
                  "%s:%s:%s:%s:%s:%s:%s:%s:%s",
                  titleEscaped,
                  commandLineEscaped,
                  workingDirectoryEscaped,
                  showButton ? "yes":"no",
                  showMenuItem ? "yes":"no",
                  inputCustomText ? "yes":"no",
                  runInDockerContainer ? "yes":"no",
                  runRemote ? "yes":"no",
                  parseOutput ? "yes":"no"
                 );
  g_free(workingDirectoryEscaped);
  g_free(commandLineEscaped);
  g_free(titleEscaped);

  g_key_file_set_string(configuration,
                        CONFIGURATION_GROUP_BUILDER,
                        name,
                        string->str
                       );

  g_free(workingDirectory);
  g_free(commandLine);
  g_free(title);

  g_string_free(string,TRUE);

  return result;
}

/***********************************************************************\
* Name   : configurationSaveCommandList
* Purpose: save commands into configuration
* Input  : configuration - configuration
*          listStore     - list store
* Output : -
* Return : TRUE iff saved
* Notes  : -
\***********************************************************************/

LOCAL gboolean configurationSaveCommandList(GKeyFile     *configuration,
                                            GtkListStore *listStore
                                           )
{
  g_assert(configuration != NULL);
  g_assert(listStore != NULL);

  gboolean result = TRUE;

  GtkTreeIter treeIterator;
  if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(listStore), &treeIterator))
  {
    guint i = 0;
    do
    {
      gchar name[64];
      g_snprintf(name,sizeof(name),"command%u",i);
      result = configurationSaveCommand(configuration, listStore, &treeIterator, name);

      i++;
    }
    while (result && gtk_tree_model_iter_next(GTK_TREE_MODEL(listStore), &treeIterator));
  }

  return result;
}

// TODO: used?
#if 0
/***********************************************************************\
* Name   : existsCommand
* Purpose: check if command already exists
* Input  : listStore - model
*          title     - command title
* Output : -
* Return : TRUE iff already exists
* Notes  : -
\***********************************************************************/

LOCAL gboolean existsCommand(GtkListStore *listStore,
                             const gchar  *title
                            )
{
  g_assert(listStore != NULL);
  g_assert(title != NULL);

  gboolean found = FALSE;
  GtkTreeIter treeIterator;
  if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(listStore), &treeIterator))
  {
    do
    {
      gchar      *otherTitle;
      gtk_tree_model_get(GTK_TREE_MODEL(listStore),
                         &treeIterator,
                         MODEL_COMMAND_TITLE, &otherTitle,
                         MODEL_END
                        );
      g_assert(otherTitle != NULL);

      found = stringEquals(title, otherTitle);

      g_free(otherTitle);
    }
    while (!found && gtk_tree_model_iter_next(GTK_TREE_MODEL(listStore), &treeIterator));
  }

  return found;
}
#endif

/***********************************************************************\
* Name   : existsRegex
* Purpose: check if regular expression already exists
* Input  : listStore     - list store
*          regexLanguage - regex language
*          regexGroup    - regex group
*          regexType     - regex type
*          regex         - regex
* Output : -
* Return : TRUE iff already exists
* Notes  : -
\***********************************************************************/

LOCAL gboolean existsRegex(GtkListStore *listStore,
                           const gchar  *regexLanguage,
                           const gchar  *regexGroup,
                           RegexTypes   regexType,
                           const gchar  *regex
                          )
{
  g_assert(listStore != NULL);
  g_assert(regexLanguage != NULL);
  g_assert(regexGroup != NULL);
  g_assert(regex != NULL);

  gboolean found = FALSE;
  GtkTreeIter treeIterator;
  if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(listStore), &treeIterator))
  {
    do
    {
      gchar      *otherRegExLanguage;
      gchar      *otherRegExGroup;
      RegexTypes otherRegexType;
      gchar      *otherRegEx;
      gtk_tree_model_get(GTK_TREE_MODEL(listStore),
                         &treeIterator,
                         MODEL_REGEX_LANGUAGE, &otherRegExLanguage,
                         MODEL_REGEX_GROUP,    &otherRegExGroup,
                         MODEL_REGEX_TYPE,     &otherRegexType,
                         MODEL_REGEX_REGEX,    &otherRegEx,
                         MODEL_END
                        );
      g_assert(otherRegExLanguage != NULL);
      g_assert(otherRegExGroup != NULL);
      g_assert(otherRegexType >= REGEX_TYPE_MIN);
      g_assert(otherRegexType <= REGEX_TYPE_MAX);
      g_assert(otherRegEx != NULL);

      found =    stringEquals(regexLanguage, otherRegExLanguage)
              && stringEquals(regexGroup, otherRegExGroup)
              && (regexType == otherRegexType)
              && stringEquals(regex, otherRegEx);

      g_free(otherRegEx);
      g_free(otherRegExGroup);
      g_free(otherRegExLanguage);
    }
    while (!found && gtk_tree_model_iter_next(GTK_TREE_MODEL(listStore), &treeIterator));
  }

  return found;
}

/***********************************************************************\
* Name   : configurationLoadRegex
* Purpose: load regular expression from configuration
* Input  : configuration - configuration to load values from
*          listStore     - list store
*          name          - value name
* Output : -
* Return : TRUE iff loaded
* Notes  : -
\***********************************************************************/

LOCAL gboolean configurationLoadRegex(GKeyFile     *configuration,
                                      GtkListStore *listStore,
                                      const gchar  *name
                                     )
{
  g_assert(configuration != NULL);
  g_assert(listStore != NULL);
  g_assert(name != NULL);

  gboolean result = FALSE;

  gchar *string = g_key_file_get_string(configuration, CONFIGURATION_GROUP_BUILDER, name, NULL);
  if (string != NULL)
  {
    // parse
    gchar **tokens   = stringSplit(string, ":", '\\', 4);
    g_assert(tokens != NULL);
    guint tokenCount = g_strv_length(tokens);

    const gchar *regexLanguage = (tokenCount >= 1) ? tokens[0] : "";
    const gchar *regexGroup    = (tokenCount >= 2) ? tokens[1] : "";
    RegexTypes  regexType      = REGEX_TYPE_NONE;
    if (tokenCount >= 3)
    {
      if      (stringEquals(tokens[2], REGEX_TYPE_STRINGS[REGEX_TYPE_ENTER    ])) regexType = REGEX_TYPE_ENTER;
      else if (stringEquals(tokens[2], REGEX_TYPE_STRINGS[REGEX_TYPE_LEAVE    ])) regexType = REGEX_TYPE_LEAVE;
      else if (stringEquals(tokens[2], REGEX_TYPE_STRINGS[REGEX_TYPE_ERROR    ])) regexType = REGEX_TYPE_ERROR;
      else if (stringEquals(tokens[2], REGEX_TYPE_STRINGS[REGEX_TYPE_WARNING  ])) regexType = REGEX_TYPE_WARNING;
      else if (stringEquals(tokens[2], REGEX_TYPE_STRINGS[REGEX_TYPE_EXTENSION])) regexType = REGEX_TYPE_EXTENSION;
    }
    const gchar *regex         = (tokenCount >= 4) ? tokens[3] : "";

    if (!existsRegex(listStore,
                     regexLanguage,
                     regexGroup,
                     regexType,
                     regex
                    )
       )
    {
      // add regex definition
      gtk_list_store_insert_with_values(listStore,
                                        NULL,
                                        -1,
                                        MODEL_REGEX_LANGUAGE, (tokenCount >= 1) ? tokens[0] : "",
                                        MODEL_REGEX_GROUP,    (tokenCount >= 2) ? tokens[1] : "",
                                        MODEL_REGEX_TYPE,     regexType,
                                        MODEL_REGEX_REGEX,    (tokenCount >= 4) ? tokens[3] : "",
                                        MODEL_END
                                       );
    }

    // free resources
    g_strfreev(tokens);
    g_free(string);

    result = TRUE;
  }

  return result;
}

/***********************************************************************\
* Name   : configurationLoadRegexList
* Purpose: load regex list from configuration
* Input  : configuration - configuration to load values from
*          listStore     - list store variable
* Output : -
* Return : TRUE iff loaded
* Notes  : -
\***********************************************************************/

LOCAL gboolean configurationLoadRegexList(GKeyFile     *configuration,
                                          GtkListStore *listStore
                                         )
{
  g_assert(listStore != NULL);
  g_assert(configuration != NULL);

  gboolean result = TRUE;

  gtk_list_store_clear(listStore);

// TODO: remove, deprecated
#if 1
  gchar **stringArray = g_key_file_get_string_list(configuration, CONFIGURATION_GROUP_BUILDER, "regexs", NULL, NULL);
  if (stringArray != NULL)
  {
    gchar **string;

    foreach_strv(string, stringArray)
    {
      gchar **tokens   = g_strsplit(*string, ":", 4);
      g_assert(tokens != NULL);
      guint tokenCount = g_strv_length(tokens);

      // parse
      RegexTypes regexType = REGEX_TYPE_NONE;
      if (tokenCount >= 3)
      {
        if      (stringEquals(tokens[2], REGEX_TYPE_STRINGS[REGEX_TYPE_ENTER    ])) regexType = REGEX_TYPE_ENTER;
        else if (stringEquals(tokens[2], REGEX_TYPE_STRINGS[REGEX_TYPE_LEAVE    ])) regexType = REGEX_TYPE_LEAVE;
        else if (stringEquals(tokens[2], REGEX_TYPE_STRINGS[REGEX_TYPE_ERROR    ])) regexType = REGEX_TYPE_ERROR;
        else if (stringEquals(tokens[2], REGEX_TYPE_STRINGS[REGEX_TYPE_WARNING  ])) regexType = REGEX_TYPE_WARNING;
        else if (stringEquals(tokens[2], REGEX_TYPE_STRINGS[REGEX_TYPE_EXTENSION])) regexType = REGEX_TYPE_EXTENSION;
      }

      if (!existsRegex(listStore,
                       (tokenCount >= 1) ? tokens[0] : "",
                       (tokenCount >= 2) ? tokens[1] : "",
                       regexType,
                       (tokenCount >= 4) ? tokens[3] : ""
                      )
         )
      {
        // add regex definition
        gtk_list_store_insert_with_values(listStore,
                                          NULL,
                                          -1,
                                          MODEL_REGEX_LANGUAGE, (tokenCount >= 1) ? tokens[0] : "",
                                          MODEL_REGEX_GROUP,    (tokenCount >= 2) ? tokens[1] : "",
                                          MODEL_REGEX_TYPE,     regexType,
                                          MODEL_REGEX_REGEX,    (tokenCount >= 4) ? tokens[3] : "",
                                          MODEL_END
                                         );
      }

      // free resources
      g_strfreev(tokens);
    }
    g_strfreev(stringArray);
  }
#endif

  guint    i = 0;
  do
  {
    gchar    name[64];
    g_snprintf(name,sizeof(name),"regex%u",i);
    result = configurationLoadRegex(configuration, listStore, name);
    i++;
  }
  while (result);

  return result;
}

/***********************************************************************\
* Name   : configurationSaveRegex
* Purpose: save command into configuration
* Input  : configuration - configuration
*          listStore     - list store
*          treeIterator  - iterator to command in store
*          name          - name
* Output : -
* Return : TRUE iff saved
* Notes  : -
\***********************************************************************/

LOCAL gboolean configurationSaveRegex(GKeyFile     *configuration,
                                      GtkListStore *listStore,
                                      GtkTreeIter  *treeIterator,
                                      const gchar  *name
                                     )
{
  g_assert(configuration != NULL);
  g_assert(listStore != NULL);
  g_assert(treeIterator != NULL);
  g_assert(name != NULL);

  gboolean result = TRUE;

  GString *string = g_string_new(NULL);

  gchar      *regexLanguage;
  gchar      *regexGroup;
  RegexTypes regexType;
  gchar      *regex;
  gtk_tree_model_get(GTK_TREE_MODEL(listStore),
                     treeIterator,
                     MODEL_REGEX_LANGUAGE, &regexLanguage,
                     MODEL_REGEX_GROUP,    &regexGroup,
                     MODEL_REGEX_TYPE,     &regexType,
                     MODEL_REGEX_REGEX,    &regex,
                     MODEL_END
                    );
  g_assert(regexLanguage != NULL);
  g_assert(regexGroup != NULL);
  g_assert(regex != NULL);

  gchar *regexTypeText;
  switch (regexType)
  {
    case REGEX_TYPE_NONE:      regexTypeText = "";          break;
    case REGEX_TYPE_ENTER:     regexTypeText = "enter";     break;
    case REGEX_TYPE_LEAVE:     regexTypeText = "leave";     break;
    case REGEX_TYPE_ERROR:     regexTypeText = "error";     break;
    case REGEX_TYPE_WARNING:   regexTypeText = "warning";   break;
    case REGEX_TYPE_EXTENSION: regexTypeText = "extension"; break;
  }
  g_string_printf(string,
                  "%s:%s:%s:%s",
                  regexLanguage,
                  regexGroup,
                  regexTypeText,
                  regex
                 );

  g_key_file_set_string(configuration,
                        CONFIGURATION_GROUP_BUILDER,
                        name,
                        string->str
                       );

  g_free(regex);
  g_free(regexGroup);
  g_free(regexLanguage);

  g_string_free(string,TRUE);

  return result;
}

/***********************************************************************\
* Name   : configurationSaveRegexList
* Purpose: save regex list into configuration
* Input  : configuration - configuration
*          listStore     - list store
* Output : -
* Return : TRUE iff saved
* Notes  : -
\***********************************************************************/

LOCAL gboolean configurationSaveRegexList(GKeyFile     *configuration,
                                          GtkListStore *listStore
                                         )
{
  g_assert(configuration != NULL);
  g_assert(listStore != NULL);

  gboolean result = TRUE;

// TODO: remove deprecated
#if 0
  GPtrArray *regexArray = g_ptr_array_new_with_free_func(g_free);

  // get error/warning regular expressions as arrays
  GtkTreeIter treeIterator;
  if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(listStore), &treeIterator))
  {
    gchar      *regexLanguage;
    gchar      *regexGroup;
    RegexTypes regexType;
    gchar      *regex;
    GString    *string = g_string_new(NULL);
    do
    {
      gtk_tree_model_get(GTK_TREE_MODEL(listStore),
                         &treeIterator,
                         MODEL_REGEX_LANGUAGE, &regexLanguage,
                         MODEL_REGEX_GROUP,    &regexGroup,
                         MODEL_REGEX_TYPE,     &regexType,
                         MODEL_REGEX_REGEX,    &regex,
                         MODEL_END
                        );
      g_assert(regexLanguage != NULL);
      g_assert(regexGroup != NULL);
      g_assert(regexType >= REGEX_TYPE_MIN);
      g_assert(regexType <= REGEX_TYPE_MAX);
      g_assert(regex != NULL);

      g_string_printf(string,"%s:%s:%s:%s", regexLanguage, regexGroup, REGEX_TYPE_STRINGS[regexType], regex);
      g_ptr_array_add(regexArray, g_strdup(string->str));

      g_free(regex);
      g_free(regexGroup);
      g_free(regexLanguage);
    }
    while (gtk_tree_model_iter_next(GTK_TREE_MODEL(listStore), &treeIterator));
    g_string_free(string, TRUE);
  }

  g_key_file_set_string_list(configuration,
                             CONFIGURATION_GROUP_BUILDER,
                             "regexs",
                             (const gchar**)regexArray->pdata,
                             regexArray->len
                            );

  // free resources
  g_ptr_array_free(regexArray, TRUE);
#endif

  GtkTreeIter treeIterator;
  if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(listStore), &treeIterator))
  {
    guint i = 0;
    do
    {
      gchar name[64];
      g_snprintf(name,sizeof(name),"regex%u",i);
      result = configurationSaveRegex(configuration, listStore, &treeIterator, name);

      i++;
    }
    while (result && gtk_tree_model_iter_next(GTK_TREE_MODEL(listStore), &treeIterator));
  }

  return result;
}

/***********************************************************************\
* Name   : configurationLoadColor
* Purpose: load color from configuration
* Input  : configuration - configuration to load values from
*          value         - value variable
*          name          - value name
* Output : value - RGBA color
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void configurationLoadColor(GKeyFile    *configuration,
                                  GdkRGBA     *value,
                                  const gchar *name
                                 )
{
  g_assert(configuration != NULL);
  g_assert(value != NULL);
  g_assert(name != NULL);

  GdkRGBA color;
  if (gdk_rgba_parse(&color, g_key_file_get_string(configuration, CONFIGURATION_GROUP_BUILDER, name, NULL)))
  {
    (*value) = color;
  }
}

/***********************************************************************\
* Name   : configurationSaveColor
* Purpose: save color into configuration
* Input  : configuration - configuration
*          value         - color to save
*          name          - name
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void configurationSaveColor(GKeyFile      *configuration,
                                  const GdkRGBA *value,
                                  const gchar   *name
                                 )
{
  g_assert(configuration != NULL);
  g_assert(value != NULL);
  g_assert(name != NULL);

  gchar *colorString = gdk_rgba_to_string(value);

  g_key_file_set_string(configuration, CONFIGURATION_GROUP_BUILDER, name, colorString);

  g_free(colorString);
}

/***********************************************************************\
* Name   : configurationLoad
* Purpose: load configuration
* Input  : -
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void configurationLoad()
{
  GKeyFile *configuration = g_key_file_new();

  // load configuration
  g_key_file_load_from_file(configuration, pluginData.configuration.filePath, G_KEY_FILE_NONE, NULL);

  // get commands
  configurationLoadCommandList(configuration, pluginData.configuration.commandStore);

  // get regular expressions
  configurationLoadRegexList(configuration, pluginData.configuration.regexStore);

  // get values
  configurationLoadBoolean  (configuration, &pluginData.configuration.errorIndicators,        "errorIndicators");
  configurationLoadColor    (configuration, &pluginData.configuration.errorIndicatorColor,    "errorIndicatorColor");
  configurationLoadBoolean  (configuration, &pluginData.configuration.warningIndicators,      "warningIndicators");
  configurationLoadColor    (configuration, &pluginData.configuration.warningIndicatorColor,  "warningIndicatorColor");

  configurationLoadBoolean  (configuration, &pluginData.configuration.addProjectRegExResults, "addProjectRegExResults");
  configurationLoadBoolean  (configuration, &pluginData.configuration.abortButton,            "abortButton");
  configurationLoadBoolean  (configuration, &pluginData.configuration.autoSaveAll,            "autoSaveAll");
  configurationLoadBoolean  (configuration, &pluginData.configuration.autoShowFirstError,     "autoShowFirstError");
  configurationLoadBoolean  (configuration, &pluginData.configuration.autoShowFirstWarning,   "autoShowFirstWarning");

  // free resources
  g_key_file_free(configuration);
}

/***********************************************************************\
* Name   : configurationSave
* Purpose: save configuration
* Input  : -
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void configurationSave()
{
  GKeyFile *configuration = g_key_file_new();

  g_key_file_remove_group(configuration, CONFIGURATION_GROUP_BUILDER, NULL);

  // save commands
  (void)configurationSaveCommandList(configuration, pluginData.configuration.commandStore);

  // save regular expressions
  (void)configurationSaveRegexList(configuration, pluginData.configuration.regexStore);

  // save values
  g_key_file_set_boolean(configuration, CONFIGURATION_GROUP_BUILDER, "errorIndicators",pluginData.configuration.errorIndicators);
  configurationSaveColor(configuration, &pluginData.configuration.errorIndicatorColor, "errorIndicatorColor");
  g_key_file_set_boolean(configuration, CONFIGURATION_GROUP_BUILDER, "warningIndicators",pluginData.configuration.warningIndicators);
  configurationSaveColor(configuration, &pluginData.configuration.warningIndicatorColor, "warningIndicatorColor");

  g_key_file_set_boolean(configuration, CONFIGURATION_GROUP_BUILDER, "addProjectRegExResults",pluginData.configuration.addProjectRegExResults);
  g_key_file_set_boolean(configuration, CONFIGURATION_GROUP_BUILDER, "abortButton",           pluginData.configuration.abortButton);
  g_key_file_set_boolean(configuration, CONFIGURATION_GROUP_BUILDER, "autoSaveAll",           pluginData.configuration.autoSaveAll);
  g_key_file_set_boolean(configuration, CONFIGURATION_GROUP_BUILDER, "autoShowFirstError",    pluginData.configuration.autoShowFirstError);
  g_key_file_set_boolean(configuration, CONFIGURATION_GROUP_BUILDER, "autoShowFirstWarning",  pluginData.configuration.autoShowFirstWarning);

  // create configuration directory (if it does not exists)
  gchar *configurationDir = g_path_get_dirname(pluginData.configuration.filePath);
  if (!g_file_test(configurationDir, G_FILE_TEST_IS_DIR) && utils_mkdir(configurationDir, TRUE) != 0)
  {
    dialogs_show_msgbox(GTK_MESSAGE_ERROR,
                        _("Plugin configuration directory could not be created.")
                       );
    g_free(configurationDir);
    return;
  }
  g_free(configurationDir);

  // write configuration data into file
  gchar *configurationData = g_key_file_to_data(configuration, NULL, NULL);
  utils_write_file(pluginData.configuration.filePath, configurationData);
  g_free(configurationData);

  // free resources
  g_key_file_free(configuration);
}

/***********************************************************************\
* Name   : projectConfigurationLoad
* Purpose: load project specific configuration
* Input  : configuration - configuration to load project values from
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void projectConfigurationLoad(GKeyFile *configuration)
{
  g_assert(configuration != NULL);

  // get commands
  configurationLoadCommandList(configuration, pluginData.projectProperties.commandStore);

  // get regular expressions
// TODO:

  // get values
  configurationLoadString(configuration,  pluginData.projectProperties.errorRegEx,              "errorRegEx");
  configurationLoadString(configuration,  pluginData.projectProperties.warningRegEx,            "warningRegEx");

  configurationLoadString(configuration,  pluginData.projectProperties.remote.hostName,         "remoteHostName");
  configurationLoadInteger(configuration, (gint*)&pluginData.projectProperties.remote.hostPort, "remoteHostPort");
  configurationLoadString(configuration,  pluginData.projectProperties.remote.userName,         "remoteUserName");
  configurationLoadString(configuration,  pluginData.projectProperties.remote.publicKey,        "remotePublicKey");
  configurationLoadString(configuration,  pluginData.projectProperties.remote.privateKey,       "remotePrivateKey");

  // set default values
  if (pluginData.projectProperties.remote.hostPort == 0) pluginData.projectProperties.remote.hostPort = 22;
  const gchar *homeDirectory = g_get_home_dir();
  if (homeDirectory != NULL)
  {
    if (stringIsEmpty(pluginData.projectProperties.remote.publicKey->str))
    {
      g_string_printf(pluginData.projectProperties.remote.publicKey,
                      "%s/.ssh/id_rsa.pub",
                      homeDirectory
                     );
    }
    if (stringIsEmpty(pluginData.projectProperties.remote.privateKey->str))
    {
      g_string_printf(pluginData.projectProperties.remote.privateKey,
                      "%s/.ssh/id_rsa",
                      homeDirectory
                     );
    }
  }
}

/***********************************************************************\
* Name   : projectConfigurationSave
* Purpose: save project specific configuration
* Input  : configuration - configuration to save project values to
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void projectConfigurationSave(GKeyFile *configuration)
{
  g_assert(configuration != NULL);

  g_key_file_remove_group(configuration, CONFIGURATION_GROUP_BUILDER, NULL);

  // save commands
  (void)configurationSaveCommandList(configuration, pluginData.projectProperties.commandStore);

  // save regular expressions
// TODO:
//  (void)configurationSaveRegexList(configuration, pluginData.projectProperties.regexStore);

  // save values
// TODO: use configurationSaveString/Integer
  g_key_file_set_string (configuration, CONFIGURATION_GROUP_BUILDER, "errorRegEx",      pluginData.projectProperties.errorRegEx->str);
  g_key_file_set_string (configuration, CONFIGURATION_GROUP_BUILDER, "warningRegEx",    pluginData.projectProperties.warningRegEx->str);

  g_key_file_set_string (configuration, CONFIGURATION_GROUP_BUILDER, "remoteHostName",  pluginData.projectProperties.remote.hostName->str);
  g_key_file_set_integer(configuration, CONFIGURATION_GROUP_BUILDER, "remoteHostPort",  pluginData.projectProperties.remote.hostPort);
  g_key_file_set_string (configuration, CONFIGURATION_GROUP_BUILDER, "remoteUserName",  pluginData.projectProperties.remote.userName->str);
  g_key_file_set_string (configuration, CONFIGURATION_GROUP_BUILDER, "remotePublicKey", pluginData.projectProperties.remote.publicKey->str);
  g_key_file_set_string (configuration, CONFIGURATION_GROUP_BUILDER, "remotePrivateKey",pluginData.projectProperties.remote.privateKey->str);
}

// ---------------------------------------------------------------------

/***********************************************************************\
* Name   : setEnableAbort
* Purpose: enable/disable toolbar abort button
* Input  : enabled - TRUE to enable
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void setEnableAbort(gboolean enabled)
{
  g_assert(pluginData.widgets.buttons.abort != NULL);
  g_assert(pluginData.widgets.menuItems.abort != NULL);

  gtk_widget_set_sensitive(GTK_WIDGET(pluginData.widgets.buttons.abort), enabled);
  gtk_widget_set_sensitive(GTK_WIDGET(pluginData.widgets.menuItems.abort), enabled);
}

/***********************************************************************\
* Name   : setEnableToolbar
* Purpose: enable/disable toolbar buttons/abort menu item
* Input  : enabled - TRUE to enable, FALSE to disable
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void setEnableToolbar(gboolean enabled)
{
  if (enabled)
  {
    g_assert(pluginData.widgets.disableCounter > 0);
    g_atomic_int_add(&pluginData.widgets.disableCounter, -1);
  }
  else
  {
    g_atomic_int_add(&pluginData.widgets.disableCounter, 1);
  }

  updateEnableToolbarButtons();
}

/***********************************************************************\
* Name   : printMessage
* Purpose: print message to message tab
* Input  : format - printf-like format string
*          ...    - optionla arguments
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void printMessage(gchar const *format, ...)
{
  g_assert(format != NULL);

  GString *message = g_string_new(NULL);
  g_assert(message != NULL);

  va_list arguments;
  va_start(arguments, format);
  g_string_vprintf(message,format,arguments);
  va_end(arguments);

  gtk_list_store_insert_with_values(pluginData.build.messagesStore,
                                    NULL,
                                    -1,
                                    MODEL_MESSAGE_COLOR,   COLOR_BUILD_INFO,
                                    MODEL_MESSAGE_MESSAGE, message->str,
                                    MODEL_END
                                   );

  g_string_free(message, TRUE);
}

/***********************************************************************\
* Name   : clearAll
* Purpose: clear all messages and indicators
* Input  : -
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void clearAll()
{
  guint i;

  // clear messages
  gtk_list_store_clear(pluginData.build.messagesStore);
  gtk_tree_store_clear(pluginData.build.errorsStore);
  gtk_tree_store_clear(pluginData.build.warningsStore);
  msgwin_clear_tab(MSG_MESSAGE);

  // clear indicators
  foreach_document(i)
  {
    clearIndicator(documents[i], ERROR_INDICATOR_INDEX);
    clearIndicator(documents[i], WARNING_INDICATOR_INDEX);
  }

  // clear iterators
  string_stack_clear(pluginData.build.directoryPrefixStack);
  pluginData.build.errorsTreeIterorValid                = FALSE;
  pluginData.build.warningsTreeIterorValid              = FALSE;
  pluginData.build.lastErrorsWarningsInsertStore        = NULL;
  pluginData.build.lastErrorsWarningsInsertTreeIterator = NULL;
  pluginData.build.errorWarningIndicatorsCount          = 0;

  gtk_label_set_text(GTK_LABEL(pluginData.widgets.errorsTabLabel), "Errors");
  gtk_label_set_text(GTK_LABEL(pluginData.widgets.warningsTabLabel), "Warnings");
}

/***********************************************************************\
* Name   : showBuildMessagesTab
* Purpose: show build messages tab
* Input  : -
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void showBuildMessagesTab()
{
  gtk_notebook_set_current_page(GTK_NOTEBOOK(ui_lookup_widget(geany_data->main_widgets->window, "notebook_info")),
                                pluginData.widgets.tabIndex
                               );
  gtk_notebook_set_current_page(GTK_NOTEBOOK(pluginData.widgets.tabs),
                                pluginData.widgets.messagesTabIndex
                               );
}

/***********************************************************************\
* Name   : showBuildErrorsTab
* Purpose: show build errors tab
* Input  : -
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void showBuildErrorsTab()
{
  g_assert(geany_data != NULL);
  g_assert(geany_data->main_widgets != NULL);

  gtk_notebook_set_current_page(GTK_NOTEBOOK(ui_lookup_widget(geany_data->main_widgets->window, "notebook_info")),
                                pluginData.widgets.tabIndex
                               );
  gtk_notebook_set_current_page(GTK_NOTEBOOK(pluginData.widgets.tabs),
                                pluginData.widgets.errorsTabIndex
                               );
}

/***********************************************************************\
* Name   : showBuildWarningsTab
* Purpose: show build warnings tab
* Input  : -
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void showBuildWarningsTab()
{
  g_assert(geany_data != NULL);
  g_assert(geany_data->main_widgets != NULL);

  gtk_notebook_set_current_page(GTK_NOTEBOOK(ui_lookup_widget(geany_data->main_widgets->window, "notebook_info")),
                                pluginData.widgets.tabIndex
                               );
  gtk_notebook_set_current_page(GTK_NOTEBOOK(pluginData.widgets.tabs),
                                pluginData.widgets.warningsTabIndex
                               );
}

/***********************************************************************\
* Name   : onFocusNextWidget
* Purpose: focus next widget
* Input  : widget   - current widget (not used)
*          userData - user data: next widget to focus
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onFocusNextWidget(GtkWidget *widget,
                             gpointer  userData
                            )
{
  GtkWidget *nextWidget = GTK_WIDGET(userData);
  g_assert(nextWidget != NULL);

  UNUSED_VARIABLE(widget);

  gtk_widget_grab_focus(nextWidget);
}

/***********************************************************************\
* Name   : dialogCommand
* Purpose: input command dialog
* Input  : parentWindow - parent window
*          title        - dialog title
* Output : titleString            - command tile
*          commandLineString      - command line
*          workingDirectoryString - working directory
*          showButton             - show button
*          showMenuItem           - show menu item
*          inputCustomText        - input custom text
*          runInDockerContainer   - run in docker container only
*          runRemote              - run remote only
*          parseOutput            - parse output
* Return : TRUE on "ok", FALSE otherwise
* Notes  : -
\***********************************************************************/

LOCAL gboolean dialogCommand(GtkWindow   *parentWindow,
                             const gchar *title,
                             GString     *titleString,
                             GString     *commandLineString,
                             GString     *workingDirectoryString,
                             gboolean    *showButton,
                             gboolean    *showMenuItem,
                             gboolean    *inputCustomText,
                             gboolean    *runInDockerContainer,
                             gboolean    *runRemote,
                             gboolean    *parseOutput
                            )
{
  GtkWidget *widgetTitle;
  GtkWidget *widgetCommandLine;
  GtkWidget *widgetWorkingDirectory;
  GtkWidget *widgetShowButton, *widgetShowMenuItem, *widgetInputCustomText, *widgetRunInDockerContainer, *widgetRunRemote, *widgetParseOutput;
  GtkWidget *widgetOK;
  gint      result;

  g_assert(parentWindow != NULL);
  g_assert(title != NULL);
  g_assert(titleString != NULL);
  g_assert(commandLineString != NULL);
  g_assert(workingDirectoryString != NULL);
  g_assert(showButton != NULL);
  g_assert(showMenuItem != NULL);
  g_assert(inputCustomText != NULL);
  g_assert(runInDockerContainer != NULL);
  g_assert(parseOutput != NULL);

  // create dialog
  GtkWidget *dialog = gtk_dialog_new_with_buttons(title,
                                                  parentWindow,
                                                  GTK_DIALOG_DESTROY_WITH_PARENT,
                                                  _("_Cancel"),
                                                  GTK_RESPONSE_CANCEL,
                                                  NULL
                                                 );

  widgetOK = gtk_dialog_add_button(GTK_DIALOG(dialog), _("_OK"), GTK_RESPONSE_ACCEPT);
  g_object_set_data(G_OBJECT(dialog), "ok", widgetOK);

  GtkBox *vbox = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 6));
  gtk_widget_set_margin_top(GTK_WIDGET(vbox), 6);
  gtk_widget_set_margin_bottom(GTK_WIDGET(vbox), 6);
  {
    GtkGrid *grid = GTK_GRID(gtk_grid_new());
    gtk_grid_set_row_spacing(grid, 6);
    gtk_grid_set_column_spacing(grid, 12);
    gtk_widget_set_hexpand(GTK_WIDGET(grid), TRUE);
    g_object_set(grid, "margin", 6, NULL);
    {
      addGrid(grid, 0, 0, 1, newLabel(NULL, G_OBJECT(dialog), NULL, _("Title"), NULL));
      addGrid(grid, 0, 1, 1, newEntry(&widgetTitle, G_OBJECT(dialog), "title", "Command title."));
      addGrid(grid, 1, 0, 1, newLabel(NULL, G_OBJECT(dialog), NULL, _("Command"), NULL));
      addGrid(grid, 1, 1, 1, newEntry(&widgetCommandLine, G_OBJECT(dialog), "commandLine", "Command line."));
      addGrid(grid, 2, 0, 1, newLabel(NULL, G_OBJECT(dialog), NULL, _("Working directory"), NULL));
      addGrid(grid, 2, 1, 1, newWorkingDirectoryChooser(&widgetWorkingDirectory, G_OBJECT(dialog), "workingDirectory", "Working directory for command."));
      GtkBox *hbox;
      hbox = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6));
      {
        addBox(hbox, FALSE, newCheckButton(&widgetShowButton,           G_OBJECT(dialog), "showButton",           _("button"), "Show button."));
        addBox(hbox, FALSE, newCheckButton(&widgetShowMenuItem,         G_OBJECT(dialog), "showMenuItem",         _("menu"),   "Show menu item."));
        addBox(hbox, FALSE, newCheckButton(&widgetInputCustomText,      G_OBJECT(dialog), "inputCustomText",      _("input"),  "Input custom text."));
        addBox(hbox, FALSE, newCheckButton(&widgetRunInDockerContainer, G_OBJECT(dialog), "runInDockerContainer", _("docker"), "Run in docker container."));
        addBox(hbox, FALSE, newCheckButton(&widgetRunRemote,            G_OBJECT(dialog), "runRemote",            _("remote"), "Run remote."));
        addBox(hbox, FALSE, newCheckButton(&widgetParseOutput,          G_OBJECT(dialog), "parseOutput",          _("parse"),  "Parse output for errors/warnings."));
      }
      addGrid(grid, 3, 0, 2, GTK_WIDGET(hbox));
    }
    addBox(vbox, FALSE, GTK_WIDGET(grid));
  }
  addBox(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), TRUE, GTK_WIDGET(vbox));
  gtk_widget_show_all(dialog);

  // set data
  gtk_entry_set_text(GTK_ENTRY(widgetTitle), titleString->str);
  gtk_entry_set_text(GTK_ENTRY(widgetCommandLine), commandLineString->str);
  gtk_entry_set_text(GTK_ENTRY(widgetWorkingDirectory), workingDirectoryString->str);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widgetShowButton), *showButton);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widgetShowMenuItem), *showMenuItem);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widgetInputCustomText), *inputCustomText);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widgetRunInDockerContainer), *runInDockerContainer);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widgetRunRemote), *runRemote);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widgetParseOutput), *parseOutput);

  // run dialog
  result = gtk_dialog_run(GTK_DIALOG(dialog));
  if (result == GTK_RESPONSE_ACCEPT)
  {
    // get result
    g_string_assign(titleString, gtk_entry_get_text(GTK_ENTRY(widgetTitle)));
    g_string_assign(commandLineString, gtk_entry_get_text(GTK_ENTRY(widgetCommandLine)));
    g_string_assign(workingDirectoryString, gtk_entry_get_text(GTK_ENTRY(widgetWorkingDirectory)));
    (*showButton          ) = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widgetShowButton));
    (*showMenuItem        ) = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widgetShowMenuItem));
    (*inputCustomText     ) = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widgetInputCustomText));
    (*runInDockerContainer) = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widgetRunInDockerContainer));
    (*runRemote           ) = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widgetRunRemote));
    (*parseOutput         ) = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widgetParseOutput));
  }

  // free resources
  gtk_widget_destroy(dialog);

  return (result == GTK_RESPONSE_ACCEPT);
}

/***********************************************************************\
* Name   : addCommand
* Purpose: add command
* Input  : listStore - list store
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void addCommand(GtkListStore *listStore)
{
  g_assert(geany_data != NULL);
  g_assert(geany_data->main_widgets != NULL);

  GString  *titleString            = g_string_new(NULL);
  GString  *commandLineString      = g_string_new(NULL);
  GString  *workingDirectoryString = g_string_new(NULL);
  gboolean showButton              = TRUE;
  gboolean showMenuItem            = TRUE;
  gboolean inputCustomText         = FALSE;
  gboolean runInDockerContainer    = FALSE;
  gboolean runRemote               = FALSE;
  gboolean parseOutput             = TRUE;
  if (dialogCommand(GTK_WINDOW(geany_data->main_widgets->window),
                    _("Add command"),
                    titleString,
                    commandLineString,
                    workingDirectoryString,
                    &showButton,
                    &showMenuItem,
                    &inputCustomText,
                    &runInDockerContainer,
                    &runRemote,
                    &parseOutput
                   )
     )
  {
    gtk_list_store_insert_with_values(listStore,
                                      NULL,
                                      -1,
                                      MODEL_COMMAND_TITLE,                   titleString->str,
                                      MODEL_COMMAND_COMMAND_LINE,            commandLineString->str,
                                      MODEL_COMMAND_WORKING_DIRECTORY,       workingDirectoryString->str,
                                      MODEL_COMMAND_SHOW_BUTTON,             showButton,
                                      MODEL_COMMAND_SHOW_MENU_ITEM,          showMenuItem,
                                      MODEL_COMMAND_INPUT_CUSTOM_TEXT,       inputCustomText,
                                      MODEL_COMMAND_RUN_IN_DOCKER_CONTAINER, runInDockerContainer,
                                      MODEL_COMMAND_RUN_REMOTE,              runRemote,
                                      MODEL_COMMAND_PARSE_OUTPUT,            parseOutput,
                                      MODEL_END
                                     );
  }
  g_string_free(workingDirectoryString, TRUE);
  g_string_free(commandLineString, TRUE);
  g_string_free(titleString, TRUE);
}

/***********************************************************************\
* Name   : cloneCommand
* Purpose: clone command
* Input  : listStore    - model
*          treeIterator - iterator in model
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void cloneCommand(GtkListStore *listStore,
                        GtkTreeIter  *treeIterator
                       )
{
  g_assert(geany_data != NULL);
  g_assert(geany_data->main_widgets != NULL);
  g_assert(listStore != NULL);
  g_assert(treeIterator != NULL);

  gchar    *title;
  gchar    *commandLine;
  gchar    *workingDirectory;
  gboolean showButton;
  gboolean showMenuItem;
  gboolean inputCustomText;
  gboolean runInDockerContainer;
  gboolean runRemote;
  gboolean parseOutput;
  gtk_tree_model_get(GTK_TREE_MODEL(listStore),
                     treeIterator,
                     MODEL_COMMAND_TITLE,                   &title,
                     MODEL_COMMAND_COMMAND_LINE,            &commandLine,
                     MODEL_COMMAND_WORKING_DIRECTORY,       &workingDirectory,
                     MODEL_COMMAND_SHOW_BUTTON,             &showButton,
                     MODEL_COMMAND_SHOW_MENU_ITEM,          &showMenuItem,
                     MODEL_COMMAND_INPUT_CUSTOM_TEXT,       &inputCustomText,
                     MODEL_COMMAND_RUN_IN_DOCKER_CONTAINER, &runInDockerContainer,
                     MODEL_COMMAND_RUN_REMOTE,              &runRemote,
                     MODEL_COMMAND_PARSE_OUTPUT,            &parseOutput,
                     MODEL_END
                    );
  g_assert(title != NULL);
  g_assert(commandLine != NULL);
  g_assert(workingDirectory != NULL);

  GString  *titleString            = g_string_new(title);
  GString  *commandLineString      = g_string_new(commandLine);
  GString  *workingDirectoryString = g_string_new(workingDirectory);
  if (dialogCommand(GTK_WINDOW(geany_data->main_widgets->window),
                    _("Clone command"),
                    titleString,
                    commandLineString,
                    workingDirectoryString,
                    &showButton,
                    &showMenuItem,
                    &inputCustomText,
                    &runInDockerContainer,
                    &runRemote,
                    &parseOutput
                   )
     )
  {
    gtk_list_store_insert_with_values(listStore,
                                      NULL,
                                      -1,
                                      MODEL_COMMAND_TITLE,                   titleString->str,
                                      MODEL_COMMAND_COMMAND_LINE,            commandLineString->str,
                                      MODEL_COMMAND_WORKING_DIRECTORY,       workingDirectoryString->str,
                                      MODEL_COMMAND_SHOW_BUTTON,             showButton,
                                      MODEL_COMMAND_SHOW_MENU_ITEM,          showMenuItem,
                                      MODEL_COMMAND_INPUT_CUSTOM_TEXT,       inputCustomText,
                                      MODEL_COMMAND_RUN_IN_DOCKER_CONTAINER, runInDockerContainer,
                                      MODEL_COMMAND_RUN_REMOTE,              runRemote,
                                      MODEL_COMMAND_PARSE_OUTPUT,            parseOutput,
                                      MODEL_END
                                     );
  }
  g_string_free(workingDirectoryString, TRUE);
  g_string_free(commandLineString, TRUE);
  g_string_free(titleString, TRUE);

  g_free(workingDirectory);
  g_free(commandLine);
  g_free(title);
}

/***********************************************************************\
* Name   : editCommand
* Purpose: edit command
* Input  : listStore    - model
*          treeIterator - iterator in model
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void editCommand(GtkListStore *listStore,
                       GtkTreeIter  *treeIterator
                      )
{
  g_assert(geany_data != NULL);
  g_assert(geany_data->main_widgets != NULL);
  g_assert(listStore != NULL);
  g_assert(treeIterator != NULL);

  gchar    *title;
  gchar    *commandLine;
  gchar    *workingDirectory;
  gboolean showButton;
  gboolean showMenuItem;
  gboolean inputCustomText;
  gboolean runInDockerContainer;
  gboolean runRemote;
  gboolean parseOutput;
  gtk_tree_model_get(GTK_TREE_MODEL(listStore),
                     treeIterator,
                     MODEL_COMMAND_TITLE,                   &title,
                     MODEL_COMMAND_COMMAND_LINE,            &commandLine,
                     MODEL_COMMAND_WORKING_DIRECTORY,       &workingDirectory,
                     MODEL_COMMAND_SHOW_BUTTON,             &showButton,
                     MODEL_COMMAND_SHOW_MENU_ITEM,          &showMenuItem,
                     MODEL_COMMAND_INPUT_CUSTOM_TEXT,       &inputCustomText,
                     MODEL_COMMAND_RUN_IN_DOCKER_CONTAINER, &runInDockerContainer,
                     MODEL_COMMAND_RUN_REMOTE,              &runRemote,
                     MODEL_COMMAND_PARSE_OUTPUT,            &parseOutput,
                     MODEL_END
                    );
  g_assert(title != NULL);
  g_assert(commandLine != NULL);
  g_assert(workingDirectory != NULL);

  GString *titleString            = g_string_new(title);
  GString *commandLineString      = g_string_new(commandLine);
  GString *workingDirectoryString = g_string_new(workingDirectory);
  if (dialogCommand(GTK_WINDOW(geany_data->main_widgets->window),
                    _("Edit command"),
                    titleString,
                    commandLineString,
                    workingDirectoryString,
                    &showButton,
                    &showMenuItem,
                    &inputCustomText,
                    &runInDockerContainer,
                    &runRemote,
                    &parseOutput
                   )
     )
  {
    gtk_list_store_set(listStore,
                       treeIterator,
                       MODEL_COMMAND_TITLE,                   titleString->str,
                       MODEL_COMMAND_COMMAND_LINE,            commandLineString->str,
                       MODEL_COMMAND_WORKING_DIRECTORY,       workingDirectoryString->str,
                       MODEL_COMMAND_SHOW_BUTTON,             showButton,
                       MODEL_COMMAND_SHOW_MENU_ITEM,          showMenuItem,
                       MODEL_COMMAND_INPUT_CUSTOM_TEXT,       inputCustomText,
                       MODEL_COMMAND_RUN_IN_DOCKER_CONTAINER, runInDockerContainer,
                       MODEL_COMMAND_RUN_REMOTE,              runRemote,
                       MODEL_COMMAND_PARSE_OUTPUT,            parseOutput,
                       MODEL_END
                      );
  }
  g_string_free(workingDirectoryString, TRUE);
  g_string_free(commandLineString, TRUE);
  g_string_free(titleString, TRUE);

  g_free(workingDirectory);
  g_free(commandLine);
  g_free(title);
}

/***********************************************************************\
* Name   : dialogRegexTypeCellRenderer
* Purpose: render regex type column
* Input  : cellLayout   - cell layout
*          cellRenderer - cell renderer
*          treeModel    - model
*          treeIterator - tree entry iterator
*          userData     - user data (not used)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void  dialogRegexTypeCellRenderer(GtkCellLayout   *cellLayout,
                                        GtkCellRenderer *cellRenderer,
                                        GtkTreeModel    *treeModel,
                                        GtkTreeIter     *treeIterator,
                                        gpointer         userData
                                       )
{
  g_assert(cellLayout != NULL);
  g_assert(cellRenderer != NULL);
  g_assert(treeModel != NULL);
  g_assert(treeIterator != NULL);

  UNUSED_VARIABLE(userData);

  RegexTypes type;
  gtk_tree_model_get(treeModel,
                     treeIterator,
                     MODEL_REGEX_TYPE, &type,
                     MODEL_END
                    );
  g_assert(type >= REGEX_TYPE_MIN);
  g_assert(type <= REGEX_TYPE_MAX);
  g_object_set(cellRenderer, "text", REGEX_TYPE_STRINGS[type], NULL);
}

/***********************************************************************\
* Name   : dialogRegexUpdateMatch
* Purpose: update regular expression dialog match
* Input  : widgetLanguage     - language widget
*          widgetGroup        - group widget
*          widgetRegex        - regular expression entry widget
*          widgetSample       - sample entry widget
*          widgetFilePath     - view file path entry widget
*          widgetLineNumber   - view line number entry widget
*          widgetColumnNumber - view column number entry widget
*          widgetMessage      - view message entry widget
*          widgetOK           - OK button widget
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void dialogRegexUpdateMatch(GtkWidget *widgetLanguage,
                                  GtkWidget *widgetGroup,
                                  GtkWidget *widgetRegex,
                                  GtkWidget *widgetSample,
                                  GtkWidget *widgetFilePath,
                                  GtkWidget *widgetLineNumber,
                                  GtkWidget *widgetColumnNumber,
                                  GtkWidget *widgetMessage,
                                  GtkWidget *widgetOK
                                 )
{
  g_assert(widgetRegex != NULL);
  g_assert(widgetSample != NULL);
  g_assert(widgetFilePath != NULL);
  g_assert(widgetLineNumber != NULL);
  g_assert(widgetColumnNumber != NULL);
  g_assert(widgetMessage != NULL);
  g_assert(widgetOK != NULL);

  UNUSED_VARIABLE(widgetLanguage);
  UNUSED_VARIABLE(widgetGroup);

  const gchar *regexString = gtk_entry_get_text(GTK_ENTRY(widgetRegex));
  if (!EMPTY(regexString))
  {
    // validate regular expression, enable/disable ok-button
    GRegex      *regex;
    GMatchInfo  *matchInfo;
    regex = g_regex_new(regexString,
                        0, // compile_optipns
                        0, // match option
                        NULL // error
                       );
    if (regex != NULL)
    {
      gtk_widget_set_sensitive(widgetOK, TRUE);

      if (g_regex_match(regex, gtk_entry_get_text(GTK_ENTRY(widgetSample)), 0, &matchInfo))
      {
        gint filePathMatchNumber     = g_regex_get_string_number(regex, "filePath");
        gint lineNumberMatchNumber   = g_regex_get_string_number(regex, "lineNumber");
        gint columnNumberMatchNumber = g_regex_get_string_number(regex, "columnNumber");
        gint messageMatchNumber      = g_regex_get_string_number(regex, "message");

        if (filePathMatchNumber >= 0)
        {
          gtk_entry_set_text(GTK_ENTRY(widgetFilePath), g_match_info_fetch(matchInfo, filePathMatchNumber));
        }
        else
        {
          gtk_entry_set_text(GTK_ENTRY(widgetFilePath), "");
        }
        if (lineNumberMatchNumber >= 0)
        {
          gtk_entry_set_text(GTK_ENTRY(widgetLineNumber), g_match_info_fetch(matchInfo, lineNumberMatchNumber));
        }
        else
        {
          gtk_entry_set_text(GTK_ENTRY(widgetLineNumber), "");
        }
        if (columnNumberMatchNumber >= 0)
        {
          gtk_entry_set_text(GTK_ENTRY(widgetColumnNumber), g_match_info_fetch(matchInfo, columnNumberMatchNumber));
        }
        else
        {
          gtk_entry_set_text(GTK_ENTRY(widgetColumnNumber), "");
        }
        if (messageMatchNumber >= 0)
        {
          gtk_entry_set_text(GTK_ENTRY(widgetMessage), g_match_info_fetch(matchInfo, messageMatchNumber));
        }
        else
        {
          gtk_entry_set_text(GTK_ENTRY(widgetMessage), "");
        }
      }
      else
      {
        // regex do not match
        gtk_entry_set_text(GTK_ENTRY(widgetFilePath), "");
        gtk_entry_set_text(GTK_ENTRY(widgetLineNumber), "");
        gtk_entry_set_text(GTK_ENTRY(widgetColumnNumber), "");
        gtk_entry_set_text(GTK_ENTRY(widgetMessage), "");
      }

      g_match_info_free(matchInfo);
      g_regex_unref(regex);
    }
    else
    {
      // invalid regex
      gtk_widget_set_sensitive(widgetOK, FALSE);
      gtk_entry_set_text(GTK_ENTRY(widgetFilePath), "");
      gtk_entry_set_text(GTK_ENTRY(widgetLineNumber), "");
      gtk_entry_set_text(GTK_ENTRY(widgetColumnNumber), "");
      gtk_entry_set_text(GTK_ENTRY(widgetMessage), "");
    }
  }
  else
  {
    // empty regex
    gtk_widget_set_sensitive(widgetOK, FALSE);
    gtk_entry_set_text(GTK_ENTRY(widgetFilePath), "");
    gtk_entry_set_text(GTK_ENTRY(widgetLineNumber), "");
    gtk_entry_set_text(GTK_ENTRY(widgetColumnNumber), "");
    gtk_entry_set_text(GTK_ENTRY(widgetMessage), "");
  }
}

/***********************************************************************\
* Name   : onInputRegexDialogChanged
* Purpose: input regular expression changed callback
* Input  : entry    - entry
*          userData - user data: ok-button widget
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onInputRegexDialogChanged(GtkWidget *widget,
                                     gpointer  userData
                                    )
{
  GtkWidget *dialog = GTK_WIDGET(userData);
  g_assert(dialog != NULL);

  UNUSED_VARIABLE(widget);

  GtkWidget *widgetLanguage = g_object_get_data(G_OBJECT(dialog), "language");
  g_assert(widgetLanguage != NULL);
  GtkWidget *widgetGroup = g_object_get_data(G_OBJECT(dialog), "group");
  g_assert(widgetGroup != NULL);
  GtkWidget *widgetRegex = g_object_get_data(G_OBJECT(dialog), "regex");
  g_assert(widgetRegex != NULL);
  GtkWidget *widgetSample = g_object_get_data(G_OBJECT(dialog), "sample");
  g_assert(widgetSample != NULL);
  GtkWidget *filePath = g_object_get_data(G_OBJECT(dialog), "filePath");
  g_assert(filePath != NULL);
  GtkWidget *lineNumber = g_object_get_data(G_OBJECT(dialog), "lineNumber");
  g_assert(lineNumber != NULL);
  GtkWidget *columnNumber = g_object_get_data(G_OBJECT(dialog), "columnNumber");
  g_assert(columnNumber != NULL);
  GtkWidget *message = g_object_get_data(G_OBJECT(dialog), "message");
  g_assert(message != NULL);
  GtkWidget *widgetOK = g_object_get_data(G_OBJECT(dialog), "ok");
  g_assert(widgetOK != NULL);

  dialogRegexUpdateMatch(widgetLanguage,
                         widgetGroup,
                         widgetRegex,
                         widgetSample,
                         filePath,
                         lineNumber,
                         columnNumber,
                         message,
                         widgetOK
                        );
}

/***********************************************************************\
* Name   : validateGroup
* Purpose: validate group input: allow letters, digits, and _
* Input  : value    - new value to insert
*          userData - user data (not used)
* Output : -
* Return : TRUE iff input valid
* Notes  : -
\***********************************************************************/

LOCAL gboolean validateGroup(const gchar *value, gpointer userData)
{
  g_assert(value != NULL);

  UNUSED_VARIABLE(userData);

  while ((*value) != '\0')
  {
    if (   !g_ascii_isalnum(*value)
        && ((*value) != '_')
       )
    {
      return FALSE;
    }
    value++;
  }

  return TRUE;
}

/***********************************************************************\
* Name   : onInputRegexDialogComboGroupInsertText
* Purpose: called on input regular expression group insert text
* Input  : entry    - entry widget
*          text     - text
*          length   - length
*          position - position
*          userData - user data (not used)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onInputRegexDialogComboGroupInsertText(GtkEntry    *entry,
                                                  const gchar *text,
                                                  gint        length,
                                                  gint        *position,
                                                  gpointer    userData
)
{
  GtkEditable    *editable     = GTK_EDITABLE(entry);
  InputValidator validator     = (InputValidator)g_object_get_data(G_OBJECT(entry), "validatorFunction");
  gpointer       validatorData = g_object_get_data(G_OBJECT(entry), "validatorData");

  UNUSED_VARIABLE(userData);

  if (validator(text, validatorData))
  {
    g_signal_handlers_block_by_func(G_OBJECT(editable), G_CALLBACK(onInputRegexDialogComboGroupInsertText), userData);
    gtk_editable_insert_text(editable, text, length, position);
    g_signal_handlers_unblock_by_func(G_OBJECT(editable), G_CALLBACK(onInputRegexDialogComboGroupInsertText), userData);
  }
  g_signal_stop_emission_by_name(G_OBJECT(editable), "insert_text");
}

/***********************************************************************\
* Name   : onInputRegexDialogComboGroupChanged
* Purpose: called on group changes
* Input  : -
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onInputRegexDialogComboGroupChanged(GtkWidget *widget,
                                               gpointer  data
                                              )
{
  GtkWidget *dialog = GTK_WIDGET(data);
  g_assert(dialog != NULL);

  UNUSED_VARIABLE(widget);

  GtkWidget *widgetLanguage = g_object_get_data(G_OBJECT(dialog), "language");
  g_assert(widgetLanguage != NULL);
  GtkWidget *widgetGroup = g_object_get_data(G_OBJECT(dialog), "group");
  g_assert(widgetGroup != NULL);
  GtkWidget *enter = g_object_get_data(G_OBJECT(dialog), "enter");
  g_assert(enter != NULL);
  GtkWidget *radioLeave = g_object_get_data(G_OBJECT(dialog), "leave");
  g_assert(radioLeave != NULL);
  GtkWidget *radioError = g_object_get_data(G_OBJECT(dialog), "error");
  g_assert(radioError != NULL);
  GtkWidget *radioWarning = g_object_get_data(G_OBJECT(dialog), "warning");
  g_assert(radioWarning != NULL);
  GtkWidget *radioExtension = g_object_get_data(G_OBJECT(dialog), "extension");
  g_assert(radioExtension != NULL);
  GtkWidget *widgetRegex = g_object_get_data(G_OBJECT(dialog), "regex");
  g_assert(widgetRegex != NULL);

  GtkWidget *widgetSample = g_object_get_data(G_OBJECT(dialog), "sample");
  g_assert(widgetSample != NULL);
  GtkWidget *filePath = g_object_get_data(G_OBJECT(dialog), "filePath");
  g_assert(filePath != NULL);
  GtkWidget *lineNumber = g_object_get_data(G_OBJECT(dialog), "lineNumber");
  g_assert(lineNumber != NULL);
  GtkWidget *columnNumber = g_object_get_data(G_OBJECT(dialog), "columnNumber");
  g_assert(columnNumber != NULL);
  GtkWidget *message = g_object_get_data(G_OBJECT(dialog), "message");
  g_assert(message != NULL);
  GtkWidget *widgetOK = g_object_get_data(G_OBJECT(dialog), "ok");
  g_assert(widgetOK != NULL);

  GtkTreeIter treeIterator;
  if (gtk_combo_box_get_active_iter(GTK_COMBO_BOX(widget), &treeIterator))
  {
    GtkListStore *listStore = GTK_LIST_STORE(gtk_combo_box_get_model(GTK_COMBO_BOX(widget)));
    g_assert(listStore != NULL);

    gchar      *language;
    gchar      *group;
    RegexTypes regexType;
    gchar      *regex;
    gtk_tree_model_get(GTK_TREE_MODEL(listStore),
                       &treeIterator,
                       MODEL_REGEX_LANGUAGE, &language,
                       MODEL_REGEX_GROUP,    &group,
                       MODEL_REGEX_TYPE,     &regexType,
                       MODEL_REGEX_REGEX,    &regex,
                       MODEL_END
                      );
    g_assert(language != NULL);
    g_assert(group != NULL);
    g_assert(regexType >= REGEX_TYPE_MIN);
    g_assert(regexType <= REGEX_TYPE_MAX);
    g_assert(regex != NULL);

    guint        i = 1;
    const GSList *node;
    foreach_slist(node, filetypes_get_sorted_by_name())
    {
      const GeanyFiletype *fileType = (GeanyFiletype*)node->data;
      if (stringEquals(language, fileType->name))
      {
        gtk_combo_box_set_active(GTK_COMBO_BOX(widgetLanguage), i);
      }
      i++;
    }
    gtk_entry_set_text(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(widgetGroup))), group);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(enter),     regexType == REGEX_TYPE_ENTER    );
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radioLeave),     regexType == REGEX_TYPE_LEAVE    );
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radioError),     regexType == REGEX_TYPE_ERROR    );
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radioWarning),   regexType == REGEX_TYPE_WARNING  );
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radioExtension), regexType == REGEX_TYPE_EXTENSION);
    gtk_entry_set_text(GTK_ENTRY(widgetRegex), regex);
    g_free(regex);
    g_free(group);
    g_free(language);

    dialogRegexUpdateMatch(widgetLanguage,
                           widgetGroup,
                           widgetRegex,
                           widgetSample,
                           filePath,
                           lineNumber,
                           columnNumber,
                           message,
                           widgetOK
                          );
  }
}

/***********************************************************************\
* Name   : updateGroupStore
* Purpose: update group store
* Input  : language   - langauge or NULL
*          group      - group or NULL
*          widgetGroup - group widget
* Output : -
* Return : active index or -1
* Notes  : -
\***********************************************************************/

LOCAL gint updateGroupStore(const gchar *language, const gchar *group, GtkWidget *widgetGroup)
{
  gint index = -1;

  gtk_list_store_clear(pluginData.builtInRegExStore);
  guint n = 0;
  for (guint i = 0; i < ARRAY_SIZE(REGEX_BUILTIN); i++)
  {
    if (   EMPTY(language)
        || stringEquals(REGEX_BUILTIN[i].language, language)
        || stringEquals(REGEX_BUILTIN[i].language, "*")
       )
    {
      gtk_list_store_insert_with_values(pluginData.builtInRegExStore,
                                        NULL,
                                        -1,
                                        MODEL_REGEX_LANGUAGE, REGEX_BUILTIN[i].language,
                                        MODEL_REGEX_GROUP,    REGEX_BUILTIN[i].group,
                                        MODEL_REGEX_TYPE,     REGEX_BUILTIN[i].type,
                                        MODEL_REGEX_REGEX,    REGEX_BUILTIN[i].regex,
                                        MODEL_END
                                       );
      if ((index < 0) && stringEquals(REGEX_BUILTIN[i].group, group)) index = (gint)n;
      n++;
    }
  }

  if (!EMPTY(group))
  {
    g_signal_handlers_block_by_func(G_OBJECT(widgetGroup), G_CALLBACK(onInputRegexDialogComboGroupChanged), NULL);
    {
      gtk_entry_set_text(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(widgetGroup))), group);
    }
    g_signal_handlers_unblock_by_func(G_OBJECT(widgetGroup), G_CALLBACK(onInputRegexDialogComboGroupChanged), NULL);
  }

  return index;
}

/***********************************************************************\
* Name   : onInputRegexDialogComboLanguageChanged
* Purpose: called on language changes
* Input  : -
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onInputRegexDialogComboLanguageChanged(GtkWidget *widget,
                                                  gpointer  data
                                                 )
{
  GtkWidget *dialog = GTK_WIDGET(data);
  g_assert(dialog != NULL);

  UNUSED_VARIABLE(widget);

  GtkWidget *widgetLanguage = g_object_get_data(G_OBJECT(dialog), "language");
  g_assert(widgetLanguage != NULL);
  GtkWidget *widgetGroup = g_object_get_data(G_OBJECT(dialog), "group");
  g_assert(widgetGroup != NULL);
  GtkWidget *enter = g_object_get_data(G_OBJECT(dialog), "enter");
  g_assert(enter != NULL);
  GtkWidget *radioLeave = g_object_get_data(G_OBJECT(dialog), "leave");
  g_assert(radioLeave != NULL);
  GtkWidget *radioError = g_object_get_data(G_OBJECT(dialog), "error");
  g_assert(radioError != NULL);
  GtkWidget *radioWarning = g_object_get_data(G_OBJECT(dialog), "warning");
  g_assert(radioWarning != NULL);
  GtkWidget *radioExtension = g_object_get_data(G_OBJECT(dialog), "extension");
  g_assert(radioExtension != NULL);
  GtkWidget *widgetRegex = g_object_get_data(G_OBJECT(dialog), "regex");
  g_assert(widgetRegex != NULL);

  GtkWidget *widgetSample = g_object_get_data(G_OBJECT(dialog), "sample");
  g_assert(widgetSample != NULL);
  GtkWidget *filePath = g_object_get_data(G_OBJECT(dialog), "filePath");
  g_assert(filePath != NULL);
  GtkWidget *lineNumber = g_object_get_data(G_OBJECT(dialog), "lineNumber");
  g_assert(lineNumber != NULL);
  GtkWidget *columnNumber = g_object_get_data(G_OBJECT(dialog), "columnNumber");
  g_assert(columnNumber != NULL);
  GtkWidget *message = g_object_get_data(G_OBJECT(dialog), "message");
  g_assert(message != NULL);
  GtkWidget *widgetOK = g_object_get_data(G_OBJECT(dialog), "ok");
  g_assert(widgetOK != NULL);

  // update group model
  gchar *language = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(widgetLanguage));
  gchar *group    = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(widgetGroup));
  updateGroupStore(language, group, widgetGroup);
  g_free(group);
  g_free(language);

  dialogRegexUpdateMatch(widgetLanguage,
                         widgetGroup,
                         widgetRegex,
                         widgetSample,
                         filePath,
                         lineNumber,
                         columnNumber,
                         message,
                         widgetOK
                        );

}

/***********************************************************************\
* Name   : dialogRegex
* Purpose: input regular expressiopn dialog
* Input  : parentWindow - parent window
*          title        - dialog title
*          text         - dialog entry text
*          tooltipText  - dialog tooltip text
* Output : languageString - language name
*          groupString    - group text
*          regexType      - regex type
*          regexString    - regular expression string
*          sample         - error/warning sample string
* Return : TRUE on "ok", FALSE otherwise
* Notes  : -
\***********************************************************************/

LOCAL gboolean dialogRegex(GtkWindow   *parentWindow,
                           const gchar *title,
                           const char  *text,
                           GString     *languageString,
                           GString     *groupString,
                           RegexTypes  *regexType,
                           GString     *regexString,
                           const gchar *sample
                          )
{
  GtkWidget *widgetLanguage;
  GtkWidget *widgetGroup;
  GtkWidget *widgetRegExTypeEnter, *widgetRegExTypeLeave, *widgetRegExTypeError, *widgetRegExTypeWarning, *widgetRegExTypeExtension;
  GtkWidget *widgetRegex, *widgetSample;
  GtkWidget *widgetFilePath, *widgetLineNumber, *widgetColumnNumber, *widgetMessage;
  GtkWidget *widgetOK;
  gint      result;

  g_assert(parentWindow != NULL);
  g_assert(title != NULL);
  g_assert(text != NULL);
  g_assert(groupString != NULL);
  g_assert(regexType != NULL);
  g_assert(regexString != NULL);

  // create dialog
  GtkWidget *dialog = gtk_dialog_new_with_buttons(title,
                                                  parentWindow,
                                                  GTK_DIALOG_DESTROY_WITH_PARENT,
                                                  _("_Cancel"),
                                                  GTK_RESPONSE_CANCEL,
                                                  NULL
                                                 );

  widgetOK = gtk_dialog_add_button(GTK_DIALOG(dialog), _("_OK"), GTK_RESPONSE_ACCEPT);
  g_object_set_data(G_OBJECT(dialog), "ok", widgetOK);

  GtkBox *vbox = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 6));
  gtk_widget_set_margin_top(GTK_WIDGET(vbox), 6);
  gtk_widget_set_margin_bottom(GTK_WIDGET(vbox), 6);
  {
    GtkBox *hbox;

    GtkGrid *grid = GTK_GRID(gtk_grid_new());
    gtk_grid_set_row_spacing(grid, 6);
    gtk_grid_set_column_spacing(grid, 12);
    gtk_widget_set_hexpand(GTK_WIDGET(grid), TRUE);
    g_object_set(grid, "margin", 6, NULL);
    {
      addGrid(grid, 0, 0, 1, newLabel(NULL, G_OBJECT(dialog), NULL, "Language", NULL));
      hbox = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL,12));
      {
        // language combo oox
        addBox(hbox, TRUE, newCombo(&widgetLanguage, G_OBJECT(dialog), "language", "Language"));
        gint         i = 1;
        const GSList *node;
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(widgetLanguage), "");
        foreach_slist(node, filetypes_get_sorted_by_name())
        {
          const GeanyFiletype *fileType = (GeanyFiletype*)node->data;
          gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(widgetLanguage), fileType->name);
          if (stringEquals(languageString->str, fileType->name))
          {
            gtk_combo_box_set_active(GTK_COMBO_BOX(widgetLanguage), i);
          }
          i++;
        }
        plugin_signal_connect(geany_plugin,
                              G_OBJECT(widgetLanguage),
                              "changed",
                              FALSE,
                              G_CALLBACK(onInputRegexDialogComboLanguageChanged),
                              dialog
                             );

        // group combo box
        addBox(hbox, TRUE, newComboEntry(&widgetGroup, G_OBJECT(dialog), "group", "Group"));
        gtk_combo_box_set_model(GTK_COMBO_BOX(widgetGroup), GTK_TREE_MODEL(pluginData.builtInRegExStore));
        gtk_combo_box_set_entry_text_column(GTK_COMBO_BOX(widgetGroup), MODEL_REGEX_LANGUAGE);
#if 0
//“appears-as-list”
//id-column
g_object_set(widgetGroup,
"apperas-as-list", TRUE,
NULL
);
#endif
        updateGroupStore(languageString->str, groupString->str, widgetGroup);

        GtkCellRenderer *cellRenderer;

#if 0
        cellRenderer = gtk_cell_renderer_combo_new();
        gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(widgetGroup), cellRenderer, "text", MODEL_REGEX_LANGUAGE, NULL);
        gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(widgetGroup), cellRenderer, FALSE);

        cellRenderer = gtk_cell_renderer_combo_new();
        gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(widgetGroup), cellRenderer, "text", MODEL_REGEX_GROUP, NULL);
        gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(widgetGroup), cellRenderer, FALSE);

        cellRenderer = gtk_cell_renderer_combo_new();
        gtk_cell_layout_set_cell_data_func(GTK_CELL_LAYOUT(widgetGroup), cellRenderer, dialogRegexTypeCellRenderer, NULL, NULL);
        gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(widgetGroup), cellRenderer, "text", MODEL_REGEX_TYPE, NULL);
        gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(widgetGroup), cellRenderer, FALSE);

        cellRenderer = gtk_cell_renderer_combo_new();s
        gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(widgetGroup), cellRenderer, "text", MODEL_REGEX_REGEX, NULL);
        gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(widgetGroup), cellRenderer, FALSE);
#elif 0
        cellRenderer = gtk_cell_renderer_combo_new();
        gtk_cell_layout_set_cell_data_func(GTK_CELL_LAYOUT(widgetGroup), cellRenderer, dialogRegexCellRenderer, NULL, NULL);
        gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(widgetGroup), cellRenderer, "text", MODEL_REGEX_TYPE, NULL);
        gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(widgetGroup), cellRenderer, FALSE);
#else
        // the text entry column is always the first visible column
/*
        cellRenderer = gtk_cell_renderer_combo_new();
        gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(widgetGroup), cellRenderer, FALSE);
        gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(widgetGroup), cellRenderer, "text", MODEL_REGEX_LANGUAGE, NULL);
*/

        cellRenderer = gtk_cell_renderer_combo_new();
        gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(widgetGroup), cellRenderer, FALSE);
        gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(widgetGroup), cellRenderer, "text", MODEL_REGEX_GROUP, NULL);

        cellRenderer = gtk_cell_renderer_combo_new();
        gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(widgetGroup), cellRenderer, FALSE);
        gtk_cell_layout_set_cell_data_func(GTK_CELL_LAYOUT(widgetGroup), cellRenderer, dialogRegexTypeCellRenderer, NULL, NULL);
        gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(widgetGroup), cellRenderer, "text", MODEL_REGEX_TYPE, NULL);

        cellRenderer = gtk_cell_renderer_combo_new();
        gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(widgetGroup), cellRenderer, FALSE);
        gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(widgetGroup), cellRenderer, "text", MODEL_REGEX_REGEX, NULL);
#endif

        GtkWidget *entryGroup = gtk_bin_get_child(GTK_BIN(widgetGroup));
        g_object_set_data(G_OBJECT(entryGroup), "validatorFunction", validateGroup);
        g_object_set_data(G_OBJECT(entryGroup), "validatorData", NULL);
        plugin_signal_connect(geany_plugin,
                              G_OBJECT(entryGroup),
                              "insert_text",
                              FALSE,
                              G_CALLBACK(onInputRegexDialogComboGroupInsertText),
                              NULL
                             );
        plugin_signal_connect(geany_plugin,
                              G_OBJECT(widgetGroup),
                              "changed",
                              FALSE,
                              G_CALLBACK(onInputRegexDialogComboGroupChanged),
                              dialog
                             );
      }
      addGrid(grid, 0, 1, 2, GTK_WIDGET(hbox));

      addGrid(grid, 1, 0, 1, newLabel(NULL, G_OBJECT(dialog), NULL, "Type", NULL));
      hbox = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL,12));
      {
        addBox(hbox, FALSE, newRadioButton(&widgetRegExTypeEnter, G_OBJECT(dialog), NULL,                   "enter",     REGEX_TYPE_STRINGS[REGEX_TYPE_ENTER    ], NULL));
        if ((*regexType) == REGEX_TYPE_ENTER) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widgetRegExTypeEnter), TRUE);
        addBox(hbox, FALSE, newRadioButton(&widgetRegExTypeLeave, G_OBJECT(dialog), widgetRegExTypeEnter,   "leave",     REGEX_TYPE_STRINGS[REGEX_TYPE_LEAVE    ], NULL));
        if ((*regexType) == REGEX_TYPE_LEAVE) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widgetRegExTypeLeave), TRUE);
        addBox(hbox, FALSE, newRadioButton(&widgetRegExTypeError, G_OBJECT(dialog), widgetRegExTypeLeave,   "error",     REGEX_TYPE_STRINGS[REGEX_TYPE_ERROR    ], NULL));
        if ((*regexType) == REGEX_TYPE_ERROR) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widgetRegExTypeError), TRUE);
        addBox(hbox, FALSE, newRadioButton(&widgetRegExTypeWarning, G_OBJECT(dialog), widgetRegExTypeError,   "warning",   REGEX_TYPE_STRINGS[REGEX_TYPE_WARNING  ], NULL));
        if ((*regexType) == REGEX_TYPE_WARNING) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widgetRegExTypeWarning), TRUE);
        addBox(hbox, FALSE, newRadioButton(&widgetRegExTypeExtension, G_OBJECT(dialog), widgetRegExTypeWarning, "extension", REGEX_TYPE_STRINGS[REGEX_TYPE_EXTENSION], NULL));
        if ((*regexType) == REGEX_TYPE_EXTENSION) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widgetRegExTypeExtension), TRUE);
      }
      addGrid(grid, 1, 1, 2, GTK_WIDGET(hbox));

      addGrid(grid, 2, 0, 1, newLabel(NULL, G_OBJECT(dialog), NULL, text, NULL));
      addGrid(grid, 2, 1, 2, newEntry(&widgetRegex,
                                      G_OBJECT(dialog),
                                      "regex",
                                      "Regular expression. Group names:\n"
                                      "  <filePath>\n"
                                      "  <lineNumber>\n"
                                      "  <columnNumber>\n"
                                      "  <message>\n"
                                     )
             );
      gtk_entry_set_text(GTK_ENTRY(widgetRegex), regexString->str);

      addGrid(grid, 3, 0, 1, newLabel(NULL, G_OBJECT(dialog), NULL, _("Sample"), NULL));
      addGrid(grid, 3, 1, 2, newEntry(&widgetSample, G_OBJECT(dialog), "sample", "Regular expression match example"));
      gtk_entry_set_text(GTK_ENTRY(widgetSample), sample);

      addGrid(grid, 4, 1, 1, newLabel(NULL, G_OBJECT(dialog), NULL, "File path", NULL));
      addGrid(grid, 4, 2, 1, newView (&widgetFilePath, G_OBJECT(dialog), "filePath", NULL));
      addGrid(grid, 5, 1, 1, newLabel(NULL, G_OBJECT(dialog), NULL, "Line number", NULL));
      addGrid(grid, 5, 2, 1, newView (&widgetLineNumber, G_OBJECT(dialog), "lineNumber", NULL));
      addGrid(grid, 6, 1, 1, newLabel(NULL, G_OBJECT(dialog), NULL, "Column number", NULL));
      addGrid(grid, 6, 2, 1, newView (&widgetColumnNumber, G_OBJECT(dialog), "columnNumber", NULL));
      addGrid(grid, 7, 1, 1, newLabel(NULL, G_OBJECT(dialog), NULL, "Message", NULL));
      addGrid(grid, 7, 2, 1, newView (&widgetMessage, G_OBJECT(dialog), "message", NULL));
      plugin_signal_connect(geany_plugin,
                            G_OBJECT(widgetRegex),
                            "changed",
                            FALSE,
                            G_CALLBACK(onInputRegexDialogChanged),
                            dialog
                           );
      plugin_signal_connect(geany_plugin,
                            G_OBJECT(widgetSample),
                            "changed",
                            FALSE,
                            G_CALLBACK(onInputRegexDialogChanged),
                            dialog
                           );
    }
    addBox(vbox, FALSE, GTK_WIDGET(grid));
  }
  addBox(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), TRUE, GTK_WIDGET(vbox));
  gtk_widget_show_all(dialog);

  // initial update match
  dialogRegexUpdateMatch(widgetLanguage,
                         widgetGroup,
                         widgetRegex,
                         widgetSample,
                         widgetFilePath,
                         widgetLineNumber,
                         widgetColumnNumber,
                         widgetMessage,
                         widgetOK
                        );

  // run dialog
  result = gtk_dialog_run(GTK_DIALOG(dialog));
  if (result == GTK_RESPONSE_ACCEPT)
  {
    // get result
    g_string_assign(languageString, gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(widgetLanguage)));
    g_string_assign(groupString, gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(widgetGroup)));
    if      (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widgetRegExTypeEnter    ))) (*regexType) = REGEX_TYPE_ENTER;
    else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widgetRegExTypeLeave    ))) (*regexType) = REGEX_TYPE_LEAVE;
    else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widgetRegExTypeError    ))) (*regexType) = REGEX_TYPE_ERROR;
    else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widgetRegExTypeWarning  ))) (*regexType) = REGEX_TYPE_WARNING;
    else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widgetRegExTypeExtension))) (*regexType) = REGEX_TYPE_EXTENSION;
    g_string_assign(regexString, gtk_entry_get_text(GTK_ENTRY(widgetRegex)));
  }

  // free resources
  gtk_widget_destroy(dialog);

  return (result == GTK_RESPONSE_ACCEPT);
}

/***********************************************************************\
* Name   : addRegex
* Purpose: add regular expression
* Input  : sample - sample for regex-match or ""
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void addRegex(const gchar *sample)
{
  g_assert(geany_data != NULL);
  g_assert(geany_data->main_widgets != NULL);

  GString    *regexLanguageString = g_string_new(NULL);
  GString    *regexGroupString    = g_string_new(NULL);
  RegexTypes regexType            = REGEX_TYPE_NONE;
  GString    *regexString         = g_string_new(NULL);
  if (dialogRegex(GTK_WINDOW(geany_data->main_widgets->window),
                  _("Add regular expression"),
                  _("Regular expression:"),
                  regexLanguageString,
                  regexGroupString,
                  &regexType,
                  regexString,
                  sample
                 )
     )
  {
    g_assert(regexType >= REGEX_TYPE_MIN);
    g_assert(regexType <= REGEX_TYPE_MAX);

    if (!existsRegex(pluginData.configuration.regexStore,
                     regexLanguageString->str,
                     regexGroupString->str,
                     regexType,
                     regexString->str
                    )
       )
    {
      gtk_list_store_insert_with_values(pluginData.configuration.regexStore,
                                        NULL,
                                        -1,
                                        MODEL_REGEX_LANGUAGE, regexLanguageString->str,
                                        MODEL_REGEX_GROUP,    regexGroupString->str,
                                        MODEL_REGEX_TYPE,     regexType,
                                        MODEL_REGEX_REGEX,    regexString->str,
                                        MODEL_END
                                       );
    }
  }
  g_string_free(regexString, TRUE);
  g_string_free(regexGroupString, TRUE);
  g_string_free(regexLanguageString, TRUE);
}

/***********************************************************************\
* Name   : cloneRegex
* Purpose: clone regular expression
* Input  : listStore    - model
*          treeIterator - iterator in model
*          sample       - sample for regex-match or ""
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void cloneRegex(GtkListStore *listStore,
                      GtkTreeIter  *treeIterator,
                      const gchar  *sample
                     )
{
  g_assert(geany_data != NULL);
  g_assert(geany_data->main_widgets != NULL);
  g_assert(listStore != NULL);
  g_assert(treeIterator != NULL);

  gchar       *language;
  gchar       *group;
  RegexTypes  regexType;
  gchar       *regex;
  gtk_tree_model_get(GTK_TREE_MODEL(listStore),
                     treeIterator,
                     MODEL_REGEX_LANGUAGE, &language,
                     MODEL_REGEX_GROUP,    &group,
                     MODEL_REGEX_TYPE,     &regexType,
                     MODEL_REGEX_REGEX,    &regex,
                     MODEL_END
                    );
  g_assert(language != NULL);
  g_assert(group != NULL);
  g_assert(regexType >= REGEX_TYPE_MIN);
  g_assert(regexType <= REGEX_TYPE_MAX);
  g_assert(regex != NULL);

  GString *languageString = g_string_new(language);
  GString *groupString    = g_string_new(group);
  GString *regexString    = g_string_new(regex);
  if (dialogRegex(GTK_WINDOW(geany_data->main_widgets->window),
                  _("Clone regular expression"),
                  _("Regular expression:"),
                  languageString,
                  groupString,
                  &regexType,
                  regexString,
                  sample
                 )
     )
  {
    g_assert(regexType >= REGEX_TYPE_MIN);
    g_assert(regexType <= REGEX_TYPE_MAX);

    if (!existsRegex(pluginData.configuration.regexStore,
                     languageString->str,
                     groupString->str,
                     regexType,
                     regexString->str
                    )
       )
    {
      gtk_list_store_insert_with_values(pluginData.configuration.regexStore,
                                        NULL,
                                        -1,
                                        MODEL_REGEX_LANGUAGE, languageString->str,
                                        MODEL_REGEX_GROUP,    groupString->str,
                                        MODEL_REGEX_TYPE,     regexType,
                                        MODEL_REGEX_REGEX,    regexString->str,
                                        MODEL_END
                                       );
    }
  }
  g_string_free(regexString, TRUE);
  g_string_free(groupString, TRUE);
  g_string_free(languageString, TRUE);

  g_free(regex);
  g_free(group);
  g_free(language);
}

/***********************************************************************\
* Name   : editRegex
* Purpose: edit regular expression
* Input  : listStore    - model
*          treeIterator - iterator in model
*          sample       - sample for regex-match or ""
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void editRegex(GtkListStore *listStore,
                     GtkTreeIter  *treeIterator,
                     const gchar  *sample
                    )
{
  g_assert(geany_data != NULL);
  g_assert(geany_data->main_widgets != NULL);
  g_assert(listStore != NULL);
  g_assert(treeIterator != NULL);

  gchar       *language;
  gchar       *group;
  RegexTypes  regexType;
  gchar       *regex;
  gtk_tree_model_get(GTK_TREE_MODEL(listStore),
                     treeIterator,
                     MODEL_REGEX_LANGUAGE, &language,
                     MODEL_REGEX_GROUP,    &group,
                     MODEL_REGEX_TYPE,     &regexType,
                     MODEL_REGEX_REGEX,    &regex,
                     MODEL_END
                    );
  g_assert(language != NULL);
  g_assert(group != NULL);
  g_assert(regexType >= REGEX_TYPE_MIN);
  g_assert(regexType <= REGEX_TYPE_MAX);
  g_assert(regex != NULL);

  GString *languageString = g_string_new(language);
  GString *groupString    = g_string_new(group);
  GString *regexString    = g_string_new(regex);
  if (dialogRegex(GTK_WINDOW(geany_data->main_widgets->window),
                  _("Edit regular expression"),
                  _("Regular expression:"),
                  languageString,
                  groupString,
                  &regexType,
                  regexString,
                  sample
                 )
     )
  {
    g_assert(regexType >= REGEX_TYPE_MIN);
    g_assert(regexType <= REGEX_TYPE_MAX);

    gtk_list_store_set(listStore,
                       treeIterator,
                       MODEL_REGEX_LANGUAGE, languageString->str,
                       MODEL_REGEX_GROUP,    groupString->str,
                       MODEL_REGEX_TYPE,     regexType,
                       MODEL_REGEX_REGEX,    regexString->str,
                       MODEL_END
                      );
  }
  g_string_free(regexString, TRUE);
  g_string_free(groupString, TRUE);
  g_string_free(languageString, TRUE);

  g_free(regex);
  g_free(group);
  g_free(language);
}

// ---------------------------------------------------------------------

/***********************************************************************\
* Name   : scrollToMessage
* Purpose: scroll message list and show message on top
* Input  : messageTreePath - message tree path
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void scrollToMessage(const gchar *messageTreePath)
{
  g_assert(messageTreePath != NULL);

  GtkTreePath *treePath = gtk_tree_path_new_from_string(messageTreePath);
  g_assert(treePath != NULL);

  gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(pluginData.widgets.messagesList),
                               treePath,
                               NULL,  // column
                               TRUE,  // use_align
                               0.0,  // row_align
                               0.0  // col_align
                              );

  gtk_tree_path_free(treePath);
}

/***********************************************************************\
* Name   : showSource
* Purpose: show source, load document if not already loaded
* Input  : directory               - directory
*          filePath                - file path
*          lineNumber,columnNumber - line/column number
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void showSource(const gchar *directory,
                      const gchar *filePath,
                      gint        lineNumber,
                      gint        columnNumber
                     )
{
  g_assert(geany_data != NULL);
  g_assert(geany_data->main_widgets != NULL);
  g_assert(filePath != NULL);

  // get absolute path
  gchar *absoluteFilePath = getAbsolutePath(directory,filePath);
  gchar *absoluteFilePathLocale = utils_get_locale_from_utf8(absoluteFilePath);

  // find/open document
  GeanyDocument *document = document_find_by_filename(filePath);
  if ((document == NULL) && !stringIsEmpty(absoluteFilePathLocale))
  {
    document = document_open_file(absoluteFilePathLocale, FALSE, NULL, NULL);
  }
  if (document != NULL)
  {
    // goto line
    GeanyDocument *currentDocument = document_get_current();
    navqueue_goto_line(currentDocument, document, lineNumber);

    // goto exact position in line
    gint n =   sci_get_position_from_line(document->editor->sci, (lineNumber > 0) ? lineNumber - 1 : 0)
             + ((columnNumber > 0) ? columnNumber - 1 : 0);
    sci_set_current_position(document->editor->sci, n, TRUE);


    // show document and set focus
    gint pageIndex = document_get_notebook_page(document);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(geany_data->main_widgets->notebook),
                                  pageIndex
                                 );
    gtk_widget_grab_focus(GTK_WIDGET(document->editor->sci));
  }
  else
  {
    ui_set_statusbar(FALSE, _("Could not find file '%s'"), filePath);
  }

  // free resources
  g_free(absoluteFilePathLocale);
  g_free(absoluteFilePath);
}

/***********************************************************************\
* Name   : showPrevError
* Purpose: show orevious error
* Input  : -
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void showPrevError()
{
  showBuildErrorsTab();

  // show source of error
  GtkTreeModel *treeModel = gtk_tree_view_get_model(GTK_TREE_VIEW(pluginData.widgets.errorsTree));
  g_assert(treeModel != NULL);

  gchar *messageTreePath;
  gchar *directory, *filePath;
  gint  lineNumber, columnNumber;
  gtk_tree_model_get(treeModel,
                     &pluginData.build.errorsTreeIterator,
                     MODEL_ERROR_WARNING_TREE_PATH,     &messageTreePath,
                     MODEL_ERROR_WARNING_DIRECTORY,     &directory,
                     MODEL_ERROR_WARNING_FILE_PATH,     &filePath,
                     MODEL_ERROR_WARNING_LINE_NUMBER,   &lineNumber,
                     MODEL_ERROR_WARNING_COLUMN_NUMBER, &columnNumber,
                     MODEL_END
                    );
  if (messageTreePath != NULL)
  {
    scrollToMessage(messageTreePath);
  }
  g_assert(filePath != NULL);
  showSource(directory, filePath, lineNumber, columnNumber);
  g_free(filePath);
  g_free(directory);
  g_free(messageTreePath);

  // previous error
  GtkTreeIter treeIterator = pluginData.build.errorsTreeIterator;
  if (!gtk_tree_model_iter_previous(treeModel,
                                    &pluginData.build.errorsTreeIterator
                                   )
     )
  {
    pluginData.build.errorsTreeIterator = treeIterator;
  }
}

/***********************************************************************\
* Name   : showNextError
* Purpose: show next error
* Input  : -
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void showNextError()
{
  showBuildErrorsTab();

  // show source of error
  GtkTreeModel *treeModel = gtk_tree_view_get_model(GTK_TREE_VIEW(pluginData.widgets.errorsTree));
  g_assert(treeModel != NULL);

  gchar *messageTreePath;
  gchar *directory, *filePath;
  gint  lineNumber, columnNumber;
  gtk_tree_model_get(treeModel,
                     &pluginData.build.errorsTreeIterator,
                     MODEL_ERROR_WARNING_TREE_PATH,     &messageTreePath,
                     MODEL_ERROR_WARNING_DIRECTORY,     &directory,
                     MODEL_ERROR_WARNING_FILE_PATH,     &filePath,
                     MODEL_ERROR_WARNING_LINE_NUMBER,   &lineNumber,
                     MODEL_ERROR_WARNING_COLUMN_NUMBER, &columnNumber,
                     MODEL_END
                    );
  if (messageTreePath != NULL)
  {
    scrollToMessage(messageTreePath);
  }
  g_assert(filePath != NULL);
  showSource(directory, filePath, lineNumber, columnNumber);
  g_free(filePath);
  g_free(directory);
  g_free(messageTreePath);

  // next error
  GtkTreeIter treeIterator = pluginData.build.errorsTreeIterator;
  if (!gtk_tree_model_iter_next(treeModel,
                                &pluginData.build.errorsTreeIterator
                               )
     )
  {
    pluginData.build.errorsTreeIterator = treeIterator;
  }
}

/***********************************************************************\
* Name   : showPrevWarning
* Purpose: show previous warning
* Input  : -
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void showPrevWarning()
{
  showBuildWarningsTab();

  // show source of warning
  GtkTreeModel *treeModel = gtk_tree_view_get_model(GTK_TREE_VIEW(pluginData.widgets.warningsTree));
  g_assert(treeModel != NULL);

  gchar *messageTreePath;
  gchar *directory, *filePath;
  gint  lineNumber, columnNumber;
  gtk_tree_model_get(treeModel,
                     &pluginData.build.warningsTreeIterator,
                     MODEL_ERROR_WARNING_TREE_PATH,     &messageTreePath,
                     MODEL_ERROR_WARNING_DIRECTORY,     &directory,
                     MODEL_ERROR_WARNING_FILE_PATH,     &filePath,
                     MODEL_ERROR_WARNING_LINE_NUMBER,   &lineNumber,
                     MODEL_ERROR_WARNING_COLUMN_NUMBER, &columnNumber,
                     MODEL_END
                    );
  if (messageTreePath != NULL)
  {
    scrollToMessage(messageTreePath);
  }
  g_assert(filePath != NULL);
  showSource(directory, filePath, lineNumber, columnNumber);
  g_free(filePath);
  g_free(directory);
  g_free(messageTreePath);

  // previous warning
  GtkTreeIter treeIterator = pluginData.build.warningsTreeIterator;
  if (!gtk_tree_model_iter_previous(treeModel,
                                    &pluginData.build.warningsTreeIterator
                                   )
     )
  {
    pluginData.build.warningsTreeIterator = treeIterator;
  }
}

/***********************************************************************\
* Name   : showNextWarning
* Purpose: show next warning
* Input  : -
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void showNextWarning()
{
  showBuildWarningsTab();

  // show source of warning
  GtkTreeModel *treeModel = gtk_tree_view_get_model(GTK_TREE_VIEW(pluginData.widgets.warningsTree));
  g_assert(treeModel != NULL);

  gchar *messageTreePath;
  gchar *directory, *filePath;
  gint  lineNumber, columnNumber;
  gtk_tree_model_get(treeModel,
                     &pluginData.build.warningsTreeIterator,
                     MODEL_ERROR_WARNING_TREE_PATH,     &messageTreePath,
                     MODEL_ERROR_WARNING_DIRECTORY,     &directory,
                     MODEL_ERROR_WARNING_FILE_PATH,     &filePath,
                     MODEL_ERROR_WARNING_LINE_NUMBER,   &lineNumber,
                     MODEL_ERROR_WARNING_COLUMN_NUMBER, &columnNumber,
                     MODEL_END
                    );
  if (messageTreePath != NULL)
  {
    scrollToMessage(messageTreePath);
  }
  g_assert(filePath != NULL);
  showSource(directory, filePath, lineNumber, columnNumber);
  g_free(filePath);
  g_free(directory);
  g_free(messageTreePath);

  // next warning
  GtkTreeIter treeIterator = pluginData.build.warningsTreeIterator;
  if (!gtk_tree_model_iter_next(treeModel,
                                &pluginData.build.warningsTreeIterator
                               )
     )
  {
    pluginData.build.warningsTreeIterator = treeIterator;
  }
}

/***********************************************************************\
* Name   : isMatchingRegex
* Purpose: check if regular expression match to line
* Input  : regexString - regular expression string
*          line        - line
* Output : matchCount    - match count
*          directoryPath - directory path
*          filePath      - file path
*          lineNumber    - line number
*          columnNumber  - column number
*          message       - message
* Return : TRUE iff reqular expression matches line
* Notes  : -
\***********************************************************************/

LOCAL gboolean isMatchingRegex(const gchar  *regexString,
                               const gchar  *line,
                               gint         *matchCount,
                               GString      *directoryPath,
                               GString      *filePath,
                               guint        *lineNumber,
                               guint        *columnNumber,
                               GString      *message
                              )
{
  gboolean matchesRegEx = FALSE;

  GRegex      *regex;
  GMatchInfo  *matchInfo;

  g_assert(regexString != NULL);
  g_assert(line != NULL);
  g_assert(filePath != NULL);
  g_assert(lineNumber != NULL);
  g_assert(message != NULL);

  (*matchCount) = 0;

  regex = g_regex_new(regexString,
                      0, // compile_optipns
                      0, // match option
                      NULL // error
                     );
  if (regex != NULL)
  {
    matchesRegEx = g_regex_match(regex, line, 0, &matchInfo);
    if (matchesRegEx)
    {
      gint directoryMatchNumber    = g_regex_get_string_number(regex, "directory");
      gint filePathMatchNumber     = g_regex_get_string_number(regex, "filePath");
      gint lineNumberMatchNumber   = g_regex_get_string_number(regex, "lineNumber");
      gint columnNumberMatchNumber = g_regex_get_string_number(regex, "columnNumber");
      gint messageMatchNumber      = g_regex_get_string_number(regex, "message");
      if (directoryMatchNumber    >= 0) { if (matchCount != NULL) (*matchCount)++; } else { directoryMatchNumber    = 1; }
      if (filePathMatchNumber     >= 0) { if (matchCount != NULL) (*matchCount)++; } else { filePathMatchNumber     = 2; }
      if (lineNumberMatchNumber   >= 0) { if (matchCount != NULL) (*matchCount)++; } else { lineNumberMatchNumber   = 3; }
      if (columnNumberMatchNumber >= 0) { if (matchCount != NULL) (*matchCount)++; } else { columnNumberMatchNumber = 4; }
      if (messageMatchNumber      >= 0) { if (matchCount != NULL) (*matchCount)++; } else { messageMatchNumber      = 5; }

      g_string_assign(directoryPath,
                      (g_match_info_get_match_count(matchInfo) > directoryMatchNumber)
                        ? g_match_info_fetch(matchInfo, directoryMatchNumber)
                        : g_strdup("")
                     );
      g_string_assign(filePath,
                      (g_match_info_get_match_count(matchInfo) > filePathMatchNumber)
                        ? g_match_info_fetch(matchInfo, filePathMatchNumber)
                        : g_strdup("")
                     );
      (*lineNumber)   = (g_match_info_get_match_count(matchInfo) > lineNumberMatchNumber)
                          ? (guint)g_ascii_strtoull(g_match_info_fetch(matchInfo, lineNumberMatchNumber), NULL, 10)
                          : 0;
      (*columnNumber) = (g_match_info_get_match_count(matchInfo) > columnNumberMatchNumber)
                          ? (guint)g_ascii_strtoull(g_match_info_fetch(matchInfo, columnNumberMatchNumber), NULL, 10)
                          : 0;
      g_string_assign(message,
                      (g_match_info_get_match_count(matchInfo) > messageMatchNumber)
                        ? g_match_info_fetch(matchInfo, messageMatchNumber)
                        : g_strdup("")
                     );
    }
    g_match_info_free(matchInfo);
    g_regex_unref(regex);
  }

  return matchesRegEx;
}

/***********************************************************************\
* Name   : isMatchingRegexs
* Purpose: check if regular expression from model match to line
* Input  : listStore       - model with regular expressions
*          regexTypeFilter - regular expression types filter
*          line            - line
* Output : matchTreePathString - best matching tree path string
*          filePathString      - file path on match
*          lineNumber          - line number on match
*          columnNumber        - column number on match
*          messageString       - message on match
* Return : TRUE iff at least one reqular expression matches line
* Notes  : -
\***********************************************************************/

LOCAL gboolean isMatchingRegexs(GtkListStore *listStore,
                                const gchar  *line,
                                GString      *matchTreePathString,
                                RegexTypes   *regexType,
                                GString      *filePathString,
                                guint        *lineNumber,
                                guint        *columnNumber,
                                GString      *messageString
                               )
{
  g_assert(listStore != NULL);
  g_assert(line != NULL);
  g_assert(matchTreePathString != NULL);
  g_assert(regexType != NULL);
  g_assert(filePathString != NULL);
  g_assert(lineNumber != NULL);
  g_assert(columnNumber != NULL);
  g_assert(messageString != NULL);

  gint bestMatchCount = -1;

  (*regexType)    = REGEX_TYPE_NONE;
  g_string_assign(filePathString, "");
  (*lineNumber)   = 0;
  (*columnNumber) = 0;
  g_string_assign(messageString, "");

  GtkTreeIter treeIterator;
  if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(listStore), &treeIterator))
  {
    GString *matchDirectoryPathString = g_string_new(NULL);
    GString *matchFilePathString      = g_string_new(NULL);
    uint    matchLineNumber;
    uint    matchColumnNumber;
    GString *matchMessageString       = g_string_new(NULL);
    do
    {
      gchar      *checkRegExLanguage;
      RegexTypes checkRegExType;
      gchar      *checkRegEx;
      gtk_tree_model_get(GTK_TREE_MODEL(listStore),
                         &treeIterator,
                         MODEL_REGEX_LANGUAGE, &checkRegExLanguage,
                         MODEL_REGEX_TYPE,     &checkRegExType,
                         MODEL_REGEX_REGEX,    &checkRegEx,
                         MODEL_END
                        );
      g_assert(checkRegExLanguage != NULL);
      g_assert(checkRegExType >= REGEX_TYPE_MIN);
      g_assert(checkRegExType <= REGEX_TYPE_MAX);
      g_assert(checkRegEx != NULL);

//fprintf(stderr,"%s:%d: checkRegExLanguage=%s, checkRegEx=%s\n",__FILE__,__LINE__,checkRegExLanguage,checkRegEx);

#if 0
      // get current document (if possible)
      GeanyDocument *document = document_get_current();
      GeanyFiletype *fileType = filetypes_lookup_by_name(checkRegExLanguage);
      if (   (document == NULL)
          || (fileType == NULL)
          || (document->file_type == NULL)
          || (document->file_type->id == fileType->id)
         )
#endif
      {
#if 0
        GRegex     *regex;
        GMatchInfo *matchInfo;
        regex = g_regex_new(checkRegEx,
                            0, // compile_optipns
                            0, // match option
                            NULL // error
                           );
        if (regex != NULL)
        {
          if (g_regex_match(regex, line, 0, &matchInfo))
          {
            gint directoryMatchNumber    = g_regex_get_string_number(regex, "directory");
            gint filePathMatchNumber     = g_regex_get_string_number(regex, "filePath");
            gint lineNumberMatchNumber   = g_regex_get_string_number(regex, "lineNumber");
            gint columnNumberMatchNumber = g_regex_get_string_number(regex, "columnNumber");
            gint messageMatchNumber      = g_regex_get_string_number(regex, "message");

            gint matchCount = 0;
            if (directoryMatchNumber    >= 0) { matchCount++; };// else { directoryMatchNumber    = 1; }
            if (filePathMatchNumber     >= 0) { matchCount++; };// else { filePathMatchNumber     = 1; }
            if (lineNumberMatchNumber   >= 0) { matchCount++; };// else { lineNumberMatchNumber   = 2; }
            if (columnNumberMatchNumber >= 0) { matchCount++; };// else { columnNumberMatchNumber = 3; }
            if (messageMatchNumber      >= 0) { matchCount++; };// else { messageMatchNumber      = 4; }

            if (matchCount > bestMatchCount)
            {
              gchar *matchTreePath = gtk_tree_model_get_string_from_iter(GTK_TREE_MODEL(listStore), &treeIter);
              g_string_assign(matchTreePathString, matchTreePath);
              g_free(matchTreePath);

              (*regexType) = checkRegExType;

              // get directory
              if      (directoryMatchNumber >= 0)
              {
                if (g_match_info_get_match_count(matchInfo) > directoryMatchNumber)
                {
                  gchar *string = g_match_info_fetch(matchInfo, directoryMatchNumber);
                  g_string_assign(filePathString, string);
                  g_free(string);
                }
              }
              else if (filePathMatchNumber >= 0)
              {
                if (g_match_info_get_match_count(matchInfo) > filePathMatchNumber)
                {
                  gchar *string = g_match_info_fetch(matchInfo, filePathMatchNumber);
                  g_string_assign(filePathString, string);
                  g_free(string);
                }
              }

              // get line number
              if (lineNumberMatchNumber >= 0)
              {
                if (g_match_info_get_match_count(matchInfo) > lineNumberMatchNumber)
                {
                  gchar *string = g_match_info_fetch(matchInfo, lineNumberMatchNumber);
                  (*lineNumber) = (guint)g_ascii_strtoull(string, NULL, 10);
                  g_free(string);
                }
              }

              // get column number
              if (columnNumberMatchNumber >= 0)
              {
                if (g_match_info_get_match_count(matchInfo) > columnNumberMatchNumber)
                {
                  gchar *string = g_match_info_fetch(matchInfo, columnNumberMatchNumber);
                  (*columnNumber) = (guint)g_ascii_strtoull(string, NULL, 10);
                  g_free(string);
                }
              }

              // get message
              if (messageMatchNumber >= 0)
              {
                if (g_match_info_get_match_count(matchInfo) > messageMatchNumber)
                {
                  gchar *string = g_match_info_fetch(matchInfo, messageMatchNumber);
                  g_string_assign(messageString, string);
                  g_free(string);
                }
              }

              bestMatchCount = matchCount;
            }
          }
          g_match_info_free(matchInfo);
          g_regex_unref(regex);
        }
#else
        gint matchCount;
        if (isMatchingRegex(checkRegEx,
                            line,
                            &matchCount,
                            matchDirectoryPathString,
                            matchFilePathString,
                            &matchLineNumber,
                            &matchColumnNumber,
                            matchMessageString
                           )
           )
        {
//fprintf(stderr,"%s:%d: checkregex=%s line=%s -> matchCount=%d\n",__FILE__,__LINE__,checkRegEx,line,matchCount);
          if (matchCount > bestMatchCount)
          {
            gchar *matchTreePath = gtk_tree_model_get_string_from_iter(GTK_TREE_MODEL(listStore), &treeIterator);
            g_string_assign(matchTreePathString, matchTreePath);
            g_free(matchTreePath);

            (*regexType) = checkRegExType;

            g_string_assign(filePathString, matchFilePathString->str);
            (*lineNumber)   = matchLineNumber;
            (*columnNumber) = matchColumnNumber;
            g_string_assign(messageString, matchMessageString->str);
            bestMatchCount  = matchCount;
          }
        }
#endif
      }
      g_free(checkRegEx);
      g_free(checkRegExLanguage);
    }
    while (gtk_tree_model_iter_next(GTK_TREE_MODEL(listStore), &treeIterator));
    g_string_free(matchMessageString,TRUE);
    g_string_free(matchFilePathString,TRUE);
    g_string_free(matchDirectoryPathString,TRUE);
  }
//fprintf(stderr,"%s:%d: bestMatchCount=%d\n",__FILE__,__LINE__,bestMatchCount);

  return (bestMatchCount >= 0);
}

/***********************************************************************\
* Name   : getWorkingDirectory
* Purpose: get working directory
* Input  : workingDirectoryTemplate - working directory template or NULL
* Output : -
* Return : working directory
* Notes  : -
\***********************************************************************/

LOCAL const gchar *getWorkingDirectory(const gchar *workingDirectoryTemplate)
{
  // get project
  GeanyProject *project = geany_data->app->project;

  // get current document (if possible)
  GeanyDocument *document = document_get_current();

  // get working directory
  gchar *workingDirectory;
  if (!stringIsEmpty(workingDirectoryTemplate))
  {
    workingDirectory = expandMacros(project,
                                    document,
                                    workingDirectoryTemplate,
                                    NULL  // customText
                                   );
  }
  else
  {
    // if no working directory is given, use file/project or current directory
    if      (project != NULL)
    {
      workingDirectory = g_strdup(project->base_path);
    }
    else if (document != NULL)
    {
      workingDirectory = g_path_get_dirname(document->file_name);
    }
    else
    {
      workingDirectory = g_get_current_dir();
    }
  }
  g_assert(workingDirectory != NULL);

  return workingDirectory;
}

/***********************************************************************\
* Name   : onExecuteCommandParse
* Purpose: callback to parse command output (stdout/stderr)
* Input  : workingDirectory - working directory
*          line             - line (without trailing \n)
*          userData         - user data: TRUE to parse output
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onExecuteCommandParse(const gchar *workingDirectory,
                                 const gchar *line,
                                 void        *userData
                                )
{
  g_assert(line != NULL);
  g_assert(userData != NULL);

  gboolean parseOutput = (gboolean)GPOINTER_TO_UINT(userData);

  // prepare empty line in message tab
  gtk_list_store_insert(pluginData.build.messagesStore,
                        &pluginData.build.messageTreeIterator,
                        MODEL_END
                       );
  gchar *messageTreePath = gtk_tree_model_get_string_from_iter(GTK_TREE_MODEL(pluginData.build.messagesStore),
                                                               &pluginData.build.messageTreeIterator
                                                              );

  if (parseOutput)
  {
    // find match
    gchar       *absoluteDirectory   = NULL;
    RegexTypes  regexType;
    GString     *matchTreePathString = g_string_new(NULL);
    GString     *filePathString      = g_string_new(NULL);
    guint       lineNumber, columnNumber;
    GString     *messageString       = g_string_new(NULL);
    const gchar *messageColor        = COLOR_BUILD_MESSAGES;
    if      (isMatchingRegexs(pluginData.configuration.regexStore,
                              line,
                              matchTreePathString,
                              &regexType,
                              filePathString,
                              &lineNumber,
                              &columnNumber,
                              messageString
                             )
            )
    {
      switch (regexType)
      {
        case REGEX_TYPE_NONE:
          break;
        case REGEX_TYPE_ENTER:
          // get directory prefix
          string_stack_push(pluginData.build.directoryPrefixStack, filePathString->str);
          break;
        case REGEX_TYPE_LEAVE:
          // clear directory prefix
          string_stack_pop(pluginData.build.directoryPrefixStack);
          break;
        case REGEX_TYPE_ERROR:
          // insert error message
          absoluteDirectory = getAbsoluteDirectory(workingDirectory,
                                                   string_stack_top(pluginData.build.directoryPrefixStack),
                                                   NULL
                                                  );
          gtk_tree_store_insert_with_values(pluginData.build.errorsStore,
                                            &pluginData.build.insertIterator,
                                            NULL,  // parent
                                            -1,  // position
                                            MODEL_ERROR_WARNING_TREE_PATH,     messageTreePath,
                                            MODEL_ERROR_WARNING_DIRECTORY,     absoluteDirectory,
                                            MODEL_ERROR_WARNING_FILE_PATH,     filePathString->str,
                                            MODEL_ERROR_WARNING_LINE_NUMBER,   lineNumber,
                                            MODEL_ERROR_WARNING_COLUMN_NUMBER, columnNumber,
                                            MODEL_ERROR_WARNING_MESSAGE,       messageString->str,
                                            MODEL_END
                                           );

          // set indicator (if document is loaded)
          if (   pluginData.configuration.errorIndicators
              && (pluginData.build.errorWarningIndicatorsCount < MAX_ERROR_WARNING_INDICATORS)
             )
          {
            setIndicator(filePathString->str, lineNumber, &pluginData.configuration.errorIndicatorColor, ERROR_INDICATOR_INDEX);

            pluginData.build.errorWarningIndicatorsCount++;
          }

          // get message color
          messageColor = COLOR_BUILD_MESSAGES_MATCHED_ERROR;

          // save last insert position
          pluginData.build.lastErrorsWarningsInsertStore        = pluginData.build.errorsStore;
          pluginData.build.lastErrorsWarningsInsertTreeIterator = &pluginData.build.insertIterator;
          pluginData.build.lastErrorsWarningsInsertColor        = messageColor;
          break;
        case REGEX_TYPE_WARNING:
          // insert warning message
          absoluteDirectory = getAbsoluteDirectory(workingDirectory,
                                                   string_stack_top(pluginData.build.directoryPrefixStack),
                                                   NULL
                                                  );
          gtk_tree_store_insert_with_values(pluginData.build.warningsStore,
                                            &pluginData.build.insertIterator,
                                            NULL,  // parent
                                            -1,  // position
                                            MODEL_ERROR_WARNING_TREE_PATH,     messageTreePath,
                                            MODEL_ERROR_WARNING_DIRECTORY,     absoluteDirectory,
                                            MODEL_ERROR_WARNING_FILE_PATH,     filePathString->str,
                                            MODEL_ERROR_WARNING_LINE_NUMBER,   lineNumber,
                                            MODEL_ERROR_WARNING_COLUMN_NUMBER, columnNumber,
                                            MODEL_ERROR_WARNING_MESSAGE,       messageString->str,
                                            MODEL_END
                                           );

          // set indicator (if document is loaded)
          if (   pluginData.configuration.warningIndicators
              && (pluginData.build.errorWarningIndicatorsCount < MAX_ERROR_WARNING_INDICATORS)
             )
          {
            setIndicator(filePathString->str, lineNumber, &pluginData.configuration.warningIndicatorColor, WARNING_INDICATOR_INDEX);

            pluginData.build.errorWarningIndicatorsCount++;
          }

          // get message color
          messageColor = COLOR_BUILD_MESSAGES_MATCHED_WARNING;

          // save last insert position
          pluginData.build.lastErrorsWarningsInsertStore        = pluginData.build.warningsStore;
          pluginData.build.lastErrorsWarningsInsertTreeIterator = &pluginData.build.insertIterator;
          pluginData.build.lastErrorsWarningsInsertColor        = messageColor;
          break;
        case REGEX_TYPE_EXTENSION:
          // append to last error/warning message
          if (   (pluginData.build.lastErrorsWarningsInsertStore != NULL)
              && (pluginData.build.lastErrorsWarningsInsertTreeIterator != NULL)
             )
          {
            absoluteDirectory = getAbsoluteDirectory(workingDirectory,
                                                     string_stack_top(pluginData.build.directoryPrefixStack),
                                                   NULL
                                                    );
            gtk_tree_store_insert_with_values(pluginData.build.lastErrorsWarningsInsertStore,
                                              NULL,
                                              pluginData.build.lastErrorsWarningsInsertTreeIterator,
                                              -1,  // position
                                              MODEL_ERROR_WARNING_TREE_PATH,     messageTreePath,
                                              MODEL_ERROR_WARNING_DIRECTORY,     absoluteDirectory,
                                              MODEL_ERROR_WARNING_FILE_PATH,     filePathString->str,
                                              MODEL_ERROR_WARNING_LINE_NUMBER,   lineNumber,
                                              MODEL_ERROR_WARNING_COLUMN_NUMBER, columnNumber,
                                              MODEL_ERROR_WARNING_MESSAGE,       messageString->str,
                                              MODEL_END
                                             );
          }

          // get message color
          messageColor = pluginData.build.lastErrorsWarningsInsertColor;
          break;
      }
    }
    else if (   (pluginData.projectProperties.errorRegEx->len > 0)
             && isMatchingRegex(pluginData.projectProperties.errorRegEx->str,
                                line,
                                NULL,  // matchCoung
                                NULL,  // directortPathString
                                filePathString,
                                &lineNumber,
                                &columnNumber,
                                messageString
                               )

            )
    {
      // insert error message
      absoluteDirectory = getAbsoluteDirectory(workingDirectory,
                                               string_stack_top(pluginData.build.directoryPrefixStack),
                                               NULL
                                              );
      gtk_tree_store_insert_with_values(pluginData.build.errorsStore,
                                        &pluginData.build.insertIterator,
                                        NULL,  // parent
                                        -1,  // position
                                        MODEL_ERROR_WARNING_TREE_PATH,     messageTreePath,
                                        MODEL_ERROR_WARNING_DIRECTORY,     absoluteDirectory,
                                        MODEL_ERROR_WARNING_FILE_PATH,     filePathString->str,
                                        MODEL_ERROR_WARNING_LINE_NUMBER,   lineNumber,
                                        MODEL_ERROR_WARNING_COLUMN_NUMBER, columnNumber,
                                        MODEL_ERROR_WARNING_MESSAGE,       messageString->str,
                                        MODEL_END
                                       );

      // set indicator (if document is loaded)
      if (   pluginData.configuration.errorIndicators
          && (pluginData.build.errorWarningIndicatorsCount < MAX_ERROR_WARNING_INDICATORS)
         )
      {
        setIndicator(filePathString->str, lineNumber, &pluginData.configuration.errorIndicatorColor, ERROR_INDICATOR_INDEX);

        pluginData.build.errorWarningIndicatorsCount++;
      }

      // save last insert position
      pluginData.build.lastErrorsWarningsInsertStore        = pluginData.build.errorsStore;
      pluginData.build.lastErrorsWarningsInsertTreeIterator = &pluginData.build.insertIterator;

      // get message color
      messageColor = COLOR_BUILD_MESSAGES_MATCHED_ERROR;
    }
    else if (   (pluginData.projectProperties.warningRegEx->len > 0)
             && isMatchingRegex(pluginData.projectProperties.warningRegEx->str,
                                line,
                                NULL,  // matchCoung
                                NULL,  // matchDirectoryPathString
                                filePathString,
                                &lineNumber,
                                &columnNumber,
                                messageString
                               )

            )
    {
      // insert warning message
      absoluteDirectory = getAbsoluteDirectory(workingDirectory,
                                               string_stack_top(pluginData.build.directoryPrefixStack),
                                               NULL
                                              );
      gtk_tree_store_insert_with_values(pluginData.build.warningsStore,
                                        &pluginData.build.insertIterator,
                                        NULL,  // parent
                                        -1,  // position
                                        MODEL_ERROR_WARNING_TREE_PATH,     messageTreePath,
                                        MODEL_ERROR_WARNING_DIRECTORY,     absoluteDirectory,
                                        MODEL_ERROR_WARNING_FILE_PATH,     filePathString->str,
                                        MODEL_ERROR_WARNING_LINE_NUMBER,   lineNumber,
                                        MODEL_ERROR_WARNING_COLUMN_NUMBER, columnNumber,
                                        MODEL_ERROR_WARNING_MESSAGE,       messageString->str,
                                        MODEL_END
                                       );

      // set indicator (if document is loaded)
      if (   pluginData.configuration.warningIndicators
          && (pluginData.build.errorWarningIndicatorsCount < MAX_ERROR_WARNING_INDICATORS)
         )
      {
        setIndicator(filePathString->str, lineNumber, &pluginData.configuration.warningIndicatorColor, WARNING_INDICATOR_INDEX);

        pluginData.build.errorWarningIndicatorsCount++;
      }

      // save last insert position
      pluginData.build.lastErrorsWarningsInsertStore        = pluginData.build.warningsStore;
      pluginData.build.lastErrorsWarningsInsertTreeIterator = &pluginData.build.insertIterator;

      // get message color
      messageColor = COLOR_BUILD_MESSAGES_MATCHED_WARNING;
    }

    // set message in builder message tab
    gtk_list_store_set(pluginData.build.messagesStore,
                       &pluginData.build.messageTreeIterator,
                       MODEL_MESSAGE_COLOR,         messageColor,
                       MODEL_MESSAGE_TREE_PATH,     matchTreePathString->str,
                       MODEL_MESSAGE_DIRECTORY,     absoluteDirectory,
                       MODEL_MESSAGE_FILE_PATH,     filePathString->str,
                       MODEL_MESSAGE_LINE_NUMBER,   lineNumber,
                       MODEL_MESSAGE_COLUMN_NUMBER, columnNumber,
                       MODEL_MESSAGE_MESSAGE,       line,
                       MODEL_END
                      );

// TODO: msgwin_compiler_add(COLOR_BLUE, _("%s (in directory: %s)"), cmd, utf8_working_dir);
    showLastLine(GTK_SCROLLED_WINDOW(pluginData.widgets.messagesTab));

    g_string_free(messageString, TRUE);
    g_string_free(filePathString, TRUE);
    g_string_free(matchTreePathString, TRUE);
    g_free(absoluteDirectory);
    g_free(messageTreePath);

    // update number of errors/warnings
    GString *string = g_string_new(NULL);
    uint n;
    n = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(pluginData.build.errorsStore),NULL);
    if (n > 0)
    {
      g_string_printf(string, "Errors [%u]", n);
    }
    else
    {
      g_string_printf(string, "Errors");
    }
    gtk_label_set_text(GTK_LABEL(pluginData.widgets.errorsTabLabel), string->str);
    n = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(pluginData.build.warningsStore),NULL);
    if (n > 0)
    {
      g_string_printf(string, "Warnings [%u]", n);
    }
    else
    {
      g_string_printf(string, "Warnings");
    }
    gtk_label_set_text(GTK_LABEL(pluginData.widgets.warningsTabLabel), string->str);

    if (!pluginData.build.showedFirstErrorWarning)
    {
      // initialise prev/next error/warning iterators
      pluginData.build.errorsTreeIterorValid = gtk_tree_model_get_iter_first(gtk_tree_view_get_model(GTK_TREE_VIEW(pluginData.widgets.errorsTree)),
                                                                             &pluginData.build.errorsTreeIterator
                                                                            );
      pluginData.build.warningsTreeIterorValid = gtk_tree_model_get_iter_first(gtk_tree_view_get_model(GTK_TREE_VIEW(pluginData.widgets.warningsTree)),
                                                                               &pluginData.build.warningsTreeIterator
                                                                              );

      // show first error/warning
      if      (pluginData.configuration.autoShowFirstError && pluginData.build.errorsTreeIterorValid)
      {
        showBuildErrorsTab();
        showNextError();
        pluginData.build.showedFirstErrorWarning = TRUE;
      }
      else if (pluginData.configuration.autoShowFirstWarning && pluginData.build.warningsTreeIterorValid)
      {
        showBuildWarningsTab();
        showNextWarning();
        pluginData.build.showedFirstErrorWarning = TRUE;
      }
    }
    g_string_free(string, TRUE);
  }
  else
  {
    // set message in builder message tab
    gtk_list_store_set(pluginData.build.messagesStore,
                       &pluginData.build.messageTreeIterator,
                       MODEL_MESSAGE_COLOR,         COLOR_BUILD_MESSAGES,
                       MODEL_MESSAGE_MESSAGE,       line,
                       MODEL_END
                      );

    // insert into geany message tab
    msgwin_msg_add(COLOR_BLACK, -1, NULL, "%s", line);
  }
}

/***********************************************************************\
* Name   : onExecuteCommandExit
* Purpose: callback on command execute exit
* Input  : status   - status
*          userData - user data (not used)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onExecuteCommandExit(gint status, void *userData)
{
  UNUSED_VARIABLE(userData);

  gchar *doneMessage = g_strdup_printf(_("Build done (exit code: %d)"), status);
  gtk_list_store_insert_with_values(pluginData.build.messagesStore,
                                    NULL,
                                    -1,
                                    MODEL_MESSAGE_COLOR,   COLOR_BUILD_INFO,
                                    MODEL_MESSAGE_MESSAGE, doneMessage,
                                    MODEL_END
                                   );
  g_free(doneMessage);
  showLastLine(GTK_SCROLLED_WINDOW(pluginData.widgets.messagesTab));

  // initialise prev/next error/warning iterators
  pluginData.build.errorsTreeIterorValid = gtk_tree_model_get_iter_first(gtk_tree_view_get_model(GTK_TREE_VIEW(pluginData.widgets.errorsTree)),
                                                                         &pluginData.build.errorsTreeIterator
                                                                        );
  pluginData.build.warningsTreeIterorValid = gtk_tree_model_get_iter_first(gtk_tree_view_get_model(GTK_TREE_VIEW(pluginData.widgets.warningsTree)),
                                                                           &pluginData.build.warningsTreeIterator
                                                                          );

  if (!pluginData.build.showedFirstErrorWarning)
  {
    // show first error/warning
    if      (pluginData.configuration.autoShowFirstError && pluginData.build.errorsTreeIterorValid)
    {
      showBuildErrorsTab();
      showNextError();
      pluginData.build.showedFirstErrorWarning = TRUE;
    }
    else if (pluginData.configuration.autoShowFirstWarning && pluginData.build.warningsTreeIterorValid)
    {
      showBuildWarningsTab();
      showNextWarning();
      pluginData.build.showedFirstErrorWarning = TRUE;
    }
  }

  setEnableToolbar(TRUE);
}

/***********************************************************************\
* Name   : executeCommand
* Purpose: execute external command
* Input  : commandLineTemplate      - command line template to execute
*          workingDirectoryTemplate - working directory template
*          customText               - custome text or NULL
*          dockerContainerId        - docker container id
*          parseOutput              - parse output of command for
*                                     errors/warnings
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void executeCommand(const gchar *commandLineTemplate,
                          const gchar *workingDirectoryTemplate,
                          const gchar *customText,
                          const gchar *dockerContainerId,
                          gboolean    parseOutput
                         )
{
  g_assert(commandLineTemplate != NULL);
  g_assert(geany_data != NULL);
  g_assert(geany_data->app != NULL);

  if (!stringIsEmpty(commandLineTemplate))
  {
    clearAll();
    pluginData.build.showedFirstErrorWarning = FALSE;

    // save all documents
    if (pluginData.configuration.autoSaveAll)
    {
      guint i;
      foreach_document(i)
      {
        if (documents[i]->changed && (documents[i]->file_name != NULL))
        {
          if (!document_save_file(documents[i], FALSE))
          {
            dialogs_show_msgbox(GTK_MESSAGE_ERROR, "Cannot save document '%s'", documents[i]->file_name);
            setEnableToolbar(TRUE);
            return;
          }
        }
      }
    }

    // get working directory
    const gchar *workingDirectory = getWorkingDirectory(workingDirectoryTemplate);
    g_assert(workingDirectory != NULL);

    // get command line
    const gchar *commandLine = expandMacros(geany_data->app->project,
                                            document_get_current(),
                                            commandLineTemplate,
                                            customText
                                           );
    g_assert(commandLine != NULL);

    // execute command
    gchar errorMessage[256];
    printMessage(_("Working directory: %s"), workingDirectory);
    printMessage(_("Build command line: %s"), commandLine);
    if (!Execute_asyncExecute(commandLine,
                              workingDirectory,
                              dockerContainerId,
                              CALLBACK(onExecuteCommandParse,&parseOutput),
                              CALLBACK(onExecuteCommandExit,NULL),
                              errorMessage,
                              sizeof(errorMessage)
                             )
       )
    {
      dialogs_show_msgbox(GTK_MESSAGE_ERROR, "%s", errorMessage);
      setEnableToolbar(TRUE);
      g_free((gchar*)commandLine);
      g_free((gchar*)workingDirectory);
      return;
    }

    // free resources
    g_free((gchar*)commandLine);
    g_free((gchar*)workingDirectory);
  }
}

/***********************************************************************\
* Name   : executeCommandAbort
* Purpose: abort running command
* Input  : -
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void executeCommandAbort()
{
  Execute_abortAyncCommand();
  setEnableAbort(FALSE);
}

// ---------------------------------------------------------------------

/***********************************************************************\
* Name   : onExecuteCommand
* Purpose: execute command callback
* Input  : widget   - widget
*          userData - user data (not used)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onExecuteCommand(GtkWidget *widget, gpointer userData)
{
  g_assert(widget != NULL);

  UNUSED_VARIABLE(userData);

  // execute command
  GtkListStore *listStore      = GTK_LIST_STORE(g_object_get_data(G_OBJECT(widget), "listStore"));
  g_assert(listStore != NULL);
  const gchar  *iteratorString = (gchar*)g_object_get_data(G_OBJECT(widget), "iteratorString");
  g_assert(iteratorString != NULL);

  GtkTreeIter treeIterator;
  if (gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(listStore),
                                          &treeIterator,
                                          iteratorString
                                         )
     )
  {
    gchar    *commandLine;
    gchar    *workingDirectory;
    gboolean parseOutput;
    gtk_tree_model_get(GTK_TREE_MODEL(listStore),
                       &treeIterator,
                       MODEL_COMMAND_COMMAND_LINE,     &commandLine,
                       MODEL_COMMAND_WORKING_DIRECTORY,&workingDirectory,
                       MODEL_COMMAND_PARSE_OUTPUT,     &parseOutput,
                       MODEL_END
                      );

    setEnableToolbar(FALSE);
    showBuildMessagesTab();
    executeCommand(commandLine,
                   workingDirectory,
                   NULL,  // customText
                   pluginData.attachedDockerContainerId,
                   parseOutput
                  );

    g_free(workingDirectory);
    g_free(commandLine);
  }

  // update key binding for last command
  if (   (pluginData.build.lastCommandListStore != listStore)
      || !stringEquals(pluginData.build.lastCommandIteratorString->str,iteratorString)
     )
  {
    pluginData.build.lastCommandListStore = listStore;
    g_string_assign(pluginData.build.lastCommandIteratorString,iteratorString);
    updateToolbarMenuItems();
  }
}

/***********************************************************************\
* Name   : onMenuItemShowPrevError
* Purpose:
* Input  : widget   - widget (unused)
*          userData - user data (unused)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onMenuItemShowPrevError(GtkWidget *widget, gpointer userData)
{
  UNUSED_VARIABLE(widget);
  UNUSED_VARIABLE(userData);

  showPrevError();
}

/***********************************************************************\
* Name   : onMenuItemShowNextError
* Purpose:
* Input  : widget   - widget
*          userData - user data
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onMenuItemShowNextError(GtkWidget *widget, gpointer userData)
{
  UNUSED_VARIABLE(widget);
  UNUSED_VARIABLE(userData);

  showNextError();
}

/***********************************************************************\
* Name   : onMenuItemShowPrevWarning
* Purpose:
* Input  : widget   - widget (unused)
*          userData - user data (unused)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onMenuItemShowPrevWarning(GtkWidget *widget, gpointer userData)
{
  UNUSED_VARIABLE(widget);
  UNUSED_VARIABLE(userData);

  showPrevWarning();
}

/***********************************************************************\
* Name   : onMenuItemShowNextWarning
* Purpose:
* Input  : widget   - widget (unused)
*          userData - user data (unused)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onMenuItemShowNextWarning(GtkWidget *widget, gpointer userData)
{
  UNUSED_VARIABLE(widget);
  UNUSED_VARIABLE(userData);

  showNextWarning();
}

/***********************************************************************\
* Name   : onMenuItemAbort
* Purpose:
* Input  : widget   - widget (unused)
*          userData - user data (unused)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onMenuItemAbort(GtkWidget *widget, gpointer userData)
{
  UNUSED_VARIABLE(widget);
  UNUSED_VARIABLE(userData);

  executeCommandAbort();
}

/***********************************************************************\
* Name   : attachDockerContainer
* Purpose: attach to docker container
* Input  : dockerContainerId - docker container id
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void attachDockerContainer(const gchar *dockerContainerId)
{
  if (pluginData.attachedDockerContainerId != NULL) g_free((gchar*)pluginData.attachedDockerContainerId);
  pluginData.attachedDockerContainerId = dockerContainerId;
  gchar *text = g_strdup_printf(_("Attached to docker container '%s'"), pluginData.attachedDockerContainerId);
  {
    gtk_menu_item_set_label(GTK_MENU_ITEM(pluginData.widgets.menuItems.dockerContainer),text);
  }
  g_free(text);
  gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(pluginData.widgets.menuItems.dockerContainer),TRUE);
}

/***********************************************************************\
* Name   : detachDockerContainer
* Purpose: detach from docker container
* Input  : -
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void detachDockerContainer()
{
  g_free((gchar*)pluginData.attachedDockerContainerId);
  pluginData.attachedDockerContainerId = NULL;

  gtk_menu_item_set_label(GTK_MENU_ITEM(pluginData.widgets.menuItems.dockerContainer),_("Attach to docker container"));
  gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(pluginData.widgets.menuItems.dockerContainer),FALSE);
}

/***********************************************************************\
* Name   : onDockerContainerDialogRowActivated
* Purpose: callback on row activated
* Input  : treeView       - tree view (not used)
*          treePath       - tree path (not used)
*          treeViewColumn - tree view column (not used)
*          userData       - user data: dialog
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onDockerContainerDialogRowActivated(GtkTreeView       *treeView,
                                               GtkTreePath       *treePath,
                                               GtkTreeViewColumn *treeViewColumn,
                                               gpointer          userData
                                              )
{
  GtkWidget *dialog = (GtkWidget*)userData;
  g_assert(dialog != NULL);

  UNUSED_VARIABLE(treeView);
  UNUSED_VARIABLE(treePath);
  UNUSED_VARIABLE(treeViewColumn);

  gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
}

/***********************************************************************\
* Name   : onDockerContainerDialogCursorChanged
* Purpose: callback on cursor changed (selection changed)
* Input  : treeView - tree view widget (not used)
*          userData - user data: OK widget
* Output : -
* Return : FALSE
* Notes  : -
\***********************************************************************/

LOCAL gboolean onDockerContainerDialogCursorChanged(GtkTreeView *treeView,
                                                    gpointer    userData
                                                   )
{
  GtkWidget *widgetOK = (GtkWidget*)userData;
  g_assert(widgetOK != NULL);

  UNUSED_VARIABLE(treeView);

  gtk_widget_set_sensitive(widgetOK, TRUE);

  return FALSE;
}

/***********************************************************************\
* Name   : onDockerContainerListParse
* Purpose: callback to parse docker container list output
* Input  : workingDirectory - working directory
*          line             - line (without trailing \n)
*          userData         - user data: TRUE to parse output
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onDockerContainerListParse(const gchar *workingDirectory,
                                      const gchar *line,
                                      void        *userData
                                     )
{
  GtkListStore *listStore = (GtkListStore*)userData;
  g_assert(listStore != NULL);

  UNUSED_VARIABLE(workingDirectory);

  if (!stringIsEmpty(line))
  {
    gchar **tokens   = g_strsplit(line, " ", 2);
    g_assert(tokens != NULL);
    guint tokenCount = g_strv_length(tokens);

    gtk_list_store_insert_with_values(listStore,
                                      NULL,
                                      -1,
                                      MODEL_ATTACH_DOCKER_CONTAINER_ID,    (tokenCount >= 1) ? tokens[0] : "",
                                      MODEL_ATTACH_DOCKER_CONTAINER_IMAGE, (tokenCount >= 2) ? tokens[1] : "",
                                      MODEL_END
                                     );
    g_strfreev(tokens);
  }
}

/***********************************************************************\
* Name   : attachDockerContainerDialog
* Purpose: attach to docker container dialog
* Input  : -
* Output : -
* Return : docker container id or NULL
* Notes  : -
\***********************************************************************/

LOCAL gchar* attachDockerContainerDialog()
{
  gchar *dockerContainerId = NULL;

  // get running docker containers
  GtkListStore *dockerContainerStore = gtk_list_store_new(MODEL_ATTACH_DOCKER_CONTAINER_COUNT,
                                                          G_TYPE_STRING,  // language
                                                          G_TYPE_STRING  // group
                                                         );
  const gchar *workingDirectory = getWorkingDirectory(NULL);
  g_assert(workingDirectory != NULL);
  gchar errorMessage[256];
  if (Execute_syncExecute("docker ps --format '{{.ID}} {{.Image}}'",
                          workingDirectory,
                          NULL, // dockerContainerId,
                          CALLBACK(onDockerContainerListParse,dockerContainerStore),
                          errorMessage,
                          sizeof(errorMessage)
                         )
     )
  {
      // create dialog
    GtkWidget *widgetContainerList;
    GtkWidget *widgetOK;
    GtkWidget *dialog = gtk_dialog_new_with_buttons(_("Attach to docker container"),
                                                    GTK_WINDOW(geany_data->main_widgets->window),
                                                    GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    _("_Cancel"),
                                                    GTK_RESPONSE_CANCEL,
                                                    NULL
                                                   );
    gtk_window_set_default_size(GTK_WINDOW(dialog), 400, 200);

    widgetOK = gtk_dialog_add_button(GTK_DIALOG(dialog), _("_OK"), GTK_RESPONSE_ACCEPT);
    g_object_set_data(G_OBJECT(dialog), "ok", widgetOK);
    gtk_widget_set_sensitive(widgetOK, FALSE);

    GtkBox *vbox = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 6));
    gtk_widget_set_margin_top(GTK_WIDGET(vbox), 6);
    gtk_widget_set_margin_bottom(GTK_WIDGET(vbox), 6);
    {
      // create scrolled build messages list
      GtkWidget *scrolledWindow = gtk_scrolled_window_new(NULL, NULL);
      gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledWindow),
                                     GTK_POLICY_AUTOMATIC,
                                     GTK_POLICY_ALWAYS
                                    );
      {
        widgetContainerList = gtk_tree_view_new_with_model(GTK_TREE_MODEL(dockerContainerStore));
        gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(widgetContainerList), TRUE);
        gtk_tree_view_set_headers_clickable(GTK_TREE_VIEW(widgetContainerList), TRUE);
        gtk_tree_view_set_search_column(GTK_TREE_VIEW(widgetContainerList), MODEL_ATTACH_DOCKER_CONTAINER_IMAGE);
        gtk_tree_view_set_show_expanders(GTK_TREE_VIEW(widgetContainerList), TRUE);
        {
          GtkCellRenderer *textRenderer;
          GtkTreeViewColumn *column;

          textRenderer = gtk_cell_renderer_text_new();
          column = gtk_tree_view_column_new_with_attributes("ID",
                                                            textRenderer,
                                                            "text", MODEL_ATTACH_DOCKER_CONTAINER_ID,
                                                            NULL
                                                           );
          gtk_tree_view_column_set_sort_indicator(column, FALSE);
          gtk_tree_view_column_set_sort_column_id(column, 1);
          gtk_tree_view_column_set_resizable(column, TRUE);
          gtk_tree_view_append_column(GTK_TREE_VIEW(widgetContainerList), column);

          textRenderer = gtk_cell_renderer_text_new();
          column = gtk_tree_view_column_new_with_attributes("Image",
                                                            textRenderer,
                                                            "text", MODEL_ATTACH_DOCKER_CONTAINER_IMAGE,
                                                            NULL
                                                           );
          gtk_tree_view_column_set_sort_indicator(column, FALSE);
          gtk_tree_view_column_set_sort_column_id(column, 2);
          gtk_tree_view_column_set_resizable(column, TRUE);
          gtk_tree_view_append_column(GTK_TREE_VIEW(widgetContainerList), column);

          plugin_signal_connect(geany_plugin,
                                G_OBJECT(widgetContainerList),
                                "cursor-changed",
                                FALSE,
                                G_CALLBACK(onDockerContainerDialogCursorChanged),
                                widgetOK
                               );
          plugin_signal_connect(geany_plugin,
                                G_OBJECT(widgetContainerList),
                                "row-activated",
                                FALSE,
                                G_CALLBACK(onDockerContainerDialogRowActivated),
                                dialog
                               );
        }
        gtk_container_add(GTK_CONTAINER(scrolledWindow), widgetContainerList);
      }
      addBox(vbox, TRUE, GTK_WIDGET(scrolledWindow));
    }
    addBox(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), TRUE, GTK_WIDGET(vbox));
    gtk_widget_show_all(GTK_WIDGET(dialog));

    // run dialog
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
    {
      GtkTreeSelection *selection;
      GtkTreeModel     *treeModel;
      GtkTreeIter      treeIterator;

      selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(widgetContainerList));
      if (gtk_tree_selection_get_selected(selection, &treeModel, &treeIterator))
      {
        gtk_tree_model_get(treeModel,
                           &treeIterator,
                           MODEL_ATTACH_DOCKER_CONTAINER_ID, &dockerContainerId,
                           MODEL_END
                          );
      }
    }

    // free resources
    gtk_widget_destroy(dialog);
    g_object_unref(dockerContainerStore);
  }
  else
  {
    dialogs_show_msgbox(GTK_MESSAGE_ERROR, "Cannot run 'docker ps': %s", errorMessage);
  }
  g_free((gchar*)workingDirectory);

  return dockerContainerId;
}

/***********************************************************************\
* Name   : onMenuItemDockerContainer
* Purpose: callback on menu item attach/detach docker container
* Input  : widget   - widget (unused)
*          userData - user data (unused)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onMenuItemDockerContainer(GtkWidget *widget,
                                     gpointer  userData
                                    )
{
  g_assert(geany_data != NULL);
  g_assert(geany_data->main_widgets != NULL);

  UNUSED_VARIABLE(widget);
  UNUSED_VARIABLE(userData);

  if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(pluginData.widgets.menuItems.dockerContainer)))
  {
    gchar *dockerContainerId = attachDockerContainerDialog();
    if (dockerContainerId != NULL)
    {
      attachDockerContainer(dockerContainerId);
    }
    else
    {
      gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(pluginData.widgets.menuItems.dockerContainer),FALSE);
    }
  }
  else
  {
    detachDockerContainer();
  }

  // update enable buttons/menu items
  updateEnableToolbarButtons();
  updateEnableToolbarMenuItems();
}

/***********************************************************************\
* Name   : onRemoteDialogChanged
* Purpose: called on dialog changes
* Input  : widget   - widget (not used)
*          userData - user data: dialog
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onRemoteDialogChanged(GtkWidget *widget,
                                 gpointer  userData
                                )
{
  GtkWidget *dialog = GTK_WIDGET(userData);
  g_assert(dialog != NULL);

  UNUSED_VARIABLE(widget);

  GtkWidget *widgetHostName = g_object_get_data(G_OBJECT(dialog), "hostName");
  g_assert(widgetHostName != NULL);
  GtkWidget *widgetUserName = g_object_get_data(G_OBJECT(dialog), "userName");
  g_assert(widgetUserName != NULL);
  GtkWidget *widgetPublicKey = g_object_get_data(G_OBJECT(dialog), "publicKey");
  g_assert(widgetPublicKey != NULL);
  GtkWidget *widgetPrivateKey = g_object_get_data(G_OBJECT(dialog), "privateKey");
  g_assert(widgetPrivateKey != NULL);
  GtkWidget *widgetPassword = g_object_get_data(G_OBJECT(dialog), "password");
  g_assert(widgetPassword != NULL);
  GtkWidget *widgetOK = g_object_get_data(G_OBJECT(dialog), "ok");
  g_assert(widgetOK != NULL);

  gtk_widget_set_sensitive(widgetOK,
                              !stringIsEmpty(gtk_entry_get_text(GTK_ENTRY(widgetHostName)))
                           && !stringIsEmpty(gtk_entry_get_text(GTK_ENTRY(widgetUserName)))
                           && (   (   !stringIsEmpty(gtk_entry_get_text(GTK_ENTRY(widgetPublicKey)))
                                   && !stringIsEmpty(gtk_entry_get_text(GTK_ENTRY(widgetPrivateKey)))
                                  )
                               || !stringIsEmpty(gtk_entry_get_text(GTK_ENTRY(widgetPassword)))
                              )
                          );
}

/***********************************************************************\
* Name   : onMenuItemRemote
* Purpose: callback on menu item connect remote
* Input  : widget   - widget (unused)
*          userData - user data (unused)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onMenuItemRemote(GtkWidget *widget,
                            gpointer  userData
                           )
{
  GtkWidget *widgetHostName, *widgetHostPort;
  GtkWidget *widgetUserName;
  GtkWidget *widgetPublicKey, *widgetPrivateKey;
  GtkWidget *widgetPassword;
  GtkWidget *widgetOK;
  gint      result;

  g_assert(geany_data != NULL);
  g_assert(geany_data->main_widgets != NULL);

  UNUSED_VARIABLE(widget);
  UNUSED_VARIABLE(userData);

  if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(pluginData.widgets.menuItems.remote)))
  {
    // create dialog
    GtkWidget *dialog = gtk_dialog_new_with_buttons(_("Connect remote"),
                                                    GTK_WINDOW(geany_data->main_widgets->window),
                                                    GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    _("_Cancel"),
                                                    GTK_RESPONSE_CANCEL,
                                                    NULL
                                                   );
    gtk_window_set_default_size(GTK_WINDOW(dialog), 500, 200);

    widgetOK = gtk_dialog_add_button(GTK_DIALOG(dialog), _("_OK"), GTK_RESPONSE_ACCEPT);
    g_object_set_data(G_OBJECT(dialog), "ok", widgetOK);
    gtk_widget_set_sensitive(widgetOK, FALSE);

    GtkBox *vbox = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 6));
    gtk_widget_set_margin_top(GTK_WIDGET(vbox), 6);
    gtk_widget_set_margin_bottom(GTK_WIDGET(vbox), 6);
    {
      GtkGrid *grid = GTK_GRID(gtk_grid_new());
      gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
      gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
      gtk_widget_set_hexpand(GTK_WIDGET(grid), TRUE);
      {
        addGrid(grid, 0, 0, 1, newLabel(NULL, G_OBJECT(dialog), NULL, _("Host name"), NULL));
        addGrid(grid, 0, 1, 1, newEntry(&widgetHostName, G_OBJECT(dialog), "hostName", "Host name."));
        addGrid(grid, 0, 2, 1, newSpinButton(&widgetHostPort, G_OBJECT(dialog), "hostPort", "Host port number.", 0, 65535));
        addGrid(grid, 1, 0, 1, newLabel(NULL, G_OBJECT(dialog), NULL, _("User name"), NULL));
        addGrid(grid, 1, 1, 2, newEntry(&widgetUserName, G_OBJECT(dialog), "userName", "User login name."));
        addGrid(grid, 2, 0, 1, newLabel(NULL, G_OBJECT(dialog), NULL, _("Public key"), NULL));
        addGrid(grid, 2, 1, 2, newFileChooser(&widgetPublicKey, G_OBJECT(dialog), "publicKey", "User public key."));
        addGrid(grid, 3, 0, 1, newLabel(NULL, G_OBJECT(dialog), NULL, _("Private key"), NULL));
        addGrid(grid, 3, 1, 2, newFileChooser(&widgetPrivateKey, G_OBJECT(dialog), "privateKey", "User private key."));
        addGrid(grid, 4, 0, 1, newLabel(NULL, G_OBJECT(dialog), NULL, _("Password"), NULL));
        addGrid(grid, 4, 1, 2, newPasswordEntry(&widgetPassword, G_OBJECT(dialog), "password", "User login or private key password."));

        plugin_signal_connect(geany_plugin,
                              G_OBJECT(widgetHostName),
                              "changed",
                              FALSE,
                              G_CALLBACK(onRemoteDialogChanged),
                              dialog
                             );
        plugin_signal_connect(geany_plugin,
                              G_OBJECT(widgetHostName),
                              "activate",
                              FALSE,
                              G_CALLBACK(onFocusNextWidget),
                              widgetHostPort
                             );
        plugin_signal_connect(geany_plugin,
                              G_OBJECT(widgetHostPort),
                              "changed",
                              FALSE,
                              G_CALLBACK(onRemoteDialogChanged),
                              dialog
                             );
        plugin_signal_connect(geany_plugin,
                              G_OBJECT(widgetHostPort),
                              "activate",
                              FALSE,
                              G_CALLBACK(onFocusNextWidget),
                              widgetUserName
                             );
        plugin_signal_connect(geany_plugin,
                              G_OBJECT(widgetUserName),
                              "changed",
                              FALSE,
                              G_CALLBACK(onRemoteDialogChanged),
                              dialog
                             );
        plugin_signal_connect(geany_plugin,
                              G_OBJECT(widgetUserName),
                              "activate",
                              FALSE,
                              G_CALLBACK(onFocusNextWidget),
                              widgetPublicKey
                             );
        plugin_signal_connect(geany_plugin,
                              G_OBJECT(widgetPublicKey),
                              "changed",
                              FALSE,
                              G_CALLBACK(onRemoteDialogChanged),
                              dialog
                             );
        plugin_signal_connect(geany_plugin,
                              G_OBJECT(widgetPublicKey),
                              "activate",
                              FALSE,
                              G_CALLBACK(onFocusNextWidget),
                              widgetPrivateKey
                             );
        plugin_signal_connect(geany_plugin,
                              G_OBJECT(widgetPrivateKey),
                              "changed",
                              FALSE,
                              G_CALLBACK(onRemoteDialogChanged),
                              dialog
                             );
        plugin_signal_connect(geany_plugin,
                              G_OBJECT(widgetPrivateKey),
                              "activate",
                              FALSE,
                              G_CALLBACK(onFocusNextWidget),
                              widgetPassword
                             );
        plugin_signal_connect(geany_plugin,
                              G_OBJECT(widgetPassword),
                              "changed",
                              FALSE,
                              G_CALLBACK(onRemoteDialogChanged),
                              dialog
                             );
        plugin_signal_connect(geany_plugin,
                              G_OBJECT(widgetPassword),
                              "activate",
                              FALSE,
                              G_CALLBACK(onFocusNextWidget),
                              widgetOK
                             );
      }
      addBox(GTK_BOX(vbox), FALSE, GTK_WIDGET(grid));
    }
    addBox(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), TRUE, GTK_WIDGET(vbox));
    gtk_widget_show_all(GTK_WIDGET(dialog));

    // set data
    gtk_entry_set_text(GTK_ENTRY(widgetHostName),pluginData.projectProperties.remote.hostName->str);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(widgetHostPort),(gdouble)pluginData.projectProperties.remote.hostPort);
    gtk_entry_set_text(GTK_ENTRY(widgetUserName),pluginData.projectProperties.remote.userName->str);
    gtk_entry_set_text(GTK_ENTRY(widgetPublicKey),pluginData.projectProperties.remote.publicKey->str);
    gtk_entry_set_text(GTK_ENTRY(widgetPrivateKey),pluginData.projectProperties.remote.privateKey->str);
    gtk_entry_set_text(GTK_ENTRY(widgetPassword),pluginData.projectProperties.remote.password->str);

    gboolean retry = FALSE;
    do
    {
      // set focus
      if      (stringIsEmpty(gtk_entry_get_text(GTK_ENTRY(widgetHostName))))
      {
        gtk_widget_grab_focus(widgetHostName);
      }
      else if (stringIsEmpty(gtk_entry_get_text(GTK_ENTRY(widgetUserName))))
      {
        gtk_widget_grab_focus(widgetUserName);
      }
      else if (   stringIsEmpty(gtk_entry_get_text(GTK_ENTRY(widgetPublicKey)))
               && stringIsEmpty(gtk_entry_get_text(GTK_ENTRY(widgetPassword)))
              )
      {
        gtk_widget_grab_focus(widgetPublicKey);
      }
      else if (   stringIsEmpty(gtk_entry_get_text(GTK_ENTRY(widgetPrivateKey)))
               && stringIsEmpty(gtk_entry_get_text(GTK_ENTRY(widgetPassword)))
              )
      {
        gtk_widget_grab_focus(widgetPrivateKey);
      }
      else
      {
        gtk_widget_grab_focus(widgetPassword);
      }
      gtk_window_present(GTK_WINDOW(dialog));

      // run dialog
      result = gtk_dialog_run(GTK_DIALOG(dialog));
      if (result == GTK_RESPONSE_ACCEPT)
      {
        // get remote data
        g_string_assign(pluginData.projectProperties.remote.hostName,gtk_entry_get_text(GTK_ENTRY(widgetHostName)));
        pluginData.projectProperties.remote.hostPort = (guint)gtk_spin_button_get_value(GTK_SPIN_BUTTON(widgetHostPort));
        g_string_assign(pluginData.projectProperties.remote.userName,gtk_entry_get_text(GTK_ENTRY(widgetUserName)));
        g_string_assign(pluginData.projectProperties.remote.publicKey,gtk_entry_get_text(GTK_ENTRY(widgetPublicKey)));
        g_string_assign(pluginData.projectProperties.remote.privateKey,gtk_entry_get_text(GTK_ENTRY(widgetPrivateKey)));
        g_string_assign(pluginData.projectProperties.remote.password,gtk_entry_get_text(GTK_ENTRY(widgetPassword)));

        if (pluginData.projectProperties.filePath != NULL)
        {
          // save configuration
          GKeyFile *configuration = g_key_file_new();
          g_key_file_load_from_file(configuration, pluginData.projectProperties.filePath, G_KEY_FILE_NONE, NULL);
  // TODO: use configurationSaveString/Integer
          g_key_file_set_string (configuration, CONFIGURATION_GROUP_BUILDER, "remoteHostName",  pluginData.projectProperties.remote.hostName->str);
          g_key_file_set_integer(configuration, CONFIGURATION_GROUP_BUILDER, "remoteHostPort",  pluginData.projectProperties.remote.hostPort);
          g_key_file_set_string (configuration, CONFIGURATION_GROUP_BUILDER, "remoteUserName",  pluginData.projectProperties.remote.userName->str);
          g_key_file_set_string (configuration, CONFIGURATION_GROUP_BUILDER, "remotePulicKey",  pluginData.projectProperties.remote.publicKey->str);
          g_key_file_set_string (configuration, CONFIGURATION_GROUP_BUILDER, "remotePrivateKey",pluginData.projectProperties.remote.privateKey->str);

  // TODO: use g_key_file_save_to_file()?
          gchar *configurationData = g_key_file_to_data(configuration, NULL, NULL);
          utils_write_file(geany_data->app->project->file_name, configurationData);
          g_free(configurationData);

          g_key_file_free(configuration);
        }

        // connect to remote
        gchar errorMessage[256];
        if (Remote_connect(pluginData.projectProperties.remote.hostName->str,
                           pluginData.projectProperties.remote.hostPort,
                           pluginData.projectProperties.remote.userName->str,
                           pluginData.projectProperties.remote.publicKey->str,
                           pluginData.projectProperties.remote.privateKey->str,
                           pluginData.projectProperties.remote.password->str,
                           errorMessage,
                           sizeof(errorMessage)
                          )
           )
        {
          gchar *text = g_strdup_printf(_("Connected to remote '%s:%d'"), pluginData.projectProperties.remote.hostName->str,pluginData.projectProperties.remote.hostPort);
          {
            gtk_menu_item_set_label(GTK_MENU_ITEM(pluginData.widgets.menuItems.remote),text);
          }
          g_free(text);

          if (pluginData.attachedDockerContainerId != NULL)
          {
            GtkWidget *confirmDialog = gtk_message_dialog_new(GTK_WINDOW(geany_data->main_widgets->window),
                                                              GTK_DIALOG_DESTROY_WITH_PARENT,
                                                              GTK_MESSAGE_QUESTION,
                                                              GTK_BUTTONS_OK_CANCEL,
                                                              "Attached to the docker container '%s'. Reattach on remote?",
                                                              pluginData.attachedDockerContainerId
                                                             );
            result = gtk_dialog_run(GTK_DIALOG(confirmDialog));
            if (result == GTK_RESPONSE_OK)
            {
              detachDockerContainer();

              gchar *dockerContainerId = attachDockerContainerDialog();
              if (dockerContainerId != NULL)
              {
                attachDockerContainer(dockerContainerId);
              }
            }
            gtk_widget_destroy(confirmDialog);
          }
        }
        else
        {
          GtkWidget *errorDialog = gtk_message_dialog_new(GTK_WINDOW(geany_data->main_widgets->window),
                                                          GTK_DIALOG_DESTROY_WITH_PARENT,
                                                          GTK_MESSAGE_ERROR,
                                                          GTK_BUTTONS_CLOSE,
                                                          "Connect to %s:%d failed\n\nError: %s",
                                                          pluginData.projectProperties.remote.hostName->str,
                                                          pluginData.projectProperties.remote.hostPort,
                                                          errorMessage
                                                         );
          gtk_dialog_run(GTK_DIALOG(errorDialog));
          gtk_widget_destroy(errorDialog);

          gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(pluginData.widgets.menuItems.remote),FALSE);

          retry = TRUE;
        }
      }
      else
      {
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(pluginData.widgets.menuItems.remote),FALSE);

        retry = FALSE;
      }
    }
    while (retry);

    // free resources
    gtk_widget_destroy(dialog);
  }
  else
  {
    Remote_disconnect();

    gtk_menu_item_set_label(GTK_MENU_ITEM(pluginData.widgets.menuItems.remote),_("Connect remote"));
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(pluginData.widgets.menuItems.remote),FALSE);
  }

  // update enable buttons/menu items
  updateEnableToolbarButtons();
  updateEnableToolbarMenuItems();
}

/***********************************************************************\
* Name   : onMenuItemProjectPreferences
* Purpose:
* Input  : widget   - widget (unused)
*          userData - user data (unused)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onMenuItemProjectPreferences(GtkWidget *widget, gpointer userData)
{
  g_assert(geany_data != NULL);
  g_assert(geany_data->main_widgets != NULL);

  UNUSED_VARIABLE(widget);
  UNUSED_VARIABLE(userData);

  // request show plugin preferences tab
  pluginData.widgets.showProjectPropertiesTab = TRUE;

  // activate "project preferences" menu item
  GtkWidget *menuItem = ui_lookup_widget(geany_data->main_widgets->window, "project_properties1");
  gtk_menu_item_activate(GTK_MENU_ITEM(menuItem));
}

/***********************************************************************\
* Name   : onMenuItemConfiguration
* Purpose: open plugin configuration dialog
* Input  : widget - widget (unused)
*          data   - user data (unused)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onMenuItemConfiguration(GtkWidget *widget, gpointer userData)
{
  UNUSED_VARIABLE(widget);
  UNUSED_VARIABLE(userData);

  plugin_show_configure(geany_plugin);
}

/***********************************************************************\
* Name   : onErrorWarningTreeSelectionChanged
* Purpose: selection changed callback
* Input  : data - tree widget
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL gboolean onMessageListSelectionChanged(gpointer data)
{
  GtkTreeView *messageList = GTK_TREE_VIEW(data);

  g_assert(messageList != NULL);

  GtkTreeSelection *treeSelection = gtk_tree_view_get_selection(messageList);
  g_assert(treeSelection != NULL);

  GtkTreeModel *treeModel;
  GtkTreeIter  treeIterator;
  if (gtk_tree_selection_get_selected(treeSelection, &treeModel, &treeIterator))
  {
//    gchar *messageTreePath;
    gchar *directory, *filePath;
    gint  lineNumber, columnNumber;
    gtk_tree_model_get(treeModel,
                       &treeIterator,
// TODO: remove
//                       MODEL_ERROR_WARNING_TREE_PATH,&messageTreePath,
                       MODEL_MESSAGE_DIRECTORY,      &directory,
                       MODEL_MESSAGE_FILE_PATH,      &filePath,
                       MODEL_MESSAGE_LINE_NUMBER,    &lineNumber,
                       MODEL_MESSAGE_COLUMN_NUMBER,  &columnNumber,
                       MODEL_END
                      );
    if (filePath != NULL)
    {
      showSource(directory, filePath, lineNumber, columnNumber);
    }
    g_free(filePath);
    g_free(directory);
//    g_free(messageTreePath);
  }

  return FALSE;
}

/***********************************************************************\
* Name   : onMessageListButtonPress
* Purpose: button press callback
* Input  : messageList - message list widget
*          eventButton - button event
*          userData    - user data: popup menu
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL gboolean onMessageListButtonPress(GtkTreeView    *messageList,
                                        GdkEventButton *eventButton,
                                        gpointer       userData
                                       )
{
  GtkMenu *menu = (GtkMenu*)userData;
  g_assert(messageList != NULL);
  g_assert(eventButton != NULL);
  g_assert(menu != NULL);

  if      (eventButton->button == 1)
  {
    g_idle_add(onMessageListSelectionChanged, messageList);
  }
  else if (eventButton->button == 3)
  {
    gtk_tree_path_free(pluginData.build.messagesTreePath);
    pluginData.build.messagesTreePath = gtk_tree_path_new();
    if (gtk_tree_view_get_path_at_pos(messageList,
                                      eventButton->x,
                                      eventButton->y,
                                      &pluginData.build.messagesTreePath,
                                      NULL,
                                      NULL,
                                      NULL
                                     )
       )
    {
      GtkTreeIter treeIterator;
      if (gtk_tree_model_get_iter(GTK_TREE_MODEL(pluginData.build.messagesStore),
                                  &treeIterator,
                                  pluginData.build.messagesTreePath
                                 )
         )
      {
        gchar *treePath;
        gtk_tree_model_get(GTK_TREE_MODEL(pluginData.build.messagesStore),
                           &treeIterator,
                           MODEL_MESSAGE_TREE_PATH, &treePath,
                           MODEL_END
                          );
        gtk_widget_set_sensitive(GTK_WIDGET(pluginData.widgets.menuItems.editRegex), !stringIsEmpty(treePath));
        g_free(treePath);
      }

      gtk_menu_popup_at_pointer(menu, NULL);
    }
  }

  return FALSE;
}

/***********************************************************************\
* Name   : onErrorWarningTreeSelectionChanged
* Purpose: selection changed callback
* Input  : userData - user data: tree widget
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL gboolean onErrorWarningTreeSelectionChanged(gpointer userData)
{
  GtkTreeView *treeView = GTK_TREE_VIEW(userData);
  g_assert(treeView != NULL);

  GtkTreeSelection *treeSelection;
  treeSelection = gtk_tree_view_get_selection(treeView);

  GtkTreeModel *treeModel;
  GtkTreeIter  treeIterator;
  if (gtk_tree_selection_get_selected(treeSelection, &treeModel, &treeIterator))
  {
    gchar *messageTreePath;
    gchar *directory, *filePath;
    gint  lineNumber, columnNumber;
    gtk_tree_model_get(treeModel,
                       &treeIterator,
                       MODEL_ERROR_WARNING_TREE_PATH,     &messageTreePath,
                       MODEL_ERROR_WARNING_DIRECTORY,     &directory,
                       MODEL_ERROR_WARNING_FILE_PATH,     &filePath,
                       MODEL_ERROR_WARNING_LINE_NUMBER,   &lineNumber,
                       MODEL_ERROR_WARNING_COLUMN_NUMBER, &columnNumber,
                       MODEL_END
                      );
    if (messageTreePath != NULL)
    {
      scrollToMessage(messageTreePath);
    }
    if (filePath != NULL)
    {
      g_assert(directory);
      showSource(directory, filePath, lineNumber, columnNumber);
    }
    g_free(filePath);
    g_free(directory);
    g_free(messageTreePath);
  }

  return FALSE;
}

/***********************************************************************\
* Name   : onErrorWarningTreeButtonPress
* Purpose: button press callback
* Input  : widget      - widget
*          eventButton - button event
*          userData    - user data (not used)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL gboolean onErrorWarningTreeButtonPress(GtkTreeView    *treeView,
                                             GdkEventButton *eventButton,
                                             gpointer       userData
                                            )
{
  g_assert(treeView != NULL);
  g_assert(eventButton != NULL);

  UNUSED_VARIABLE(userData);

  if      (eventButton->button == 1)
  {
    g_idle_add(onErrorWarningTreeSelectionChanged, treeView);
  }
  else if (eventButton->button == 3)
  {
  }

  return FALSE;
}

/***********************************************************************\
* Name   : onErrorWarningTreeKeyPress
* Purpose: key press callback
* Input  : widget   - widget
*          eventKey - key event
*          userData - user data (not used)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL gboolean onErrorWarningTreeKeyPress(GtkTreeView *treeView,
                                          GdkEventKey *eventKey,
                                          gpointer    userData
                                         )
{
  g_assert(treeView != NULL);
  g_assert(eventKey != NULL);

  UNUSED_VARIABLE(userData);

  if (   (eventKey->keyval == GDK_Return)
      || (eventKey->keyval == GDK_ISO_Enter)
      || (eventKey->keyval == GDK_KP_Enter)
      || (eventKey->keyval == GDK_space)
     )
  {
    g_idle_add(onErrorWarningTreeSelectionChanged, treeView);
  }

// TODO: needed?
#if 0
  if ((eventKey->keyval == GDK_F10 && eventKey->state & GDK_SHIFT_MASK) || eventKey->keyval == GDK_Menu)
  {
  }
#endif

  return FALSE;
}

/***********************************************************************\
* Name   : onErrorWarningTreeDoubleClick
* Purpose: double-click callback: expand/collaps tree entry
* Input  : widget   - widget
*          treePath - tree path
*          column   - column (not used)
*          userData - user data (not used)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onErrorWarningTreeDoubleClick(GtkTreeView       *treeView,
                                         GtkTreePath       *treePath,
                                         GtkTreeViewColumn *column,
                                         gpointer           userData
                                        )
{
  g_assert(treeView != NULL);
  g_assert(treePath != NULL);
  g_assert(column != NULL);

  UNUSED_VARIABLE(column);
  UNUSED_VARIABLE(userData);

  GtkTreeModel *model = gtk_tree_view_get_model(treeView);

  GtkTreeIter treeIterator;
  if (gtk_tree_model_get_iter(model, &treeIterator, treePath))
  {
    if (gtk_tree_view_row_expanded(treeView, treePath))
    {
      gtk_tree_view_collapse_row(treeView, treePath);
    }
    else
    {
      gtk_tree_view_expand_to_path(treeView, treePath);
    }
  }
}

/***********************************************************************\
* Name   : onKeyBinding
* Purpose: handle key short-cut
* Input  : keyId - key id
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onKeyBinding(guint keyId)
{
  switch (keyId)
  {
    case KEY_BINDING_BUILD:
      {
        if (   (pluginData.build.lastCommandListStore != NULL)
            && !stringIsEmpty(pluginData.build.lastCommandIteratorString->str)
           )
        {
          // execute command
          GtkTreeIter treeIterator;
          if (gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(pluginData.build.lastCommandListStore),
                                                  &treeIterator,
                                                  pluginData.build.lastCommandIteratorString->str
                                                 )
             )
          {
            gchar    *commandLine;
            gchar    *workingDirectory;
            gboolean parseOutput;
            gtk_tree_model_get(GTK_TREE_MODEL(pluginData.build.lastCommandListStore),
                               &treeIterator,
                               MODEL_COMMAND_COMMAND_LINE,     &commandLine,
                               MODEL_COMMAND_WORKING_DIRECTORY,&workingDirectory,
                               MODEL_COMMAND_PARSE_OUTPUT,     &parseOutput,
                               MODEL_END
                              );

            setEnableToolbar(FALSE);
            showBuildMessagesTab();
            executeCommand(commandLine,
                           workingDirectory,
                           NULL,  // customText
                           pluginData.attachedDockerContainerId,
                           parseOutput
                          );

            g_free(workingDirectory);
            g_free(commandLine);
          }
        }
      }
      break;
    case KEY_BINDING_ABORT:
      executeCommandAbort();
      break;
    case KEY_BINDING_PREV_ERROR:
      if (pluginData.build.errorsTreeIterorValid)
      {
        showPrevError();
      }
      break;
    case KEY_BINDING_NEXT_ERROR:
      if (pluginData.build.errorsTreeIterorValid)
      {
        showNextError();
      }
      break;
    case KEY_BINDING_PREV_WARNING:
      if (pluginData.build.warningsTreeIterorValid)
      {
        showPrevWarning();
      }
      break;
    case KEY_BINDING_NEXT_WARNING:
      if (pluginData.build.warningsTreeIterorValid)
      {
        showNextWarning();
      }
      break;
    default:
      break;
  }
}

/***********************************************************************\
* Name   : updateEnableToolbarButtons
* Purpose: enable toolbar buttons
* Input  : -
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void updateEnableToolbarButtons()
{
  gboolean enableWidgets = pluginData.widgets.disableCounter == 0;

  for (guint i = 0; i < MAX_COMMANDS; i++)
  {
    if (pluginData.widgets.buttons.commands[i] != NULL)
    {
      GtkListStore *listStore      = GTK_LIST_STORE(g_object_get_data(G_OBJECT(pluginData.widgets.buttons.commands[i]), "listStore"));
      gchar        *iteratorString = (gchar*)g_object_get_data(G_OBJECT(pluginData.widgets.buttons.commands[i]), "iteratorString");

      gboolean enabled = FALSE;
      if ((listStore != NULL) && (iteratorString != NULL))
      {
        GtkTreeIter treeIterator;
        if (gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(listStore),
                                                &treeIterator,
                                                iteratorString
                                               )
           )
        {
          gboolean runInDockerContainer;
          gboolean runRemote;
          gtk_tree_model_get(GTK_TREE_MODEL(listStore),
                             &treeIterator,
                             MODEL_COMMAND_RUN_IN_DOCKER_CONTAINER, &runInDockerContainer,
                             MODEL_COMMAND_RUN_REMOTE,              &runRemote,
                             MODEL_END
                            );

          enabled =    enableWidgets
                    && (!runInDockerContainer || (pluginData.attachedDockerContainerId != NULL))
                    && (!runRemote            || Remote_isConnected());
        }
      }

      gtk_widget_set_sensitive(GTK_WIDGET(pluginData.widgets.buttons.commands[i]),
                               (i == 0) || enabled
                              );

    }
  }

  for (guint i = 0; i < MAX_COMMANDS; i++)
  {
    if (pluginData.widgets.buttons.projectCommands[i] != NULL)
    {
      GtkListStore *listStore      = GTK_LIST_STORE(g_object_get_data(G_OBJECT(pluginData.widgets.buttons.projectCommands[i]), "listStore"));
      gchar        *iteratorString = (gchar*)g_object_get_data(G_OBJECT(pluginData.widgets.buttons.projectCommands[i]), "iteratorString");

      gboolean enabled = FALSE;
      if ((listStore != NULL) && (iteratorString != NULL))
      {
        GtkTreeIter treeIterator;
        if (gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(listStore),
                                                &treeIterator,
                                                iteratorString
                                               )
           )
        {
          gboolean runInDockerContainer;
          gboolean runRemote;
          gtk_tree_model_get(GTK_TREE_MODEL(listStore),
                             &treeIterator,
                             MODEL_COMMAND_RUN_IN_DOCKER_CONTAINER, &runInDockerContainer,
                             MODEL_COMMAND_RUN_REMOTE,              &runRemote,
                             MODEL_END
                            );

          enabled =    enableWidgets
                    && (!runInDockerContainer || (pluginData.attachedDockerContainerId != NULL))
                    && (!runRemote            || Remote_isConnected());
        }
      }

      gtk_widget_set_sensitive(GTK_WIDGET(pluginData.widgets.buttons.projectCommands[i]), enabled);
    }
  }

  gtk_widget_set_sensitive(GTK_WIDGET(pluginData.widgets.buttons.abort), enableWidgets);
}

/***********************************************************************\
* Name   : updateToolbarButtons
* Purpose: update toolbar buttons
* Input  : -
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void updateToolbarButtons()
{
  // discard buttons
  for (guint i = 0; i < MAX_COMMANDS; i++)
  {
    if (pluginData.widgets.buttons.commands[i] != NULL)
    {
      gchar *commandIteratorString = g_object_get_data(G_OBJECT(pluginData.widgets.buttons.commands[i]), "iteratorString");
      gtk_widget_destroy(GTK_WIDGET(pluginData.widgets.buttons.commands[i]));
      g_free(commandIteratorString);
    }
    pluginData.widgets.buttons.commands[i] = NULL;
  }
  if (pluginData.widgets.buttons.abort != NULL)
  {
    gtk_widget_destroy(GTK_WIDGET(pluginData.widgets.buttons.abort));
  }
  pluginData.widgets.buttons.abort = NULL;

  gboolean    isFirst = TRUE;
  guint       i       = 0;
  GtkTreeIter treeIterator;

  // command buttons
  if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(pluginData.configuration.commandStore), &treeIterator))
  {
    do
    {
      gchar    *title;
      gboolean showButton;
      gboolean runInDockerContainer;
      gboolean runRemote;
      gtk_tree_model_get(GTK_TREE_MODEL(pluginData.configuration.commandStore),
                         &treeIterator,
                         MODEL_COMMAND_TITLE,                   &title,
                         MODEL_COMMAND_SHOW_BUTTON,             &showButton,
                         MODEL_COMMAND_RUN_IN_DOCKER_CONTAINER, &runInDockerContainer,
                         MODEL_COMMAND_RUN_REMOTE,              &runRemote,
                         MODEL_END
                        );
      g_assert(title != NULL);

      if (showButton)
      {
        if (i < MAX_COMMANDS)
        {
          gchar *commandIteratorString = gtk_tree_model_get_string_from_iter(GTK_TREE_MODEL(pluginData.configuration.commandStore),&treeIterator);

          pluginData.widgets.buttons.commands[i] = isFirst
                                                     ? gtk_menu_tool_button_new(NULL, title)
                                                     : gtk_tool_button_new(NULL, title);
          g_object_set_data(G_OBJECT(pluginData.widgets.buttons.commands[i]), "listStore", pluginData.configuration.commandStore);
          g_object_set_data(G_OBJECT(pluginData.widgets.buttons.commands[i]), "iteratorString", commandIteratorString);
          plugin_add_toolbar_item(geany_plugin, GTK_TOOL_ITEM(pluginData.widgets.buttons.commands[i]));
          gtk_widget_set_sensitive(GTK_WIDGET(pluginData.widgets.buttons.commands[i]),
                                      (!runInDockerContainer || (pluginData.attachedDockerContainerId != NULL))
                                   && (!runRemote            || Remote_isConnected())
                                  );
          gtk_widget_show(GTK_WIDGET(pluginData.widgets.buttons.commands[i]));

          plugin_signal_connect(geany_plugin,
                                G_OBJECT(pluginData.widgets.buttons.commands[i]),
                                "clicked",
                                FALSE,
                                G_CALLBACK(onExecuteCommand),
                                NULL
                               );

          isFirst = FALSE;
          i++;
        }
      }

      g_free(title);
    }
    while (gtk_tree_model_iter_next(GTK_TREE_MODEL(pluginData.configuration.commandStore), &treeIterator));
  }

  // project command buttons
  if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(pluginData.projectProperties.commandStore), &treeIterator))
  {
    do
    {
      gchar    *title;
      gboolean showButton;
      gtk_tree_model_get(GTK_TREE_MODEL(pluginData.projectProperties.commandStore),
                         &treeIterator,
                         MODEL_COMMAND_TITLE,       &title,
                         MODEL_COMMAND_SHOW_BUTTON, &showButton,
                         MODEL_END
                        );
      g_assert(title != NULL);

      if (showButton)
      {
        if (i < MAX_COMMANDS)
        {
          gchar *commandIteratorString = gtk_tree_model_get_string_from_iter(GTK_TREE_MODEL(pluginData.projectProperties.commandStore),&treeIterator);

          pluginData.widgets.buttons.commands[i] = isFirst
                                                     ? gtk_menu_tool_button_new(NULL, title)
                                                     : gtk_tool_button_new(NULL, title);
          g_object_set_data(G_OBJECT(pluginData.widgets.buttons.commands[i]), "listStore", pluginData.projectProperties.commandStore);
          g_object_set_data(G_OBJECT(pluginData.widgets.buttons.commands[i]), "iteratorString", commandIteratorString);
          plugin_add_toolbar_item(geany_plugin, GTK_TOOL_ITEM(pluginData.widgets.buttons.commands[i]));
          gtk_widget_show(GTK_WIDGET(pluginData.widgets.buttons.commands[i]));
          plugin_signal_connect(geany_plugin,
                                G_OBJECT(pluginData.widgets.buttons.commands[i]),
                                "clicked",
                                FALSE,
                                G_CALLBACK(onExecuteCommand),
                                NULL
                               );

          isFirst = FALSE;
          i++;
        }
      }

      g_free(title);
    }
    while (gtk_tree_model_iter_next(GTK_TREE_MODEL(pluginData.projectProperties.commandStore), &treeIterator));
  }

  // add default button
  if (i <= 0)
  {
    pluginData.widgets.buttons.commands[i] = gtk_menu_tool_button_new(NULL, _("Builder"));
    plugin_add_toolbar_item(geany_plugin, GTK_TOOL_ITEM(pluginData.widgets.buttons.commands[i]));
    gtk_widget_show(GTK_WIDGET(pluginData.widgets.buttons.commands[i]));
  }

  // abort button
  if (pluginData.configuration.abortButton)
  {
    pluginData.widgets.buttons.abort = gtk_tool_button_new(NULL, _("Abort"));
    plugin_add_toolbar_item(geany_plugin, GTK_TOOL_ITEM(pluginData.widgets.buttons.abort));
    gtk_widget_show(GTK_WIDGET(pluginData.widgets.buttons.abort));
    plugin_signal_connect(geany_plugin,
                          G_OBJECT(pluginData.widgets.buttons.abort),
                          "clicked",
                          FALSE,
                          G_CALLBACK(onMenuItemAbort),
                          NULL
                         );
  }

  updateEnableToolbarButtons();
}

/***********************************************************************\
* Name   : updateEnableToolbarMenuItems
* Purpose: enable toolbar menu items
* Input  : -
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void updateEnableToolbarMenuItems()
{
  guint i = 0;
  while ((i < MAX_COMMANDS) && (pluginData.widgets.menuItems.commands[i] != NULL))
  {
    GtkListStore *listStore      = GTK_LIST_STORE(g_object_get_data(G_OBJECT(pluginData.widgets.menuItems.commands[i]), "listStore"));
    g_assert(listStore != NULL);
    gchar        *iteratorString = (gchar*)g_object_get_data(G_OBJECT(pluginData.widgets.menuItems.commands[i]), "iteratorString");
    g_assert(iteratorString != NULL);

    GtkTreeIter treeIterator;
    if (gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(listStore),
                                            &treeIterator,
                                            iteratorString
                                           )
       )
    {
      gboolean runInDockerContainer;
      gboolean runRemote;
      gtk_tree_model_get(GTK_TREE_MODEL(listStore),
                         &treeIterator,
                         MODEL_COMMAND_RUN_IN_DOCKER_CONTAINER, &runInDockerContainer,
                         MODEL_COMMAND_RUN_REMOTE,              &runRemote,
                         MODEL_END
                        );

      gtk_widget_set_sensitive(GTK_WIDGET(pluginData.widgets.menuItems.commands[i]),
                                  (!runInDockerContainer || (pluginData.attachedDockerContainerId != NULL))
                               && (!runRemote            || Remote_isConnected())
                              );
    }

    i++;
  }

  gtk_widget_set_sensitive(GTK_WIDGET(pluginData.widgets.menuItems.abort), FALSE);
}

/***********************************************************************\
* Name   : updateToolbarMenuItems
* Purpose: update toolbar button menu
* Input  : -
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void updateToolbarMenuItems()
{
  // discard menu items
  for (guint i = 0; i < MAX_COMMANDS; i++)
  {
    pluginData.widgets.menuItems.commands[i] = NULL;
  }

  // create menu
  GtkWidget *lastCommandMenuItem = NULL;
  GtkWidget *menu = gtk_menu_new();
  {
    guint     i = 0;
    GtkWidget *menuItem;

    // add command menu items
    GtkTreeIter treeIterator;
    if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(pluginData.configuration.commandStore), &treeIterator))
    {
      do
      {
        gchar    *title;
        gboolean showMenuItem;
        gtk_tree_model_get(GTK_TREE_MODEL(pluginData.configuration.commandStore),
                           &treeIterator,
                           MODEL_COMMAND_TITLE,          &title,
                           MODEL_COMMAND_SHOW_MENU_ITEM, &showMenuItem,
                           MODEL_END
                          );
        g_assert(title != NULL);

        if (showMenuItem)
        {
          if (i < MAX_COMMANDS)
          {
            gchar *commandIteratorString = gtk_tree_model_get_string_from_iter(GTK_TREE_MODEL(pluginData.configuration.commandStore),&treeIterator);

            pluginData.widgets.menuItems.commands[i] = gtk_menu_item_new_with_mnemonic(title);
            g_assert(pluginData.widgets.menuItems.commands[i] != NULL);
            g_object_set_data(G_OBJECT(pluginData.widgets.menuItems.commands[i]), "listStore", pluginData.configuration.commandStore);
            if (pluginData.widgets.menuItems.commands[i] != NULL)
            {
              g_free(g_object_get_data(G_OBJECT(pluginData.widgets.menuItems.commands[i]), "iteratorString"));
            }
            g_object_set_data(G_OBJECT(pluginData.widgets.menuItems.commands[i]), "iteratorString", commandIteratorString);
            gtk_container_add(GTK_CONTAINER(menu), pluginData.widgets.menuItems.commands[i]);

            if (   (pluginData.build.lastCommandListStore == pluginData.configuration.commandStore)
                && stringEquals(pluginData.build.lastCommandIteratorString->str,commandIteratorString)
               )
            {
              lastCommandMenuItem = pluginData.widgets.menuItems.commands[i];
            }

            plugin_signal_connect(geany_plugin,
                                  G_OBJECT(pluginData.widgets.menuItems.commands[i]),
                                  "activate",
                                  FALSE,
                                  G_CALLBACK(onExecuteCommand),
                                  NULL
                                 );

            i++;
          }
        }

        g_free(title);
      }
      while (gtk_tree_model_iter_next(GTK_TREE_MODEL(pluginData.configuration.commandStore), &treeIterator));
    }

    // add separator
    menuItem = gtk_separator_menu_item_new();
    gtk_container_add(GTK_CONTAINER(menu), menuItem);

    // add project command menu items
    if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(pluginData.projectProperties.commandStore), &treeIterator))
    {
      do
      {
        gchar    *title;
        gboolean showMenuItem;
        gtk_tree_model_get(GTK_TREE_MODEL(pluginData.projectProperties.commandStore),
                           &treeIterator,
                           MODEL_COMMAND_TITLE,          &title,
                           MODEL_COMMAND_SHOW_MENU_ITEM, &showMenuItem,
                           MODEL_END
                          );
        g_assert(title != NULL);

        if (showMenuItem)
        {
          if (i < MAX_COMMANDS)
          {
            gchar *commandIteratorString = gtk_tree_model_get_string_from_iter(GTK_TREE_MODEL(pluginData.projectProperties.commandStore),&treeIterator);

            pluginData.widgets.menuItems.commands[i] = gtk_menu_item_new_with_mnemonic(title);
            g_assert(pluginData.widgets.menuItems.commands[i] != NULL);
            g_object_set_data(G_OBJECT(pluginData.widgets.menuItems.commands[i]), "listStore", pluginData.projectProperties.commandStore);
            if (pluginData.widgets.menuItems.commands[i] != NULL)
            {
              g_free(g_object_get_data(G_OBJECT(pluginData.widgets.menuItems.commands[i]), "iteratorString"));
            }
            g_object_set_data(G_OBJECT(pluginData.widgets.menuItems.commands[i]), "iteratorString", commandIteratorString);
            gtk_container_add(GTK_CONTAINER(menu), pluginData.widgets.menuItems.commands[i]);

            if (   (pluginData.build.lastCommandListStore == pluginData.projectProperties.commandStore)
                && stringEquals(pluginData.build.lastCommandIteratorString->str,commandIteratorString)
               )
            {
              lastCommandMenuItem = pluginData.widgets.menuItems.commands[i];
            }

            plugin_signal_connect(geany_plugin,
                                  G_OBJECT(pluginData.widgets.menuItems.commands[i]),
                                  "activate",
                                  FALSE,
                                  G_CALLBACK(onExecuteCommand),
                                  NULL
                                 );

            i++;
          }
        }

        g_free(title);
      }
      while (gtk_tree_model_iter_next(GTK_TREE_MODEL(pluginData.projectProperties.commandStore), &treeIterator));
    }

    // add separator
    menuItem = gtk_separator_menu_item_new();
    gtk_container_add(GTK_CONTAINER(menu), menuItem);

    // add general commands
    pluginData.widgets.menuItems.abort = gtk_menu_item_new_with_mnemonic(_("Abort"));
    gtk_widget_set_sensitive(GTK_WIDGET(pluginData.widgets.menuItems.abort), FALSE);
    gtk_container_add(GTK_CONTAINER(menu), pluginData.widgets.menuItems.abort);
    plugin_signal_connect(geany_plugin,
                          G_OBJECT(pluginData.widgets.menuItems.abort),
                          "activate",
                          FALSE,
                          G_CALLBACK(onMenuItemAbort),
                          NULL
                         );

    menuItem = gtk_separator_menu_item_new();
    gtk_container_add(GTK_CONTAINER(menu), menuItem);

    pluginData.widgets.menuItems.showPrevError = gtk_menu_item_new_with_mnemonic(_("Show previous error"));
    gtk_container_add(GTK_CONTAINER(menu), pluginData.widgets.menuItems.showPrevError);
    plugin_signal_connect(geany_plugin,
                          G_OBJECT(pluginData.widgets.menuItems.showPrevError),
                          "activate", FALSE,
                          G_CALLBACK(onMenuItemShowPrevError),
                          NULL
                         );

    pluginData.widgets.menuItems.showNextError = gtk_menu_item_new_with_mnemonic(_("Show next error"));
    gtk_container_add(GTK_CONTAINER(menu), pluginData.widgets.menuItems.showNextError);
    plugin_signal_connect(geany_plugin,
                          G_OBJECT(pluginData.widgets.menuItems.showNextError),
                          "activate",
                          FALSE,
                          G_CALLBACK(onMenuItemShowNextError),
                          NULL
                         );

    pluginData.widgets.menuItems.showPrevWarning = gtk_menu_item_new_with_mnemonic(_("Show previous warning"));
    gtk_container_add(GTK_CONTAINER(menu), pluginData.widgets.menuItems.showPrevWarning);
    plugin_signal_connect(geany_plugin,
                          G_OBJECT(pluginData.widgets.menuItems.showPrevWarning),
                          "activate",
                          FALSE,
                          G_CALLBACK(onMenuItemShowPrevWarning),
                          NULL
                         );

    pluginData.widgets.menuItems.showNextWarning = gtk_menu_item_new_with_mnemonic(_("Show next warning"));
    gtk_container_add(GTK_CONTAINER(menu), pluginData.widgets.menuItems.showNextWarning);
    plugin_signal_connect(geany_plugin,
                          G_OBJECT(pluginData.widgets.menuItems.showNextWarning),
                          "activate",
                          FALSE,
                          G_CALLBACK(onMenuItemShowNextWarning),
                          NULL
                         );

    menuItem = gtk_separator_menu_item_new();
    gtk_container_add(GTK_CONTAINER(menu), menuItem);

    pluginData.widgets.menuItems.dockerContainer = gtk_check_menu_item_new_with_label(_("Attach to docker container"));
    gtk_container_add(GTK_CONTAINER(menu), pluginData.widgets.menuItems.dockerContainer);
    plugin_signal_connect(geany_plugin,
                          G_OBJECT(pluginData.widgets.menuItems.dockerContainer),
                          "activate",
                          FALSE,
                          G_CALLBACK(onMenuItemDockerContainer),
                          NULL
                         );

    pluginData.widgets.menuItems.remote = gtk_check_menu_item_new_with_label(_("Connect remote"));
    gtk_container_add(GTK_CONTAINER(menu), pluginData.widgets.menuItems.remote);
    plugin_signal_connect(geany_plugin,
                          G_OBJECT(pluginData.widgets.menuItems.remote),
                          "activate",
                          FALSE,
                          G_CALLBACK(onMenuItemRemote),
                          NULL
                         );

    menuItem = gtk_separator_menu_item_new();
    gtk_container_add(GTK_CONTAINER(menu), menuItem);

    pluginData.widgets.menuItems.projectProperties = gtk_menu_item_new_with_label(_("Project properties"));
    gtk_container_add(GTK_CONTAINER(menu), pluginData.widgets.menuItems.projectProperties);
    plugin_signal_connect(geany_plugin,
                          G_OBJECT(pluginData.widgets.menuItems.projectProperties),
                          "activate",
                          FALSE,
                          G_CALLBACK(onMenuItemProjectPreferences),
                          NULL
                         );
    gtk_widget_set_sensitive(GTK_WIDGET(pluginData.widgets.menuItems.projectProperties), geany_data->app->project != NULL);

    pluginData.widgets.menuItems.configuration = gtk_menu_item_new_with_label(_("Configuration"));
    gtk_container_add(GTK_CONTAINER(menu), pluginData.widgets.menuItems.configuration);
    plugin_signal_connect(geany_plugin,
                          G_OBJECT(pluginData.widgets.menuItems.configuration),
                          "activate",
                          FALSE,
                          G_CALLBACK(onMenuItemConfiguration),
                          NULL
                         );
  }
  gtk_widget_show_all(menu);

  gtk_menu_tool_button_set_menu(GTK_MENU_TOOL_BUTTON(pluginData.widgets.buttons.commands[0]), menu);

  updateKeyBinding(geany_plugin,lastCommandMenuItem);
  updateEnableToolbarMenuItems();
}

/***********************************************************************\
* Name   : initToolbar
* Purpose: init toolbar
* Input  : -
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void initToolbar()
{
  // update view
  updateToolbarButtons();
  updateToolbarMenuItems();
}

/***********************************************************************\
* Name   : doneToolbar
* Purpose: done toolbar
* Input  : plugin - Geany plugin
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void doneToolbar(GeanyPlugin *plugin)
{
  g_assert(plugin != NULL);

  UNUSED_VARIABLE(plugin);
}

/***********************************************************************\
* Name   : onMessageListAddRegEx
* Purpose: add regular expression
* Input  : widget      - widget (not used)
*          eventButton - event button (not used)
*          userData    - user data (no used)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onMessageListAddRegEx(GtkWidget      *widget,
                                 GdkEventButton *eventButton,
                                 gpointer       userData
                                )
{
  UNUSED_VARIABLE(widget);
  UNUSED_VARIABLE(eventButton);
  UNUSED_VARIABLE(userData);

  GtkTreeIter treeIterator;
  if (gtk_tree_model_get_iter(GTK_TREE_MODEL(pluginData.build.messagesStore),
                              &treeIterator,
                              pluginData.build.messagesTreePath
                             )
     )
  {
    gchar *message;
    gtk_tree_model_get(GTK_TREE_MODEL(pluginData.build.messagesStore),
                       &treeIterator,
                       MODEL_MESSAGE_MESSAGE, &message,
                       MODEL_END
                      );
    g_assert(message != NULL);

    addRegex(message);

    g_free(message);

    configurationSave();
  }
}

/***********************************************************************\
* Name   : onMessageListEditRegEx
* Purpose: edit regular expression
* Input  : widget      - widget (not used)
*          eventButton - event button (not used)
*          userData    - user data (no used)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onMessageListEditRegEx(GtkWidget      *widget,
                                  GdkEventButton *eventButton,
                                  gpointer       userData
                                 )
{
  UNUSED_VARIABLE(widget);
  UNUSED_VARIABLE(eventButton);
  UNUSED_VARIABLE(userData);

  GtkTreeIter treeIterator;
  if (gtk_tree_model_get_iter(GTK_TREE_MODEL(pluginData.build.messagesStore),
                              &treeIterator,
                              pluginData.build.messagesTreePath
                             )
     )
  {
    gchar *treePath;
    gchar *message;
    gtk_tree_model_get(GTK_TREE_MODEL(pluginData.build.messagesStore),
                       &treeIterator,
                       MODEL_MESSAGE_TREE_PATH, &treePath,
                       MODEL_MESSAGE_MESSAGE,   &message,
                       MODEL_END
                      );
    g_assert(treePath != NULL);
    g_assert(message != NULL);

    if (gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(pluginData.configuration.regexStore),
                                            &treeIterator,
                                            treePath
                                           )
       )
    {
      editRegex(pluginData.configuration.regexStore, &treeIterator, message);
    }

    g_free(message);
    g_free(treePath);
  }
}

/***********************************************************************\
* Name   : errorWarningTreeViewCellRendererInteger
* Purpose: render integer in cell
* Input  : column       - tree column
*          cellRenderer - cell renderer
*          treeModel    - tree data model
*          treeIterator - tree entry iterator
*          userData     - user data: column number
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void  errorWarningTreeViewCellRendererInteger(GtkTreeViewColumn *column,
                                                    GtkCellRenderer   *cellRenderer,
                                                    GtkTreeModel      *treeModel,
                                                    GtkTreeIter       *treeIterator,
                                                    gpointer          userData
                                                   )
{
  guint i = GPOINTER_TO_INT(userData);

  g_assert(column != NULL);
  g_assert(cellRenderer != NULL);
  g_assert(treeModel != NULL);
  g_assert(treeIterator != NULL);

  gint n;
  gtk_tree_model_get(treeModel, treeIterator, i, &n, MODEL_END);
  if (n > 0)
  {
    gchar buffer[64];
    g_snprintf(buffer, sizeof(buffer), "%d", n);
    g_object_set(cellRenderer, "text", buffer, NULL);
  }
  else
  {
    g_object_set(cellRenderer, "text", "", NULL);
  }
}

/***********************************************************************\
* Name   : newErrorWarningTreeView
* Purpose: create new error/warning tree view
* Input  : -
* Output : -
* Return : tree widget
* Notes  : -
\***********************************************************************/

LOCAL GtkWidget *newErrorWarningTreeView()
{
  GtkWidget         *treeView;
  GtkCellRenderer   *cellRenderer;
  GtkTreeViewColumn *column;

  treeView = gtk_tree_view_new();
  gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(treeView), TRUE);
  gtk_tree_view_set_headers_clickable(GTK_TREE_VIEW(treeView), TRUE);
  gtk_tree_view_set_search_column(GTK_TREE_VIEW(treeView), MODEL_ERROR_WARNING_FILE_PATH);
  {
    cellRenderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(column, _("File"));
    gtk_tree_view_column_pack_start(column, cellRenderer, TRUE);
    gtk_tree_view_column_set_attributes(column, cellRenderer, "text", MODEL_ERROR_WARNING_FILE_PATH, NULL);
    gtk_tree_view_column_set_sort_indicator(column, FALSE);
    gtk_tree_view_column_set_sort_column_id(column, MODEL_ERROR_WARNING_FILE_PATH);
    gtk_tree_view_column_set_resizable(column, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeView), column);

    cellRenderer = gtk_cell_renderer_text_new();
    g_object_set(GTK_CELL_RENDERER(cellRenderer), "xalign", 1.0, NULL);
    column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(column, _("Line"));
    gtk_tree_view_column_pack_start(column, cellRenderer, TRUE);
    gtk_tree_view_column_set_attributes(column, cellRenderer, "text", MODEL_ERROR_WARNING_LINE_NUMBER, NULL);
    gtk_tree_view_column_set_cell_data_func(column, cellRenderer, errorWarningTreeViewCellRendererInteger, GINT_TO_POINTER(MODEL_ERROR_WARNING_LINE_NUMBER), NULL);
    gtk_tree_view_column_set_sort_indicator(column, FALSE);
    gtk_tree_view_column_set_sort_column_id(column, MODEL_ERROR_WARNING_LINE_NUMBER);
    gtk_tree_view_column_set_resizable(column, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeView), column);

    cellRenderer = gtk_cell_renderer_text_new();
    g_object_set(GTK_CELL_RENDERER(cellRenderer), "xalign", 1.0, NULL);
    column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(column, _("Column"));
    gtk_tree_view_column_pack_start(column, cellRenderer, TRUE);
    gtk_tree_view_column_set_attributes(column, cellRenderer, "text", MODEL_ERROR_WARNING_COLUMN_NUMBER, NULL);
    gtk_tree_view_column_set_cell_data_func(column, cellRenderer, errorWarningTreeViewCellRendererInteger, GINT_TO_POINTER(MODEL_ERROR_WARNING_COLUMN_NUMBER), NULL);
    gtk_tree_view_column_set_sort_indicator(column, FALSE);
    gtk_tree_view_column_set_sort_column_id(column, MODEL_ERROR_WARNING_COLUMN_NUMBER);
    gtk_tree_view_column_set_resizable(column, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeView), column);

    cellRenderer = gtk_cell_renderer_text_new();
    g_object_set(cellRenderer, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
    column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(column, _("Message"));
    gtk_tree_view_column_pack_start(column, cellRenderer, TRUE);
    gtk_tree_view_column_set_attributes(column, cellRenderer, "text", MODEL_ERROR_WARNING_MESSAGE, NULL);
    gtk_tree_view_column_set_sort_indicator(column, FALSE);
    gtk_tree_view_column_set_sort_column_id(column, MODEL_ERROR_WARNING_MESSAGE);
    gtk_tree_view_column_set_resizable(column, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeView), column);
  }

  return treeView;
}

/***********************************************************************\
* Name   : onConfigureDoubleClickCommand
* Purpose: handle double-click: edit selected regular expression
* Input  : treeView - tree view
*          treePath - tree path (not used)
*          column   - tree column (not used)
*          userData - user data (not used)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onConfigureDoubleClickCommand(GtkTreeView       *treeView,
                                         GtkTreePath       *treePath,
                                         GtkTreeViewColumn *column,
                                         gpointer           userData
                                        )
{
  UNUSED_VARIABLE(treePath);
  UNUSED_VARIABLE(column);
  UNUSED_VARIABLE(userData);

  GtkTreeSelection *treeSelection = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeView));
  g_assert(treeSelection != NULL);

  GtkTreeModel *treeModel;
  GtkTreeIter  treeIterator;
  if (gtk_tree_selection_get_selected(treeSelection, &treeModel, &treeIterator))
  {
    editCommand(GTK_LIST_STORE(treeModel),
                &treeIterator
               );
  }
}

/***********************************************************************\
* Name   : onConfigureAddCommand
* Purpose: add command
* Input  : widget      - widget (not used)
*          eventButton - event button (not used)
*          userData    - user data: list store
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onConfigureAddCommand(GtkWidget      *widget,
                                 GdkEventButton *eventButton,
                                 gpointer       userData
                                )
{
  UNUSED_VARIABLE(widget);
  UNUSED_VARIABLE(eventButton);

  GtkListStore *listStore = GTK_LIST_STORE(userData);
  g_assert(listStore != NULL);

  addCommand(listStore);
}

/***********************************************************************\
* Name   : onConfigureCloneCommand
* Purpose: clone command
* Input  : widget      - widget (not used)
*          eventButton - event button (not used)
*          data        - user data: list view widget
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onConfigureCloneCommand(GtkWidget      *widget,
                                   GdkEventButton *eventButton,
                                   gpointer       userData
                                  )
{
  UNUSED_VARIABLE(widget);
  UNUSED_VARIABLE(eventButton);

  GtkWidget *treeView = GTK_WIDGET(userData);
  g_assert(treeView != NULL);
  GtkTreeSelection *treeSelection = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeView));
  g_assert(treeSelection != NULL);

  GtkTreeModel *treeModel;
  GtkTreeIter  treeIterator;
  if (gtk_tree_selection_get_selected(treeSelection, &treeModel, &treeIterator))
  {
    g_assert(treeModel != NULL);
    cloneCommand(GTK_LIST_STORE(treeModel),
                 &treeIterator
                );
  }
}

/***********************************************************************\
* Name   : onConfigureEditCommand
* Purpose: edit command
* Input  : widget      - widget (not used)
*          eventButton - event button (not used)
*          userData    - user data: list view widget
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onConfigureEditCommand(GtkWidget      *widget,
                                  GdkEventButton *eventButton,
                                  gpointer       userData
                                 )
{
  UNUSED_VARIABLE(widget);
  UNUSED_VARIABLE(eventButton);
  UNUSED_VARIABLE(userData);

  GtkWidget *treeView = GTK_WIDGET(userData);
  g_assert(treeView != NULL);
  GtkTreeSelection *treeSelection = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeView));
  g_assert(treeSelection != NULL);

  GtkTreeModel *treeModel;
  GtkTreeIter  treeIterator;
  if (gtk_tree_selection_get_selected(treeSelection, &treeModel, &treeIterator))
  {
    g_assert(treeModel != NULL);
    editCommand(GTK_LIST_STORE(treeModel),
                &treeIterator
               );
  }
}

/***********************************************************************\
* Name   : onConfigureRemoveCommand
* Purpose: remove command
* Input  : widget      - widget (not used)
*          eventButton - event button (not used)
*          userData    - user data: list view widget
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onConfigureRemoveCommand(GtkWidget      *widget,
                                    GdkEventButton *eventButton,
                                    gpointer       userData
                                   )
{
  UNUSED_VARIABLE(widget);
  UNUSED_VARIABLE(eventButton);

  GtkWidget *treeView = GTK_WIDGET(userData);
  g_assert(treeView != NULL);
  GtkTreeSelection *treeSelection = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeView));
  g_assert(treeSelection != NULL);

  GtkTreeModel *treeModel;
  GtkTreeIter  treeIterator;
  if (gtk_tree_selection_get_selected(treeSelection, &treeModel, &treeIterator))
  {
    g_assert(treeModel != NULL);
    gtk_list_store_remove(GTK_LIST_STORE(treeModel), &treeIterator);
  }
}

/***********************************************************************\
* Name   : onConfigureDoubleClickRegex
* Purpose: handle double-click: edit selected regular expression
* Input  : treeView - tree view
*          treePath - tree path (not used)
*          column   - tree column (not used)
*          userData - user data (not used)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onConfigureDoubleClickRegex(GtkTreeView       *treeView,
                                       GtkTreePath       *treePath,
                                       GtkTreeViewColumn *column,
                                       gpointer           userData
                                      )
{
  UNUSED_VARIABLE(treePath);
  UNUSED_VARIABLE(column);
  UNUSED_VARIABLE(userData);

  GtkTreeSelection *treeSelection = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeView));
  g_assert(treeSelection != NULL);

  GtkTreeModel *treeModel;
  GtkTreeIter  treeIterator;
  if (gtk_tree_selection_get_selected(treeSelection, &treeModel, &treeIterator))
  {
    g_assert(treeModel != NULL);
    editRegex(GTK_LIST_STORE(treeModel),
              &treeIterator,
              ""
             );
  }
}

/***********************************************************************\
* Name   : onConfigureAddRegEx
* Purpose: add regular expression
* Input  : widget      - widget (not used)
*          eventButton - event button (not used)
*          userData    - user data (not used)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onConfigureAddRegEx(GtkWidget      *widget,
                               GdkEventButton *eventButton,
                               gpointer       userData
                              )
{
  UNUSED_VARIABLE(widget);
  UNUSED_VARIABLE(eventButton);
  UNUSED_VARIABLE(userData);

// TODO: list store
fprintf(stderr,"%s:%d: _\n",__FILE__,__LINE__);

  addRegex("");
}

/***********************************************************************\
* Name   : onConfigureCloneRegex
* Purpose: clone regular expression
* Input  : widget      - widget (not used)
*          eventButton - event button (not used)
*          userData    - user data: list view widget
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onConfigureCloneRegex(GtkWidget      *widget,
                                 GdkEventButton *eventButton,
                                 gpointer       userData
                                )
{
  UNUSED_VARIABLE(widget);
  UNUSED_VARIABLE(eventButton);

  GtkWidget *treeView = GTK_WIDGET(userData);
  g_assert(treeView != NULL);
  GtkTreeSelection *treeSelection = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeView));
  g_assert(treeSelection != NULL);

  GtkTreeModel *treeModel;
  GtkTreeIter  treeIterator;
  if (gtk_tree_selection_get_selected(treeSelection, &treeModel, &treeIterator))
  {
    cloneRegex(GTK_LIST_STORE(treeModel),
               &treeIterator,
               ""
              );
  }
}

/***********************************************************************\
* Name   : onConfigureEditRegex
* Purpose: edit regular expression
* Input  : widget      - widget (not used)
*          eventButton - event button (not used)
*          userData    - user data: list view widget
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onConfigureEditRegex(GtkWidget      *widget,
                                GdkEventButton *eventButton,
                                gpointer       userData
                               )
{
  UNUSED_VARIABLE(widget);
  UNUSED_VARIABLE(eventButton);

  GtkWidget *treeView = GTK_WIDGET(userData);
  g_assert(treeView != NULL);
  GtkTreeSelection *treeSelection = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeView));
  g_assert(treeSelection != NULL);

  GtkTreeModel *treeModel;
  GtkTreeIter  treeIterator;
  if (gtk_tree_selection_get_selected(treeSelection, &treeModel, &treeIterator))
  {
    editRegex(GTK_LIST_STORE(treeModel),
              &treeIterator,
              ""
             );
  }
}

/***********************************************************************\
* Name   : onConfigureRemoveRegex
* Purpose: remove regular expression
* Input  : widget      - widget (not used)
*          eventButton - event button (not used)
*          userData    - user data: list view widget
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onConfigureRemoveRegex(GtkWidget      *widget,
                                  GdkEventButton *eventButton,
                                  gpointer       userData
                                 )
{
  UNUSED_VARIABLE(widget);
  UNUSED_VARIABLE(eventButton);

  GtkWidget *treeView = GTK_WIDGET(userData);
  g_assert(treeView != NULL);
  GtkTreeSelection *treeSelection = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeView));
  g_assert(treeSelection != NULL);

  GtkTreeModel *treeModel;
  GtkTreeIter  treeIterator;
  if (gtk_tree_selection_get_selected(treeSelection, &treeModel, &treeIterator))
  {
    gtk_list_store_remove(GTK_LIST_STORE(treeModel), &treeIterator);
  }
}

/***********************************************************************\
* Name   : initTab
* Purpose: init tab
* Input  : plugin - Geany plugin
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void initTab(GeanyPlugin *plugin)
{
  GtkTreeSortable *sortable;

  g_assert(plugin != NULL);
  g_assert(plugin->geany_data != NULL);
  g_assert(plugin->geany_data->main_widgets != NULL);

  // create tab widget
  pluginData.widgets.tabs = gtk_notebook_new();
  gtk_notebook_set_tab_pos(GTK_NOTEBOOK(pluginData.widgets.tabs), GTK_POS_TOP);
  {
    // create scrolled build messages list
    pluginData.widgets.messagesTab = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(pluginData.widgets.messagesTab),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_ALWAYS
                                  );
    {
      pluginData.widgets.messagesList = gtk_tree_view_new();
      gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(pluginData.widgets.messagesList), FALSE);
      {
        GtkTreeViewColumn *column;

        GtkCellRenderer *textRenderer = gtk_cell_renderer_text_new();
        column = gtk_tree_view_column_new();
        gtk_tree_view_column_pack_start(column, textRenderer, TRUE);
        gtk_tree_view_column_set_attributes(column,
                                            textRenderer,
                                            "foreground", MODEL_MESSAGE_COLOR,
                                            "text",       MODEL_MESSAGE_MESSAGE,
                                            NULL
                                           );
        gtk_tree_view_column_set_sort_indicator(column, FALSE);
        gtk_tree_view_column_set_sort_column_id(column, 1);
        gtk_tree_view_column_set_resizable(column, TRUE);
        gtk_tree_view_append_column(GTK_TREE_VIEW(pluginData.widgets.messagesList), column);

        gtk_tree_view_set_model(GTK_TREE_VIEW(pluginData.widgets.messagesList), GTK_TREE_MODEL(pluginData.build.messagesStore));

        GtkWidget *menu = gtk_menu_new();
        {
          GtkWidget *menuItem;

          pluginData.widgets.menuItems.editRegex = gtk_menu_item_new_with_label("edit regular expression");
          gtk_container_add(GTK_CONTAINER(menu), pluginData.widgets.menuItems.editRegex);
          plugin_signal_connect(geany_plugin,
                                G_OBJECT(pluginData.widgets.menuItems.editRegex),
                                "activate",
                                FALSE,
                                G_CALLBACK(onMessageListEditRegEx),
                                NULL
                               );

          menuItem = gtk_menu_item_new_with_label("add regular expression");
          gtk_container_add(GTK_CONTAINER(menu), menuItem);
          plugin_signal_connect(geany_plugin,
                                G_OBJECT(menuItem),
                                "activate",
                                FALSE,
                                G_CALLBACK(onMessageListAddRegEx),
                                NULL
                               );
        }
        gtk_widget_show_all(menu);

        plugin_signal_connect(geany_plugin,
                              G_OBJECT(pluginData.widgets.messagesList),
                              "button-press-event",
                              FALSE,
                              G_CALLBACK(onMessageListButtonPress),
                              menu
                             );
      }
      gtk_container_add(GTK_CONTAINER(pluginData.widgets.messagesTab), pluginData.widgets.messagesList);
    }
    pluginData.widgets.messagesTabIndex = gtk_notebook_append_page(GTK_NOTEBOOK(pluginData.widgets.tabs), pluginData.widgets.messagesTab, gtk_label_new("Messages"));
    gtk_widget_show_all(pluginData.widgets.messagesTab);

    // create scrolled errors tab
    pluginData.widgets.errorsTab = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(pluginData.widgets.errorsTab),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_ALWAYS
                                  );
    {
      // create build errors tree view
      pluginData.widgets.errorsTree = newErrorWarningTreeView();
      gtk_tree_view_set_show_expanders(GTK_TREE_VIEW(pluginData.widgets.errorsTree), TRUE);
      {
        gtk_tree_view_set_model(GTK_TREE_VIEW(pluginData.widgets.errorsTree), GTK_TREE_MODEL(pluginData.build.errorsStore));

// TODO:
//  treeSelection = gtk_tree_view_get_selection(GTK_TREE_VIEW(pluginData.tree));
//  gtk_tree_selection_set_mode(treeSelection, GTK_SELECTION_SINGLE);

        // add connections
        plugin_signal_connect(geany_plugin,
                              G_OBJECT(pluginData.widgets.errorsTree),
                              "button-press-event",
                              FALSE,
                              G_CALLBACK(onErrorWarningTreeButtonPress),
                              NULL
                             );
        plugin_signal_connect(geany_plugin,
                              G_OBJECT(pluginData.widgets.errorsTree),
                              "key-press-event",
                              FALSE,
                              G_CALLBACK(onErrorWarningTreeKeyPress),
                              NULL
                             );
        plugin_signal_connect(geany_plugin,
                              G_OBJECT(pluginData.widgets.errorsTree),
                              "row-activated",
                              FALSE,
                              G_CALLBACK(onErrorWarningTreeDoubleClick),
                              NULL
                             );

        // sorting
        sortable = GTK_TREE_SORTABLE(GTK_TREE_MODEL(pluginData.build.errorsStore));
        gtk_tree_sortable_set_sort_column_id(sortable, MODEL_ERROR_WARNING_FILE_PATH, GTK_SORT_ASCENDING);
      }
      gtk_container_add(GTK_CONTAINER(pluginData.widgets.errorsTab), pluginData.widgets.errorsTree);
    }
    pluginData.widgets.errorsTabLabel = gtk_label_new("Errors");
    g_object_set(GTK_LABEL(pluginData.widgets.errorsTabLabel), "xalign", 0.0, NULL);
    gtk_label_set_width_chars(GTK_LABEL(pluginData.widgets.errorsTabLabel), 6+1+4);
    pluginData.widgets.errorsTabIndex = gtk_notebook_append_page(GTK_NOTEBOOK(pluginData.widgets.tabs), pluginData.widgets.errorsTab, pluginData.widgets.errorsTabLabel);
    gtk_widget_show_all(pluginData.widgets.errorsTab);

    // create scrolled warnings tab
    pluginData.widgets.warningsTab = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(pluginData.widgets.warningsTab),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_ALWAYS
                                  );
    {
      // create build warnings tree view
      pluginData.widgets.warningsTree = newErrorWarningTreeView();
      gtk_tree_view_set_show_expanders(GTK_TREE_VIEW(pluginData.widgets.warningsTree), TRUE);
      {
        gtk_tree_view_set_model(GTK_TREE_VIEW(pluginData.widgets.warningsTree), GTK_TREE_MODEL(pluginData.build.warningsStore));

// TODO: remove
//  treeSelection = gtk_tree_view_get_selection(GTK_TREE_VIEW(pluginData.tree));
//  gtk_tree_selection_set_mode(treeSelection, GTK_SELECTION_SINGLE);

        // add connections
        plugin_signal_connect(geany_plugin,
                              G_OBJECT(pluginData.widgets.warningsTree),
                              "button-press-event",
                              FALSE,
                              G_CALLBACK(onErrorWarningTreeButtonPress),
                              NULL
                             );
        plugin_signal_connect(geany_plugin,
                              G_OBJECT(pluginData.widgets.warningsTree),
                              "key-press-event",
                              FALSE,
                              G_CALLBACK(onErrorWarningTreeKeyPress),
                              NULL
                             );
        plugin_signal_connect(geany_plugin,
                              G_OBJECT(pluginData.widgets.warningsTree),
                              "row-activated",
                              FALSE,
                              G_CALLBACK(onErrorWarningTreeDoubleClick),
                              NULL
                             );

        // sorting
        sortable = GTK_TREE_SORTABLE(GTK_TREE_MODEL(pluginData.build.warningsStore));
        gtk_tree_sortable_set_sort_column_id(sortable,
                                             MODEL_ERROR_WARNING_FILE_PATH,
                                             GTK_SORT_ASCENDING
                                            );
      }
      gtk_container_add(GTK_CONTAINER(pluginData.widgets.warningsTab), pluginData.widgets.warningsTree);
    }
    pluginData.widgets.warningsTabLabel = gtk_label_new("Warnings");
    g_object_set(GTK_LABEL(pluginData.widgets.warningsTabLabel), "xalign", 0.0, NULL);
    gtk_label_set_width_chars(GTK_LABEL(pluginData.widgets.warningsTabLabel), 8+1+4);
    pluginData.widgets.warningsTabIndex = gtk_notebook_append_page(GTK_NOTEBOOK(pluginData.widgets.tabs), pluginData.widgets.warningsTab, pluginData.widgets.warningsTabLabel);
    gtk_widget_show_all(pluginData.widgets.warningsTab);
  }
  gtk_widget_show_all(pluginData.widgets.tabs);
  gtk_notebook_set_current_page(GTK_NOTEBOOK(pluginData.widgets.tabs), 1);

  // add tab to geany notebook
  pluginData.widgets.tabIndex = gtk_notebook_append_page(GTK_NOTEBOOK(ui_lookup_widget(plugin->geany_data->main_widgets->window, "notebook_info")),
                                                         pluginData.widgets.tabs,
                                                         gtk_label_new(_("Builder"))
                                                        );
}

/***********************************************************************\
* Name   : initKeyBinding
* Purpose: init key bindings
* Input  : plugin        - Geany plugin
*          buildMenuItem - build menu item or NULL
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void initKeyBinding(GeanyPlugin *plugin, GtkWidget *buildMenuItem)
{
  GeanyKeyGroup *keyGroup;

  g_assert(plugin != NULL);

  keyGroup = plugin_set_key_group(plugin, KEY_GROUP_BUILDER, KEY_BINDING_COUNT, NULL);

  keybindings_set_item(keyGroup,
                       KEY_BINDING_BUILD,
                       onKeyBinding,
                       0,
                       0,
                       "build",
                       _("Build"),
                       buildMenuItem
                      );
  keybindings_set_item(keyGroup,
                       KEY_BINDING_ABORT,
                       onKeyBinding,
                       0,
                       0,
                       "abort",
                       _("Abort"),
                       pluginData.widgets.menuItems.abort
                      );
  keybindings_set_item(keyGroup,
                       KEY_BINDING_PREV_ERROR,
                       onKeyBinding,
                       0,
                       0,
                       "show_prev_error",
                       _("Show previous error"),
                       pluginData.widgets.menuItems.showPrevError
                      );
  keybindings_set_item(keyGroup,
                       KEY_BINDING_NEXT_ERROR,
                       onKeyBinding,
                       0,
                       0,
                       "show_next_error",
                       _("Show next error"),
                       pluginData.widgets.menuItems.showNextError
                      );
  keybindings_set_item(keyGroup,
                       KEY_BINDING_PREV_WARNING,
                       onKeyBinding,
                       0,
                       0,
                       "show_prev_warning",
                       _("Show previous warning"),
                       pluginData.widgets.menuItems.showPrevWarning
                      );
  keybindings_set_item(keyGroup,
                       KEY_BINDING_NEXT_WARNING,
                       onKeyBinding,
                       0,
                       0,
                       "show_next_warning",
                       _("Show next warning"),
                       pluginData.widgets.menuItems.showNextWarning
                      );
  keybindings_set_item(keyGroup,
                       KEY_BINDING_PROJECT_PROPERTIES,
                       onKeyBinding,
                       0,
                       0,
                       "project_properties",
                       _("Project properties"),
                       pluginData.widgets.menuItems.projectProperties
                      );
  keybindings_set_item(keyGroup,
                       KEY_BINDING_PLUGIN_CONFIGURATION,
                       onKeyBinding,
                       0,
                       0,
                       "plugin_configuration",
                       _("Plugin configuration"),
                       pluginData.widgets.menuItems.configuration
                      );
}

/***********************************************************************\
* Name   : updateKeyBinding
* Purpose: update key binding
* Input  : plugin - Geany plugin
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void updateKeyBinding(GeanyPlugin *plugin, GtkWidget *buildMenuItem)
{
  initKeyBinding(plugin,buildMenuItem);
  keybindings_load_keyfile();
}

/***********************************************************************\
* Name   : doneTab
* Purpose: done tab
* Input  : plugin - Geany plugin
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void doneTab(GeanyPlugin *plugin)
{
  g_assert(plugin != NULL);
  g_assert(plugin->geany_data != NULL);
  g_assert(plugin->geany_data->main_widgets != NULL);

  gtk_notebook_remove_page(GTK_NOTEBOOK(ui_lookup_widget(plugin->geany_data->main_widgets->window, "notebook_info")),
                           pluginData.widgets.tabIndex
                          );
}

/***********************************************************************\
* Name   : dialogRegexTypeCellRenderer
* Purpose: render regex type column
* Input  : cellLayout   - cell layout
*          cellRenderer - cell renderer
*          treeModel    - tree data model
*          treeIterator - tree entry iterator
*          userData     - user data: model index
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void configureCellRendererBoolean(GtkTreeViewColumn *cellLayout,
                                        GtkCellRenderer   *cellRenderer,
                                        GtkTreeModel      *treeModel,
                                        GtkTreeIter       *treeIterator,
                                        gpointer           userData
                                       )
{
  guint modelIndex = GPOINTER_TO_UINT(userData);

  g_assert(cellLayout != NULL);
  g_assert(cellRenderer != NULL);
  g_assert(treeModel != NULL);
  g_assert(treeIterator != NULL);

  UNUSED_VARIABLE(userData);

  gboolean value = FALSE;
  gtk_tree_model_get(treeModel, treeIterator, modelIndex, &value, MODEL_END);
  g_object_set(cellRenderer, "text", value ? "\u2713" : "-", NULL);
}

/***********************************************************************\
* Name   : configureCellRendererRegexType
* Purpose: render cell 'regular expression type'
* Input  : column       - column (not used)
*          cellRenderer - cell renderer
*          treeModel    - model
*          treeIterator - element iterator in model
*          userData     - user data (not used)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void configureCellRendererRegexType(GtkTreeViewColumn *column,
                                          GtkCellRenderer   *cellRenderer,
                                          GtkTreeModel      *treeModel,
                                          GtkTreeIter       *treeIterator,
                                          gpointer          userData
                                         )
{
  RegexTypes regexType;

  UNUSED_VARIABLE(column);
  UNUSED_VARIABLE(userData);

  g_assert(treeModel != NULL);
  g_assert(treeIterator != NULL);

  gtk_tree_model_get(treeModel,
                     treeIterator,
                     MODEL_REGEX_TYPE, &regexType,
                     MODEL_END
                    );
  g_assert(regexType >= REGEX_TYPE_MIN);
  g_assert(regexType <= REGEX_TYPE_MAX);
  g_object_set(cellRenderer, "text", REGEX_TYPE_STRINGS[regexType], NULL);
}

/***********************************************************************\
* Name   : onConfigureResponse
* Purpose: save configuration
* Input  : dialog   - dialog
*          response - dialog response code
*          userData - user data (not used)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onConfigureResponse(GtkDialog *dialog,
                               gint      response,
                               gpointer  userData
                              )
{
  UNUSED_VARIABLE(userData);

  // check if configuration should be saved/applied
  if (   (response != GTK_RESPONSE_OK)
      && (response != GTK_RESPONSE_APPLY)
     )
  {
    return;
  }

  // update values
  GdkRGBA color;
  gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(g_object_get_data(G_OBJECT(dialog), "error_indicator_color")), &color);
  pluginData.configuration.errorIndicators     = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(dialog), "error_indicators")));
  pluginData.configuration.errorIndicatorColor = color;
  gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(g_object_get_data(G_OBJECT(dialog), "warning_indicator_color")), &color);
  pluginData.configuration.warningIndicators     = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(dialog), "warning_indicators")));
  pluginData.configuration.warningIndicatorColor = color;

  pluginData.configuration.abortButton            = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(dialog), "abort_button")));
  pluginData.configuration.autoSaveAll            = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(dialog), "auto_save_all")));
  pluginData.configuration.addProjectRegExResults = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(dialog), "add_project_regex_results")));
  pluginData.configuration.autoShowFirstError     = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(dialog), "auto_show_first_error")));
  pluginData.configuration.autoShowFirstWarning   = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(dialog), "auto_show_first_warning")));

  // save configuration
  configurationSave();

  // update view
  updateToolbarButtons();
  updateToolbarMenuItems();
}

/***********************************************************************\
* Name   : configure
* Purpose: global configuration dialog
* Input  : plugin   - Geany plugin
*          dialog   - dialog
*          userData - user data (not used)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL GtkWidget *configure(GeanyPlugin *plugin,
                           GtkDialog   *dialog,
                           gpointer    userData
                          )
{
  g_assert(plugin != NULL);

  UNUSED_VARIABLE(userData);

  GtkWidget *notebook = gtk_notebook_new();
  g_assert(notebook != NULL);
  gtk_notebook_set_tab_pos(GTK_NOTEBOOK (notebook), GTK_POS_TOP);

  GtkWidget *tab;

  // create command widgets
  tab = addTab(notebook,"Commands");
  g_assert(tab != NULL);
  {
    GtkGrid *grid;

    grid = GTK_GRID(gtk_grid_new());
    gtk_grid_set_row_spacing(grid, 6);
    gtk_grid_set_column_spacing(grid, 12);
    g_object_set(grid, "margin", 6, NULL);
    GtkBox *vbox = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 6));
    gtk_widget_set_margin_top(GTK_WIDGET(vbox), 6);
    {
      GtkWidget *treeView;

      GtkWidget *scrolledWindow = gtk_scrolled_window_new(NULL, NULL);
      gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledWindow),
                                     GTK_POLICY_AUTOMATIC,
                                     GTK_POLICY_ALWAYS
                                    );
      {
        treeView = gtk_tree_view_new();
        gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(treeView), TRUE);
        {
          GtkCellRenderer   *cellRenderer;
          GtkTreeViewColumn *column;

          cellRenderer = gtk_cell_renderer_text_new();
          column = gtk_tree_view_column_new_with_attributes("Title",cellRenderer,"text", MODEL_COMMAND_TITLE, NULL);
          gtk_tree_view_column_set_sort_indicator(column, FALSE);
          gtk_tree_view_column_set_sort_column_id(column, MODEL_COMMAND_TITLE);
          gtk_tree_view_column_set_resizable(column, TRUE);
          gtk_tree_view_append_column(GTK_TREE_VIEW(treeView), column);

          cellRenderer = gtk_cell_renderer_text_new();
          column = gtk_tree_view_column_new_with_attributes("Command",cellRenderer,"text", MODEL_COMMAND_COMMAND_LINE, NULL);
          gtk_tree_view_column_set_sort_indicator(column, FALSE);
          gtk_tree_view_column_set_sort_column_id(column, MODEL_COMMAND_COMMAND_LINE);
          gtk_tree_view_column_set_resizable(column, TRUE);
          gtk_tree_view_append_column(GTK_TREE_VIEW(treeView), column);

          cellRenderer = gtk_cell_renderer_text_new();
          column = gtk_tree_view_column_new_with_attributes("Directory",cellRenderer,"text", MODEL_COMMAND_WORKING_DIRECTORY, NULL);
          gtk_tree_view_column_set_sort_indicator(column, FALSE);
          gtk_tree_view_column_set_sort_column_id(column, MODEL_COMMAND_WORKING_DIRECTORY);
          gtk_tree_view_column_set_resizable(column, TRUE);
          gtk_tree_view_append_column(GTK_TREE_VIEW(treeView), column);

          cellRenderer = gtk_cell_renderer_text_new();
          column = gtk_tree_view_column_new_with_attributes("B",cellRenderer,"text", MODEL_COMMAND_SHOW_BUTTON, NULL);
          gtk_tree_view_column_set_cell_data_func(column, cellRenderer, configureCellRendererBoolean, GUINT_TO_POINTER(MODEL_COMMAND_SHOW_BUTTON), NULL);
          gtk_tree_view_column_set_sort_indicator(column, FALSE);
          gtk_tree_view_column_set_sort_column_id(column, MODEL_COMMAND_SHOW_BUTTON);
          gtk_tree_view_append_column(GTK_TREE_VIEW(treeView), column);

          cellRenderer = gtk_cell_renderer_text_new();
          column = gtk_tree_view_column_new_with_attributes("M",cellRenderer,"text", MODEL_COMMAND_SHOW_MENU_ITEM, NULL);
          gtk_tree_view_column_set_cell_data_func(column, cellRenderer, configureCellRendererBoolean, GUINT_TO_POINTER(MODEL_COMMAND_SHOW_MENU_ITEM), NULL);
          gtk_tree_view_column_set_sort_indicator(column, FALSE);
          gtk_tree_view_column_set_sort_column_id(column, MODEL_COMMAND_SHOW_MENU_ITEM);
          gtk_tree_view_append_column(GTK_TREE_VIEW(treeView), column);

          cellRenderer = gtk_cell_renderer_text_new();
          column = gtk_tree_view_column_new_with_attributes("C",cellRenderer,"text", MODEL_COMMAND_INPUT_CUSTOM_TEXT, NULL);
          gtk_tree_view_column_set_cell_data_func(column, cellRenderer, configureCellRendererBoolean, GUINT_TO_POINTER(MODEL_COMMAND_INPUT_CUSTOM_TEXT), NULL);
          gtk_tree_view_column_set_sort_indicator(column, FALSE);
          gtk_tree_view_column_set_sort_column_id(column, MODEL_COMMAND_INPUT_CUSTOM_TEXT);
          gtk_tree_view_append_column(GTK_TREE_VIEW(treeView), column);

          cellRenderer = gtk_cell_renderer_text_new();
          column = gtk_tree_view_column_new_with_attributes("D",cellRenderer,"text", MODEL_COMMAND_RUN_IN_DOCKER_CONTAINER, NULL);
          gtk_tree_view_column_set_cell_data_func(column, cellRenderer, configureCellRendererBoolean, GUINT_TO_POINTER(MODEL_COMMAND_RUN_IN_DOCKER_CONTAINER), NULL);
          gtk_tree_view_column_set_sort_indicator(column, FALSE);
          gtk_tree_view_column_set_sort_column_id(column, MODEL_COMMAND_RUN_IN_DOCKER_CONTAINER);
          gtk_tree_view_append_column(GTK_TREE_VIEW(treeView), column);

          cellRenderer = gtk_cell_renderer_text_new();
          column = gtk_tree_view_column_new_with_attributes("R",cellRenderer,"text", MODEL_COMMAND_RUN_REMOTE, NULL);
          gtk_tree_view_column_set_cell_data_func(column, cellRenderer, configureCellRendererBoolean, GUINT_TO_POINTER(MODEL_COMMAND_RUN_REMOTE), NULL);
          gtk_tree_view_column_set_sort_indicator(column, FALSE);
          gtk_tree_view_column_set_sort_column_id(column, MODEL_COMMAND_RUN_REMOTE);
          gtk_tree_view_append_column(GTK_TREE_VIEW(treeView), column);

          cellRenderer = gtk_cell_renderer_text_new();
          column = gtk_tree_view_column_new_with_attributes("P",cellRenderer,"text", MODEL_COMMAND_PARSE_OUTPUT, NULL);
          gtk_tree_view_column_set_cell_data_func(column, cellRenderer, configureCellRendererBoolean, GUINT_TO_POINTER(MODEL_COMMAND_PARSE_OUTPUT), NULL);
          gtk_tree_view_column_set_sort_indicator(column, FALSE);
          gtk_tree_view_column_set_sort_column_id(column, MODEL_COMMAND_PARSE_OUTPUT);
          gtk_tree_view_append_column(GTK_TREE_VIEW(treeView), column);

          gtk_tree_view_set_model(GTK_TREE_VIEW(treeView),
                                  GTK_TREE_MODEL(pluginData.configuration.commandStore)
                                 );

          plugin_signal_connect(geany_plugin,
                                G_OBJECT(treeView),
                                "row-activated",
                                FALSE,
                                G_CALLBACK(onConfigureDoubleClickCommand),
                                NULL
                               );
        }
        gtk_container_add(GTK_CONTAINER(scrolledWindow), treeView);
      }
      addBox(vbox, TRUE, scrolledWindow);

      GtkBox *hbox = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12));
      g_object_set(hbox, "margin", 6, NULL);
      {
        GtkWidget *button;
        GtkWidget *image;

        button = gtk_button_new_with_label(_("Add"));
        image = gtk_image_new();
        gtk_image_set_from_icon_name(GTK_IMAGE(image), "list-add", GTK_ICON_SIZE_BUTTON);
        gtk_button_set_image(GTK_BUTTON(button), image);
        addBox(hbox, FALSE, button);
        plugin_signal_connect(geany_plugin,
                              G_OBJECT(button),
                              "button-press-event",
                              FALSE,
                              G_CALLBACK(onConfigureAddCommand),
                              pluginData.configuration.commandStore
                             );

        button = gtk_button_new_with_label(_("Clone"));
        image = gtk_image_new();
        gtk_image_set_from_icon_name(GTK_IMAGE(image), "edit-copy", GTK_ICON_SIZE_BUTTON);
        gtk_button_set_image(GTK_BUTTON(button), image);
        addBox(hbox, FALSE, button);
        plugin_signal_connect(geany_plugin,
                              G_OBJECT(button),
                              "button-press-event",
                              FALSE,
                              G_CALLBACK(onConfigureCloneCommand),
                              treeView
                             );

        button = gtk_button_new_with_label(_("Edit"));
        image = gtk_image_new();
        gtk_image_set_from_icon_name(GTK_IMAGE(image), "edit-paste", GTK_ICON_SIZE_BUTTON);
        gtk_button_set_image(GTK_BUTTON(button), image);
        addBox(hbox, FALSE, button);
        plugin_signal_connect(geany_plugin,
                              G_OBJECT(button),
                              "button-press-event",
                              FALSE,
                              G_CALLBACK(onConfigureEditCommand),
                              treeView
                             );

        button = gtk_button_new_with_label(_("Remove"));
        image = gtk_image_new();
        gtk_image_set_from_icon_name(GTK_IMAGE(image), "list-remove", GTK_ICON_SIZE_BUTTON);
        gtk_button_set_image(GTK_BUTTON(button), image);
        addBox(hbox, FALSE, button);
        plugin_signal_connect(geany_plugin,
                              G_OBJECT(button),
                              "button-press-event",
                              FALSE,
                              G_CALLBACK(onConfigureRemoveCommand),
                              treeView
                             );
      }
      addBox(vbox, FALSE, GTK_WIDGET(hbox));
    }
    addBox(GTK_BOX(tab), TRUE, GTK_WIDGET(vbox));

    GtkBox *hbox = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12));
    gtk_widget_set_margin_start(GTK_WIDGET(hbox), 6);
    {
      addBox(hbox, FALSE, gtk_label_new("Indicator colors: "));

      addBox(hbox, FALSE, newCheckButton (NULL, G_OBJECT(dialog), "error_indicators",        "errors", "Show error indicators."));
      addBox(hbox, FALSE, newColorChooser(NULL, G_OBJECT(dialog), "error_indicator_color",   "Error indicator color."));
      addBox(hbox, FALSE, newCheckButton (NULL, G_OBJECT(dialog), "warning_indicators",      "warnings", "Show warning indicators."));
      addBox(hbox, FALSE, newColorChooser(NULL, G_OBJECT(dialog), "warning_indicator_color", "Warning indicator color."));
    }
    addBox(GTK_BOX(tab), FALSE, GTK_WIDGET(hbox));

    grid = GTK_GRID(gtk_grid_new());
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_widget_set_hexpand(GTK_WIDGET(grid), TRUE);
    {
      addGrid(grid, 0, 0, 1, newCheckButton(NULL, G_OBJECT(dialog), "abort_button",             "Abort button", "Show abort button."));
      addGrid(grid, 1, 0, 1, newCheckButton(NULL, G_OBJECT(dialog), "auto_save_all",            "Auto save all", "Auto save all before build is started."));
      addGrid(grid, 2, 0, 1, newCheckButton(NULL, G_OBJECT(dialog), "add_project_regex_results","Add results of project regular expressions","Add results of project defined regular expression. If disabled only project regular expressions - if defined - are used only to detect errors or warnings."));
      addGrid(grid, 3, 0, 1, newCheckButton(NULL, G_OBJECT(dialog), "auto_show_first_error",    "Show first error", "Auto show first error after build is done."));
      addGrid(grid, 4, 0, 1, newCheckButton(NULL, G_OBJECT(dialog), "auto_show_first_warning",  "Show first warning", "Auto show first warning after build is done without errors."));
    }
    addBox(GTK_BOX(tab), FALSE, GTK_WIDGET(grid));
  }

  // create regular expression list widgets
  tab = addTab(notebook,"Regular expressions");
  g_assert(tab != NULL);
  {
    GtkBox *vbox = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 6));
    gtk_widget_set_margin_top(GTK_WIDGET(vbox), 6);
    {
      GtkWidget *scrolledWindow = gtk_scrolled_window_new(NULL, NULL);
      gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledWindow),
                                     GTK_POLICY_AUTOMATIC,
                                     GTK_POLICY_ALWAYS
                                    );
      {
        GtkWidget *treeView = gtk_tree_view_new();
        gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(treeView), TRUE);
        g_object_set_data(G_OBJECT(dialog), "regex_list", treeView);
        {
          GtkCellRenderer   *cellRenderer;
          GtkTreeViewColumn *column;

          cellRenderer = gtk_cell_renderer_text_new();
          column = gtk_tree_view_column_new_with_attributes("Language",cellRenderer,"text", MODEL_REGEX_LANGUAGE, NULL);
          gtk_tree_view_column_set_sort_indicator(column, FALSE);
          gtk_tree_view_column_set_sort_column_id(column, MODEL_REGEX_LANGUAGE);
          gtk_tree_view_column_set_resizable(column, TRUE);
          gtk_tree_view_append_column(GTK_TREE_VIEW(treeView), column);

          cellRenderer = gtk_cell_renderer_text_new();
          column = gtk_tree_view_column_new_with_attributes("Group",cellRenderer,"text", MODEL_REGEX_GROUP, NULL);
          gtk_tree_view_column_set_sort_indicator(column, FALSE);
          gtk_tree_view_column_set_sort_column_id(column, MODEL_REGEX_GROUP);
          gtk_tree_view_column_set_resizable(column, TRUE);
          gtk_tree_view_append_column(GTK_TREE_VIEW(treeView), column);

          cellRenderer = gtk_cell_renderer_text_new();
          column = gtk_tree_view_column_new_with_attributes("Type",cellRenderer,"text", MODEL_REGEX_TYPE, NULL);
          gtk_tree_view_column_set_cell_data_func(column, cellRenderer, configureCellRendererRegexType, NULL, NULL);
          gtk_tree_view_column_set_sort_indicator(column, FALSE);
          gtk_tree_view_column_set_sort_column_id(column, MODEL_REGEX_TYPE);
          gtk_tree_view_column_set_resizable(column, TRUE);
          gtk_tree_view_append_column(GTK_TREE_VIEW(treeView), column);

          cellRenderer = gtk_cell_renderer_text_new();
          column = gtk_tree_view_column_new_with_attributes("Regex",cellRenderer,"text", MODEL_REGEX_REGEX, NULL);
          gtk_tree_view_column_set_sort_indicator(column, FALSE);
          gtk_tree_view_column_set_sort_column_id(column, MODEL_REGEX_REGEX);
          gtk_tree_view_column_set_resizable(column, TRUE);
          gtk_tree_view_append_column(GTK_TREE_VIEW(treeView), column);

          gtk_tree_view_set_model(GTK_TREE_VIEW(treeView),
                                  GTK_TREE_MODEL(pluginData.configuration.regexStore)
                                 );

          plugin_signal_connect(geany_plugin,
                                G_OBJECT(treeView),
                                "row-activated",
                                FALSE,
                                G_CALLBACK(onConfigureDoubleClickRegex),
                                NULL
                               );
        }
        gtk_container_add(GTK_CONTAINER(scrolledWindow), treeView);
      }
      addBox(vbox, TRUE, scrolledWindow);

      GtkBox *hbox = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12));
      g_object_set(hbox, "margin", 6, NULL);
      {
        GtkWidget *button;
        GtkWidget *image;

        button = gtk_button_new_with_label(_("Add"));
        image = gtk_image_new();
        gtk_image_set_from_icon_name(GTK_IMAGE(image), "list-add", GTK_ICON_SIZE_BUTTON);
        gtk_button_set_image(GTK_BUTTON(button), image);
        addBox(hbox, FALSE, button);
        plugin_signal_connect(geany_plugin,
                              G_OBJECT(button),
                              "button-press-event",
                              FALSE,
                              G_CALLBACK(onConfigureAddRegEx),
                              NULL
                             );

        button = gtk_button_new_with_label(_("Clone"));
        image = gtk_image_new();
        gtk_image_set_from_icon_name(GTK_IMAGE(image), "edit-copy", GTK_ICON_SIZE_BUTTON);
        gtk_button_set_image(GTK_BUTTON(button), image);
        addBox(hbox, FALSE, button);
        plugin_signal_connect(geany_plugin,
                              G_OBJECT(button),
                              "button-press-event",
                              FALSE,
                              G_CALLBACK(onConfigureCloneRegex),
                              g_object_get_data(G_OBJECT(dialog), "regex_list")
                             );

        button = gtk_button_new_with_label(_("Edit"));
        image = gtk_image_new();
        gtk_image_set_from_icon_name(GTK_IMAGE(image), "edit-paste", GTK_ICON_SIZE_BUTTON);
        gtk_button_set_image(GTK_BUTTON(button), image);
        addBox(hbox, FALSE, button);
        plugin_signal_connect(geany_plugin,
                              G_OBJECT(button),
                              "button-press-event",
                              FALSE,
                              G_CALLBACK(onConfigureEditRegex),
                              g_object_get_data(G_OBJECT(dialog), "regex_list")
                             );

        button = gtk_button_new_with_label(_("Remove"));
        image = gtk_image_new();
        gtk_image_set_from_icon_name(GTK_IMAGE(image), "list-remove", GTK_ICON_SIZE_BUTTON);
        gtk_button_set_image(GTK_BUTTON(button), image);
        addBox(hbox, FALSE, button);
        plugin_signal_connect(geany_plugin,
                              G_OBJECT(button),
                              "button-press-event",
                              FALSE,
                              G_CALLBACK(onConfigureRemoveRegex),
                              g_object_get_data(G_OBJECT(dialog), "regex_list")
                             );
      }
      addBox(vbox, FALSE, GTK_WIDGET(hbox));
    }
    addBox(GTK_BOX(tab), TRUE, GTK_WIDGET(vbox));
  }

  gtk_widget_show_all(GTK_WIDGET(notebook));

  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(dialog), "error_indicators"         )), pluginData.configuration.errorIndicators);
  gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(g_object_get_data(G_OBJECT(dialog),   "error_indicator_color"    )), &pluginData.configuration.errorIndicatorColor);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(dialog), "warning_indicators"       )), pluginData.configuration.warningIndicators);
  gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(g_object_get_data(G_OBJECT(dialog),   "warning_indicator_color"  )), &pluginData.configuration.warningIndicatorColor);

  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(dialog), "abort_button"             )), pluginData.configuration.abortButton);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(dialog), "auto_save_all"            )), pluginData.configuration.autoSaveAll);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(dialog), "add_project_regex_results")), pluginData.configuration.addProjectRegExResults);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(dialog), "auto_show_first_error"    )), pluginData.configuration.autoShowFirstError);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(dialog), "auto_show_first_warning"  )), pluginData.configuration.autoShowFirstWarning);

  // add connections
  plugin_signal_connect(geany_plugin,
                        G_OBJECT(dialog),
                        "response",
                        FALSE,
                        G_CALLBACK(onConfigureResponse),
                        NULL
                       );

  return GTK_WIDGET(notebook);
}

/***********************************************************************\
* Name   : showProjectSettingsTab
* Purpose: show project settings tab
* Input  : userData- user data: notebook widget
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL gboolean showProjectSettingsTab(gpointer userData)
{
  GtkWidget *notebook = (GtkWidget*)userData;
  g_assert(notebook != NULL);

  gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook),
                                pluginData.widgets.projectPropertiesTabIndex
                               );

  return FALSE;
}

/***********************************************************************\
* Name   : onProjectDialogOpen
* Purpose: add project settings
* Input  : object   - object (not used)
*          notebook - notebook
*          userData - user data (not used)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onProjectDialogOpen(GObject   *object,
                               GtkWidget *notebook,
                               gpointer  userData
                              )
{
  UNUSED_VARIABLE(object);
  UNUSED_VARIABLE(userData);

  // init project configuration settings
  pluginData.widgets.projectProperties = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 6));
  gtk_widget_set_margin_top(GTK_WIDGET(pluginData.widgets.projectProperties), 6);
  gtk_widget_set_margin_bottom(GTK_WIDGET(pluginData.widgets.projectProperties), 6);
  {
    GtkWidget *treeView;

    GtkWidget *scrolledWindow = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledWindow),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_ALWAYS
                                  );
    {
      treeView = gtk_tree_view_new();
      gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(treeView), TRUE);
      {
        GtkCellRenderer   *cellRenderer;
        GtkTreeViewColumn *column;

        cellRenderer = gtk_cell_renderer_text_new();
        column = gtk_tree_view_column_new_with_attributes("Title",cellRenderer,"text", MODEL_COMMAND_TITLE, NULL);
        gtk_tree_view_column_set_sort_indicator(column, FALSE);
        gtk_tree_view_column_set_sort_column_id(column, MODEL_COMMAND_TITLE);
        gtk_tree_view_column_set_resizable(column, TRUE);
        gtk_tree_view_append_column(GTK_TREE_VIEW(treeView), column);

        cellRenderer = gtk_cell_renderer_text_new();
        column = gtk_tree_view_column_new_with_attributes("Command",cellRenderer,"text", MODEL_COMMAND_COMMAND_LINE, NULL);
        gtk_tree_view_column_set_sort_indicator(column, FALSE);
        gtk_tree_view_column_set_sort_column_id(column, MODEL_COMMAND_COMMAND_LINE);
        gtk_tree_view_column_set_resizable(column, TRUE);
        gtk_tree_view_append_column(GTK_TREE_VIEW(treeView), column);

        cellRenderer = gtk_cell_renderer_text_new();
        column = gtk_tree_view_column_new_with_attributes("Directory",cellRenderer,"text", MODEL_COMMAND_WORKING_DIRECTORY, NULL);
        gtk_tree_view_column_set_sort_indicator(column, FALSE);
        gtk_tree_view_column_set_sort_column_id(column, MODEL_COMMAND_WORKING_DIRECTORY);
        gtk_tree_view_column_set_resizable(column, TRUE);
        gtk_tree_view_append_column(GTK_TREE_VIEW(treeView), column);

        cellRenderer = gtk_cell_renderer_text_new();
        column = gtk_tree_view_column_new_with_attributes("B",cellRenderer,"text", MODEL_COMMAND_SHOW_BUTTON, NULL);
        gtk_tree_view_column_set_cell_data_func(column, cellRenderer, configureCellRendererBoolean, GUINT_TO_POINTER(MODEL_COMMAND_SHOW_BUTTON), NULL);
        gtk_tree_view_column_set_sort_indicator(column, FALSE);
        gtk_tree_view_column_set_sort_column_id(column, MODEL_COMMAND_SHOW_BUTTON);
        gtk_tree_view_append_column(GTK_TREE_VIEW(treeView), column);

        cellRenderer = gtk_cell_renderer_text_new();
        column = gtk_tree_view_column_new_with_attributes("M",cellRenderer,"text", MODEL_COMMAND_SHOW_MENU_ITEM, NULL);
        gtk_tree_view_column_set_cell_data_func(column, cellRenderer, configureCellRendererBoolean, GUINT_TO_POINTER(MODEL_COMMAND_SHOW_MENU_ITEM), NULL);
        gtk_tree_view_column_set_sort_indicator(column, FALSE);
        gtk_tree_view_column_set_sort_column_id(column, MODEL_COMMAND_SHOW_MENU_ITEM);
        gtk_tree_view_append_column(GTK_TREE_VIEW(treeView), column);

        cellRenderer = gtk_cell_renderer_text_new();
        column = gtk_tree_view_column_new_with_attributes("C",cellRenderer,"text", MODEL_COMMAND_INPUT_CUSTOM_TEXT, NULL);
        gtk_tree_view_column_set_cell_data_func(column, cellRenderer, configureCellRendererBoolean, GUINT_TO_POINTER(MODEL_COMMAND_INPUT_CUSTOM_TEXT), NULL);
        gtk_tree_view_column_set_sort_indicator(column, FALSE);
        gtk_tree_view_column_set_sort_column_id(column, MODEL_COMMAND_INPUT_CUSTOM_TEXT);
        gtk_tree_view_append_column(GTK_TREE_VIEW(treeView), column);

        cellRenderer = gtk_cell_renderer_text_new();
        column = gtk_tree_view_column_new_with_attributes("D",cellRenderer,"text", MODEL_COMMAND_RUN_IN_DOCKER_CONTAINER, NULL);
        gtk_tree_view_column_set_cell_data_func(column, cellRenderer, configureCellRendererBoolean, GUINT_TO_POINTER(MODEL_COMMAND_RUN_IN_DOCKER_CONTAINER), NULL);
        gtk_tree_view_column_set_sort_indicator(column, FALSE);
        gtk_tree_view_column_set_sort_column_id(column, MODEL_COMMAND_RUN_IN_DOCKER_CONTAINER);
        gtk_tree_view_append_column(GTK_TREE_VIEW(treeView), column);

        cellRenderer = gtk_cell_renderer_text_new();
        column = gtk_tree_view_column_new_with_attributes("R",cellRenderer,"text", MODEL_COMMAND_RUN_REMOTE, NULL);
        gtk_tree_view_column_set_cell_data_func(column, cellRenderer, configureCellRendererBoolean, GUINT_TO_POINTER(MODEL_COMMAND_RUN_REMOTE), NULL);
        gtk_tree_view_column_set_sort_indicator(column, FALSE);
        gtk_tree_view_column_set_sort_column_id(column, MODEL_COMMAND_RUN_REMOTE);
        gtk_tree_view_append_column(GTK_TREE_VIEW(treeView), column);

        cellRenderer = gtk_cell_renderer_text_new();
        column = gtk_tree_view_column_new_with_attributes("P",cellRenderer,"text", MODEL_COMMAND_PARSE_OUTPUT, NULL);
        gtk_tree_view_column_set_cell_data_func(column, cellRenderer, configureCellRendererBoolean, GUINT_TO_POINTER(MODEL_COMMAND_PARSE_OUTPUT), NULL);
        gtk_tree_view_column_set_sort_indicator(column, FALSE);
        gtk_tree_view_column_set_sort_column_id(column, MODEL_COMMAND_PARSE_OUTPUT);
        gtk_tree_view_append_column(GTK_TREE_VIEW(treeView), column);

        gtk_tree_view_set_model(GTK_TREE_VIEW(treeView),
                                GTK_TREE_MODEL(pluginData.projectProperties.commandStore)
                               );

        plugin_signal_connect(geany_plugin,
                              G_OBJECT(treeView),
                              "row-activated",
                              FALSE,
                              G_CALLBACK(onConfigureDoubleClickCommand),
                              NULL
                             );
      }
      gtk_container_add(GTK_CONTAINER(scrolledWindow), treeView);
    }
    addBox(GTK_BOX(pluginData.widgets.projectProperties), TRUE, scrolledWindow);

    GtkBox *hbox = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12));
    g_object_set(hbox, "margin", 6, NULL);
    {
      GtkWidget *button;
      GtkWidget *image;

      button = gtk_button_new_with_label(_("Add"));
      image = gtk_image_new();
      gtk_image_set_from_icon_name(GTK_IMAGE(image), "list-add", GTK_ICON_SIZE_BUTTON);
      gtk_button_set_image(GTK_BUTTON(button), image);
      addBox(hbox, FALSE, button);
      plugin_signal_connect(geany_plugin,
                            G_OBJECT(button),
                            "button-press-event",
                            FALSE,
                            G_CALLBACK(onConfigureAddCommand),
                            pluginData.projectProperties.commandStore
                           );

      button = gtk_button_new_with_label(_("Clone"));
      image = gtk_image_new();
      gtk_image_set_from_icon_name(GTK_IMAGE(image), "edit-copy", GTK_ICON_SIZE_BUTTON);
      gtk_button_set_image(GTK_BUTTON(button), image);
      addBox(hbox, FALSE, button);
      plugin_signal_connect(geany_plugin,
                            G_OBJECT(button),
                            "button-press-event",
                            FALSE,
                            G_CALLBACK(onConfigureCloneCommand),
                            treeView
                           );

      button = gtk_button_new_with_label(_("Edit"));
      image = gtk_image_new();
      gtk_image_set_from_icon_name(GTK_IMAGE(image), "edit-paste", GTK_ICON_SIZE_BUTTON);
      gtk_button_set_image(GTK_BUTTON(button), image);
      addBox(hbox, FALSE, button);
      plugin_signal_connect(geany_plugin,
                            G_OBJECT(button),
                            "button-press-event",
                            FALSE,
                            G_CALLBACK(onConfigureEditCommand),
                            treeView
                           );

      button = gtk_button_new_with_label(_("Remove"));
      image = gtk_image_new();
      gtk_image_set_from_icon_name(GTK_IMAGE(image), "list-remove", GTK_ICON_SIZE_BUTTON);
      gtk_button_set_image(GTK_BUTTON(button), image);
      addBox(hbox, FALSE, button);
      plugin_signal_connect(geany_plugin,
                            G_OBJECT(button),
                            "button-press-event",
                            FALSE,
                            G_CALLBACK(onConfigureRemoveCommand),
                            treeView
                           );
    }
    addBox(GTK_BOX(pluginData.widgets.projectProperties), FALSE, GTK_WIDGET(hbox));

    GtkGrid *grid = GTK_GRID(gtk_grid_new());
// TODO: remove
//GdkColor red = {0, 0xffff, 0x0000, 0x0000};
//gtk_widget_modify_bg(grid, GTK_STATE_NORMAL, &red);
    gtk_widget_set_margin_start(GTK_WIDGET(grid), 6);
    gtk_widget_set_margin_end(GTK_WIDGET(grid), 6);
    gtk_grid_set_row_spacing(grid, 6);
    gtk_grid_set_column_spacing(grid, 12);
    gtk_widget_set_hexpand(GTK_WIDGET(grid), TRUE);
    {
      addGrid(grid, 0, 0, 1, newLabel(NULL, G_OBJECT(pluginData.widgets.projectProperties), NULL, "Error regular expression", NULL));
      addGrid(grid, 0, 1, 1, newEntry(NULL, G_OBJECT(pluginData.widgets.projectProperties), "error_regex", "Regular expression to recognize errors"));

      addGrid(grid, 1, 0, 1, newLabel(NULL, G_OBJECT(pluginData.widgets.projectProperties), NULL, "Warning regular expression", NULL));
      addGrid(grid, 1, 1, 1, newEntry(NULL, G_OBJECT(pluginData.widgets.projectProperties), "warning_regex", "Regular expression to recognize warnings"));
    }
    addBox(GTK_BOX(pluginData.widgets.projectProperties), FALSE, GTK_WIDGET(grid));
  }
  gtk_widget_show_all(GTK_WIDGET(pluginData.widgets.projectProperties));

  // add project properties tab
  pluginData.widgets.projectPropertiesTabIndex = gtk_notebook_append_page(GTK_NOTEBOOK(notebook), GTK_WIDGET(pluginData.widgets.projectProperties), gtk_label_new("Builder"));

  gtk_entry_set_text(GTK_ENTRY(g_object_get_data(G_OBJECT(pluginData.widgets.projectProperties), "error_regex")), pluginData.projectProperties.errorRegEx->str);
  gtk_entry_set_text(GTK_ENTRY(g_object_get_data(G_OBJECT(pluginData.widgets.projectProperties), "warning_regex")), pluginData.projectProperties.warningRegEx->str);

  if (pluginData.widgets.showProjectPropertiesTab)
  {
    // show project settings tab
    pluginData.widgets.showProjectPropertiesTab = FALSE;
    g_idle_add(showProjectSettingsTab, GTK_NOTEBOOK(notebook));
  }
}

/***********************************************************************\
* Name   : onProjectDialogConfirmed
* Purpose: handle project properties dialog confirm: save values
* Input  : object   - object (not used)
*          notebook - notebook (not used)
*          userData - user data (not used)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onProjectDialogConfirmed(GObject   *object,
                                    GtkWidget *notebook,
                                    gpointer  userData
                                   )
{
  UNUSED_VARIABLE(object);
  UNUSED_VARIABLE(notebook);
  UNUSED_VARIABLE(userData);

  // update values
  g_string_assign(pluginData.projectProperties.errorRegEx,   gtk_entry_get_text(GTK_ENTRY(g_object_get_data(G_OBJECT(pluginData.widgets.projectProperties), "error_regex"))));
  g_string_assign(pluginData.projectProperties.warningRegEx, gtk_entry_get_text(GTK_ENTRY(g_object_get_data(G_OBJECT(pluginData.widgets.projectProperties), "warning_regex"))));

  // update enable buttons/menu items
  updateEnableToolbarButtons();
  updateEnableToolbarMenuItems();
}

/***********************************************************************\
* Name   : onProjectDialogClose
* Purpose: handle project properties close
* Input  : object   - object (not used)
*          notebook - notebook
*          userData - user data (not used)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onProjectDialogClose(GObject   *object,
                                GtkWidget *notebook,
                                gpointer  userData
                               )
{
  UNUSED_VARIABLE(object);
  UNUSED_VARIABLE(userData);

  // remove project properties tab
  gtk_notebook_remove_page(GTK_NOTEBOOK(notebook), pluginData.widgets.projectPropertiesTabIndex);
}

/***********************************************************************\
* Name   : onProjectOpen
* Purpose: open project and load configuration
* Input  : object        - object (not used)
*          configuration - project configuration to load values from
*          userData      - user data (not used)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onProjectOpen(GObject  *object,
                         GKeyFile *configuration,
                         gpointer userData
                        )
{
  UNUSED_VARIABLE(object);
  UNUSED_VARIABLE(userData);

  pluginData.projectProperties.filePath = geany_data->app->project->file_name;

  // load configuration
  projectConfigurationLoad(configuration);

  // update view
  updateToolbarButtons();
  updateToolbarMenuItems();
}

/***********************************************************************\
* Name   : onProjectClose
* Purpose: close project
* Input  : object        - object (not used)
*          configuration - project configuration to load values from
*          userData      - user data (not used)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onProjectClose(GObject  *object,
                          GKeyFile *configuration,
                          gpointer userData
                         )
{
  UNUSED_VARIABLE(object);
  UNUSED_VARIABLE(configuration);
  UNUSED_VARIABLE(userData);

  pluginData.projectProperties.filePath = NULL;

  // update view
  updateToolbarButtons();
  updateToolbarMenuItems();
}

/***********************************************************************\
* Name   : onProjectSave
* Purpose: save project configuration
* Input  : object        - object (not used)
*          configuration - project configuration to save values to
*          userData      - user data (not used)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onProjectSave(GObject  *object,
                         GKeyFile *configuration,
                         gpointer userData
                        )
{
  UNUSED_VARIABLE(object);
  UNUSED_VARIABLE(userData);

  // save values
  projectConfigurationSave(configuration);

  // update view
  updateToolbarButtons();
  updateToolbarMenuItems();
}

/***********************************************************************\
* Name   : init
* Purpose: plugin init
* Input  : plugin   - Geany plugin
*          userData - user data (not used)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL gboolean init(GeanyPlugin *plugin, gpointer userData)
{
  UNUSED_VARIABLE(userData);

  if (!Execute_init())
  {
    return FALSE;
  }

  // init remote
  if (!Remote_init())
  {
    return FALSE;
  }

  // init variables
  pluginData.builtInRegExStore                      = gtk_list_store_new(MODEL_REGEX_COUNT,
                                                                         G_TYPE_STRING,  // language
                                                                         G_TYPE_STRING,  // group
                                                                         G_TYPE_INT,     // type
                                                                         G_TYPE_STRING   // regex
                                                                        );
  pluginData.configuration.filePath                 = g_strconcat(plugin->geany->app->configdir,
                                                                  G_DIR_SEPARATOR_S,
                                                                  "plugins", G_DIR_SEPARATOR_S,
                                                                  "builder", G_DIR_SEPARATOR_S,
                                                                  "builder.conf",
                                                                  NULL
                                                                 );
  pluginData.configuration.commandStore             = gtk_list_store_new(MODEL_COMMAND_COUNT,
                                                                         G_TYPE_STRING,  // title
                                                                         G_TYPE_STRING,  // command
                                                                         G_TYPE_STRING,  // directory
                                                                         G_TYPE_BOOLEAN, // button
                                                                         G_TYPE_BOOLEAN, // menu item
                                                                         G_TYPE_BOOLEAN, // custom text
                                                                         G_TYPE_BOOLEAN, // run in docker container
                                                                         G_TYPE_BOOLEAN, // run remote
                                                                         G_TYPE_BOOLEAN  // parse output
                                                                        );
  gtk_list_store_insert_with_values(pluginData.configuration.commandStore,
                                    NULL,
                                    -1,
                                    MODEL_COMMAND_TITLE,                   "Build",
                                    MODEL_COMMAND_COMMAND_LINE,            DEFAULT_BUILD_COMMAND,
                                    MODEL_COMMAND_WORKING_DIRECTORY,       "%p",
                                    MODEL_COMMAND_SHOW_BUTTON,             TRUE,
                                    MODEL_COMMAND_SHOW_MENU_ITEM,          TRUE,
                                    MODEL_COMMAND_INPUT_CUSTOM_TEXT,       FALSE,
                                    MODEL_COMMAND_RUN_IN_DOCKER_CONTAINER, FALSE,
                                    MODEL_COMMAND_RUN_REMOTE,              FALSE,
                                    MODEL_COMMAND_PARSE_OUTPUT,            TRUE,
                                    MODEL_END
                                   );
  gtk_list_store_insert_with_values(pluginData.configuration.commandStore,
                                    NULL,
                                    -1,
                                    MODEL_COMMAND_TITLE,                   "Clean",
                                    MODEL_COMMAND_COMMAND_LINE,            DEFAULT_CLEAN_COMMAND,
                                    MODEL_COMMAND_WORKING_DIRECTORY,       "%p",
                                    MODEL_COMMAND_SHOW_BUTTON,             TRUE,
                                    MODEL_COMMAND_SHOW_MENU_ITEM,          TRUE,
                                    MODEL_COMMAND_INPUT_CUSTOM_TEXT,       FALSE,
                                    MODEL_COMMAND_RUN_IN_DOCKER_CONTAINER, FALSE,
                                    MODEL_COMMAND_RUN_REMOTE,              FALSE,
                                    MODEL_COMMAND_PARSE_OUTPUT,            FALSE,
                                    MODEL_END
                                   );
  pluginData.configuration.regexStore               = gtk_list_store_new(MODEL_REGEX_COUNT,
                                                                         G_TYPE_STRING,  // language
                                                                         G_TYPE_STRING,  // group
                                                                         G_TYPE_INT,     // type
                                                                         G_TYPE_STRING   // regex
                                                                        );
  pluginData.configuration.errorIndicators          = TRUE;
  pluginData.configuration.errorIndicatorColor      = DEFAULT_ERROR_INDICATOR_COLOR;
  pluginData.configuration.warningIndicators        = FALSE;
  pluginData.configuration.warningIndicatorColor    = DEFAULT_WARNING_INDICATOR_COLOR;
  pluginData.configuration.addProjectRegExResults   = FALSE;
  pluginData.configuration.abortButton              = TRUE;
  pluginData.configuration.autoSaveAll              = FALSE;
  pluginData.configuration.autoShowFirstError       = FALSE;
  pluginData.configuration.autoShowFirstWarning     = FALSE;

  pluginData.projectProperties.filePath             = NULL;
  pluginData.projectProperties.commandStore         = gtk_list_store_new(MODEL_COMMAND_COUNT,
                                                                         G_TYPE_STRING,  // title
                                                                         G_TYPE_STRING,  // command
                                                                         G_TYPE_STRING,  // directory
                                                                         G_TYPE_BOOLEAN, // button
                                                                         G_TYPE_BOOLEAN, // menu item
                                                                         G_TYPE_BOOLEAN, // custom text
                                                                         G_TYPE_BOOLEAN, // run in docker container
                                                                         G_TYPE_BOOLEAN, // run remote
                                                                         G_TYPE_BOOLEAN  // parse output
                                                                        );
  pluginData.projectProperties.errorRegEx           = g_string_new(NULL);
  pluginData.projectProperties.warningRegEx         = g_string_new(NULL);

  pluginData.projectProperties.remote.hostName      = g_string_new(NULL);
  pluginData.projectProperties.remote.hostPort      = 22;
  pluginData.projectProperties.remote.userName      = g_string_new(NULL);
  pluginData.projectProperties.remote.publicKey     = g_string_new(NULL);
  pluginData.projectProperties.remote.privateKey    = g_string_new(NULL);
  pluginData.projectProperties.remote.password      = g_string_new(NULL);

  for (guint i = 0; i < MAX_COMMANDS; i++)
  {
    pluginData.widgets.menuItems.commands[i] = NULL;
    pluginData.widgets.buttons.commands[i]   = NULL;
  }
  pluginData.widgets.buttons.abort                  = NULL;
  pluginData.widgets.disableCounter                 = 0;
  pluginData.widgets.projectProperties              = NULL;
  pluginData.widgets.showProjectPropertiesTab       = FALSE;

  pluginData.attachedDockerContainerId              = NULL;

  pluginData.build.lastCommandListStore             = NULL;
  pluginData.build.lastCommandIteratorString        = g_string_new(NULL);
  pluginData.build.directoryPrefixStack             = string_stack_new();
  pluginData.build.messagesStore = gtk_list_store_new(MODEL_MESSAGE_COUNT,
                                                      G_TYPE_STRING,  // color
                                                      G_TYPE_STRING,  // tree path
                                                      G_TYPE_STRING,  // directory
                                                      G_TYPE_STRING,  // file path
                                                      G_TYPE_INT,     // line
                                                      G_TYPE_INT,     // column
                                                      G_TYPE_STRING   // message
                                                     );
  pluginData.build.messagesTreePath                 = NULL;
  pluginData.build.errorsStore = gtk_tree_store_new(MODEL_ERROR_WARNING_COUNT,
                                                    G_TYPE_STRING,  // tree path
                                                    G_TYPE_STRING,  // directory
                                                    G_TYPE_STRING,  // file path
                                                    G_TYPE_INT,     // line
                                                    G_TYPE_INT,     // column
                                                    G_TYPE_STRING   // message
                                                   );
  pluginData.build.warningsStore = gtk_tree_store_new(MODEL_ERROR_WARNING_COUNT,
                                                      G_TYPE_STRING, // tree path
                                                      G_TYPE_STRING, // directory
                                                      G_TYPE_STRING, // file path
                                                      G_TYPE_INT,    // line
                                                      G_TYPE_INT,    // column
                                                      G_TYPE_STRING  // message
                                                     );
  pluginData.build.errorsTreeIterorValid                = FALSE;
  pluginData.build.warningsTreeIterorValid              = FALSE;
  pluginData.build.lastErrorsWarningsInsertStore        = NULL;
  pluginData.build.lastErrorsWarningsInsertTreeIterator = NULL;
  pluginData.build.errorWarningIndicatorsCount          = 0;

  for (guint i = 0; i < ARRAY_SIZE(REGEX_BUILTIN); i++)
  {
    gtk_list_store_insert_with_values(pluginData.builtInRegExStore,
                                      NULL,
                                      -1,
                                      MODEL_REGEX_GROUP, REGEX_BUILTIN[i].group,
                                      MODEL_REGEX_TYPE,  REGEX_BUILTIN[i].type,
                                      MODEL_REGEX_REGEX, REGEX_BUILTIN[i].regex,
                                      MODEL_END
                                     );
  }

  // load configuration
  configurationLoad();

  // init key binding
  initKeyBinding(plugin,NULL);

  // init GUI elements
  initToolbar(plugin);
  initTab(plugin);

//fprintf(stderr,"%s:%d: ----------------------------------------------\n",__FILE__,__LINE__);
//testSSH2();

  return TRUE;
}

/***********************************************************************\
* Name   : cleanup
* Purpose: plugin cleanup
* Input  : plugin   - Geany plugin
*          userData - user data (not used)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void cleanup(GeanyPlugin *plugin, gpointer userData)
{
  UNUSED_VARIABLE(userData);

  // done GUI elements
  doneTab(plugin);
  doneToolbar(plugin);

  // free resources
  string_stack_free(pluginData.build.directoryPrefixStack);
  g_string_free(pluginData.build.lastCommandIteratorString, TRUE);
  gtk_tree_path_free(pluginData.build.messagesTreePath);
  g_free((gchar*)pluginData.attachedDockerContainerId);
  g_string_free(pluginData.projectProperties.remote.password, TRUE);
  g_string_free(pluginData.projectProperties.remote.privateKey, TRUE);
  g_string_free(pluginData.projectProperties.remote.publicKey, TRUE);
  g_string_free(pluginData.projectProperties.remote.userName, TRUE);
  g_string_free(pluginData.projectProperties.remote.hostName, TRUE);
  g_string_free(pluginData.projectProperties.warningRegEx, TRUE);
  g_string_free(pluginData.projectProperties.errorRegEx, TRUE);
  g_free(pluginData.configuration.filePath);

  // done remote
  Remote_done();
}

/***********************************************************************\
* Name   : geany_load_module
* Purpose: load Geany module
* Input  : plugin - Geany plugin
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

G_MODULE_EXPORT void geany_load_module(GeanyPlugin *plugin)
{
  static PluginCallback pluginCallbacks[] =
  {
    {"project-dialog-open",      (GCallback)&onProjectDialogOpen, TRUE, NULL},
    {"project-dialog-confirmed", (GCallback)&onProjectDialogConfirmed, TRUE, NULL},
    {"project-dialog-close",     (GCallback)&onProjectDialogClose, TRUE, NULL},
    {"project-open",             (GCallback)&onProjectOpen, TRUE, NULL},
    {"project-close",            (GCallback)&onProjectClose, TRUE, NULL},
    {"project-save",             (GCallback)&onProjectSave, TRUE, NULL},
    {NULL, NULL, FALSE, NULL}
  };

  g_assert(plugin != NULL);
  g_assert(plugin->info != NULL);
  g_assert(plugin->funcs != NULL);

  plugin->info->name        = "Builder";
  plugin->info->description = "Builder tool";
  plugin->info->version     = VERSION;
  plugin->info->author      = "Torsten Rupp <torsten.rupp@gmx.net>";
  plugin->funcs->init       = init;
  plugin->funcs->cleanup    = cleanup;
  plugin->funcs->configure  = configure;
  plugin->funcs->callbacks  = pluginCallbacks;

  GEANY_PLUGIN_REGISTER(plugin, 225);
  /* alternatively:
  GEANY_PLUGIN_REGISTER_FULL(plugin, 225, data, free_func); */

  // initialize global Geany data references
  geany_plugin = plugin;
  geany_data   = plugin->geany_data;
  g_assert(geany_data != NULL);
  g_assert(geany_data->app != NULL);
}

/* end of file */
