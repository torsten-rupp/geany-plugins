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
#include "builder.h"

#define LOCAL static

/****************** Conditional compilation switches *******************/

/***************************** Constants *******************************/
const gchar *CONFIGURATION_GROUP_BUILDER        = "builder";

const gchar *KEY_GROUP_BUILDER                  = "builder";

const gchar *COLOR_BUILD_INFO                   = "Blue";
const gchar *COLOR_BUILD_ERROR                  = "Red";
const gchar *COLOR_BUILD_MESSAGES               = "Black";
const gchar *COLOR_BUILD_MESSAGES_MATCHED       = "Green";

#define MAX_COMMANDS                            5
const gchar *DEFAULT_BUILD_COMMAND              = "make";
const gchar *DEFAULT_CLEAN_COMMAND              = "make clean";
#if 0
const gchar *DEFAULT_REGEX_ENTER                = "^\\s*Entering directory\\s+'(?<directory>[^']*)'\\s*$";
const gchar *DEFAULT_REGEX_LEAVE                = "^\\s*Leaving directory\\s+'(?<directory>[^']*)'\\s*$";
const gchar *DEFAULT_REGEX_ERROR                = "^(?<filePath>[^:]*):(?<lineNumber>[^:]*):(?<columnNumber>[^:]*):\\s*error:\\s*(?<message>.*)$";
const gchar *DEFAULT_REGEX_WARNING              = "^(?<filePath>[^:]*):(?<lineNumber>[^:]*):(?<columnNumber>[^:]*):\\s*warning:\\s*(?<message>.*)$";
#endif

const GdkRGBA DEFAULT_ERROR_INDICATOR_COLOR     = {0xFF,0x00,0x00,0xFF};
const GdkRGBA DEFAULT_WARNING_INDICATOR_COLOR   = {0x00,0xFF,0x00,0xFF};

const guint ERROR_INDICATOR_INDEX               = GEANY_INDICATOR_ERROR;
const guint WARNING_INDICATOR_INDEX             = 1;

const guint MAX_ERROR_WARNING_INDICATORS        = 16;

/***************************** Datatypes *******************************/

// command list columns
enum
{
  MODEL_COMMAND_TITLE,
  MODEL_COMMAND_COMMAND_LINE,
  MODEL_COMMAND_WORKING_DIRECTORY,
  MODEL_COMMAND_SHOW_BUTTON,
  MODEL_COMMAND_SHOW_MENU_ITEM,
  MODEL_COMMAND_INPUT_CUSTOM_TEXT,
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
  MODEL_MESSAGE_DIRECTORY,
  MODEL_MESSAGE_TREE_PATH,
  MODEL_MESSAGE_FILE_PATH,
  MODEL_MESSAGE_LINE_NUMBER,
  MODEL_MESSAGE_COLUMN_NUMBER,
  MODEL_MESSAGE_MESSAGE,

  MODEL_MESSAGE_COUNT
};

// build error/warning columns
enum
{
  MODEL_ERROR_WARNING_COLOR,
  MODEL_ERROR_WARNING_DIRECTORY,
  MODEL_ERROR_WARNING_FILE_PATH,
  MODEL_ERROR_WARNING_LINE_NUMBER,
  MODEL_ERROR_WARNING_COLUMN_NUMBER,
  MODEL_ERROR_WARNING_MESSAGE,

  MODEL_ERROR_WARNING_COUNT
};

// key short-cuts
typedef enum
{
  KEY_BINDING_COMMAND,
  KEY_BINDING_COMMAND1 = KEY_BINDING_COMMAND,
  KEY_BINDING_COMMAND2,
  KEY_BINDING_COMMAND3,
  KEY_BINDING_COMMAND4,
  KEY_BINDING_COMMAND5,
  KEY_BINDING_BUILD,
  KEY_BINDING_CLEAN,
  KEY_BINDING_MAKE_ALL,
  KEY_BINDING_MAKE_CUSTOM_TARGET,
  KEY_BINDING_MAKE_OBJECT,
  KEY_BINDING_PREV_ERROR,
  KEY_BINDING_NEXT_ERROR,
  KEY_BINDING_PREV_WARNING,
  KEY_BINDING_NEXT_WARNING,
  KEY_BINDING_RUN,
  KEY_BINDING_STOP,
  KEY_BINDING_PROJECT_PROPERTIES,
  KEY_BINDING_PLUGIN_CONFIGURATION,

  KEY_BINDING_COUNT
} KeyBindings;

typedef void(*OutputHandler)(GString *line, GIOCondition ioCondition, gpointer data);
typedef gboolean(*InputValidator)(const gchar *value, gpointer data);

typedef struct
{
  gchar    *title;
  gchar    *commandLine;
  gchar    *workingDirectory;
  gboolean showButton;
  gboolean showMenuItem;
  gboolean inputCustomText;
  gboolean parseOutput;
} Command;

typedef enum
{
  REGEX_TYPE_NONE,
  REGEX_TYPE_ENTER,
  REGEX_TYPE_LEAVE,
  REGEX_TYPE_ERROR,
  REGEX_TYPE_WARNING,
  REGEX_TYPE_EXTENSION
} RegExTypes;
RegExTypes REGEX_TYPE_MIN = REGEX_TYPE_NONE;
RegExTypes REGEX_TYPE_MAX = REGEX_TYPE_EXTENSION;

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

    Command      commands[MAX_COMMANDS];
    GtkListStore *regExStore;

    gboolean     errorIndicators;
    GdkRGBA      errorIndicatorColor;
    gboolean     warningIndicators;
    GdkRGBA      warningIndicatorColor;

    gboolean     addProjectRegExResults;
    gboolean     stopButton;
    gboolean     autoSaveAll;
    gboolean     autoShowFirstError;
    gboolean     autoShowFirstWarning;
  } configuration;

  // project specific properties
  struct
  {
    Command           commands[MAX_COMMANDS];
    GtkListStore      *commandStore;
    GString           *errorRegEx;
    GString           *warningRegEx;
  } projectProperties;

  // widgets
  struct
  {
    struct
    {
      GtkWidget *commands[MAX_COMMANDS];

      GtkWidget *showPrevError;
      GtkWidget *showNextError;
      GtkWidget *showPrevWarning;
      GtkWidget *showNextWarning;
      GtkWidget *stop;
      GtkWidget *projectProperties;
      GtkWidget *configuration;

      GtkWidget *editRegEx;
    } menuItems;

    GtkWidget     *tabs;
    gint          tabIndex;
    GPtrArray     *buttons;
    GtkWidget     *buttonMenu;
    GtkToolItem   *buttonStop;

    GtkBox        *projectProperties;
    gint          projectPropertiesTabIndex;
    gboolean      showProjectPropertiesTab;

    GtkWidget     *messagesTab;
    guint         messagesTabIndex;
    GtkWidget     *errorsTab;
    GtkWidget     *errorsTabLabel;
    guint         errorsTabIndex;
    GtkWidget     *errorsTree;
    GtkWidget     *warningsTab;
    GtkWidget     *warningsTabLabel;
    guint         warningsTabIndex;
    GtkWidget     *warningsTree;
  } widgets;

  // build results
  struct
  {
    GPid         pid;
    GString      *workingDirectory;
    StringStack  *directoryPrefixStack;

    gboolean     showedFirstErrorWarning;
    GtkWidget    *messagesList;
    GtkListStore *messagesStore;
    GtkTreePath  *messagesTreePath;
    GtkTreeStore *errorsStore;
    GtkTreeIter  errorsTreeIter;
    gboolean     errorsTreeIterValid;
    GtkTreeStore *warningsStore;
    GtkTreeIter  warningsTreeIter;
    gboolean     warningsTreeIterValid;
    GtkTreeIter  insertIter;
    GtkTreeStore *lastInsertStore;
    GtkTreeIter  *lastInsertTreeIter;
    guint        errorWarningIndicatorsCount;

    GString      *lastCustomTarget;
  } build;
} pluginData;

/****************************** Macros *********************************/
#define LOCAL_INLINE static inline
#define UNUSED_VARIABLE(name) (void)name

#define ARRAY_SIZE(array) (sizeof(array)/sizeof(array[0]))

/***************************** Forwards ********************************/

/***************************** Functions *******************************/

/***********************************************************************\
* Name   : initCommands
* Purpose: init commands
* Input  : commands     - commands array
*          commandCount - length of commands array
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void initCommands(Command commands[], gint commandCount)
{
  g_assert(commands != NULL);

  for (gint i = 0; i < commandCount; i++)
  {
    commands[i].title            = NULL;
    commands[i].commandLine      = NULL;
    commands[i].workingDirectory = NULL;
    commands[i].showButton       = FALSE;
    commands[i].showMenuItem     = FALSE;
    commands[i].parseOutput      = FALSE;
  }
}

/***********************************************************************\
* Name   : doneCommands
* Purpose: done commands
* Input  : commands     - commands array
*          commandCount - length of commands array
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void doneCommands(Command commands[], gint commandCount)
{
  g_assert(commands != NULL);

  for (gint i = 0; i < commandCount; i++)
  {
    g_free(commands[i].workingDirectory);
    g_free(commands[i].commandLine);
    g_free(commands[i].title);
  }
}

/***********************************************************************\
* Name   : setCommand
* Purpose: set command values
* Input  : command - command to set
*          title   - title
*          commandLine      - commandLine
*          workingDirectory - title
*          showButton       - show button
*          showMenuItem     - show in menu
*          inputCustomText  - input custom text
*          parseOutput      - parse output
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void setCommand(Command     *command,
                      const gchar *title,
                      const gchar *commandLine,
                      const gchar *workingDirectory,
                      gboolean    showButton,
                      gboolean    showMenuItem,
                      gboolean    inputCustomText,
                      gboolean    parseOutput
                     )
{
  g_assert(command != NULL);

  if (command->title != NULL) g_free(command->title);
  command->title = g_strdup(title);
  if (command->commandLine != NULL) g_free(command->commandLine);
  command->commandLine = g_strdup(commandLine);
  if (command->workingDirectory != NULL) g_free(command->workingDirectory);
  command->workingDirectory = g_strdup(workingDirectory);
  command->showButton      = showButton;
  command->showMenuItem    = showMenuItem;
  command->inputCustomText = inputCustomText;
  command->parseOutput     = parseOutput;
}

/***********************************************************************\
* Name   : getCommand
* Purpose: get command
* Input  : i - command index 0..MAX_COMMANDS-1
* Output : -
* Return : command or NULL
* Notes  : -
\***********************************************************************/

LOCAL const Command *getCommand(gint i)
{
  const Command *command = NULL;
  if ((command == NULL) && !isStringEmpty(pluginData.projectProperties.commands[i].title)) command = &pluginData.projectProperties.commands[i];
  if ((command == NULL) && !isStringEmpty(pluginData.configuration.commands[i].title)    ) command = &pluginData.configuration.commands[i];

  return command;
}

/***********************************************************************\
* Name   : configurationLoadBoolean
* Purpose: load configuration boolean
* Input  : boolean       - boolean variable
*          configuration - configuration to load values from
*          name          - value name
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void configurationLoadBoolean(gboolean    *boolean,
                                    GKeyFile    *configuration,
                                    const gchar *name
                                   )
{
  g_assert(boolean != NULL);
  g_assert(configuration != NULL);
  g_assert(name != NULL);

  (*boolean) = g_key_file_get_boolean(configuration, CONFIGURATION_GROUP_BUILDER, name, NULL);
}

/***********************************************************************\
* Name   : configurationStringUpdate
* Purpose: load configuration string
* Input  : string        - string variable
*          configuration - configuration to load values from
*          groupName     - group name
*          name          - value name
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void configurationLoadString(GString     *string,
                                   GKeyFile    *configuration,
                                   const gchar *name
                                  )
{
  g_assert(string != NULL);
  g_assert(configuration != NULL);
  g_assert(name != NULL);

  g_string_assign(string, g_key_file_get_string(configuration, CONFIGURATION_GROUP_BUILDER, name, NULL));
}

/***********************************************************************\
* Name   : configurationLoadCommand
* Purpose: load command from configuration
* Input  : command       - command variable
*          configuration - configuration to load values from
*          name          - value name
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void configurationLoadCommand(Command     *command,
                                    GKeyFile    *configuration,
                                    const gchar *name
                                   )
{
  g_assert(command != NULL);
  g_assert(configuration != NULL);
  g_assert(name != NULL);

  gchar *string = g_key_file_get_string(configuration, CONFIGURATION_GROUP_BUILDER, name, NULL);
  if (string != NULL)
  {
    gchar **tokens   = stringSplit(string, ":", '\\', 6);
    g_assert(tokens != NULL);
    guint tokenCount = g_strv_length(tokens);

    g_free(command->title);
    command->title            = (tokenCount >= 1) ? stringUnescape(tokens[0],'\\') : NULL;
    g_free(command->commandLine);
    command->commandLine      = (tokenCount >= 2) ? stringUnescape(tokens[1],'\\') : NULL;
    g_free(command->workingDirectory);
    command->workingDirectory = (tokenCount >= 3) ? stringUnescape(tokens[2],'\\') : NULL;
    command->showButton       = (tokenCount >= 4) ? stringEquals(tokens[3],"yes")  : FALSE;
    command->showMenuItem     = (tokenCount >= 5) ? stringEquals(tokens[4],"yes")  : FALSE;
    command->parseOutput      = (tokenCount >= 6) ? stringEquals(tokens[5],"yes")  : FALSE;

    g_strfreev(tokens);
    g_free(string);
  }
}

/***********************************************************************\
* Name   : configurationSaveCommand
* Purpose: save command command into configuration
* Input  : treeModel     - command
*          configuration - configuration
*          name          - name
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void configurationSaveCommand(const Command *command,
                                    GKeyFile      *configuration,
                                    const gchar   *name
                                   )
{
  g_assert(command != NULL);
  g_assert(configuration != NULL);
  g_assert(name != NULL);

  GString *string = g_string_new(NULL);

  gchar *titleEscaped            = stringEscape(command->title,":",'\\');
  gchar *commandLineEscaped      = stringEscape(command->commandLine,":",'\\');
  gchar *workingDirectoryEscaped = stringEscape(command->workingDirectory,":",'\\');
  g_string_printf(string,
                  "%s:%s:%s:%s:%s:%s",
                  titleEscaped,
                  commandLineEscaped,
                  workingDirectoryEscaped,
                  command->showButton ? "yes":"no",
                  command->showMenuItem ? "yes":"no",
                  command->parseOutput ? "yes":"no"
                 );
  g_free(workingDirectoryEscaped);
  g_free(commandLineEscaped);
  g_free(titleEscaped);

  g_key_file_set_string(configuration,
                        CONFIGURATION_GROUP_BUILDER,
                        name,
                        string->str
                       );

  g_string_free(string,TRUE);
}

/***********************************************************************\
* Name   : configurationLoadRegexList
* Purpose: load regex list from configuration
* Input  : listStore     - list store variable
*          configuration - configuration to load values from
*          name          - value name
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void configurationLoadRegexList(GtkListStore *listStore,
                                      GKeyFile     *configuration,
                                      const gchar  *name
                                     )
{
  g_assert(listStore != NULL);
  g_assert(configuration != NULL);
  g_assert(name != NULL);

  gchar **stringArray = g_key_file_get_string_list(configuration, CONFIGURATION_GROUP_BUILDER, name, NULL, NULL);
  if (stringArray != NULL)
  {
    gchar **string;

    gtk_list_store_clear(listStore);
    foreach_strv(string, stringArray)
    {
      gchar **tokens    = g_strsplit(*string, ":", 4);
      g_assert(tokens != NULL);
      guint tokenCount = g_strv_length(tokens);

      RegExTypes regExType = REGEX_TYPE_NONE;
      if (tokenCount >= 3)
      {
        if      (stringEquals(tokens[2], REGEX_TYPE_STRINGS[REGEX_TYPE_ENTER    ])) regExType = REGEX_TYPE_ENTER;
        else if (stringEquals(tokens[2], REGEX_TYPE_STRINGS[REGEX_TYPE_LEAVE    ])) regExType = REGEX_TYPE_LEAVE;
        else if (stringEquals(tokens[2], REGEX_TYPE_STRINGS[REGEX_TYPE_ERROR    ])) regExType = REGEX_TYPE_ERROR;
        else if (stringEquals(tokens[2], REGEX_TYPE_STRINGS[REGEX_TYPE_WARNING  ])) regExType = REGEX_TYPE_WARNING;
        else if (stringEquals(tokens[2], REGEX_TYPE_STRINGS[REGEX_TYPE_EXTENSION])) regExType = REGEX_TYPE_EXTENSION;
      }

      gtk_list_store_insert_with_values(listStore,
                                        NULL,
                                        -1,
                                        MODEL_REGEX_LANGUAGE, (tokenCount >= 1) ? tokens[0] : "",
                                        MODEL_REGEX_GROUP,    (tokenCount >= 2) ? tokens[1] : "",
                                        MODEL_REGEX_TYPE,     regExType,
                                        MODEL_REGEX_REGEX,    (tokenCount >= 4) ? tokens[3] : "",
                                        -1
                                       );
      g_strfreev(tokens);
    }
    g_strfreev(stringArray);
  }
}

/***********************************************************************\
* Name   : configurationSaveRegexList
* Purpose: save regex list into configuration
* Input  : treeModel     - list store model
*          configuration - configuration
*          name          - name
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void configurationSaveRegexList(GtkTreeModel *treeModel,
                                      GKeyFile     *configuration,
                                      const gchar  *name
                                     )
{
  GPtrArray *regexArray = g_ptr_array_new_with_free_func(g_free);

  // get error/warning regular expressions as arrays
  GtkTreeIter treeIter;
  if (gtk_tree_model_get_iter_first(treeModel, &treeIter))
  {
    gchar      *language;
    gchar      *group;
    RegExTypes regExType;
    gchar      *regex;
    GString    *string = g_string_new(NULL);
    do
    {
      gtk_tree_model_get(treeModel,
                         &treeIter,
                         MODEL_REGEX_LANGUAGE, &language,
                         MODEL_REGEX_GROUP,    &group,
                         MODEL_REGEX_TYPE,     &regExType,
                         MODEL_REGEX_REGEX,    &regex,
                         -1
                        );
      g_assert(language != NULL);
      g_assert(group != NULL);
      g_assert(regExType >= REGEX_TYPE_MIN);
      g_assert(regExType <= REGEX_TYPE_MAX);
      g_assert(regex != NULL);

      g_string_printf(string,"%s:%s:%s:%s", language, group, REGEX_TYPE_STRINGS[regExType], regex);
      g_ptr_array_add(regexArray, g_strdup(string->str));

      g_free(regex);
      g_free(group);
      g_free(language);
    }
    while (gtk_tree_model_iter_next(treeModel, &treeIter));
    g_string_free(string, TRUE);
  }

  g_key_file_set_string_list(configuration,
                             CONFIGURATION_GROUP_BUILDER,
                             name,
                             (const gchar**)regexArray->pdata,
                             regexArray->len
                            );

  // free resources
  g_ptr_array_free(regexArray, TRUE);
}

/***********************************************************************\
* Name   : configurationLoadColor
* Purpose: load configuration color
* Input  : rgba          - color RGBA variable
*          configuration - configuration to load values from
*          name          - value name
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void configurationLoadColor(GdkRGBA     *rgba,
                                  GKeyFile    *configuration,
                                  const gchar *name
                                 )
{
  g_assert(rgba != NULL);
  g_assert(configuration != NULL);
  g_assert(name != NULL);

  GdkRGBA color;
  if (gdk_rgba_parse(&color, g_key_file_get_string(configuration, CONFIGURATION_GROUP_BUILDER, name, NULL)))
  {
    (*rgba) = color;
  }
}

/***********************************************************************\
* Name   : configurationSaveColor
* Purpose: save color into configuration
* Input  : color         - color to save
*          configuration - configuration
*          name          - name
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void configurationSaveColor(const GdkRGBA *color,
                                  GKeyFile      *configuration,
                                  const gchar   *name
                                 )
{
  gchar *colorString = gdk_rgba_to_string(color);

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

  // get values
  for (guint i = 0; i < MAX_COMMANDS; i++)
  {
    gchar name[32];

    g_snprintf(name,sizeof(name),"command%d",i);
    configurationLoadCommand(&pluginData.configuration.commands[i], configuration, name);
  }
  configurationLoadRegexList(pluginData.configuration.regExStore,              configuration, "regExs");

  configurationLoadBoolean  (&pluginData.configuration.errorIndicators,        configuration, "errorIndicators");
  configurationLoadColor    (&pluginData.configuration.errorIndicatorColor,    configuration, "errorIndicatorColor");
  configurationLoadBoolean  (&pluginData.configuration.warningIndicators,      configuration, "warningIndicators");
  configurationLoadColor    (&pluginData.configuration.warningIndicatorColor,  configuration, "warningIndicatorColor");

  configurationLoadBoolean  (&pluginData.configuration.addProjectRegExResults, configuration, "addProjectRegExResults");
  configurationLoadBoolean  (&pluginData.configuration.stopButton,             configuration, "stopButton");
  configurationLoadBoolean  (&pluginData.configuration.autoSaveAll,            configuration, "autoSaveAll");
  configurationLoadBoolean  (&pluginData.configuration.autoShowFirstError,     configuration, "autoShowFirstError");
  configurationLoadBoolean  (&pluginData.configuration.autoShowFirstWarning,   configuration, "autoShowFirstWarning");

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

  for (guint i = 0; i < MAX_COMMANDS; i++)
  {
    gchar name[32];

    g_snprintf(name,sizeof(name),"command%d",i);
    configurationSaveCommand(&pluginData.configuration.commands[i], configuration, name);
  }
  configurationSaveRegexList(GTK_TREE_MODEL(pluginData.configuration.regExStore), configuration, "regExs");

  g_key_file_set_boolean(configuration,     CONFIGURATION_GROUP_BUILDER, "errorIndicators",pluginData.configuration.errorIndicators);
  configurationSaveColor(&pluginData.configuration.errorIndicatorColor,   configuration, "errorIndicatorColor");
  g_key_file_set_boolean(configuration,     CONFIGURATION_GROUP_BUILDER, "warningIndicators",pluginData.configuration.warningIndicators);
  configurationSaveColor(&pluginData.configuration.warningIndicatorColor, configuration, "warningIndicatorColor");

  g_key_file_set_boolean(configuration,     CONFIGURATION_GROUP_BUILDER, "addProjectRegExResults",pluginData.configuration.addProjectRegExResults);
  g_key_file_set_boolean(configuration,     CONFIGURATION_GROUP_BUILDER, "stopButton",            pluginData.configuration.stopButton);
  g_key_file_set_boolean(configuration,     CONFIGURATION_GROUP_BUILDER, "autoSaveAll",           pluginData.configuration.autoSaveAll);
  g_key_file_set_boolean(configuration,     CONFIGURATION_GROUP_BUILDER, "autoShowFirstError",    pluginData.configuration.autoShowFirstError);
  g_key_file_set_boolean(configuration,     CONFIGURATION_GROUP_BUILDER, "autoShowFirstWarning",  pluginData.configuration.autoShowFirstWarning);

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

  // write configuration data to file
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

  // load configuration values
  for (guint i = 0; i < MAX_COMMANDS; i++)
  {
    gchar name[32];

    g_snprintf(name,sizeof(name),"command%d",i);
    configurationLoadCommand(&pluginData.projectProperties.commands[i], configuration, name);
  }

  configurationLoadString(pluginData.projectProperties.errorRegEx,   configuration, "errorRegEx");
  configurationLoadString(pluginData.projectProperties.warningRegEx, configuration, "warningRegEx");
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

  // save configuration values
  for (guint i = 0; i < MAX_COMMANDS; i++)
  {
    gchar name[32];

    g_snprintf(name,sizeof(name),"command%d",i);
    configurationSaveCommand(&pluginData.projectProperties.commands[i], configuration, name);
  }

  g_key_file_set_string(configuration, CONFIGURATION_GROUP_BUILDER, "errorRegEx",   pluginData.projectProperties.errorRegEx->str);
  g_key_file_set_string(configuration, CONFIGURATION_GROUP_BUILDER, "warningRegEx", pluginData.projectProperties.warningRegEx->str);
}

// ---------------------------------------------------------------------

/***********************************************************************\
* Name   : setEnableStop
* Purpose: enable/disable toolbar stop button
* Input  : enabled - TRUE to enable
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void setEnableStop(gboolean enabled)
{
  gtk_widget_set_sensitive(GTK_WIDGET(pluginData.widgets.buttonStop), enabled);
  gtk_widget_set_sensitive(GTK_WIDGET(pluginData.widgets.menuItems.stop), enabled);
}

/***********************************************************************\
* Name   : setEnableToolbarButton
* Purpose: enable/disable toolbar button
* Input  : data     - button
*          userData - enable/disable
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void setEnableToolbarButton(gpointer data, gpointer userData)
{
  GtkWidget *button = (GtkWidget*)data;
  gboolean  enabled = (gboolean)GPOINTER_TO_INT(userData);

  gtk_widget_set_sensitive(button,enabled);
}

/***********************************************************************\
* Name   : setEnableToolbar
* Purpose: enable/disable toolbar buttons/stop menu item
* Input  : enabled - TRUE to enable, FALSE to disable
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void setEnableToolbar(gboolean enabled)
{
  g_ptr_array_foreach(pluginData.widgets.buttons,
                      setEnableToolbarButton,
                      GINT_TO_POINTER(enabled)
                     );
  setEnableStop(!enabled);
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
  pluginData.build.errorsTreeIterValid         = FALSE;
  pluginData.build.warningsTreeIterValid       = FALSE;
  pluginData.build.lastInsertStore             = NULL;
  pluginData.build.lastInsertTreeIter          = NULL;
  pluginData.build.errorWarningIndicatorsCount = 0;

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
  gtk_notebook_set_current_page(GTK_NOTEBOOK(ui_lookup_widget(geany_data->main_widgets->window, "notebook_info")),
                                pluginData.widgets.tabIndex
                               );
  gtk_notebook_set_current_page(GTK_NOTEBOOK(pluginData.widgets.tabs),
                                pluginData.widgets.warningsTabIndex
                               );
}

/***********************************************************************\
* Name   : dialogRegexTypeCellRenderer
* Purpose: render regex type column
* Input  : cellLayout   - cell layout
*          cellRenderer - cell renderer
*          treeModel    - tree data model
*          treeIter     - tree entry iterator
*          data         - user data (not used)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void  dialogRegexTypeCellRenderer(GtkCellLayout   *cellLayout,
                                        GtkCellRenderer *cellRenderer,
                                        GtkTreeModel    *treeModel,
                                        GtkTreeIter     *treeIter,
                                        gpointer         data
                                       )
{
  g_assert(cellLayout != NULL);
  g_assert(cellRenderer != NULL);
  g_assert(treeModel != NULL);
  g_assert(treeIter != NULL);

  UNUSED_VARIABLE(data);

  RegExTypes type;
  gtk_tree_model_get(treeModel, treeIter, MODEL_REGEX_TYPE, &type, -1);
  g_assert(type >= REGEX_TYPE_MIN);
  g_assert(type <= REGEX_TYPE_MAX);
  g_object_set(cellRenderer, "text", REGEX_TYPE_STRINGS[type], NULL);
}

/***********************************************************************\
* Name   : dialogRegexUpdateMatch
* Purpose: update regular expression dialog match
* Input  : widgetLanguage    - language widget
*          widgetGroup       - group widget
*          widgetRegex       - regular expression entry widget
*          widgetSample      - sample entry widget
*          widgetFilePath     - view file path entry widget
*          widgetLineNumber   - view line number entry widget
*          widgetColumnNumber - view column number entry widget
*          widgetMessage      - view message entry widget
*          widgetOK         - OK button widget
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

  const gchar *regex = gtk_entry_get_text(GTK_ENTRY(widgetRegex));
  if (!EMPTY(regex))
  {
    // validate regular expression, enable/disable ok-button
    GRegex      *regExCompiled;
    GMatchInfo  *matchInfo;
    regExCompiled = g_regex_new(gtk_entry_get_text(GTK_ENTRY(widgetRegex)),
                                0, // compile_optipns
                                0, // match option
                                NULL // error
                               );
    if (regExCompiled != NULL)
    {
      gtk_widget_set_sensitive(widgetOK, TRUE);

      if (g_regex_match(regExCompiled, gtk_entry_get_text(GTK_ENTRY(widgetSample)), 0, &matchInfo))
      {
        gint filePathMatchNumber         = g_regex_get_string_number(regExCompiled, "filePath");
        gint fileLineNumberMatchNumber   = g_regex_get_string_number(regExCompiled, "lineNumber");
        gint fileColumnNumberMatchNumber = g_regex_get_string_number(regExCompiled, "columnNumber");
        gint fileMessageMatchNumber      = g_regex_get_string_number(regExCompiled, "message");

        if (filePathMatchNumber >= 0)
        {
          gtk_entry_set_text(GTK_ENTRY(widgetFilePath), g_match_info_fetch(matchInfo, filePathMatchNumber));
        }
        else
        {
          gtk_entry_set_text(GTK_ENTRY(widgetFilePath), "");
        }
        if (fileLineNumberMatchNumber >= 0)
        {
          gtk_entry_set_text(GTK_ENTRY(widgetLineNumber), g_match_info_fetch(matchInfo, fileLineNumberMatchNumber));
        }
        else
        {
          gtk_entry_set_text(GTK_ENTRY(widgetLineNumber), "");
        }
        if (fileColumnNumberMatchNumber >= 0)
        {
          gtk_entry_set_text(GTK_ENTRY(widgetColumnNumber), g_match_info_fetch(matchInfo, fileColumnNumberMatchNumber));
        }
        else
        {
          gtk_entry_set_text(GTK_ENTRY(widgetColumnNumber), "");
        }
        if (fileMessageMatchNumber >= 0)
        {
          gtk_entry_set_text(GTK_ENTRY(widgetMessage), g_match_info_fetch(matchInfo, fileMessageMatchNumber));
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
      g_regex_unref(regExCompiled);
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
* Input  : entry - entry
*          data  - ok-button widget
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onInputRegexDialogChanged(GtkWidget *widget,
                                     gpointer  data
                                    )
{
  GtkWidget *dialog = GTK_WIDGET(data);

  g_assert(dialog != NULL);

  UNUSED_VARIABLE(widget);

  GtkWidget *widgetLanguage = g_object_get_data(G_OBJECT(dialog), "combo_language");
  g_assert(widgetLanguage != NULL);
  GtkWidget *widgetGroup = g_object_get_data(G_OBJECT(dialog), "combo_group");
  g_assert(widgetGroup != NULL);
  GtkWidget *widgetRegex = g_object_get_data(G_OBJECT(dialog), "entry_regex");
  g_assert(widgetRegex != NULL);
  GtkWidget *widgetSample = g_object_get_data(G_OBJECT(dialog), "entry_sample");
  g_assert(widgetSample != NULL);
  GtkWidget *filePath = g_object_get_data(G_OBJECT(dialog), "view_file_path");
  g_assert(filePath != NULL);
  GtkWidget *lineNumber = g_object_get_data(G_OBJECT(dialog), "view_line_number");
  g_assert(lineNumber != NULL);
  GtkWidget *columnNumber = g_object_get_data(G_OBJECT(dialog), "view_column_number");
  g_assert(columnNumber != NULL);
  GtkWidget *message = g_object_get_data(G_OBJECT(dialog), "view_message");
  g_assert(message != NULL);
  GtkWidget *widgetOK = g_object_get_data(G_OBJECT(dialog), "button_ok");
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
* Input  : value - new value to insert
*          data  - user data (not used)
* Output : -
* Return : TRUE iff input valid
* Notes  : -
\***********************************************************************/

LOCAL gboolean validateGroup(const gchar *value, gpointer data)
{
  g_assert(value != NULL);

  UNUSED_VARIABLE(data);

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
*          data     - user data
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onInputRegexDialogComboGroupInsertText(GtkEntry    *entry,
                                                  const gchar *text,
                                                  gint        length,
                                                  gint        *position,
                                                  gpointer    data
)
{
  GtkEditable    *editable     = GTK_EDITABLE(entry);
  InputValidator validator     = (InputValidator)g_object_get_data(G_OBJECT(entry), "validator_function");
  gpointer       validatorData = g_object_get_data(G_OBJECT(entry), "validator_data");

  if (validator(text, validatorData))
  {
    g_signal_handlers_block_by_func(G_OBJECT(editable), G_CALLBACK(onInputRegexDialogComboGroupInsertText), data);
    gtk_editable_insert_text(editable, text, length, position);
    g_signal_handlers_unblock_by_func(G_OBJECT(editable), G_CALLBACK(onInputRegexDialogComboGroupInsertText), data);
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

  GtkWidget *widgetLanguage = g_object_get_data(G_OBJECT(dialog), "combo_language");
  g_assert(widgetLanguage != NULL);
  GtkWidget *widgetGroup = g_object_get_data(G_OBJECT(dialog), "combo_group");
  g_assert(widgetGroup != NULL);
  GtkWidget *radioEnter = g_object_get_data(G_OBJECT(dialog), "radio_enter");
  g_assert(radioEnter != NULL);
  GtkWidget *radioLeave = g_object_get_data(G_OBJECT(dialog), "radio_leave");
  g_assert(radioLeave != NULL);
  GtkWidget *radioError = g_object_get_data(G_OBJECT(dialog), "radio_error");
  g_assert(radioError != NULL);
  GtkWidget *radioWarning = g_object_get_data(G_OBJECT(dialog), "radio_warning");
  g_assert(radioWarning != NULL);
  GtkWidget *radioExtension = g_object_get_data(G_OBJECT(dialog), "radio_extension");
  g_assert(radioExtension != NULL);
  GtkWidget *widgetRegex = g_object_get_data(G_OBJECT(dialog), "entry_regex");
  g_assert(widgetRegex != NULL);

  GtkWidget *widgetSample = g_object_get_data(G_OBJECT(dialog), "entry_sample");
  g_assert(widgetSample != NULL);
  GtkWidget *filePath = g_object_get_data(G_OBJECT(dialog), "view_file_path");
  g_assert(filePath != NULL);
  GtkWidget *lineNumber = g_object_get_data(G_OBJECT(dialog), "view_line_number");
  g_assert(lineNumber != NULL);
  GtkWidget *columnNumber = g_object_get_data(G_OBJECT(dialog), "view_column_number");
  g_assert(columnNumber != NULL);
  GtkWidget *message = g_object_get_data(G_OBJECT(dialog), "view_message");
  g_assert(message != NULL);
  GtkWidget *widgetOK = g_object_get_data(G_OBJECT(dialog), "button_ok");
  g_assert(widgetOK != NULL);

  GtkTreeIter treeIter;
  if (gtk_combo_box_get_active_iter(GTK_COMBO_BOX(widget), &treeIter))
  {
    GtkTreeModel *treeModel = gtk_combo_box_get_model(GTK_COMBO_BOX(widget));
    g_assert(treeModel != NULL);

    gchar      *language;
    gchar      *group;
    RegExTypes type;
    gchar      *regex;
    gtk_tree_model_get(treeModel,
                       &treeIter,
                       MODEL_REGEX_LANGUAGE, &language,
                       MODEL_REGEX_GROUP,    &group,
                       MODEL_REGEX_TYPE,     &type,
                       MODEL_REGEX_REGEX,    &regex,
                       -1
                      );
    gint         i = 1;
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
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radioEnter),     type == REGEX_TYPE_ENTER    );
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radioLeave),     type == REGEX_TYPE_LEAVE    );
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radioError),     type == REGEX_TYPE_ERROR    );
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radioWarning),   type == REGEX_TYPE_WARNING  );
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radioExtension), type == REGEX_TYPE_EXTENSION);
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
                                        -1
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

  GtkWidget *widgetLanguage = g_object_get_data(G_OBJECT(dialog), "combo_language");
  g_assert(widgetLanguage != NULL);
  GtkWidget *widgetGroup = g_object_get_data(G_OBJECT(dialog), "combo_group");
  g_assert(widgetGroup != NULL);
  GtkWidget *radioEnter = g_object_get_data(G_OBJECT(dialog), "radio_enter");
  g_assert(radioEnter != NULL);
  GtkWidget *radioLeave = g_object_get_data(G_OBJECT(dialog), "radio_leave");
  g_assert(radioLeave != NULL);
  GtkWidget *radioError = g_object_get_data(G_OBJECT(dialog), "radio_error");
  g_assert(radioError != NULL);
  GtkWidget *radioWarning = g_object_get_data(G_OBJECT(dialog), "radio_warning");
  g_assert(radioWarning != NULL);
  GtkWidget *radioExtension = g_object_get_data(G_OBJECT(dialog), "radio_extension");
  g_assert(radioExtension != NULL);
  GtkWidget *widgetRegex = g_object_get_data(G_OBJECT(dialog), "entry_regex");
  g_assert(widgetRegex != NULL);

  GtkWidget *widgetSample = g_object_get_data(G_OBJECT(dialog), "entry_sample");
  g_assert(widgetSample != NULL);
  GtkWidget *filePath = g_object_get_data(G_OBJECT(dialog), "view_file_path");
  g_assert(filePath != NULL);
  GtkWidget *lineNumber = g_object_get_data(G_OBJECT(dialog), "view_line_number");
  g_assert(lineNumber != NULL);
  GtkWidget *columnNumber = g_object_get_data(G_OBJECT(dialog), "view_column_number");
  g_assert(columnNumber != NULL);
  GtkWidget *message = g_object_get_data(G_OBJECT(dialog), "view_message");
  g_assert(message != NULL);
  GtkWidget *widgetOK = g_object_get_data(G_OBJECT(dialog), "button_ok");
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
*          regExType      - regex type
*          regExString    - regular expression string
*          sample         - error/warning sample string
* Return : TRUE on "ok", FALSE otherwise
* Notes  : -
\***********************************************************************/

LOCAL gboolean dialogRegex(GtkWindow   *parentWindow,
                           const gchar *title,
                           const char  *text,
                           GString     *languageString,
                           GString     *groupString,
                           RegExTypes  *regExType,
                           GString     *regExString,
                           const gchar *sample
                          )
{
  GtkWidget *widgetLanguage, *widgetGroup;
  GtkWidget *buttonRegExTypeEnter, *buttonRegExTypeLeave, *buttonRegExTypeError, *buttonRegExTypeWarning, *buttonRegExTypeExtension;
  GtkWidget *widgetRegex, *widgetSample;
  GtkWidget *widgetFilePath, *widgetLineNumber, *widgetColumnNumber, *widgetMessage;
  GtkWidget *widgetOK;
  gint      result;

  g_assert(parentWindow != NULL);
  g_assert(title != NULL);
  g_assert(text != NULL);
  g_assert(groupString != NULL);
  g_assert(regExType != NULL);
  g_assert(regExString != NULL);

  // create dialog
  GtkWidget *dialog = gtk_dialog_new_with_buttons(title,
                                                  parentWindow,
                                                  GTK_DIALOG_DESTROY_WITH_PARENT,
                                                  _("_Cancel"),
                                                  GTK_RESPONSE_CANCEL,
                                                  NULL
                                                 );

  widgetOK = gtk_dialog_add_button(GTK_DIALOG(dialog), _("_OK"), GTK_RESPONSE_ACCEPT);
  g_object_set_data(G_OBJECT(dialog), "button_ok", widgetOK);

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
      addGrid(grid, 0, 0, 1, newLabel(G_OBJECT(dialog), NULL, "Language", NULL));
      hbox = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL,12));
      {
        // language combo oox
        widgetLanguage = addBox(hbox, TRUE, newCombo(G_OBJECT(dialog), "combo_language", "Language"));
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

        // group combo oox
        widgetGroup = addBox(hbox, TRUE, newComboEntry(G_OBJECT(dialog), "combo_group", "Group"));
        gtk_combo_box_set_model(GTK_COMBO_BOX(widgetGroup), GTK_TREE_MODEL(pluginData.builtInRegExStore));
        gtk_combo_box_set_entry_text_column(GTK_COMBO_BOX(widgetGroup), MODEL_REGEX_LANGUAGE);
//“appears-as-list”
//id-column
#if 1
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
        g_object_set_data(G_OBJECT(entryGroup), "validator_function", validateGroup);
        g_object_set_data(G_OBJECT(entryGroup), "validator_data", NULL);
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

      addGrid(grid, 1, 0, 1, newLabel(G_OBJECT(dialog), NULL, "Type", NULL));
      hbox = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL,12));
      {
        buttonRegExTypeEnter     = addBox(hbox, FALSE, newRadioButton(G_OBJECT(dialog), NULL,                   "radio_enter",     REGEX_TYPE_STRINGS[REGEX_TYPE_ENTER    ], NULL));
        if ((*regExType) == REGEX_TYPE_ENTER) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(buttonRegExTypeEnter), TRUE);
        buttonRegExTypeLeave     = addBox(hbox, FALSE, newRadioButton(G_OBJECT(dialog), buttonRegExTypeEnter,   "radio_leave",     REGEX_TYPE_STRINGS[REGEX_TYPE_LEAVE    ], NULL));
        if ((*regExType) == REGEX_TYPE_LEAVE) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(buttonRegExTypeLeave), TRUE);
        buttonRegExTypeError     = addBox(hbox, FALSE, newRadioButton(G_OBJECT(dialog), buttonRegExTypeLeave,   "radio_error",     REGEX_TYPE_STRINGS[REGEX_TYPE_ERROR    ], NULL));
        if ((*regExType) == REGEX_TYPE_ERROR) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(buttonRegExTypeError), TRUE);
        buttonRegExTypeWarning   = addBox(hbox, FALSE, newRadioButton(G_OBJECT(dialog), buttonRegExTypeError,   "radio_warning",   REGEX_TYPE_STRINGS[REGEX_TYPE_WARNING  ], NULL));
        if ((*regExType) == REGEX_TYPE_WARNING) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(buttonRegExTypeWarning), TRUE);
        buttonRegExTypeExtension = addBox(hbox, FALSE, newRadioButton(G_OBJECT(dialog), buttonRegExTypeWarning, "radio_extension", REGEX_TYPE_STRINGS[REGEX_TYPE_EXTENSION], NULL));
        if ((*regExType) == REGEX_TYPE_EXTENSION) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(buttonRegExTypeExtension), TRUE);
      }
      addGrid(grid, 1, 1, 2, GTK_WIDGET(hbox));

      addGrid(grid, 2, 0, 1, newLabel(G_OBJECT(dialog), NULL, text, NULL));
      widgetRegex = addGrid(grid, 2, 1, 2, newEntry(G_OBJECT(dialog),
                                                    "entry_regex",
                                                    "Regular expression. Group names:\n"
                                                    "  <filePath>\n"
                                                    "  <lineNumber>\n"
                                                    "  <columnNumber>\n"
                                                    "  <message>\n"
                                                   )
                           );
      gtk_entry_set_text(GTK_ENTRY(widgetRegex), regExString->str);

      addGrid(grid, 3, 0, 1, newLabel(G_OBJECT(dialog), NULL, _("Sample"), NULL));
      widgetSample = addGrid(grid, 3, 1, 2, newEntry(G_OBJECT(dialog), "entry_sample", "Regular expression match example"));
      gtk_entry_set_text(GTK_ENTRY(widgetSample), sample);

      addGrid(grid, 4, 1, 1, newLabel(G_OBJECT(dialog), NULL, "File path", NULL));
      widgetFilePath = addGrid(grid, 4, 2, 1, newView (G_OBJECT(dialog), "view_file_path", NULL));
      addGrid(grid, 5, 1, 1, newLabel(G_OBJECT(dialog), NULL, "Line number", NULL));
      widgetLineNumber = addGrid(grid, 5, 2, 1, newView (G_OBJECT(dialog), "view_line_number", NULL));
      addGrid(grid, 6, 1, 1, newLabel(G_OBJECT(dialog), NULL, "Column number", NULL));
      widgetColumnNumber = addGrid(grid, 6, 2, 1, newView (G_OBJECT(dialog), "view_column_number", NULL));
      addGrid(grid, 7, 1, 1, newLabel(G_OBJECT(dialog), NULL, "Message", NULL));
      widgetMessage = addGrid(grid, 7, 2, 1, newView (G_OBJECT(dialog), "view_message", NULL));

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
    if      (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(buttonRegExTypeEnter    ))) (*regExType) = REGEX_TYPE_ENTER;
    else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(buttonRegExTypeLeave    ))) (*regExType) = REGEX_TYPE_LEAVE;
    else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(buttonRegExTypeError    ))) (*regExType) = REGEX_TYPE_ERROR;
    else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(buttonRegExTypeWarning  ))) (*regExType) = REGEX_TYPE_WARNING;
    else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(buttonRegExTypeExtension))) (*regExType) = REGEX_TYPE_EXTENSION;
    g_string_assign(regExString, gtk_entry_get_text(GTK_ENTRY(widgetRegex)));
  }

  // free resources
  gtk_widget_destroy(dialog);

  return (result == GTK_RESPONSE_ACCEPT);
}

/***********************************************************************\
* Name   : addRegEx
* Purpose: add regular expression
* Input  : sample - sample for regex-match or ""
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void addRegEx(const gchar *sample)
{
  GString    *languageString = g_string_new(NULL);
  GString    *groupString    = g_string_new(NULL);
  RegExTypes regExType       = REGEX_TYPE_NONE;
  GString    *regexString    = g_string_new(NULL);
  if (dialogRegex(GTK_WINDOW(geany_data->main_widgets->window),
                  _("Add regular expression"),
                  _("Regular expression:"),
                  languageString,
                  groupString,
                  &regExType,
                  regexString,
                  sample
                 )
     )
  {
    g_assert(regExType >= REGEX_TYPE_MIN);
    g_assert(regExType <= REGEX_TYPE_MAX);

    gtk_list_store_insert_with_values(pluginData.configuration.regExStore,
                                      NULL,
                                      -1,
                                      MODEL_REGEX_LANGUAGE, languageString->str,
                                      MODEL_REGEX_GROUP,    groupString->str,
                                      MODEL_REGEX_TYPE,     regExType,
                                      MODEL_REGEX_REGEX,    regexString->str,
                                      -1
                                     );
  }
  g_string_free(regexString, TRUE);
  g_string_free(groupString, TRUE);
  g_string_free(languageString, TRUE);
}

/***********************************************************************\
* Name   : cloneRegEx
* Purpose: clone regular expression
* Input  : treeModel - model
*          treeIter  - iterator in model
*          sample    - sample for regex-match or ""
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void cloneRegEx(GtkTreeModel *treeModel,
                      GtkTreeIter  *treeIter,
                      const gchar  *sample
                     )
{
  g_assert(treeModel != NULL);
  g_assert(treeIter != NULL);

  gchar       *language;
  gchar       *group;
  RegExTypes  regExType;
  gchar       *regEx;
  gtk_tree_model_get(treeModel,
                     treeIter,
                     MODEL_REGEX_LANGUAGE, &language,
                     MODEL_REGEX_GROUP,    &group,
                     MODEL_REGEX_TYPE,     &regExType,
                     MODEL_REGEX_REGEX,    &regEx,
                     -1
                    );
  g_assert(language != NULL);
  g_assert(group != NULL);
  g_assert(regExType >= REGEX_TYPE_MIN);
  g_assert(regExType <= REGEX_TYPE_MAX);
  g_assert(regEx != NULL);

  GString *languageString = g_string_new(language);
  GString *groupString    = g_string_new(group);
  GString *regExString    = g_string_new(regEx);
  if (dialogRegex(GTK_WINDOW(geany_data->main_widgets->window),
                  _("Clone regular expression"),
                  _("Regular expression:"),
                  languageString,
                  groupString,
                  &regExType,
                  regExString,
                  sample
                 )
     )
  {
    g_assert(regExType >= REGEX_TYPE_MIN);
    g_assert(regExType <= REGEX_TYPE_MAX);

    gtk_list_store_insert_with_values(pluginData.configuration.regExStore,
                                      NULL,
                                      -1,
                                      MODEL_REGEX_LANGUAGE, languageString->str,
                                      MODEL_REGEX_GROUP,    groupString->str,
                                      MODEL_REGEX_TYPE,     regExType,
                                      MODEL_REGEX_REGEX,    regExString->str,
                                      -1
                                     );
  }
  g_string_free(regExString, TRUE);
  g_string_free(groupString, TRUE);
  g_string_free(languageString, TRUE);

  g_free(regEx);
  g_free(group);
  g_free(language);
}

/***********************************************************************\
* Name   : editRegEx
* Purpose: edit regular expression
* Input  : treeModel - model
*          treeIter  - iterator in model
*          sample    - sample for regex-match or ""
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void editRegEx(GtkTreeModel *treeModel,
                     GtkTreeIter  *treeIter,
                     const gchar  *sample
                    )
{
  g_assert(treeModel != NULL);
  g_assert(treeIter != NULL);

  gchar       *language;
  gchar       *group;
  RegExTypes  regExType;
  gchar       *regEx;
  gtk_tree_model_get(treeModel,
                     treeIter,
                     MODEL_REGEX_LANGUAGE, &language,
                     MODEL_REGEX_GROUP,    &group,
                     MODEL_REGEX_TYPE,     &regExType,
                     MODEL_REGEX_REGEX,    &regEx,
                     -1
                    );
  g_assert(language != NULL);
  g_assert(group != NULL);
  g_assert(regExType >= REGEX_TYPE_MIN);
  g_assert(regExType <= REGEX_TYPE_MAX);
  g_assert(regEx != NULL);

  GString *languageString = g_string_new(language);
  GString *groupString    = g_string_new(group);
  GString *regExString    = g_string_new(regEx);
  if (dialogRegex(GTK_WINDOW(geany_data->main_widgets->window),
                  _("Edit regular expression"),
                  _("Regular expression:"),
                  languageString,
                  groupString,
                  &regExType,
                  regExString,
                  sample
                 )
     )
  {
    g_assert(regExType >= REGEX_TYPE_MIN);
    g_assert(regExType <= REGEX_TYPE_MAX);

    gtk_list_store_set(pluginData.configuration.regExStore,
                       treeIter,
                       MODEL_REGEX_LANGUAGE, languageString->str,
                       MODEL_REGEX_GROUP,    groupString->str,
                       MODEL_REGEX_TYPE,     regExType,
                       MODEL_REGEX_REGEX,    regExString->str,
                       -1
                      );
  }
  g_string_free(regExString, TRUE);
  g_string_free(groupString, TRUE);
  g_string_free(languageString, TRUE);

  g_free(regEx);
  g_free(group);
  g_free(language);
}

// ---------------------------------------------------------------------

/***********************************************************************\
* Name   : getAbsolutePath
* Purpose: get absolute path from directory and file path
* Input  : directory - directory (can be NULL)
*          filePath  - file path
* Output : -
* Return : absolute file path
* Notes  : -
\***********************************************************************/

LOCAL gchar *getAbsolutePath(const gchar *directory,
                             const gchar *filePath
                            )
{
  g_assert(filePath != NULL);

  gchar *absoluteFilePath;
  if (g_path_is_absolute(filePath) || EMPTY(directory))
  {
    absoluteFilePath = g_strdup(filePath);
  }
  else
  {
    absoluteFilePath = g_strconcat(directory, G_DIR_SEPARATOR_S, filePath, NULL);
  }

  return absoluteFilePath;
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
  g_assert(filePath != NULL);

  // get absolute path
  gchar *absoluteFilePath = getAbsolutePath(directory,filePath);
  gchar *absoluteFilePathLocale = utils_get_locale_from_utf8(absoluteFilePath);

  // find/open document
  GeanyDocument *document = document_find_by_filename(filePath);
  if ((document == NULL) && !EMPTY(absoluteFilePathLocale))
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
  gchar *directory, *filePath;
  gint  lineNumber, columnNumber;
  gtk_tree_model_get(treeModel,
                     &pluginData.build.errorsTreeIter,
                     MODEL_ERROR_WARNING_DIRECTORY,     &directory,
                     MODEL_ERROR_WARNING_FILE_PATH,      &filePath,
                     MODEL_ERROR_WARNING_LINE_NUMBER,   &lineNumber,
                     MODEL_ERROR_WARNING_COLUMN_NUMBER, &columnNumber,
                     -1
                    );
  g_assert(filePath != NULL);
  showSource(directory, filePath, lineNumber, columnNumber);
  g_free(filePath);
  g_free(directory);

  // previous error
  GtkTreeIter treeIter = pluginData.build.errorsTreeIter;
  if (!gtk_tree_model_iter_previous(treeModel,
                                    &pluginData.build.errorsTreeIter
                                   )
     )
  {
    pluginData.build.errorsTreeIter = treeIter;
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
  gchar *directory, *filePath;
  gint  lineNumber, columnNumber;
  gtk_tree_model_get(treeModel,
                     &pluginData.build.errorsTreeIter,
                     MODEL_ERROR_WARNING_DIRECTORY,     &directory,
                     MODEL_ERROR_WARNING_FILE_PATH,     &filePath,
                     MODEL_ERROR_WARNING_LINE_NUMBER,   &lineNumber,
                     MODEL_ERROR_WARNING_COLUMN_NUMBER, &columnNumber,
                     -1
                    );
  g_assert(filePath != NULL);
  showSource(directory, filePath, lineNumber, columnNumber);
  g_free(filePath);
  g_free(directory);

  // next error
  GtkTreeIter treeIter = pluginData.build.errorsTreeIter;
  if (!gtk_tree_model_iter_next(treeModel,
                                &pluginData.build.errorsTreeIter
                               )
     )
  {
    pluginData.build.errorsTreeIter = treeIter;
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
  gchar *directory, *filePath;
  gint  lineNumber, columnNumber;
  gtk_tree_model_get(treeModel,
                     &pluginData.build.warningsTreeIter,
                     MODEL_ERROR_WARNING_DIRECTORY,     &directory,
                     MODEL_ERROR_WARNING_FILE_PATH,      &filePath,
                     MODEL_ERROR_WARNING_LINE_NUMBER,   &lineNumber,
                     MODEL_ERROR_WARNING_COLUMN_NUMBER, &columnNumber,
                     -1
                    );
  g_assert(filePath != NULL);
  showSource(directory, filePath, lineNumber, columnNumber);
  g_free(filePath);
  g_free(directory);

  // previous warning
  GtkTreeIter treeIter = pluginData.build.warningsTreeIter;
  if (!gtk_tree_model_iter_previous(treeModel,
                                    &pluginData.build.warningsTreeIter
                                   )
     )
  {
    pluginData.build.warningsTreeIter = treeIter;
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
  gchar *directory, *filePath;
  gint  lineNumber, columnNumber;
  gtk_tree_model_get(treeModel,
                     &pluginData.build.warningsTreeIter,
                     MODEL_ERROR_WARNING_DIRECTORY,     &directory,
                     MODEL_ERROR_WARNING_FILE_PATH,      &filePath,
                     MODEL_ERROR_WARNING_LINE_NUMBER,   &lineNumber,
                     MODEL_ERROR_WARNING_COLUMN_NUMBER, &columnNumber,
                     -1
                    );
  g_assert(filePath != NULL);
  showSource(directory, filePath, lineNumber, columnNumber);
  g_free(filePath);
  g_free(directory);

  // next warning
  GtkTreeIter treeIter = pluginData.build.warningsTreeIter;
  if (!gtk_tree_model_iter_next(treeModel,
                                &pluginData.build.warningsTreeIter
                               )
     )
  {
    pluginData.build.warningsTreeIter = treeIter;
  }
}

/***********************************************************************\
* Name   : isMatchingRegEx
* Purpose: check if regular expression match to line
* Input  : regExString - regular expression string
*          line        - line
* Output : filePath     - file path
*          lineNumber   - line number
*          columnNumber - column number
*          message      - message
* Return : TRUE iff reqular expression matches line
* Notes  : -
\***********************************************************************/

LOCAL gboolean isMatchingRegEx(const gchar  *regExString,
                               const gchar  *line,
                               GString      *filePath,
                               guint        *lineNumber,
                               guint        *columnNumber,
                               GString      *message
                              )
{
  gboolean matchesRegEx = FALSE;

  GRegex      *regex;
  GMatchInfo  *matchInfo;

  g_assert(regExString != NULL);
  g_assert(line != NULL);
  g_assert(filePath != NULL);
  g_assert(lineNumber != NULL);
  g_assert(message != NULL);

  regex = g_regex_new(regExString,
                      0, // compile_optipns
                      0, // match option
                      NULL // error
                     );
  if (regex != NULL)
  {
    matchesRegEx = g_regex_match(regex, line, 0, &matchInfo);
    if (matchesRegEx)
    {
      gint filePathMatchNumber         = g_regex_get_string_number(regex, "filePath");
      gint fileLineNumberMatchNumber   = g_regex_get_string_number(regex, "lineNumber");
      gint fileColumnNumberMatchNumber = g_regex_get_string_number(regex, "columnNumber");
      gint fileMessageMatchNumber      = g_regex_get_string_number(regex, "message");
      if (filePathMatchNumber         == -1) filePathMatchNumber         = 1;
      if (fileLineNumberMatchNumber   == -1) fileLineNumberMatchNumber   = 2;
      if (fileColumnNumberMatchNumber == -1) fileColumnNumberMatchNumber = 3;
      if (fileMessageMatchNumber      == -1) fileMessageMatchNumber      = 4;

      g_string_assign(filePath,
                      (g_match_info_get_match_count(matchInfo) > filePathMatchNumber)
                        ? g_match_info_fetch(matchInfo, filePathMatchNumber)
                        : g_strdup("")
                     );
      (*lineNumber)   = (g_match_info_get_match_count(matchInfo) > fileLineNumberMatchNumber)
                          ? (guint)g_ascii_strtoull(g_match_info_fetch(matchInfo, fileLineNumberMatchNumber), NULL, 10)
                          : 0;
      (*columnNumber) = (g_match_info_get_match_count(matchInfo) > fileColumnNumberMatchNumber)
                          ? (guint)g_ascii_strtoull(g_match_info_fetch(matchInfo, fileColumnNumberMatchNumber), NULL, 10)
                          : 0;
      g_string_assign(message,
                      (g_match_info_get_match_count(matchInfo) > fileMessageMatchNumber)
                        ? g_match_info_fetch(matchInfo, fileMessageMatchNumber)
                        : g_strdup("")
                     );
    }
    g_match_info_free(matchInfo);
    g_regex_unref(regex);
  }

  return matchesRegEx;
}

/***********************************************************************\
* Name   : isMatchingRegExs
* Purpose: check if regular expression from model match to line
* Input  : treeModel       - model with regular expressions
*          regExTypeFilter - regular expression types filter
*          line            - line
* Output : matchTreePathString - best matching tree path string
*          filePathString      - file path on match
*          lineNumber          - line number on match
*          columnNumber        - column number on match
*          messageString       - message on match
* Return : TRUE iff at least one reqular expression matches line
* Notes  : -
\***********************************************************************/

LOCAL gboolean isMatchingRegExs(GtkTreeModel *treeModel,
                                const gchar  *line,
                                GString      *matchTreePathString,
                                RegExTypes   *regExType,
                                GString      *filePathString,
                                guint        *lineNumber,
                                guint        *columnNumber,
                                GString      *messageString
                               )
{
  g_assert(treeModel != NULL);
  g_assert(line != NULL);
  g_assert(matchTreePathString != NULL);
  g_assert(regExType != NULL);
  g_assert(filePathString != NULL);
  g_assert(lineNumber != NULL);
  g_assert(columnNumber != NULL);
  g_assert(messageString != NULL);

  gint bestMatchCount = -1;

  GtkTreeIter treeIter;
  if (gtk_tree_model_get_iter_first(treeModel, &treeIter))
  {
    do
    {
      RegExTypes checkRegExType;
      gchar      *checkRegEx;
      gtk_tree_model_get(treeModel,
                         &treeIter,
                         MODEL_REGEX_TYPE,  &checkRegExType,
                         MODEL_REGEX_REGEX, &checkRegEx,
                         -1
                        );
      g_assert(checkRegExType >= REGEX_TYPE_MIN);
      g_assert(checkRegExType <= REGEX_TYPE_MAX);
      g_assert(checkRegEx != NULL);

      GRegex     *regExCompiled;
      GMatchInfo *matchInfo;
      regExCompiled = g_regex_new(checkRegEx,
                                  0, // compile_optipns
                                  0, // match option
                                  NULL // error
                                 );
      if (regExCompiled != NULL)
      {
        if (g_regex_match(regExCompiled, line, 0, &matchInfo))
        {
          gint directoryMatchNumber        = g_regex_get_string_number(regExCompiled, "directory");
          gint filePathMatchNumber         = g_regex_get_string_number(regExCompiled, "filePath");
          gint fileLineNumberMatchNumber   = g_regex_get_string_number(regExCompiled, "lineNumber");
          gint fileColumnNumberMatchNumber = g_regex_get_string_number(regExCompiled, "columnNumber");
          gint fileMessageMatchNumber      = g_regex_get_string_number(regExCompiled, "message");

          gint matchCount = 0;
          if (directoryMatchNumber        >=0) { matchCount++; } else { directoryMatchNumber        = 1; }
          if (filePathMatchNumber         >=0) { matchCount++; } else { filePathMatchNumber         = 1; }
          if (fileLineNumberMatchNumber   >=0) { matchCount++; } else { fileLineNumberMatchNumber   = 2; }
          if (fileColumnNumberMatchNumber >=0) { matchCount++; } else { fileColumnNumberMatchNumber = 3; }
          if (fileMessageMatchNumber      >=0) { matchCount++; } else { fileMessageMatchNumber      = 4; }

          if (matchCount > bestMatchCount)
          {
            gchar *matchTreePath = gtk_tree_model_get_string_from_iter(treeModel, &treeIter);
            g_string_assign(matchTreePathString, matchTreePath);
            g_free(matchTreePath);

            (*regExType) = checkRegExType;

            if (directoryMatchNumber >= 0)
            {
              if (g_match_info_get_match_count(matchInfo) > directoryMatchNumber)
              {
                gchar *string = g_match_info_fetch(matchInfo, directoryMatchNumber);
                g_string_assign(filePathString, string);
                g_free(string);
              }
              else
              {
                g_string_assign(filePathString, "");
              }
            }
            else
            {
              if (g_match_info_get_match_count(matchInfo) > filePathMatchNumber)
              {
                gchar *string = g_match_info_fetch(matchInfo, filePathMatchNumber);
                g_string_assign(filePathString, string);
                g_free(string);
              }
              else
              {
                g_string_assign(filePathString, "");
              }
            }
            if (lineNumber != NULL)
            {
              if (g_match_info_get_match_count(matchInfo) > fileLineNumberMatchNumber)
              {
                gchar *string = g_match_info_fetch(matchInfo, fileLineNumberMatchNumber);
                (*lineNumber) = (guint)g_ascii_strtoull(string, NULL, 10);
                g_free(string);
              }
              else
              {
                (*lineNumber) = 0;
              }
            }
            if (columnNumber != NULL)
            {
              if (g_match_info_get_match_count(matchInfo) > fileColumnNumberMatchNumber)
              {
                gchar *string = g_match_info_fetch(matchInfo, fileColumnNumberMatchNumber);
                (*columnNumber) = (guint)g_ascii_strtoull(string, NULL, 10);
                g_free(string);
              }
              else
              {
                (*columnNumber) = 0;
              }
            }
            if (messageString != NULL)
            {
              if (g_match_info_get_match_count(matchInfo) > fileMessageMatchNumber)
              {
                gchar *string = g_match_info_fetch(matchInfo, fileMessageMatchNumber);
                g_string_assign(messageString, string);
                g_free(string);
              }
              else
              {
                g_string_assign(messageString, "");
              }
            }

            bestMatchCount = matchCount;
          }
        }
        g_match_info_free(matchInfo);
        g_regex_unref(regExCompiled);

        g_free(checkRegEx);
      }
    }
    while (gtk_tree_model_iter_next(treeModel, &treeIter));
  }

  return (bestMatchCount >= 0);
}

/***********************************************************************\
* Name   : onExecuteCommandOutput
* Purpose: handle stderr
* Input  : line        - line
*          ioCondition - i/o condition set
*          data        - user data (not used)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onExecuteCommandOutput(GString *line, GIOCondition ioCondition, gpointer data)
{
  GString *string;

  g_assert(line != NULL);

  UNUSED_VARIABLE(data);

  if ((ioCondition & (G_IO_IN | G_IO_PRI)) != 0)
  {
    // init variables
    string = g_string_new(NULL);

    // remove LF/CR
    g_strchomp(line->str);

    // find match
    gboolean matched = FALSE;
    gchar       *absoluteDirectory   = NULL;
    RegExTypes  regExType;
    GString     *matchTreePathString = g_string_new(NULL);
    GString     *filePathString      = g_string_new(NULL);
    guint       lineNumber, columnNumber;
    GString     *messageString       = g_string_new(NULL);
    if      (isMatchingRegExs(GTK_TREE_MODEL(pluginData.configuration.regExStore),
                              line->str,
                              matchTreePathString,
                              &regExType,
                              filePathString,
                              &lineNumber,
                              &columnNumber,
                              messageString
                             )
            )
    {
      switch (regExType)
      {
        case REGEX_TYPE_NONE:
          break;
        case REGEX_TYPE_ENTER:
          // get directory prefix
          string_stack_push(pluginData.build.directoryPrefixStack , filePathString->str);
          break;
        case REGEX_TYPE_LEAVE:
          // clear directory prefix
          string_stack_pop(pluginData.build.directoryPrefixStack);
          break;
        case REGEX_TYPE_ERROR:
          // insert error message
          absoluteDirectory = getAbsoluteDirectory(pluginData.build.workingDirectory->str,
                                                   string_stack_top(pluginData.build.directoryPrefixStack)
                                                  );
          gtk_tree_store_insert_with_values(pluginData.build.errorsStore,
                                            &pluginData.build.insertIter,
                                            NULL,
                                            -1,
                                            MODEL_ERROR_WARNING_DIRECTORY,     absoluteDirectory,
                                            MODEL_ERROR_WARNING_FILE_PATH,     filePathString->str,
                                            MODEL_ERROR_WARNING_LINE_NUMBER,   lineNumber,
                                            MODEL_ERROR_WARNING_COLUMN_NUMBER, columnNumber,
                                            MODEL_ERROR_WARNING_MESSAGE,       messageString->str,
                                            -1
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
          pluginData.build.lastInsertStore    = pluginData.build.errorsStore;
          pluginData.build.lastInsertTreeIter = &pluginData.build.insertIter;
          break;
        case REGEX_TYPE_WARNING:
          // insert warning message
          absoluteDirectory = getAbsoluteDirectory(pluginData.build.workingDirectory->str,
                                                   string_stack_top(pluginData.build.directoryPrefixStack)
                                                  );
          gtk_tree_store_insert_with_values(pluginData.build.warningsStore,
                                            &pluginData.build.insertIter,
                                            NULL,
                                            -1,
                                            MODEL_ERROR_WARNING_DIRECTORY,     absoluteDirectory,
                                            MODEL_ERROR_WARNING_FILE_PATH,     filePathString->str,
                                            MODEL_ERROR_WARNING_LINE_NUMBER,   lineNumber,
                                            MODEL_ERROR_WARNING_COLUMN_NUMBER, columnNumber,
                                            MODEL_ERROR_WARNING_MESSAGE,       messageString->str,
                                            -1
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
          pluginData.build.lastInsertStore    = pluginData.build.warningsStore;
          pluginData.build.lastInsertTreeIter = &pluginData.build.insertIter;
          break;
        case REGEX_TYPE_EXTENSION:
          // append to last error/warning message
          if (   (pluginData.build.lastInsertStore != NULL)
              && (pluginData.build.lastInsertTreeIter != NULL)
             )
          {
            absoluteDirectory = getAbsoluteDirectory(pluginData.build.workingDirectory->str,
                                                     string_stack_top(pluginData.build.directoryPrefixStack)
                                                    );
            gtk_tree_store_insert_with_values(pluginData.build.lastInsertStore,
                                              NULL,
                                              pluginData.build.lastInsertTreeIter,
                                              -1,
                                              MODEL_ERROR_WARNING_DIRECTORY,     absoluteDirectory,
                                              MODEL_ERROR_WARNING_FILE_PATH,     filePathString->str,
                                              MODEL_ERROR_WARNING_LINE_NUMBER,   lineNumber,
                                              MODEL_ERROR_WARNING_COLUMN_NUMBER, columnNumber,
                                              MODEL_ERROR_WARNING_MESSAGE,       messageString->str,
                                              -1
                                             );
          }
          break;
      }

      matched = TRUE;
    }
    else if (   (pluginData.projectProperties.errorRegEx->len > 0)
             && isMatchingRegEx(pluginData.projectProperties.errorRegEx->str,
                                line->str,
                                filePathString,
                                &lineNumber,
                                &columnNumber,
                                messageString
                               )

            )
    {
      // insert error message
      absoluteDirectory = getAbsoluteDirectory(pluginData.build.workingDirectory->str,
                                               string_stack_top(pluginData.build.directoryPrefixStack)
                                              );
      gtk_tree_store_insert_with_values(pluginData.build.errorsStore,
                                        &pluginData.build.insertIter,
                                        NULL,
                                        -1,
                                        MODEL_ERROR_WARNING_DIRECTORY,     absoluteDirectory,
                                        MODEL_ERROR_WARNING_FILE_PATH,     filePathString->str,
                                        MODEL_ERROR_WARNING_LINE_NUMBER,   lineNumber,
                                        MODEL_ERROR_WARNING_COLUMN_NUMBER, columnNumber,
                                        MODEL_ERROR_WARNING_MESSAGE,       messageString->str,
                                        -1
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
      pluginData.build.lastInsertStore    = pluginData.build.errorsStore;
      pluginData.build.lastInsertTreeIter = &pluginData.build.insertIter;

      matched = TRUE;
    }
    else if (   (pluginData.projectProperties.warningRegEx->len > 0)
             && isMatchingRegEx(pluginData.projectProperties.warningRegEx->str,
                                line->str,
                                filePathString,
                                &lineNumber,
                                &columnNumber,
                                messageString
                               )

            )
    {
      // insert warning message
      absoluteDirectory = getAbsoluteDirectory(pluginData.build.workingDirectory->str,
                                               string_stack_top(pluginData.build.directoryPrefixStack)
                                              );
      gtk_tree_store_insert_with_values(pluginData.build.warningsStore,
                                        &pluginData.build.insertIter,
                                        NULL,
                                        -1,
                                        MODEL_ERROR_WARNING_DIRECTORY,     absoluteDirectory,
                                        MODEL_ERROR_WARNING_FILE_PATH,     filePathString->str,
                                        MODEL_ERROR_WARNING_LINE_NUMBER,   lineNumber,
                                        MODEL_ERROR_WARNING_COLUMN_NUMBER, columnNumber,
                                        MODEL_ERROR_WARNING_MESSAGE,       messageString->str,
                                        -1
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
      pluginData.build.lastInsertStore    = pluginData.build.warningsStore;
      pluginData.build.lastInsertTreeIter = &pluginData.build.insertIter;

      matched = TRUE;
    }

    // insert into message tab
    gtk_list_store_insert_with_values(pluginData.build.messagesStore,
                                      NULL,
                                      -1,
                                      MODEL_MESSAGE_COLOR,         matched ? COLOR_BUILD_MESSAGES_MATCHED : COLOR_BUILD_MESSAGES,
                                      MODEL_MESSAGE_DIRECTORY,     absoluteDirectory,
                                      MODEL_MESSAGE_TREE_PATH,     matchTreePathString->str,
                                      MODEL_MESSAGE_FILE_PATH,     filePathString->str,
                                      MODEL_MESSAGE_LINE_NUMBER,   lineNumber,
                                      MODEL_MESSAGE_COLUMN_NUMBER, columnNumber,
                                      MODEL_MESSAGE_MESSAGE,       line->str,
                                      -1
                                     );
// TODO: msgwin_compiler_add(COLOR_BLUE, _("%s (in directory: %s)"), cmd, utf8_working_dir);
    showLastLine(GTK_SCROLLED_WINDOW(pluginData.widgets.messagesTab));

    g_string_free(messageString, TRUE);
    g_string_free(filePathString, TRUE);
    g_string_free(matchTreePathString, TRUE);
    g_free(absoluteDirectory);

    // update number of errors/warnings
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

// TODO:
    if (!pluginData.build.showedFirstErrorWarning)
    {
      // initialise prev/next error/warning iterators
      pluginData.build.errorsTreeIterValid = gtk_tree_model_get_iter_first(gtk_tree_view_get_model(GTK_TREE_VIEW(pluginData.widgets.errorsTree)),
                                                                       &pluginData.build.errorsTreeIter
                                                                      );
      pluginData.build.warningsTreeIterValid = gtk_tree_model_get_iter_first(gtk_tree_view_get_model(GTK_TREE_VIEW(pluginData.widgets.warningsTree)),
                                                                         &pluginData.build.warningsTreeIter
                                                                        );

      // show first error/warning
      if      (pluginData.configuration.autoShowFirstError && pluginData.build.errorsTreeIterValid)
      {
        showBuildErrorsTab();
        showNextError();
        pluginData.build.showedFirstErrorWarning = TRUE;
      }
      else if (pluginData.configuration.autoShowFirstWarning && pluginData.build.warningsTreeIterValid)
      {
        showBuildWarningsTab();
        showNextWarning();
        pluginData.build.showedFirstErrorWarning = TRUE;
      }
    }

    // free resources
    g_string_free(string, TRUE);
  }
}

/***********************************************************************\
* Name   : onExecuteCommandExit
* Purpose: handle exit of execute process
* Input  : pid    - process id (not used)
*          status - process exit status
*          data   - user data (not used)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onExecuteCommandExit(GPid     pid,
                                gint     status,
                                gpointer data
                               )
{
  UNUSED_VARIABLE(pid);
  UNUSED_VARIABLE(status);
  UNUSED_VARIABLE(data);

  gchar *doneMessage = g_strdup_printf(_("Build done (exit code: %d)"), status);
  gtk_list_store_insert_with_values(pluginData.build.messagesStore,
                                    NULL,
                                    -1,
                                    MODEL_MESSAGE_COLOR,   COLOR_BUILD_INFO,
                                    MODEL_MESSAGE_MESSAGE, doneMessage,
                                    -1
                                   );
  g_free(doneMessage);
  showLastLine(GTK_SCROLLED_WINDOW(pluginData.widgets.messagesTab));

  // initialise prev/next error/warning iterators
  pluginData.build.errorsTreeIterValid = gtk_tree_model_get_iter_first(gtk_tree_view_get_model(GTK_TREE_VIEW(pluginData.widgets.errorsTree)),
                                                                       &pluginData.build.errorsTreeIter
                                                                      );
  pluginData.build.warningsTreeIterValid = gtk_tree_model_get_iter_first(gtk_tree_view_get_model(GTK_TREE_VIEW(pluginData.widgets.warningsTree)),
                                                                         &pluginData.build.warningsTreeIter
                                                                        );

  if (!pluginData.build.showedFirstErrorWarning)
  {
    // show first error/warning
    if      (pluginData.configuration.autoShowFirstError && pluginData.build.errorsTreeIterValid)
    {
      showBuildErrorsTab();
      showNextError();
      pluginData.build.showedFirstErrorWarning = TRUE;
    }
    else if (pluginData.configuration.autoShowFirstWarning && pluginData.build.warningsTreeIterValid)
    {
      showBuildWarningsTab();
      showNextWarning();
      pluginData.build.showedFirstErrorWarning = TRUE;
    }
  }

  pluginData.build.pid = 0;
  setEnableToolbar(TRUE);
}

/***********************************************************************\
* Name   : onExecuteCommandRunOutput
* Purpose: handle run stdout/stderr
* Input  : line        - line
*          ioCondition - i/o condition set
*          data        - user data (not used)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onExecuteCommandRunOutput(GString *line, GIOCondition ioCondition, gpointer data)
{
  g_assert(line != NULL);

  UNUSED_VARIABLE(data);

  if ((ioCondition & (G_IO_IN | G_IO_PRI)) != 0)
  {
    // remove LF/CR
    g_strchomp(line->str);

    // insert into message tab
    msgwin_msg_add(COLOR_BLACK, -1, NULL, "%s", line->str);
  }
}

/***********************************************************************\
* Name   : executeCommand
* Purpose: execute external command
* Input  : commandLine      - command line to execute
*          workingDirectory - working directory
*          customText       - custome text or NULL
*          stdoutHandler    - stdout-handler
*          stderrHandler    - stderr-handler
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void executeCommand(const gchar   *commandLine,
                          const gchar   *workingDirectory,
                          const gchar   *customText,
                          OutputHandler stdoutHandler,
                          OutputHandler stderrHandler
                         )
{
  GString *string;
  GError  *error;

  g_assert(commandLine != NULL);

  if (!isStringEmpty(commandLine))
  {
    // get project
    GeanyProject *project = geany_data->app->project;

    // get current document (if possible)
    GeanyDocument *document = document_get_current();

    // save all documents
    if (pluginData.configuration.autoSaveAll)
    {
      guint i;
      foreach_document(i)
      {
        if (documents[i]->changed)
        {
          if (!document_save_file(documents[i], FALSE))
          {
            dialogs_show_msgbox(GTK_MESSAGE_ERROR, "Cannot save document '%s'", documents[i]->file_name);
            pluginData.build.pid = 0;
            setEnableToolbar(TRUE);
            return;
          }
        }
      }
    }

    // expand macros
    gchar *commandLineExpanded      = expandMacros(project, document, commandLine, NULL, customText);
    gchar *workingDirectoryExpanded = expandMacros(project, document, workingDirectory, NULL, NULL);

    // create command line argument array
    GPtrArray *argv = g_ptr_array_new_with_free_func(g_free);
    #ifdef G_OS_UNIX
      // add process group wrapper: make sure signal term will terminate process and all sub-processes
      g_ptr_array_add(argv, g_strdup("setsid"));
      g_ptr_array_add(argv, g_strdup("sh"));
      g_ptr_array_add(argv, g_strdup("-c"));

      /* Note: redirect stdout to stderr with another sh-instance as a
       * work-around for unordered processing of stdout/stderr by the
       * GTK spawn-function: depending on the executed command the
       * ordering of lines printed to stdout and stderr may no be in
       * the appropiated order. This would make it impossible to get the
       * correct relation of lines printed to stdout and stderr, e. g.
       * make print directory changes to stdout, while gcc report errors
       * and warnings on stderr.
       */
      string = g_string_new("sh -c '");
      gchar *escapedCommandLine = g_strescape(commandLineExpanded, NULL);
      g_string_append(string, escapedCommandLine);
      g_free(escapedCommandLine);
      g_string_append(string, "' 1>&2");
      g_ptr_array_add(argv, string->str);
      g_string_free(string, FALSE);
    #else
      // parse and use command line directly
      gint commandLineArgc;
      gchar **commandLineArgv;
      g_shell_parse_argv(commandLineExpanded, &commandLineArgc, &commandLineArgv, NULL);
      for (guint i = 0; i < commandLineArgc; i++)
      {
        g_ptr_array_add(argv, g_strdup(commandLineArgv[i]));
      }
      g_strfreev(commandLineArgv);
    #endif
    g_ptr_array_add(argv, NULL);

    // if no working directory is given, use file/project or current directory
    if (workingDirectoryExpanded == NULL)
    {
      if      (project != NULL)
      {
        workingDirectoryExpanded = g_strdup(project->base_path);
      }
      else if (document != NULL)
      {
        workingDirectoryExpanded = g_path_get_dirname(document->file_name);
      }
      else
      {
        workingDirectoryExpanded = g_get_current_dir();
      }
    }

    // run build
    clearAll();
    string = g_string_new(NULL);
    g_string_printf(string, "Working directory: %s", workingDirectoryExpanded);
    gtk_list_store_insert_with_values(pluginData.build.messagesStore,
                                      NULL,
                                      -1,
                                      MODEL_MESSAGE_COLOR,   COLOR_BUILD_INFO,
                                      MODEL_MESSAGE_MESSAGE, string->str,
                                      -1
                                     );
    g_string_printf(string, "Build command line: %s", commandLineExpanded);
    gtk_list_store_insert_with_values(pluginData.build.messagesStore,
                                      NULL,
                                      -1,
                                      MODEL_MESSAGE_COLOR,   COLOR_BUILD_INFO,
                                      MODEL_MESSAGE_MESSAGE, string->str,
                                      -1
                                     );
    g_string_free(string, TRUE);
    showLastLine(GTK_SCROLLED_WINDOW(pluginData.widgets.messagesTab));

    g_string_assign(pluginData.build.workingDirectory, workingDirectoryExpanded);
    pluginData.build.showedFirstErrorWarning = FALSE;

    error = NULL;
    if (!spawn_with_callbacks(workingDirectoryExpanded,
                              NULL, // commandLine,
                              (gchar**)argv->pdata,
                              NULL, // envp,
                              SPAWN_ASYNC|SPAWN_LINE_BUFFERED,
                              NULL, // stdin_cb,
                              NULL, // stdin_data,
                              stdoutHandler,
                              workingDirectoryExpanded,
                              0, // stdout_max_length,
                              stderrHandler,
                              workingDirectoryExpanded,
                              0, // stderr_max_length,
                              onExecuteCommandExit, // exit_cb,
                              NULL, // exit_data,
                              &pluginData.build.pid,
                              &error
                             )
       )
    {
      gchar *failedMessage;
      if (error != NULL)
      {
        dialogs_show_msgbox(GTK_MESSAGE_ERROR, "Cannot run '%s': %s", commandLineExpanded, error->message);
        failedMessage = g_strdup_printf(_("Build failed (error: %s)"), error->message);
      }
      else
      {
        dialogs_show_msgbox(GTK_MESSAGE_ERROR, "Cannot run '%s'", commandLineExpanded);
        failedMessage = g_strdup_printf(_("Build failed"));
      }
      g_error_free(error);

      gtk_list_store_insert_with_values(pluginData.build.messagesStore,
                                        NULL,
                                        -1,
                                        MODEL_MESSAGE_COLOR,   COLOR_BUILD_ERROR,
                                        MODEL_MESSAGE_MESSAGE, failedMessage,
                                        -1
                                       );
      g_free(failedMessage);
      showLastLine(GTK_SCROLLED_WINDOW(pluginData.widgets.messagesTab));

      pluginData.build.pid = 0;
      setEnableToolbar(TRUE);
    }

    // free resources
    g_ptr_array_free(argv, TRUE);
    g_free(workingDirectoryExpanded);
    g_free(commandLineExpanded);
  }
}

/***********************************************************************\
* Name   : executeCommand*
* Purpose: execute specific command
* Input  : -
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

// TODO:
#if 0
LOCAL void executeCommandMakeCustomTarget()
{
  GString *customTarget = g_string_new(NULL);
  if (inputDialog(GTK_WINDOW(geany_data->main_widgets->window),
                  _("Make custom target"),
                  _("Custom target"),
                  _("Custom target to build."),
                  pluginData.build.lastCustomTarget->str,
                  NULL,  // validator
                  NULL,
                  customTarget
                 )
     )
  {
    g_string_assign(pluginData.build.lastCustomTarget, customTarget->str);

    setEnableToolbar(FALSE);
    showBuildMessagesTab();
    if      (pluginData.projectProperties.commands.makeCustomTarget.line->len > 0)
    {
      executeCommand(pluginData.projectProperties.commands.makeCustomTarget.line->str,
                     pluginData.projectProperties.commands.makeCustomTarget.workingDirectory->str,
                     NULL,
                     onExecuteCommandOutput,
                     onExecuteCommandOutput
                    );
    }
    else if (pluginData.configuration.commands.makeCustomTarget.line->len > 0)
    {
      executeCommand(pluginData.configuration.commands.makeCustomTarget.line->str,
      pluginData.configuration.commands.makeCustomTarget.workingDirectory->str,
      NULL,
      onExecuteCommandOutput,
      onExecuteCommandOutput);
    }
    else
    {
      executeCommand(pluginData.projectProperties.buildMenuCommands.makeCustomTarget.line->str,
      pluginData.projectProperties.buildMenuCommands.makeCustomTarget.workingDirectory->str,
      NULL,
      onExecuteCommandOutput,
      onExecuteCommandOutput);
    }
  }

  g_string_free(customTarget, TRUE);
}
#endif

LOCAL void executeCommandStop()
{
  if (pluginData.build.pid > 1)
  {
    spawn_kill_process(-pluginData.build.pid, NULL);
    setEnableStop(FALSE);
  }
}

// ---------------------------------------------------------------------

/***********************************************************************\
* Name   : onMenuItemCommand
* Purpose: menu item callbacks
* Input  : widget - widget (not used)
*          data   - user data (not used)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onMenuItemCommand(GtkWidget *widget, gpointer data)
{
  guint i = GPOINTER_TO_UINT(data);

  g_assert(i < MAX_COMMANDS);

  UNUSED_VARIABLE(widget);

  const Command *command = getCommand(i);
  if (command != NULL)
  {
    setEnableToolbar(FALSE);
    showBuildMessagesTab();
    executeCommand(command->commandLine,
                   command->workingDirectory,
                   NULL,
                   command->parseOutput ? onExecuteCommandOutput : onExecuteCommandRunOutput,
                   command->parseOutput ? onExecuteCommandOutput : onExecuteCommandRunOutput
                  );
  }
}

LOCAL void onMenuItemShowPrevError(GtkWidget *widget, gpointer data)
{
  UNUSED_VARIABLE(widget);
  UNUSED_VARIABLE(data);

  showPrevError();
}

LOCAL void onMenuItemShowNextError(GtkWidget *widget, gpointer data)
{
  UNUSED_VARIABLE(widget);
  UNUSED_VARIABLE(data);

  showNextError();
}

LOCAL void onMenuItemShowPrevWarning(GtkWidget *widget, gpointer data)
{
  UNUSED_VARIABLE(widget);
  UNUSED_VARIABLE(data);

  showPrevWarning();
}

LOCAL void onMenuItemShowNextWarning(GtkWidget *widget, gpointer data)
{
  UNUSED_VARIABLE(widget);
  UNUSED_VARIABLE(data);

  showNextWarning();
}

LOCAL void onMenuItemCommandStop(GtkWidget *widget, gpointer data)
{
  UNUSED_VARIABLE(widget);
  UNUSED_VARIABLE(data);

  executeCommandStop();
}

LOCAL void onMenuItemProjectPreferences(GtkWidget *widget, gpointer data)
{
  UNUSED_VARIABLE(widget);
  UNUSED_VARIABLE(data);

  // request show plugin preferences tab
  pluginData.widgets.showProjectPropertiesTab = TRUE;

  // activate "project preferences" menu item
  GtkWidget *menuItem = ui_lookup_widget(geany_data->main_widgets->window, "project_properties1");
  gtk_menu_item_activate(GTK_MENU_ITEM(menuItem));
}

LOCAL void onMenuItemPluginConfiguration(GtkWidget *widget, gpointer data)
{
  UNUSED_VARIABLE(widget);
  UNUSED_VARIABLE(data);

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
  GtkTreeIter  treeIter;
  if (gtk_tree_selection_get_selected(treeSelection, &treeModel, &treeIter))
  {
    gchar *directory, *filePath;
    gint  lineNumber, columnNumber;
    gtk_tree_model_get(treeModel,
                       &treeIter,
                       MODEL_MESSAGE_DIRECTORY,     &directory,
                       MODEL_MESSAGE_FILE_PATH,     &filePath,
                       MODEL_MESSAGE_LINE_NUMBER,   &lineNumber,
                       MODEL_MESSAGE_COLUMN_NUMBER, &columnNumber,
                       -1
                      );
    if (filePath != NULL)
    {
      showSource(directory, filePath, lineNumber, columnNumber);
    }
    g_free(filePath);
    g_free(directory);
  }

  return FALSE;
}

/***********************************************************************\
* Name   : onMessageListButtonPress
* Purpose: button press callback
* Input  : widget      - widget
*          eventButton - button event
*          data        - popup menu
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL gboolean onMessageListButtonPress(GtkTreeView    *messageList,
                                        GdkEventButton *eventButton,
                                        gpointer       data
                                       )
{
  GtkMenu *menu = (GtkMenu*)data;

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
      GtkTreeIter treeIter;
      if (gtk_tree_model_get_iter(GTK_TREE_MODEL(pluginData.build.messagesStore),
                                  &treeIter,
                                  pluginData.build.messagesTreePath
                                 )
         )
      {
        gchar *treePath;
        gtk_tree_model_get(GTK_TREE_MODEL(pluginData.build.messagesStore),
                           &treeIter,
                           MODEL_MESSAGE_TREE_PATH, &treePath,
                           -1
                          );
        gtk_widget_set_sensitive(GTK_WIDGET(pluginData.widgets.menuItems.editRegEx), !EMPTY(treePath));
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
* Input  : data - tree widget
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL gboolean onErrorWarningTreeSelectionChanged(gpointer data)
{
  GtkTreeView *treeView = GTK_TREE_VIEW(data);

  g_assert(treeView != NULL);

  GtkTreeSelection *treeSelection;
  treeSelection = gtk_tree_view_get_selection(treeView);

  GtkTreeModel *treeModel;
  GtkTreeIter  treeIter;
  if (gtk_tree_selection_get_selected(treeSelection, &treeModel, &treeIter))
  {
    gchar *directory, *filePath;
    gint  lineNumber, columnNumber;
    gtk_tree_model_get(treeModel,
                       &treeIter,
                       MODEL_ERROR_WARNING_DIRECTORY,     &directory,
                       MODEL_ERROR_WARNING_FILE_PATH,      &filePath,
                       MODEL_ERROR_WARNING_LINE_NUMBER,   &lineNumber,
                       MODEL_ERROR_WARNING_COLUMN_NUMBER, &columnNumber,
                       -1
                      );
    if (filePath != NULL)
    {
      showSource(directory, filePath, lineNumber, columnNumber);
    }
    g_free(filePath);
    g_free(directory);
  }

  return FALSE;
}

/***********************************************************************\
* Name   : onErrorWarningTreeButtonPress
* Purpose: button press callback
* Input  : widget      - widget
*          eventButton - button event
*          data        - user data (not used)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL gboolean onErrorWarningTreeButtonPress(GtkTreeView    *treeView,
                                             GdkEventButton *eventButton,
                                             gpointer       data
                                            )
{
  g_assert(treeView != NULL);
  g_assert(eventButton != NULL);

  UNUSED_VARIABLE(data);

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
*          data     - user data (not used)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL gboolean onErrorWarningTreeKeyPress(GtkTreeView *treeView,
                          GdkEventKey *eventKey,
                          gpointer    data
                         )
{
  g_assert(treeView != NULL);
  g_assert(eventKey != NULL);

  UNUSED_VARIABLE(data);

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
*          column   - column
*          data     - user data (not used)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onErrorWarningTreeDoubleClick(GtkTreeView       *treeView,
                                         GtkTreePath       *treePath,
                                         GtkTreeViewColumn *column,
                                         gpointer           data
                                        )
{
  g_assert(treeView != NULL);
  g_assert(treePath != NULL);
  g_assert(column != NULL);

  UNUSED_VARIABLE(column);
  UNUSED_VARIABLE(data);

  GtkTreeModel *model = gtk_tree_view_get_model(treeView);

  GtkTreeIter iter;
  if (gtk_tree_model_get_iter(model, &iter, treePath))
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
  if ((KEY_BINDING_COMMAND <= keyId) && (keyId <= KEY_BINDING_COMMAND+MAX_COMMANDS))
  {
    const Command *command = getCommand(keyId-KEY_BINDING_COMMAND);
    if (command != NULL)
    {
      setEnableToolbar(FALSE);
      showBuildMessagesTab();
      executeCommand(command->commandLine,
                     command->workingDirectory,
                     NULL,
                     command->parseOutput ? onExecuteCommandOutput : onExecuteCommandRunOutput,
                     command->parseOutput ? onExecuteCommandOutput : onExecuteCommandRunOutput
                    );
    }
  }
  else
  {
    switch (keyId)
    {
      case KEY_BINDING_PREV_ERROR:
        if (pluginData.build.errorsTreeIterValid)
        {
          showPrevError();
        }
        break;

      case KEY_BINDING_NEXT_ERROR:
        if (pluginData.build.errorsTreeIterValid)
        {
          showNextError();
        }
        break;

      case KEY_BINDING_PREV_WARNING:
        if (pluginData.build.warningsTreeIterValid)
        {
          showPrevWarning();
        }
        break;

      case KEY_BINDING_NEXT_WARNING:
        if (pluginData.build.warningsTreeIterValid)
        {
          showNextWarning();
        }
        break;

      case KEY_BINDING_STOP:
        executeCommandStop();
        break;

      default:
        break;
    }
  }
}

/***********************************************************************\
* Name   : destroyWidget
* Purpose: destroy widget callback
* Input  : data     - widget
*          userData - user data (not used)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void destroyWidget(gpointer data, gpointer userData)
{
  GtkWidget *widget = (GtkWidget*)data;

  UNUSED_VARIABLE(userData);

  gtk_widget_destroy(widget);
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
  GtkToolItem *button;

  // discard buttons
  g_ptr_array_foreach(pluginData.widgets.buttons,
                      destroyWidget,
                      NULL
                     );
  g_ptr_array_set_size(pluginData.widgets.buttons, 0);
  gtk_widget_destroy(GTK_WIDGET(pluginData.widgets.buttonStop));
  pluginData.widgets.buttonStop = NULL;

  gboolean isFirst = TRUE;
  for (guint i = 0; i < MAX_COMMANDS; i++)
  {
    const Command *command = getCommand(i);
    if (command != NULL)
    {
      if (command->showButton)
      {
        button = isFirst ? gtk_menu_tool_button_new(NULL, command->title) : gtk_tool_button_new(NULL, command->title);
        plugin_signal_connect(geany_plugin,
                              G_OBJECT(button),
                              "clicked",
                              FALSE,
                              G_CALLBACK(onMenuItemCommand),
                              GUINT_TO_POINTER(i)
                             );
        plugin_add_toolbar_item(geany_plugin, button);
        gtk_widget_show(GTK_WIDGET(button));

        g_ptr_array_add(pluginData.widgets.buttons,button);
        isFirst = FALSE;
      }
    }
  }

  if (pluginData.configuration.stopButton)
  {
    pluginData.widgets.buttonStop = gtk_tool_button_new(NULL, _("Stop"));
    plugin_signal_connect(geany_plugin,
                          G_OBJECT(button),
                          "clicked",
                          FALSE,
                          G_CALLBACK(onMenuItemCommandStop),
                          NULL
                         );
    plugin_add_toolbar_item(geany_plugin, pluginData.widgets.buttonStop);
    gtk_widget_show(GTK_WIDGET(pluginData.widgets.buttonStop));
  }
}

/***********************************************************************\
* Name   : updateToolbarButtonMenu
* Purpose: update toolbar button menu
* Input  : -
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void updateToolbarButtonMenu()
{
  // discard menu items
  gtk_widget_destroy(pluginData.widgets.buttonMenu);

  if (pluginData.widgets.buttons->len > 0)
  {
    GtkWidget *menu = gtk_menu_new();
    {
      GtkWidget *menuItem;

      for (guint i = 0; i < MAX_COMMANDS; i++)
      {
        const Command *command = getCommand(i);
        if (command != NULL)
        {
          if (command->showMenuItem)
          {
            pluginData.widgets.menuItems.commands[i] = gtk_menu_item_new_with_mnemonic(command->title);
            g_assert(pluginData.widgets.menuItems.commands[i] != NULL);
            gtk_container_add(GTK_CONTAINER(menu), pluginData.widgets.menuItems.commands[i]);
            plugin_signal_connect(geany_plugin,
                                  G_OBJECT(pluginData.widgets.menuItems.commands[i]),
                                  "activate",
                                  FALSE,
                                  G_CALLBACK(onMenuItemCommand),
                                  GUINT_TO_POINTER(i)
                                 );
          }
          else
          {
            pluginData.widgets.menuItems.commands[i] = NULL;
          }
        }
      }

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

  #if 0
      pluginData.widgets.menuItems.run = gtk_menu_item_new_with_mnemonic(_("_Run"));
      gtk_container_add(GTK_CONTAINER(menu), pluginData.widgets.menuItems.run);
      plugin_signal_connect(geany_plugin,
                            G_OBJECT(pluginData.widgets.menuItems.run),
                            "activate",
                            FALSE,
                            G_CALLBACK(onMenuItemRun),
                            NULL
                           );
  #endif

      pluginData.widgets.menuItems.stop = gtk_menu_item_new_with_mnemonic(_("Stop"));
      gtk_widget_set_sensitive(GTK_WIDGET(pluginData.widgets.menuItems.stop), FALSE);
      gtk_container_add(GTK_CONTAINER(menu), pluginData.widgets.menuItems.stop);
      plugin_signal_connect(geany_plugin,
                            G_OBJECT(pluginData.widgets.menuItems.stop),
                            "activate",
                            FALSE,
                            G_CALLBACK(onMenuItemCommandStop),
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

      pluginData.widgets.menuItems.configuration = gtk_menu_item_new_with_label(_("Configuration"));
      gtk_container_add(GTK_CONTAINER(menu), pluginData.widgets.menuItems.configuration);
      plugin_signal_connect(geany_plugin,
                            G_OBJECT(pluginData.widgets.menuItems.configuration),
                            "activate",
                            FALSE,
                            G_CALLBACK(onMenuItemPluginConfiguration),
                            NULL
                           );
    }
    gtk_widget_show_all(menu);

    gtk_menu_tool_button_set_menu(pluginData.widgets.buttons->pdata[0], menu);
  }
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
  // create toolbar buttons
  updateToolbarButtons();

  // create toolbar button menu attached to first button
  updateToolbarButtonMenu();
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

  gtk_widget_destroy(pluginData.widgets.buttonMenu);

  UNUSED_VARIABLE(plugin);
}

/***********************************************************************\
* Name   : onMessageListAddRegEx
* Purpose: add regular expression
* Input  : widget      - widget (not used)
*          eventButton - event button (not used)
*          data        - user data (no used)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onMessageListAddRegEx(GtkWidget *widget, GdkEventButton *eventButton, gpointer data)
{
  UNUSED_VARIABLE(widget);
  UNUSED_VARIABLE(eventButton);
  UNUSED_VARIABLE(data);

  GtkTreeIter treeIter;
  if (gtk_tree_model_get_iter(GTK_TREE_MODEL(pluginData.build.messagesStore),
                              &treeIter,
                              pluginData.build.messagesTreePath
                             )
     )
  {
    gchar *message;
    gtk_tree_model_get(GTK_TREE_MODEL(pluginData.build.messagesStore),
                       &treeIter,
                      MODEL_MESSAGE_MESSAGE, &message,
                       -1
                      );

    addRegEx(message);

    g_free(message);

    configurationSave();
  }
}

/***********************************************************************\
* Name   : onMessageListEditRegEx
* Purpose: edit regular expression
* Input  : widget      - widget (not used)
*          eventButton - event button (not used)
*          data        - user data (no used)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onMessageListEditRegEx(GtkWidget *widget, GdkEventButton *eventButton, gpointer data)
{
  UNUSED_VARIABLE(widget);
  UNUSED_VARIABLE(eventButton);
  UNUSED_VARIABLE(data);

  GtkTreeIter treeIter;
  if (gtk_tree_model_get_iter(GTK_TREE_MODEL(pluginData.build.messagesStore),
                              &treeIter,
                              pluginData.build.messagesTreePath
                             )
     )
  {
    gchar *treePath;
    gchar *message;
    gtk_tree_model_get(GTK_TREE_MODEL(pluginData.build.messagesStore),
                       &treeIter,
                       MODEL_MESSAGE_TREE_PATH, &treePath,
                       MODEL_MESSAGE_MESSAGE,   &message,
                       -1
                      );

    if (gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(pluginData.configuration.regExStore),
                                            &treeIter,
                                            treePath
                                           )
       )
    {
      editRegEx(GTK_TREE_MODEL(pluginData.configuration.regExStore), &treeIter, message);
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
*          treeIter     - tree entry iterator
*          data         - column number
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void  errorWarningTreeViewCellRendererInteger(GtkTreeViewColumn *column,
                                                    GtkCellRenderer   *cellRenderer,
                                                    GtkTreeModel      *treeModel,
                                                    GtkTreeIter       *treeIter,
                                                    gpointer           data
                                                   )
{
  guint i = GPOINTER_TO_INT(data);

  g_assert(column != NULL);
  g_assert(cellRenderer != NULL);
  g_assert(treeModel != NULL);
  g_assert(treeIter != NULL);

  gint n;
  gtk_tree_model_get(treeModel, treeIter, i, &n, -1);
  if (n > 0)
  {
    gchar buffer[32];
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

    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(treeView), TRUE);
    gtk_tree_view_set_headers_clickable(GTK_TREE_VIEW(treeView), TRUE);
    gtk_tree_view_set_search_column(GTK_TREE_VIEW(treeView), MODEL_ERROR_WARNING_FILE_PATH);
  }

  return treeView;
}

/***********************************************************************\
* Name   : onConfigureDoubleClickRegEx
* Purpose: handle double-click: edit selected regular expression
* Input  : treeView - tree view
*          treePath - tree path (not used)
*          column   - tree column (not used)
*          data     - data (not used)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onConfigureDoubleClickRegEx(GtkTreeView       *treeView,
                                       GtkTreePath       *treePath,
                                       GtkTreeViewColumn *column,
                                       gpointer           data
                                      )
{
  UNUSED_VARIABLE(treePath);
  UNUSED_VARIABLE(column);
  UNUSED_VARIABLE(data);

  GtkTreeSelection *treeSelection = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeView));
  g_assert(treeSelection != NULL);

  GtkTreeModel *treeModel;
  GtkTreeIter  treeIter;
  if (gtk_tree_selection_get_selected(treeSelection, &treeModel, &treeIter))
  {
    editRegEx(treeModel,
              &treeIter,
              ""
             );
  }
}

/***********************************************************************\
* Name   : onConfigureAddRegEx
* Purpose: add regular expression
* Input  : widget      - widget (not used)
*          eventButton - event button (not used)
*          data        - user data (not used)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onConfigureAddRegEx(GtkWidget *widget, GdkEventButton *eventButton, gpointer data)
{
  UNUSED_VARIABLE(widget);
  UNUSED_VARIABLE(eventButton);
  UNUSED_VARIABLE(data);

  addRegEx("");
}

/***********************************************************************\
* Name   : onConfigureCloneRegEx
* Purpose: clone regular expression
* Input  : widget      - widget (not used)
*          eventButton - event button (not used)
*          data        - list view widget
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onConfigureCloneRegEx(GtkWidget *widget, GdkEventButton *eventButton, gpointer data)
{
  UNUSED_VARIABLE(widget);
  UNUSED_VARIABLE(eventButton);

  GtkTreeSelection *treeSelection = gtk_tree_view_get_selection(GTK_TREE_VIEW(data));
  g_assert(treeSelection != NULL);

  GtkTreeModel *treeModel;
  GtkTreeIter  treeIter;
  if (gtk_tree_selection_get_selected(treeSelection, &treeModel, &treeIter))
  {
    cloneRegEx(treeModel,
               &treeIter,
               ""
              );
  }
}

/***********************************************************************\
* Name   : onConfigureEditRegEx
* Purpose: edit regular expression
* Input  : widget      - widget (not used)
*          eventButton - event button (not used)
*          data        - list view widget
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onConfigureEditRegEx(GtkWidget *widget, GdkEventButton *eventButton, gpointer data)
{
  UNUSED_VARIABLE(widget);
  UNUSED_VARIABLE(eventButton);

  GtkTreeSelection *treeSelection = gtk_tree_view_get_selection(GTK_TREE_VIEW(data));
  g_assert(treeSelection != NULL);

  GtkTreeModel *treeModel;
  GtkTreeIter  treeIter;
  if (gtk_tree_selection_get_selected(treeSelection, &treeModel, &treeIter))
  {
    editRegEx(treeModel,
              &treeIter,
              ""
             );
  }
}

/***********************************************************************\
* Name   : onConfigureRemoveRegEx
* Purpose: remove regular expression
* Input  : widget      - widget (not used)
*          eventButton - event button (not used)
*          data        - list view widget
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onConfigureRemoveRegEx(GtkWidget *widget, GdkEventButton *eventButton, gpointer data)
{
  UNUSED_VARIABLE(widget);
  UNUSED_VARIABLE(eventButton);

  GtkTreeSelection *treeSelection = gtk_tree_view_get_selection(GTK_TREE_VIEW(data));
  g_assert(treeSelection != NULL);

  GtkTreeModel     *model;
  GtkTreeIter      iter;
  if (gtk_tree_selection_get_selected(treeSelection, &model, &iter))
  {
    gtk_list_store_remove(pluginData.configuration.regExStore, &iter);
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
      pluginData.build.messagesList = gtk_tree_view_new();
      gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(pluginData.build.messagesList), FALSE);
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
        gtk_tree_view_append_column(GTK_TREE_VIEW(pluginData.build.messagesList), column);

        gtk_tree_view_set_model(GTK_TREE_VIEW(pluginData.build.messagesList), GTK_TREE_MODEL(pluginData.build.messagesStore));

        GtkWidget *menu = gtk_menu_new();
        {
          GtkWidget *menuItem;

          pluginData.widgets.menuItems.editRegEx = gtk_menu_item_new_with_label("edit regular expression");
          gtk_container_add(GTK_CONTAINER(menu), pluginData.widgets.menuItems.editRegEx);
          plugin_signal_connect(geany_plugin,
                                G_OBJECT(pluginData.widgets.menuItems.editRegEx),
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
                              G_OBJECT(pluginData.build.messagesList),
                              "button-press-event",
                              FALSE,
                              G_CALLBACK(onMessageListButtonPress),
                              menu
                             );
      }
      gtk_container_add(GTK_CONTAINER(pluginData.widgets.messagesTab), pluginData.build.messagesList);
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
      {
        gtk_tree_view_set_model(GTK_TREE_VIEW(pluginData.widgets.errorsTree), GTK_TREE_MODEL(pluginData.build.errorsStore));

        gtk_tree_view_set_show_expanders(GTK_TREE_VIEW(pluginData.widgets.errorsTree), TRUE);

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
      {
        gtk_tree_view_set_model(GTK_TREE_VIEW(pluginData.widgets.warningsTree), GTK_TREE_MODEL(pluginData.build.warningsStore));

        gtk_tree_view_set_show_expanders(GTK_TREE_VIEW(pluginData.widgets.warningsTree), TRUE);

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

        /* sorting */
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
* Input  : plugin - Geany plugin
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void initKeyBinding(GeanyPlugin *plugin)
{
  GeanyKeyGroup *keyGroup;

  g_assert(plugin != NULL);

  keyGroup = plugin_set_key_group(plugin, KEY_GROUP_BUILDER, KEY_BINDING_COUNT, NULL);

  for (guint i = 0; i < MAX_COMMANDS; i++)
  {
    gchar name[32],title[32];

    g_snprintf(name,sizeof(name),"command%d",i);
    g_snprintf(title,sizeof(title),"Command %d",i);
    keybindings_set_item(keyGroup,
                         KEY_BINDING_COMMAND+i,
                         onKeyBinding,
                         0,
                         0,
                         name,
                         title,
                         pluginData.widgets.menuItems.commands[i]
                        );
  }
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
                       KEY_BINDING_STOP,
                       onKeyBinding,
                       0,
                       0,
                       "stop",
                       _("Stop"),
                       pluginData.widgets.menuItems.stop
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

  gtk_notebook_remove_page(GTK_NOTEBOOK(ui_lookup_widget(plugin->geany_data->main_widgets->window, "notebook_info")),
                           pluginData.widgets.tabIndex
                          );
}

// // TODO: still no used
#if 0
/***********************************************************************\
* Name   : dialogRegexTypeCellRenderer
* Purpose: render regex type column
* Input  : cellLayout   - cell layout
*          cellRenderer - cell renderer
*          treeModel    - tree data model
*          treeIter     - tree entry iterator
*          data         - user data (not used)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void  configureCellRendererBoolean(GtkTreeViewColumn *cellLayout,
                                         GtkCellRenderer *cellRenderer,
                                         GtkTreeModel    *treeModel,
                                         GtkTreeIter     *treeIter,
                                         gpointer         data
                                        )
{
  gint modelIndex = (gint)(intptr_t)data;

  g_assert(cellLayout != NULL);
  g_assert(cellRenderer != NULL);
  g_assert(treeModel != NULL);
  g_assert(treeIter != NULL);

  UNUSED_VARIABLE(data);

  gboolean value;
  gtk_tree_model_get(treeModel, treeIter, modelIndex, &value, -1);
  g_object_set(cellRenderer, "text", value ? "yes" : "no", NULL);
}
#endif

/***********************************************************************\
* Name   : configureCellRendererType
* Purpose: render cell 'type'
* Input  : column       - column (not used)
*          cellRenderer - cell renderer
*          treeModel    - model
*          treeIter     - element iterator in model
*          data         - user data (not used)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void configureCellRendererType(GtkTreeViewColumn *column,
                                     GtkCellRenderer   *cellRenderer,
                                     GtkTreeModel      *treeModel,
                                     GtkTreeIter       *treeIter,
                                     gpointer          data
                                    )
{
  RegExTypes regExType;

  UNUSED_VARIABLE(column);
  UNUSED_VARIABLE(data);

  g_assert(treeModel != NULL);
  g_assert(treeIter != NULL);

  gtk_tree_model_get(treeModel, treeIter, MODEL_REGEX_TYPE, &regExType, -1);
  g_assert(regExType >= REGEX_TYPE_MIN);
  g_assert(regExType <= REGEX_TYPE_MAX);
  g_object_set(cellRenderer, "text", REGEX_TYPE_STRINGS[regExType], NULL);
}

/***********************************************************************\
* Name   : onConfigureResponse
* Purpose: save configuration
* Input  : dialog   - dialog
*          response - dialog response code
*          data     - user data (not used)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onConfigureResponse(GtkDialog *dialog, gint response, gpointer data)
{
  UNUSED_VARIABLE(data);

  // check if configuration should be saved/applied
  if ((response != GTK_RESPONSE_OK)
      && (response != GTK_RESPONSE_APPLY)
     )
  {
    return;
  }

  // get values
  gboolean isFirst = TRUE;
  for (guint i = 0; i < MAX_COMMANDS; i++)
  {
    char titleName[32],commandLineName[32],workingDirectoryName[32],showButtonName[32],showMenuItemName[32],inputCustomTextName[32],parseOutputName[32];

    g_snprintf(titleName,sizeof(titleName),"command_title%d",i);
    g_snprintf(commandLineName,sizeof(commandLineName),"command_command_line%d",i);
    g_snprintf(workingDirectoryName,sizeof(workingDirectoryName),"command_working_directory%d",i);
    g_snprintf(showButtonName,sizeof(showButtonName),"command_show_button%d",i);
    g_snprintf(showMenuItemName,sizeof(showMenuItemName),"command_show_menu_item%d",i);
    g_snprintf(inputCustomTextName,sizeof(inputCustomTextName),"command_input_custom_text%d",i);
    g_snprintf(parseOutputName,sizeof(parseOutputName),"command_parse_output%d",i);

    setCommand(&pluginData.configuration.commands[i],
               gtk_entry_get_text(GTK_ENTRY(g_object_get_data(G_OBJECT(dialog), titleName))),
               gtk_entry_get_text(GTK_ENTRY(g_object_get_data(G_OBJECT(dialog), commandLineName))),
               gtk_entry_get_text(GTK_ENTRY(g_object_get_data(G_OBJECT(dialog), workingDirectoryName))),
               gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(dialog), showButtonName))) || isFirst,
               gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(dialog), showMenuItemName))),
               gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(dialog), inputCustomTextName))),
               gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(dialog), parseOutputName)))
              );

    isFirst = FALSE;
  }

  GdkRGBA color;
  gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(g_object_get_data(G_OBJECT(dialog), "error_indicator_color")), &color);
  pluginData.configuration.errorIndicators     = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(dialog), "error_indicators")));
  pluginData.configuration.errorIndicatorColor = color;
  gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(g_object_get_data(G_OBJECT(dialog), "warning_indicator_color")), &color);
  pluginData.configuration.warningIndicators     = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(dialog), "warning_indicators")));
  pluginData.configuration.warningIndicatorColor = color;

  pluginData.configuration.stopButton             = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(dialog), "stop_button")));
  pluginData.configuration.autoSaveAll            = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(dialog), "auto_save_all")));
  pluginData.configuration.addProjectRegExResults = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(dialog), "add_project_regex_results")));
  pluginData.configuration.autoShowFirstError     = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(dialog), "auto_show_first_error")));
  pluginData.configuration.autoShowFirstWarning   = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(dialog), "auto_show_first_warning")));

  // save configuration
  configurationSave();

  // update view
  updateToolbarButtons();
  updateToolbarButtonMenu();
}

/***********************************************************************\
* Name   : configure
* Purpose: global configuration dialog
* Input  : plugin - Geany plugin
*          dialog - dialog
*          data   - user data (not used)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL GtkWidget *configure(GeanyPlugin *plugin, GtkDialog *dialog, gpointer data)
{
  g_assert(plugin != NULL);

  UNUSED_VARIABLE(data);

  GtkBox *vbox = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 6));
  {
    GtkWidget *frame;

    // commands
    frame = gtk_frame_new("Commands");
    gtk_frame_set_shadow_type(GTK_FRAME (frame), GTK_SHADOW_IN);
    gtk_frame_set_label_align(GTK_FRAME (frame), .5, 0);
    gtk_widget_set_margin_top(gtk_frame_get_label_widget(GTK_FRAME(frame)), 6);
    {
      GtkGrid *grid = GTK_GRID(gtk_grid_new());
      gtk_grid_set_row_spacing(grid, 6);
      gtk_grid_set_column_spacing(grid, 12);
      g_object_set(grid, "margin", 6, NULL);
      gtk_widget_set_hexpand(GTK_WIDGET(grid), TRUE);
      {
        gboolean isFirst = TRUE;
        for (guint i = 0; i < MAX_COMMANDS; i++)
        {
          char titleName[32],commandLineName[32],workingDirectoryName[32],showButtonName[32],showMenuItemName[32],inputCustomTextName[32],parseOutputName[32];

          g_snprintf(titleName,sizeof(titleName),"command_title%d",i);
          g_snprintf(commandLineName,sizeof(commandLineName),"command_command_line%d",i);
          g_snprintf(workingDirectoryName,sizeof(workingDirectoryName),"command_working_directory%d",i);
          g_snprintf(showButtonName,sizeof(showButtonName),"command_show_button%d",i);
          g_snprintf(showMenuItemName,sizeof(showMenuItemName),"command_show_menu_item%d",i);
          g_snprintf(inputCustomTextName,sizeof(inputCustomTextName),"command_input_custom_text%d",i);
          g_snprintf(parseOutputName,sizeof(parseOutputName),"command_parse_output%d",i);

          addGrid(grid, i, 0, 1, newEntry(G_OBJECT(dialog), titleName, "Command title."));
          addGrid(grid, i, 1, 1, newEntry(G_OBJECT(dialog), commandLineName, "Command line."));
          addGrid(grid, i, 2, 1, newWorkingDirectoryChooser(G_OBJECT(dialog), workingDirectoryName, "Working directory for command."));
          GtkWidget *button = addGrid(grid, i, 3, 1, newCheckButton(G_OBJECT(dialog), showButtonName, NULL, "Show button in toolbar."));
          if (isFirst) gtk_widget_set_sensitive(button,FALSE);
          addGrid(grid, i, 4, 1, newCheckButton(G_OBJECT(dialog), showMenuItemName, NULL, "Show menu item."));
          addGrid(grid, i, 5, 1, newCheckButton(G_OBJECT(dialog), inputCustomTextName, NULL, "Input custom text."));
          addGrid(grid, i, 6, 1, newCheckButton(G_OBJECT(dialog), parseOutputName, NULL, "Parse output for errors/warnings."));

          isFirst = FALSE;
        }
      }
      gtk_container_add(GTK_CONTAINER(frame), GTK_WIDGET(grid));
    }
    addBox(vbox, FALSE, GTK_WIDGET(frame));

    frame = gtk_frame_new("Commands2");

    // create error regular expression list
    frame = gtk_frame_new("Regular expressions");
    gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_IN);
    gtk_frame_set_label_align(GTK_FRAME(frame), .5, 0);
    gtk_widget_set_margin_top(gtk_frame_get_label_widget(GTK_FRAME(frame)), 6);
    {
      GtkBox *vbox1 = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 6));
      gtk_widget_set_margin_top(GTK_WIDGET(vbox1), 6);
      {
        GtkWidget *scrolledWindow = gtk_scrolled_window_new(NULL, NULL);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledWindow),
                                       GTK_POLICY_AUTOMATIC,
                                       GTK_POLICY_ALWAYS
                                      );
        {
          GtkWidget *listView = gtk_tree_view_new();
          gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(listView), TRUE);
          g_object_set_data(G_OBJECT(dialog), "regex_list", listView);
          {
            GtkCellRenderer   *cellRenderer;
            GtkTreeViewColumn *column;

            cellRenderer = gtk_cell_renderer_text_new();
            column = gtk_tree_view_column_new_with_attributes("Language",cellRenderer,"text", MODEL_REGEX_LANGUAGE, NULL);
            gtk_tree_view_column_set_sort_indicator(column, FALSE);
            gtk_tree_view_column_set_sort_column_id(column, MODEL_REGEX_LANGUAGE);
            gtk_tree_view_column_set_resizable(column, TRUE);
            gtk_tree_view_append_column(GTK_TREE_VIEW(listView), column);

            cellRenderer = gtk_cell_renderer_text_new();
            column = gtk_tree_view_column_new_with_attributes("Group",cellRenderer,"text", MODEL_REGEX_GROUP, NULL);
            gtk_tree_view_column_set_sort_indicator(column, FALSE);
            gtk_tree_view_column_set_sort_column_id(column, MODEL_REGEX_GROUP);
            gtk_tree_view_column_set_resizable(column, TRUE);
            gtk_tree_view_append_column(GTK_TREE_VIEW(listView), column);

            cellRenderer = gtk_cell_renderer_text_new();
            column = gtk_tree_view_column_new_with_attributes("Type",cellRenderer,"text", MODEL_REGEX_TYPE, NULL);
            gtk_tree_view_column_set_cell_data_func(column, cellRenderer, configureCellRendererType, NULL, NULL);
            gtk_tree_view_column_set_sort_indicator(column, FALSE);
            gtk_tree_view_column_set_sort_column_id(column, MODEL_REGEX_TYPE);
            gtk_tree_view_column_set_resizable(column, TRUE);
            gtk_tree_view_append_column(GTK_TREE_VIEW(listView), column);

            cellRenderer = gtk_cell_renderer_text_new();
            column = gtk_tree_view_column_new_with_attributes("Regex",cellRenderer,"text", MODEL_REGEX_REGEX, NULL);
            gtk_tree_view_column_set_sort_indicator(column, FALSE);
            gtk_tree_view_column_set_sort_column_id(column, MODEL_REGEX_REGEX);
            gtk_tree_view_column_set_resizable(column, TRUE);
            gtk_tree_view_append_column(GTK_TREE_VIEW(listView), column);

            gtk_tree_view_set_model(GTK_TREE_VIEW(listView),
                                    GTK_TREE_MODEL(pluginData.configuration.regExStore)
                                   );

            plugin_signal_connect(geany_plugin,
                                  G_OBJECT(listView),
                                  "row-activated",
                                  FALSE,
                                  G_CALLBACK(onConfigureDoubleClickRegEx),
                                  NULL
                                 );
          }
          gtk_container_add(GTK_CONTAINER(scrolledWindow), listView);
        }
        addBox(vbox1, TRUE, scrolledWindow);

        GtkBox *hbox1 = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12));
        g_object_set(hbox1, "margin", 6, NULL);
        {
          GtkWidget *button;
          GtkWidget *image;

          button = gtk_button_new_with_label(_("Add"));
          image = gtk_image_new();
          gtk_image_set_from_icon_name(GTK_IMAGE(image), "list-add", GTK_ICON_SIZE_BUTTON);
          gtk_button_set_image(GTK_BUTTON(button), image);
          addBox(hbox1, FALSE, button);
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
          addBox(hbox1, FALSE, button);
          plugin_signal_connect(geany_plugin,
                                G_OBJECT(button),
                                "button-press-event",
                                FALSE,
                                G_CALLBACK(onConfigureCloneRegEx),
                                g_object_get_data(G_OBJECT(dialog), "regex_list")
                               );

          button = gtk_button_new_with_label(_("Edit"));
          image = gtk_image_new();
          gtk_image_set_from_icon_name(GTK_IMAGE(image), "edit-paste", GTK_ICON_SIZE_BUTTON);
          gtk_button_set_image(GTK_BUTTON(button), image);
          addBox(hbox1, FALSE, button);
          plugin_signal_connect(geany_plugin,
                                G_OBJECT(button),
                                "button-press-event",
                                FALSE,
                                G_CALLBACK(onConfigureEditRegEx),
                                g_object_get_data(G_OBJECT(dialog), "regex_list")
                               );

          button = gtk_button_new_with_label(_("Remove"));
          image = gtk_image_new();
          gtk_image_set_from_icon_name(GTK_IMAGE(image), "list-remove", GTK_ICON_SIZE_BUTTON);
          gtk_button_set_image(GTK_BUTTON(button), image);
          addBox(hbox1, FALSE, button);
          plugin_signal_connect(geany_plugin,
                                G_OBJECT(button),
                                "button-press-event",
                                FALSE,
                                G_CALLBACK(onConfigureRemoveRegEx),
                                g_object_get_data(G_OBJECT(dialog), "regex_list")
                               );
        }
        addBox(vbox1, FALSE, GTK_WIDGET(hbox1));
      }
      gtk_container_add(GTK_CONTAINER(frame), GTK_WIDGET(vbox1));
    }
    addBox(vbox, TRUE, GTK_WIDGET(frame));

    GtkBox *hbox1;

#if 0
    hbox1 = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12));
    {
      addBox(hbox1, FALSE, gtk_label_new("Buttons: "));

      addBox(hbox1, FALSE, newCheckButton(G_OBJECT(dialog), "button_build", "build", "Show build button."));
      addBox(hbox1, FALSE, newCheckButton(G_OBJECT(dialog), "button_clean", "clean", "Show cleanbutton."));
      addBox(hbox1, FALSE, newCheckButton(G_OBJECT(dialog), "button_run",   "run",   "Show run button."));
      addBox(hbox1, FALSE, newCheckButton(G_OBJECT(dialog), "button_stop",  "stop",  "Show stop button."));
    }
    addBox(GTK_BOX(vbox), FALSE, GTK_WIDGET(hbox1));
#endif

    hbox1 = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12));
    {
      addBox(hbox1, FALSE, gtk_label_new("Indicator colors: "));

      addBox(hbox1, FALSE, newCheckButton(G_OBJECT(dialog), "error_indicators",  "errors",  "Show error indicators."));
      addBox(hbox1, FALSE, newColorChooser(G_OBJECT(dialog), "error_indicator_color", "Error indicator color."));
      addBox(hbox1, FALSE, newCheckButton(G_OBJECT(dialog), "warning_indicators",  "warnings",  "Show warning indicators."));
      addBox(hbox1, FALSE, newColorChooser(G_OBJECT(dialog), "warning_indicator_color", "Warning indicator color."));
    }
    addBox(vbox, FALSE, GTK_WIDGET(hbox1));

    GtkGrid *grid = GTK_GRID(gtk_grid_new());
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_widget_set_hexpand(GTK_WIDGET(grid), TRUE);
    {
      addGrid(grid, 0, 0, 1, newCheckButton(G_OBJECT(dialog), "stop_button",              "stop button", "Show stop button."));
      addGrid(grid, 1, 0, 1, newCheckButton(G_OBJECT(dialog), "auto_save_all",            "Auto save all", "Auto save all before build is started."));
      addGrid(grid, 2, 0, 1, newCheckButton(G_OBJECT(dialog), "add_project_regex_results","Add results of project regular expressions","Add results of project defined regular expression. If disabled only project regular expressions - if defined - are used only to detect errors or warnings."));
      addGrid(grid, 3, 0, 1, newCheckButton(G_OBJECT(dialog), "auto_show_first_error",    "Show first error", "Auto show first error after build is done."));
      addGrid(grid, 4, 0, 1, newCheckButton(G_OBJECT(dialog), "auto_show_first_warning",  "Show first warning", "Auto show first warning after build is done without errors."));
    }
    addBox(vbox, FALSE, GTK_WIDGET(grid));
  }
  gtk_widget_show_all(GTK_WIDGET(vbox));

  // set values
  gboolean isFirst = TRUE;
  for (guint i = 0; i < MAX_COMMANDS; i++)
  {
    const Command *command = getCommand(i);
    if (command != NULL)
    {
      char titleName[32],commandLineName[32],workingDirectoryName[32],showButtonName[32],showMenuItemName[32],inputCustomTextName[32],parseOutputName[32];

      g_snprintf(titleName,sizeof(titleName),"command_title%d",i);
      g_snprintf(commandLineName,sizeof(commandLineName),"command_command_line%d",i);
      g_snprintf(workingDirectoryName,sizeof(workingDirectoryName),"command_working_directory%d",i);
      g_snprintf(showButtonName,sizeof(showButtonName),"command_show_button%d",i);
      g_snprintf(showMenuItemName,sizeof(showMenuItemName),"command_show_menu_item%d",i);
      g_snprintf(inputCustomTextName,sizeof(inputCustomTextName),"command_input_custom_text%d",i);
      g_snprintf(parseOutputName,sizeof(parseOutputName),"command_parse_output%d",i);

      gtk_entry_set_text(GTK_ENTRY(g_object_get_data(G_OBJECT(dialog), titleName)), command->title);
      gtk_entry_set_text(GTK_ENTRY(g_object_get_data(G_OBJECT(dialog), commandLineName)), command->commandLine);
      gtk_entry_set_text(GTK_ENTRY(g_object_get_data(G_OBJECT(dialog), workingDirectoryName)), command->workingDirectory);
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(dialog), showButtonName)), command->showButton || isFirst);
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(dialog), showMenuItemName)), command->showMenuItem);
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(dialog), inputCustomTextName)), command->inputCustomText);
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(dialog), parseOutputName)), command->parseOutput);

      isFirst = FALSE;
    }
  }

  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(dialog), "error_indicators"         )), pluginData.configuration.errorIndicators);
  gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(g_object_get_data(G_OBJECT(dialog),   "error_indicator_color"    )), &pluginData.configuration.errorIndicatorColor);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(dialog), "warning_indicators"       )), pluginData.configuration.warningIndicators);
  gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(g_object_get_data(G_OBJECT(dialog),   "warning_indicator_color"  )), &pluginData.configuration.warningIndicatorColor);

  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(dialog), "stop_button"              )), pluginData.configuration.stopButton);
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

  return GTK_WIDGET(vbox);
}

/***********************************************************************\
* Name   : showProjectSettingsTab
* Purpose: show project settings tab
* Input  : data - notebook widget
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL gboolean showProjectSettingsTab(gpointer data)
{
  GtkWidget *notebook = (GtkWidget*)data;

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
*          data     - user data (not used)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

static void onProjectDialogOpen(GObject   *object,
                                GtkWidget *notebook,
                                gpointer  data
                               )
{
  UNUSED_VARIABLE(object);
  UNUSED_VARIABLE(data);

  // init project configuration settings
  pluginData.widgets.projectProperties = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 6));
  gtk_widget_set_margin_top(GTK_WIDGET(pluginData.widgets.projectProperties), 6);
  gtk_widget_set_margin_bottom(GTK_WIDGET(pluginData.widgets.projectProperties), 6);
  {
    GtkFrame *frame;
    GtkGrid  *grid;

    frame = GTK_FRAME(gtk_frame_new("Commands"));
    gtk_frame_set_shadow_type(frame, GTK_SHADOW_IN);
    gtk_frame_set_label_align(frame, .5, 0);
    gtk_widget_set_margin_top(gtk_frame_get_label_widget(frame), 6);
    {
      grid = GTK_GRID(gtk_grid_new());
      gtk_grid_set_row_spacing(grid, 6);
      gtk_grid_set_column_spacing(grid, 12);
      g_object_set(grid, "margin", 6, NULL);
      gtk_widget_set_hexpand(GTK_WIDGET(grid), TRUE);
      {
        gboolean isFirst = TRUE;
        for (guint i = 0; i < MAX_COMMANDS; i++)
        {
          char titleName[32],commandLineName[32],workingDirectoryName[32],showButtonName[32],showMenuItemName[32],inputCustomTextName[32],parseOutputName[32];

          g_snprintf(titleName,sizeof(titleName),"command_title%d",i);
          g_snprintf(commandLineName,sizeof(commandLineName),"command_command_line%d",i);
          g_snprintf(workingDirectoryName,sizeof(workingDirectoryName),"command_working_directory%d",i);
          g_snprintf(showButtonName,sizeof(showButtonName),"command_show_button%d",i);
          g_snprintf(showMenuItemName,sizeof(showMenuItemName),"command_show_menu_item%d",i);
          g_snprintf(inputCustomTextName,sizeof(inputCustomTextName),"command_input_custom_text%d",i);
          g_snprintf(parseOutputName,sizeof(parseOutputName),"command_parse_output%d",i);

          addGrid(grid, i, 0, 1, newEntry(G_OBJECT(pluginData.widgets.projectProperties), titleName, "Command title."));
          addGrid(grid, i, 1, 1, newEntry(G_OBJECT(pluginData.widgets.projectProperties), commandLineName, "Command line."));
          addGrid(grid, i, 2, 1, newWorkingDirectoryChooser(G_OBJECT(pluginData.widgets.projectProperties), workingDirectoryName, "Working directory for command."));
          GtkWidget *button = addGrid(grid, i, 3, 1, newCheckButton(G_OBJECT(pluginData.widgets.projectProperties), showButtonName, NULL, "Show button in toolbar."));
          if (isFirst) gtk_widget_set_sensitive(button,FALSE);
          addGrid(grid, i, 4, 1, newCheckButton(G_OBJECT(pluginData.widgets.projectProperties), showMenuItemName, NULL, "Show menu item."));
          addGrid(grid, i, 5, 1, newCheckButton(G_OBJECT(pluginData.widgets.projectProperties), inputCustomTextName, NULL, "Input custom text."));
          addGrid(grid, i, 6, 1, newCheckButton(G_OBJECT(pluginData.widgets.projectProperties), parseOutputName, NULL, "Parse output for errors/warnings."));

          isFirst = FALSE;
        }
      }
      gtk_container_add(GTK_CONTAINER(frame), GTK_WIDGET(grid));
    }
    addBox(GTK_BOX(pluginData.widgets.projectProperties), FALSE, GTK_WIDGET(frame));

    grid = GTK_GRID(gtk_grid_new());
// TODO: remove
//GdkColor red = {0, 0xffff, 0x0000, 0x0000};
//gtk_widget_modify_bg(grid, GTK_STATE_NORMAL, &red);
    gtk_widget_set_margin_start(GTK_WIDGET(grid), 6);
    gtk_widget_set_margin_end(GTK_WIDGET(grid), 6);
    gtk_grid_set_row_spacing(grid, 6);
    gtk_grid_set_column_spacing(grid, 12);
    gtk_widget_set_hexpand(GTK_WIDGET(grid), TRUE);
    {
      addGrid(grid, 0, 0, 1, newLabel(G_OBJECT(pluginData.widgets.projectProperties), NULL, "Error regular expression", NULL));
      addGrid(grid, 0, 1, 1, newEntry(G_OBJECT(pluginData.widgets.projectProperties), "error_regex", "Regular expression to recognize errors"));

      addGrid(grid, 1, 0, 1, newLabel(G_OBJECT(pluginData.widgets.projectProperties), NULL, "Warning regular expression", NULL));
      addGrid(grid, 1, 1, 1, newEntry(G_OBJECT(pluginData.widgets.projectProperties), "warning_regex", "Regular expression to recognize warnings"));
    }
    addBox(GTK_BOX(pluginData.widgets.projectProperties), FALSE, GTK_WIDGET(grid));
  }
  gtk_widget_show_all(GTK_WIDGET(pluginData.widgets.projectProperties));

  // add project properties tab
  pluginData.widgets.projectPropertiesTabIndex = gtk_notebook_append_page(GTK_NOTEBOOK(notebook), GTK_WIDGET(pluginData.widgets.projectProperties), gtk_label_new("Builder"));

  // set values
  gboolean isFirst = TRUE;
  for (guint i = 0; i < MAX_COMMANDS; i++)
  {
    char titleName[32],commandLineName[32],workingDirectoryName[32],showButtonName[32],showMenuItemName[32],inputCustomTextName[32],parseOutputName[32];

    g_snprintf(titleName,sizeof(titleName),"command_title%d",i);
    g_snprintf(commandLineName,sizeof(commandLineName),"command_command_line%d",i);
    g_snprintf(workingDirectoryName,sizeof(workingDirectoryName),"command_working_directory%d",i);
    g_snprintf(showButtonName,sizeof(showButtonName),"command_show_button%d",i);
    g_snprintf(showMenuItemName,sizeof(showMenuItemName),"command_show_menu_item%d",i);
    g_snprintf(inputCustomTextName,sizeof(inputCustomTextName),"command_input_custom_text%d",i);
    g_snprintf(parseOutputName,sizeof(parseOutputName),"command_parse_output%d",i);

    gtk_entry_set_text(GTK_ENTRY(g_object_get_data(G_OBJECT(pluginData.widgets.projectProperties), titleName)), pluginData.projectProperties.commands[i].title);
    gtk_entry_set_text(GTK_ENTRY(g_object_get_data(G_OBJECT(pluginData.widgets.projectProperties), commandLineName)), pluginData.projectProperties.commands[i].commandLine);
    gtk_entry_set_text(GTK_ENTRY(g_object_get_data(G_OBJECT(pluginData.widgets.projectProperties), workingDirectoryName)), pluginData.projectProperties.commands[i].workingDirectory);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(pluginData.widgets.projectProperties), showButtonName)), pluginData.projectProperties.commands[i].showButton || isFirst);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(pluginData.widgets.projectProperties), showMenuItemName)), pluginData.projectProperties.commands[i].showMenuItem);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(pluginData.widgets.projectProperties), inputCustomTextName)), pluginData.projectProperties.commands[i].inputCustomText);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(pluginData.widgets.projectProperties), parseOutputName)), pluginData.projectProperties.commands[i].parseOutput);

    isFirst = FALSE;
  }

  gtk_entry_set_text(GTK_ENTRY(g_object_get_data(G_OBJECT(pluginData.widgets.projectProperties), "error_regex")), pluginData.projectProperties.errorRegEx->str);
  gtk_entry_set_text(GTK_ENTRY(g_object_get_data(G_OBJECT(pluginData.widgets.projectProperties), "warning_regex")), pluginData.projectProperties.warningRegEx->str);

  if (pluginData.widgets.showProjectPropertiesTab)
  {
    // show projet settings tab
    pluginData.widgets.showProjectPropertiesTab = FALSE;
    g_idle_add(showProjectSettingsTab, GTK_NOTEBOOK(notebook));
  }
}

/***********************************************************************\
* Name   : onProjectDialogConfirmed
* Purpose: handle project properties dialog confirm: save values
* Input  : object   - object (not used)
*          notebook - notebook (not used)
*          data     - user data (not used)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

static void onProjectDialogConfirmed(GObject   *object,
                                     GtkWidget *notebook,
                                     gpointer  data
                                    )
{
  UNUSED_VARIABLE(object);
  UNUSED_VARIABLE(notebook);
  UNUSED_VARIABLE(data);

  // get values
  gboolean isFirst = TRUE;
  for (guint i = 0; i < MAX_COMMANDS; i++)
  {
    char titleName[32],commandLineName[32],workingDirectoryName[32],showButtonName[32],showMenuItemName[32],inputCustomTextName[32],parseOutputName[32];

    g_snprintf(titleName,sizeof(titleName),"command_title%d",i);
    g_snprintf(commandLineName,sizeof(commandLineName),"command_command_line%d",i);
    g_snprintf(workingDirectoryName,sizeof(workingDirectoryName),"command_working_directory%d",i);
    g_snprintf(showButtonName,sizeof(showButtonName),"command_show_button%d",i);
    g_snprintf(showMenuItemName,sizeof(showMenuItemName),"command_show_menu_item%d",i);
    g_snprintf(inputCustomTextName,sizeof(inputCustomTextName),"command_input_custom_text%d",i);
    g_snprintf(parseOutputName,sizeof(parseOutputName),"command_parse_output%d",i);

    setCommand(&pluginData.projectProperties.commands[i],
               gtk_entry_get_text(GTK_ENTRY(g_object_get_data(G_OBJECT(pluginData.widgets.projectProperties), titleName))),
               gtk_entry_get_text(GTK_ENTRY(g_object_get_data(G_OBJECT(pluginData.widgets.projectProperties), commandLineName))),
               gtk_entry_get_text(GTK_ENTRY(g_object_get_data(G_OBJECT(pluginData.widgets.projectProperties), workingDirectoryName))),
               gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(pluginData.widgets.projectProperties), showButtonName))) || isFirst,
               gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(pluginData.widgets.projectProperties), showMenuItemName))),
               gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(pluginData.widgets.projectProperties), inputCustomTextName))),
               gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(pluginData.widgets.projectProperties), parseOutputName)))
              );

    isFirst = FALSE;
  }
  g_string_assign(pluginData.projectProperties.errorRegEx,   gtk_entry_get_text(GTK_ENTRY(g_object_get_data(G_OBJECT(pluginData.widgets.projectProperties), "error_regex"))));
  g_string_assign(pluginData.projectProperties.warningRegEx, gtk_entry_get_text(GTK_ENTRY(g_object_get_data(G_OBJECT(pluginData.widgets.projectProperties), "warning_regex"))));
}

/***********************************************************************\
* Name   : onProjectDialogClose
* Purpose: handle project properties close
* Input  : object   - object (not used)
*          notebook - notebook
*          data     - user data (not used)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

static void onProjectDialogClose(GObject   *object,
                                 GtkWidget *notebook,
                                 gpointer  data
                                )
{
  UNUSED_VARIABLE(object);
  UNUSED_VARIABLE(data);

  // remove project properties tab
  gtk_notebook_remove_page(GTK_NOTEBOOK(notebook), pluginData.widgets.projectPropertiesTabIndex);
}

/***********************************************************************\
* Name   : onProjectOpen
* Purpose: load project cconfiguration
* Input  : object        - object (not used)
*          configuration - project configuration to load values from
*          data          - user data (not used)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onProjectOpen(GObject  *object,
                         GKeyFile *configuration,
                         gpointer data
                        )
{
  UNUSED_VARIABLE(object);
  UNUSED_VARIABLE(data);

  projectConfigurationLoad(configuration);

  // update view
  updateToolbarButtons();
  updateToolbarButtonMenu();
}

/***********************************************************************\
* Name   : onProjectSave
* Purpose: save project configuration
* Input  : object        - object (not used)
*          configuration - project configuration to save values to
*          data          - user data (not used)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void onProjectSave(GObject  *object,
                         GKeyFile *configuration,
                         gpointer data
                        )
{
  UNUSED_VARIABLE(object);
  UNUSED_VARIABLE(data);

  // save values
  projectConfigurationSave(configuration);

  // update view
  updateToolbarButtons();
  updateToolbarButtonMenu();
}

/***********************************************************************\
* Name   : init
* Purpose: plugin init
* Input  : plugin - Geany plugin
*          data   - user data (not used)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL gboolean init(GeanyPlugin *plugin, gpointer data)
{
  UNUSED_VARIABLE(data);

  // init variables
  pluginData.builtInRegExStore                    = gtk_list_store_new(MODEL_REGEX_COUNT,
                                                                       G_TYPE_STRING,
                                                                       G_TYPE_STRING,
                                                                       G_TYPE_INT,
                                                                       G_TYPE_STRING
                                                                      );
  pluginData.configuration.filePath               = g_strconcat(plugin->geany->app->configdir, G_DIR_SEPARATOR_S,
                                                                "plugins", G_DIR_SEPARATOR_S,
                                                                "builder", G_DIR_SEPARATOR_S,
                                                                "builder.conf",
                                                                NULL
                                                               );
  initCommands(pluginData.configuration.commands, MAX_COMMANDS);
  setCommand(&pluginData.configuration.commands[0],"Build",DEFAULT_BUILD_COMMAND,"%p",TRUE,TRUE,FALSE,TRUE);
  setCommand(&pluginData.configuration.commands[1],"Clean",DEFAULT_CLEAN_COMMAND,"%p",TRUE,TRUE,FALSE,FALSE);
  pluginData.configuration.regExStore             = gtk_list_store_new(MODEL_REGEX_COUNT,
                                                                       G_TYPE_STRING,
                                                                       G_TYPE_STRING,
                                                                       G_TYPE_INT,
                                                                       G_TYPE_STRING
                                                                      );
  pluginData.configuration.errorIndicators        = TRUE;
  pluginData.configuration.errorIndicatorColor    = DEFAULT_ERROR_INDICATOR_COLOR;
  pluginData.configuration.warningIndicators      = FALSE;
  pluginData.configuration.warningIndicatorColor  = DEFAULT_WARNING_INDICATOR_COLOR;
  pluginData.configuration.addProjectRegExResults = FALSE;
  pluginData.configuration.stopButton             = TRUE;
  pluginData.configuration.autoSaveAll            = FALSE;
  pluginData.configuration.autoShowFirstError     = FALSE;
  pluginData.configuration.autoShowFirstWarning   = FALSE;
  initCommands(pluginData.projectProperties.commands, MAX_COMMANDS);
  pluginData.projectProperties.errorRegEx         = g_string_new(NULL);
  pluginData.projectProperties.warningRegEx       = g_string_new(NULL);

  for (guint i = 0; i < MAX_COMMANDS; i++)
  {
    pluginData.widgets.menuItems.commands[i] = NULL;
  }
  pluginData.widgets.buttons                      = g_ptr_array_new();
  pluginData.widgets.buttonMenu                   = NULL;
  pluginData.widgets.buttonStop                   = NULL;
  pluginData.widgets.projectProperties            = NULL;
  pluginData.widgets.showProjectPropertiesTab     = FALSE;

  pluginData.build.pid                            = 0;
  pluginData.build.workingDirectory               = g_string_new(NULL);
  pluginData.build.directoryPrefixStack           = string_stack_new();
  pluginData.build.messagesStore = gtk_list_store_new(MODEL_MESSAGE_COUNT,
                                                      G_TYPE_STRING,
                                                      G_TYPE_STRING,
                                                      G_TYPE_STRING,
                                                      G_TYPE_STRING,
                                                      G_TYPE_INT,
                                                      G_TYPE_INT,
                                                      G_TYPE_STRING
                                                     );
  pluginData.build.messagesTreePath               = NULL;
  pluginData.build.errorsStore = gtk_tree_store_new(MODEL_ERROR_WARNING_COUNT,
                                                    G_TYPE_STRING,
                                                    G_TYPE_STRING,
                                                    G_TYPE_STRING,
                                                    G_TYPE_INT,
                                                    G_TYPE_INT,
                                                    G_TYPE_STRING
                                                   );
  pluginData.build.warningsStore = gtk_tree_store_new(MODEL_ERROR_WARNING_COUNT,
                                                      G_TYPE_STRING,
                                                      G_TYPE_STRING,
                                                      G_TYPE_STRING,
                                                      G_TYPE_INT,
                                                      G_TYPE_INT,
                                                      G_TYPE_STRING
                                                     );
  pluginData.build.errorsTreeIterValid            = FALSE;
  pluginData.build.warningsTreeIterValid          = FALSE;
  pluginData.build.lastInsertStore                = NULL;
  pluginData.build.lastInsertTreeIter             = NULL;
  pluginData.build.errorWarningIndicatorsCount    = 0;
  pluginData.build.lastCustomTarget               = g_string_new(NULL);

  for (guint i = 0; i < ARRAY_SIZE(REGEX_BUILTIN); i++)
  {
    gtk_list_store_insert_with_values(pluginData.builtInRegExStore,
                                      NULL,
                                      -1,
                                      MODEL_REGEX_GROUP, REGEX_BUILTIN[i].group,
                                      MODEL_REGEX_TYPE,  REGEX_BUILTIN[i].type,
                                      MODEL_REGEX_REGEX, REGEX_BUILTIN[i].regex,
                                      -1
                                     );
  }

  // load configuration
  configurationLoad();

  // init GUI elements
  initToolbar(plugin);
  initTab(plugin);
  initKeyBinding(plugin);

  return TRUE;
}

/***********************************************************************\
* Name   : cleanup
* Purpose: plugin cleanup
* Input  : plugin - Geany plugin
*          data   - user data (not used)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void cleanup(GeanyPlugin *plugin, gpointer data)
{
  UNUSED_VARIABLE(data);

  doneTab(plugin);
  doneToolbar(plugin);

  g_string_free(pluginData.build.lastCustomTarget, TRUE);
  string_stack_free(pluginData.build.directoryPrefixStack);
  gtk_tree_path_free(pluginData.build.messagesTreePath);
  g_string_free(pluginData.build.workingDirectory, TRUE);
  g_ptr_array_free(pluginData.widgets.buttons,FALSE);
  for (guint i = 0; i < MAX_COMMANDS; i++)
  {
    gtk_widget_destroy(pluginData.widgets.menuItems.commands[i]);
  }
  g_string_free(pluginData.projectProperties.warningRegEx, TRUE);
  g_string_free(pluginData.projectProperties.errorRegEx, TRUE);
  doneCommands(pluginData.projectProperties.commands,MAX_COMMANDS);
  doneCommands(pluginData.configuration.commands,MAX_COMMANDS);
  g_free(pluginData.configuration.filePath);
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
    {"project-dialog-open",      (GCallback) & onProjectDialogOpen, TRUE, NULL},
    {"project-dialog-confirmed", (GCallback) & onProjectDialogConfirmed, TRUE, NULL},
    {"project-dialog-close",     (GCallback) & onProjectDialogClose, TRUE, NULL},
    {"project-open",             (GCallback) & onProjectOpen, TRUE, NULL},
    {"project-save",             (GCallback) & onProjectSave, TRUE, NULL},
    {NULL, NULL, FALSE, NULL}
  };

  g_assert(plugin != NULL);
  g_assert(plugin->info != NULL);
  g_assert(plugin->funcs != NULL);

  plugin->info->name        = "Builder";
  plugin->info->description = "Builder tool";
  plugin->info->version     = "1.0";
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
