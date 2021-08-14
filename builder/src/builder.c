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
LOCAL const gchar *CONFIGURATION_GROUP_BUILD_MENU     = "build-menu";
LOCAL const gchar *CONFIGURATION_GROUP_BUILDER        = "builder";

LOCAL const gchar *KEY_GROUP_BUILDER                  = "builder";

LOCAL const gchar *COLOR_BUILD_INFO                   = "Blue";
LOCAL const gchar *COLOR_BUILD_MESSAGES               = "Black";
LOCAL const gchar *COLOR_BUILD_MESSAGES_MATCHED       = "Green";

LOCAL const gchar *DEFAULT_BUILD_COMMAND              = "make";
LOCAL const gchar *DEFAULT_CLEAN_COMMAND              = "make clean";
LOCAL const gchar *DEFAULT_MAKE_ALL_COMMAND           = "";
LOCAL const gchar *DEFAULT_MAKE_CUSTOM_TARGET_COMMAND = "";
LOCAL const gchar *DEFAULT_MAKE_OBJECT_COMMAND        = "";
LOCAL const gchar *DEFAULT_MAKE_RUN_COMMAND           = "make run";
#if 0
LOCAL const gchar *DEFAULT_REGEX_ENTER                = "^\\s*Entering directory\\s+'(?<directory>[^']*)'\\s*$";
LOCAL const gchar *DEFAULT_REGEX_LEAVE                = "^\\s*Leaving directory\\s+'(?<directory>[^']*)'\\s*$";
LOCAL const gchar *DEFAULT_REGEX_ERROR                = "^(?<filePath>[^:]*):(?<lineNumber>[^:]*):(?<columnNumber>[^:]*):\\s*error:\\s*(?<message>.*)$";
LOCAL const gchar *DEFAULT_REGEX_WARNING              = "^(?<filePath>[^:]*):(?<lineNumber>[^:]*):(?<columnNumber>[^:]*):\\s*warning:\\s*(?<message>.*)$";
#endif

LOCAL const GdkRGBA DEFAULT_ERROR_INDICATOR_COLOR     = {0xFF,0x00,0x00,0xFF};
LOCAL const GdkRGBA DEFAULT_WARNING_INDICATOR_COLOR   = {0x00,0xFF,0x00,0xFF};

LOCAL const guint ERROR_INDICATOR_INDEX               = GEANY_INDICATOR_ERROR;
LOCAL const guint WARNING_INDICATOR_INDEX             = 1;

LOCAL const guint MAX_ERROR_WARNING_INDICATORS        = 16;

/***************************** Datatypes *******************************/

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
//  MODEL_ERROR_WARNING_TREE_PATH,
  MODEL_ERROR_WARNING_FILE_PATH,
  MODEL_ERROR_WARNING_LINE_NUMBER,
  MODEL_ERROR_WARNING_COLUMN_NUMBER,
  MODEL_ERROR_WARNING_MESSAGE,

  MODEL_ERROR_WARNING_COUNT
};

// key short-cuts
typedef enum
{
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
  GString *line;
  GString *workingDirectory;
} Command;

typedef struct
{
  Command build;
  Command makeAll;
  Command makeCustomTarget;
  Command makeObject;
  Command run;
} BuildMenuCommands;

typedef struct
{
  Command build;
  Command clean;
  Command makeAll;
  Command makeCustomTarget;
  Command makeObject;
  Command run;
} Commands;

typedef enum
{
  REGEX_TYPE_MIN,

  REGEX_TYPE_NONE =  REGEX_TYPE_MIN,
  REGEX_TYPE_ENTER,
  REGEX_TYPE_LEAVE,
  REGEX_TYPE_ERROR,
  REGEX_TYPE_WARNING,
  REGEX_TYPE_EXTENSION,

  REGEX_TYPE_MAX = REGEX_TYPE_EXTENSION
} RegExTypes;

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

    Commands     commands;

    GtkListStore *regExStore;

    struct
    {
      gboolean     build;
      gboolean     clean;
      gboolean     run;
      gboolean     stop;
    }            buttons;

    gboolean     errorIndicators;
    GdkRGBA      errorIndicatorColor;
    gboolean     warningIndicators;
    GdkRGBA      warningIndicatorColor;

    gboolean     addProjectRegExResults;
    gboolean     autoSaveAll;
    gboolean     autoShowFirstError;
    gboolean     autoShowFirstWarning;
  } configuration;

  // project specific properties
  struct
  {
    BuildMenuCommands buildMenuCommands;
    Commands          commands;

    GString           *errorRegEx;
    GString           *warningRegEx;
  } projectProperties;

  // widgets
  struct
  {
    struct
    {
      GtkWidget *build;
      GtkWidget *clean;
      GtkWidget *makeAll;
      GtkWidget *makeCustomTarget;
      GtkWidget *makeObject;
      GtkWidget *showPrevError;
      GtkWidget *showNextError;
      GtkWidget *showPrevWarning;
      GtkWidget *showNextWarning;
      GtkWidget *run;
      GtkWidget *stop;
      GtkWidget *projectProperies;
      GtkWidget *configuration;

      GtkWidget *editRegEx;
    } menuItems;

    GtkWidget     *tabs;
    gint          tabIndex;
    GtkToolItem   *buttonBuild;
    GtkToolItem   *buttonClean;
    GtkToolItem   *buttonRun;
    GtkToolItem   *buttonStop;

    GtkBox        *projectProperies;
    gint          projectProperiesTabIndex;
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

/**
 * @brief init command
 *
 * @param [in]  command            command
 * @param [in]  defaultCommandLine default command line
 */

LOCAL void initCommand(Command *command, const gchar *defaultCommandLine)
{
  g_assert(command != NULL);

  command->line             = g_string_new(defaultCommandLine);
  command->workingDirectory = g_string_new(NULL);
}

/**
 * @brief done command
 *
 * @param [in]  command command
 */

LOCAL void doneCommand(Command *command)
{
  g_assert(command != NULL);

  g_string_free(command->workingDirectory, TRUE);
  g_string_free(command->line, TRUE);
}

/**
 * @brief initialize build menu commands
 *
 * @param [in]  buildMenuCommands build menu commands
 */

LOCAL void initBuildMenuCommands(BuildMenuCommands *buildMenuCommands)
{
  g_assert(buildMenuCommands != NULL);

  initCommand(&buildMenuCommands->build, NULL);
  initCommand(&buildMenuCommands->makeAll, NULL);
  initCommand(&buildMenuCommands->makeCustomTarget, NULL);
  initCommand(&buildMenuCommands->makeObject, NULL);
  initCommand(&buildMenuCommands->run, NULL);
}

/**
 * @brief done build menu commands
 *
 * @param [in]  buildMenuCommands build menu commands
 */

LOCAL void doneBuildMenuCommands(BuildMenuCommands *buildMenuCommands)
{
  g_assert(buildMenuCommands != NULL);

  doneCommand(&buildMenuCommands->run);
  doneCommand(&buildMenuCommands->makeObject);
  doneCommand(&buildMenuCommands->makeCustomTarget);
  doneCommand(&buildMenuCommands->makeAll);
  doneCommand(&buildMenuCommands->build);
}

/**
 * @brief initialize commands with default values
 *
 * @param [in]  commands commands
 */

LOCAL void initCommands(Commands *commands)
{
  g_assert(commands != NULL);

  initCommand(&commands->build, DEFAULT_BUILD_COMMAND);
  initCommand(&commands->clean, DEFAULT_CLEAN_COMMAND);
  initCommand(&commands->makeAll, DEFAULT_MAKE_ALL_COMMAND);
  initCommand(&commands->makeCustomTarget, DEFAULT_MAKE_CUSTOM_TARGET_COMMAND);
  initCommand(&commands->makeObject, DEFAULT_MAKE_OBJECT_COMMAND);
  initCommand(&commands->run, DEFAULT_MAKE_RUN_COMMAND);
}

/**
 * @brief done commands
 *
 * @param [in]  commands commands
 */

LOCAL void doneCommands(Commands *commands)
{
  g_assert(commands != NULL);

  doneCommand(&commands->run);
  doneCommand(&commands->makeObject);
  doneCommand(&commands->makeCustomTarget);
  doneCommand(&commands->makeAll);
  doneCommand(&commands->clean);
  doneCommand(&commands->build);
}

/**
 * @brief load configuration boolean
 *
 * @param [in]  boolean       boolean variable
 * @param [in]  configuration configuration to load values from
 * @param [in]  groupName     group name
 * @param [in]  name          value name
 */

LOCAL void confgurationLoadBoolean(gboolean    *boolean,
                                   GKeyFile    *configuration,
                                   const gchar *groupName,
                                   const gchar *name
                                  )
{
  g_assert(boolean != NULL);
  g_assert(configuration != NULL);
  g_assert(name != NULL);

  (*boolean) = g_key_file_get_boolean(configuration, groupName, name, NULL);
}

/**
 * @brief load configuration string
 *
 * @param [in]  string        string variable
 * @param [in]  configuration configuration to load values from
 * @param [in]  groupName     group name
 * @param [in]  name          value name
 */

LOCAL void confgurationLoadString(GString     *string,
                                  GKeyFile    *configuration,
                                  const gchar *groupName,
                                  const gchar *name
                                 )
{
  g_assert(string != NULL);
  g_assert(configuration != NULL);
  g_assert(name != NULL);

  g_string_assign(string, g_key_file_get_string(configuration, groupName, name, NULL));
}

/**
 * @brief load configuration list store
 *
 * @param [in]  listStore     list store variable
 * @param [in]  configuration configuration to load values from
 * @param [in]  groupName     group name
 * @param [in]  name          value name
 */

LOCAL void confgurationLoadListStore(GtkListStore *listStore,
                                     GKeyFile     *configuration,
                                     const gchar  *groupName,
                                     const gchar  *name
                                    )
{
  g_assert(listStore != NULL);
  g_assert(configuration != NULL);
  g_assert(name != NULL);

  gtk_list_store_clear(listStore);
  gchar **stringArray = g_key_file_get_string_list(configuration, groupName, name, NULL, NULL);
  gchar **string;
  foreach_strv(string, stringArray)
  {
    gchar **tokens    = g_strsplit(*string, ":", 4);
    g_assert(tokens != NULL);
    guint tokenLength = g_strv_length(tokens);

    RegExTypes regExType = REGEX_TYPE_NONE;
    if (tokenLength >= 3)
    {
      if      (strcmp(tokens[2], REGEX_TYPE_STRINGS[REGEX_TYPE_ENTER    ]) == 0) regExType = REGEX_TYPE_ENTER;
      else if (strcmp(tokens[2], REGEX_TYPE_STRINGS[REGEX_TYPE_LEAVE    ]) == 0) regExType = REGEX_TYPE_LEAVE;
      else if (strcmp(tokens[2], REGEX_TYPE_STRINGS[REGEX_TYPE_ERROR    ]) == 0) regExType = REGEX_TYPE_ERROR;
      else if (strcmp(tokens[2], REGEX_TYPE_STRINGS[REGEX_TYPE_WARNING  ]) == 0) regExType = REGEX_TYPE_WARNING;
      else if (strcmp(tokens[2], REGEX_TYPE_STRINGS[REGEX_TYPE_EXTENSION]) == 0) regExType = REGEX_TYPE_EXTENSION;
    }

    gtk_list_store_insert_with_values(listStore,
                                      NULL,
                                      -1,
                                      MODEL_REGEX_LANGUAGE, (tokenLength >= 1) ? tokens[0] : "",
                                      MODEL_REGEX_GROUP,    (tokenLength >= 2) ? tokens[1] : "",
                                      MODEL_REGEX_TYPE,     regExType,
                                      MODEL_REGEX_REGEX,    (tokenLength >= 4) ? tokens[3] : "",
                                      -1
                                     );
    g_strfreev(tokens);
  }
  g_strfreev(stringArray);
}

/**
 * @brief load configuration color
 *
 * @param [in]  rgba          color RGBA variable
 * @param [in]  configuration configuration to load values from
 * @param [in]  groupName     group name
 * @param [in]  name          value name
 */

LOCAL void confgurationLoadColor(GdkRGBA     *rgba,
                                 GKeyFile    *configuration,
                                 const gchar *groupName,
                                 const gchar *name
                                )
{
  g_assert(rgba != NULL);
  g_assert(configuration != NULL);
  g_assert(name != NULL);

  GdkRGBA color;
  if (gdk_rgba_parse(&color, g_key_file_get_string(configuration, groupName, name, NULL)))
  {
    (*rgba) = color;
  }
}

/**
 * @brief load configuration
 *
 */

LOCAL void loadConfiguration()
{
  GKeyFile *configuration = g_key_file_new();

  // load configuration
  g_key_file_load_from_file(configuration, pluginData.configuration.filePath, G_KEY_FILE_NONE, NULL);

  // get values
  gchar **buttonArray = g_key_file_get_string_list(configuration, CONFIGURATION_GROUP_BUILDER, "buttons", NULL, NULL);
  pluginData.configuration.buttons.build = FALSE;
  pluginData.configuration.buttons.clean = FALSE;
  pluginData.configuration.buttons.run   = FALSE;
  gchar **button;
  foreach_strv(button, buttonArray)
  {
    if (strcmp(*button, "build") == 0) pluginData.configuration.buttons.build = TRUE;
    if (strcmp(*button, "clean") == 0) pluginData.configuration.buttons.clean = TRUE;
    if (strcmp(*button, "run"  ) == 0) pluginData.configuration.buttons.run   = TRUE;
  }
  g_strfreev(buttonArray);

  confgurationLoadString   (pluginData.configuration.commands.build.line,                        configuration, CONFIGURATION_GROUP_BUILDER, "commandBuild");
  confgurationLoadString   (pluginData.configuration.commands.build.workingDirectory,            configuration, CONFIGURATION_GROUP_BUILDER, "workingDirectoryBuild");
  confgurationLoadString   (pluginData.configuration.commands.clean.line,                        configuration, CONFIGURATION_GROUP_BUILDER, "commandClean");
  confgurationLoadString   (pluginData.configuration.commands.clean.workingDirectory,            configuration, CONFIGURATION_GROUP_BUILDER, "workingDirectoryClean");
  confgurationLoadString   (pluginData.configuration.commands.makeAll.line,                      configuration, CONFIGURATION_GROUP_BUILDER, "commandMakeAll");
  confgurationLoadString   (pluginData.configuration.commands.makeAll.workingDirectory,          configuration, CONFIGURATION_GROUP_BUILDER, "workingDirectoryMakeAll");
  confgurationLoadString   (pluginData.configuration.commands.makeCustomTarget.line,             configuration, CONFIGURATION_GROUP_BUILDER, "commandMakeCustomTarget");
  confgurationLoadString   (pluginData.configuration.commands.makeCustomTarget.workingDirectory, configuration, CONFIGURATION_GROUP_BUILDER, "workingDirectoryMakeCustomTarget");
  confgurationLoadString   (pluginData.configuration.commands.makeObject.line,                   configuration, CONFIGURATION_GROUP_BUILDER, "commandMakeObject");
  confgurationLoadString   (pluginData.configuration.commands.makeObject.workingDirectory,       configuration, CONFIGURATION_GROUP_BUILDER, "workingDirectoryMakeObject");

  confgurationLoadListStore(pluginData.configuration.regExStore,                                 configuration, CONFIGURATION_GROUP_BUILDER, "regExs");

  confgurationLoadBoolean  (&pluginData.configuration.errorIndicators,                           configuration, CONFIGURATION_GROUP_BUILDER, "errorIndicators");
  confgurationLoadColor    (&pluginData.configuration.errorIndicatorColor,                       configuration, CONFIGURATION_GROUP_BUILDER, "errorIndicatorColor");
  confgurationLoadBoolean  (&pluginData.configuration.warningIndicators,                         configuration, CONFIGURATION_GROUP_BUILDER, "warningIndicators");
  confgurationLoadColor    (&pluginData.configuration.warningIndicatorColor,                     configuration, CONFIGURATION_GROUP_BUILDER, "warningIndicatorColor");

  confgurationLoadBoolean  (&pluginData.configuration.addProjectRegExResults,                    configuration, CONFIGURATION_GROUP_BUILDER, "addProjectRegExResults");
  confgurationLoadBoolean  (&pluginData.configuration.autoSaveAll,                               configuration, CONFIGURATION_GROUP_BUILDER, "autoSaveAll");
  confgurationLoadBoolean  (&pluginData.configuration.autoShowFirstError,                        configuration, CONFIGURATION_GROUP_BUILDER, "autoShowFirstError");
  confgurationLoadBoolean  (&pluginData.configuration.autoShowFirstWarning,                      configuration, CONFIGURATION_GROUP_BUILDER, "autoShowFirstWarning");

  // free resources
  g_key_file_free(configuration);
}

/**
 * @brief save list store into configuraiton
 *
 * @param [in]  treeModel     list store model
 * @param [in]  configuration configuration
 * @param [in]  groupName     configuration group name
 * @param [in]  name          name
 */

LOCAL void configurationSaveListStore(GtkTreeModel *treeModel,
                                      GKeyFile     *configuration,
                                      const gchar  *groupName,
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
                             groupName,
                             name,
                             (const gchar**)regexArray->pdata,
                             regexArray->len
                            );

  // free resources
  g_ptr_array_free(regexArray, TRUE);
}

/**
 * @brief save color into configuration
 *
 * @param [in]  color         color to save
 * @param [in]  configuration configuration
 * @param [in]  groupName     configuration group name
 * @param [in]  name          name
 */

LOCAL void configurationSaveColor(const GdkRGBA *color,
                                  GKeyFile      *configuration,
                                  const gchar   *groupName,
                                  const gchar   *name
                                 )
{
  gchar *colorString = gdk_rgba_to_string(color);

  g_key_file_set_string(configuration, groupName, name, colorString);

  g_free(colorString);
}

/**
 * @brief save configuration
 *
 */

LOCAL void configurationSave()
{
  GKeyFile *configuration = g_key_file_new();

  GPtrArray *buttonArray = g_ptr_array_new_with_free_func(g_free);
  if (pluginData.configuration.buttons.build) g_ptr_array_add(buttonArray, (gpointer)"build");
  if (pluginData.configuration.buttons.clean) g_ptr_array_add(buttonArray, (gpointer)"clean");
  if (pluginData.configuration.buttons.run  ) g_ptr_array_add(buttonArray, (gpointer)"run");
  g_key_file_set_string_list(configuration,
                             CONFIGURATION_GROUP_BUILDER,
                             "buttons",
                             (const gchar**)buttonArray->pdata,
                             buttonArray->len
                            );
  g_ptr_array_free(buttonArray, FALSE);

  g_key_file_set_string (configuration,     CONFIGURATION_GROUP_BUILDER, "commandBuild",                     pluginData.configuration.commands.build.line->str);
  g_key_file_set_string (configuration,     CONFIGURATION_GROUP_BUILDER, "workingDirectoryBuild",            pluginData.configuration.commands.build.workingDirectory->str);
  g_key_file_set_string (configuration,     CONFIGURATION_GROUP_BUILDER, "commandClean",                     pluginData.configuration.commands.clean.line->str);
  g_key_file_set_string (configuration,     CONFIGURATION_GROUP_BUILDER, "workingDirectoryClean",            pluginData.configuration.commands.clean.workingDirectory->str);
  g_key_file_set_string (configuration,     CONFIGURATION_GROUP_BUILDER, "commandMakeAll",                   pluginData.configuration.commands.makeAll.line->str);
  g_key_file_set_string (configuration,     CONFIGURATION_GROUP_BUILDER, "workingDirectoryMakeAll",          pluginData.configuration.commands.makeAll.workingDirectory->str);
  g_key_file_set_string (configuration,     CONFIGURATION_GROUP_BUILDER, "commandMakeCustomTarget",          pluginData.configuration.commands.makeCustomTarget.line->str);
  g_key_file_set_string (configuration,     CONFIGURATION_GROUP_BUILDER, "workingDirectoryMakeCustomTarget", pluginData.configuration.commands.makeCustomTarget.workingDirectory->str);
  g_key_file_set_string (configuration,     CONFIGURATION_GROUP_BUILDER, "commandMakeObject",                pluginData.configuration.commands.makeObject.line->str);
  g_key_file_set_string (configuration,     CONFIGURATION_GROUP_BUILDER, "workingDirectoryMakeObject",       pluginData.configuration.commands.makeObject.workingDirectory->str);

  configurationSaveListStore(GTK_TREE_MODEL(pluginData.configuration.regExStore), configuration, CONFIGURATION_GROUP_BUILDER, "regExs");

  g_key_file_set_boolean(configuration,     CONFIGURATION_GROUP_BUILDER, "errorIndicators",pluginData.configuration.errorIndicators);
  configurationSaveColor(&pluginData.configuration.errorIndicatorColor,   configuration, CONFIGURATION_GROUP_BUILDER, "errorIndicatorColor");
  g_key_file_set_boolean(configuration,     CONFIGURATION_GROUP_BUILDER, "warningIndicators",pluginData.configuration.warningIndicators);
  configurationSaveColor(&pluginData.configuration.warningIndicatorColor, configuration, CONFIGURATION_GROUP_BUILDER, "warningIndicatorColor");

  g_key_file_set_boolean(configuration,     CONFIGURATION_GROUP_BUILDER, "addProjectRegExResults",pluginData.configuration.addProjectRegExResults);
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

/**
 * @brief load project specific configuration
 *
 * @param [in]  configuration configuration to load project values from
 */

LOCAL void loadProjectConfiguration(GKeyFile *configuration)
{
  g_assert(configuration != NULL);

  // load configuration values
  confgurationLoadString(pluginData.projectProperties.commands.build.line,                                 configuration, CONFIGURATION_GROUP_BUILDER,    "commandBuild");
  confgurationLoadString(pluginData.projectProperties.commands.build.workingDirectory,                     configuration, CONFIGURATION_GROUP_BUILDER,    "workingDirectoryBuild");
  confgurationLoadString(pluginData.projectProperties.commands.clean.line,                                 configuration, CONFIGURATION_GROUP_BUILDER,    "commandClean");
  confgurationLoadString(pluginData.projectProperties.commands.clean.workingDirectory,                     configuration, CONFIGURATION_GROUP_BUILDER,    "workingDirectoryClean");
  confgurationLoadString(pluginData.projectProperties.commands.makeAll.line,                               configuration, CONFIGURATION_GROUP_BUILDER,    "commandMakeAll");
  confgurationLoadString(pluginData.projectProperties.commands.makeAll.workingDirectory,                   configuration, CONFIGURATION_GROUP_BUILDER,    "workingDirectoryMakeAll");
  confgurationLoadString(pluginData.projectProperties.commands.makeCustomTarget.line,                      configuration, CONFIGURATION_GROUP_BUILDER,    "commandMakeCustomTarget");
  confgurationLoadString(pluginData.projectProperties.commands.makeCustomTarget.workingDirectory,          configuration, CONFIGURATION_GROUP_BUILDER,    "workingDirectoryMakeCustomTarget");
  confgurationLoadString(pluginData.projectProperties.commands.makeObject.line,                            configuration, CONFIGURATION_GROUP_BUILDER,    "commandMakeObject");
  confgurationLoadString(pluginData.projectProperties.commands.makeObject.workingDirectory,                configuration, CONFIGURATION_GROUP_BUILDER,    "workingDirectoryMakeObject");

  confgurationLoadString(pluginData.projectProperties.errorRegEx,                                          configuration, CONFIGURATION_GROUP_BUILDER,    "errorRegEx");
  confgurationLoadString(pluginData.projectProperties.warningRegEx,                                        configuration, CONFIGURATION_GROUP_BUILDER,    "warningRegEx");

  confgurationLoadString(pluginData.projectProperties.buildMenuCommands.build.line,                        configuration, CONFIGURATION_GROUP_BUILD_MENU, "CFT_01_CM");
  confgurationLoadString(pluginData.projectProperties.buildMenuCommands.build.workingDirectory,            configuration, CONFIGURATION_GROUP_BUILD_MENU, "CFT_01_WD");
  confgurationLoadString(pluginData.projectProperties.buildMenuCommands.makeAll.line,                      configuration, CONFIGURATION_GROUP_BUILD_MENU, "NF_00_CM");
  confgurationLoadString(pluginData.projectProperties.buildMenuCommands.makeAll.workingDirectory,          configuration, CONFIGURATION_GROUP_BUILD_MENU, "NF_00_WD");
  confgurationLoadString(pluginData.projectProperties.buildMenuCommands.makeCustomTarget.line,             configuration, CONFIGURATION_GROUP_BUILD_MENU, "NF_01_CM");
  confgurationLoadString(pluginData.projectProperties.buildMenuCommands.makeCustomTarget.workingDirectory, configuration, CONFIGURATION_GROUP_BUILD_MENU, "NF_01_WD");
  confgurationLoadString(pluginData.projectProperties.buildMenuCommands.makeObject.line,                   configuration, CONFIGURATION_GROUP_BUILD_MENU, "NF_02_CM");
  confgurationLoadString(pluginData.projectProperties.buildMenuCommands.makeObject.workingDirectory,       configuration, CONFIGURATION_GROUP_BUILD_MENU, "NF_02_WD");
  confgurationLoadString(pluginData.projectProperties.buildMenuCommands.run.line,                          configuration, CONFIGURATION_GROUP_BUILD_MENU, "EX_00_CM");
  confgurationLoadString(pluginData.projectProperties.buildMenuCommands.run.workingDirectory,              configuration, CONFIGURATION_GROUP_BUILD_MENU, "EX_00_WD");
}

/**
 * @brief save project specific configuration
 *
 * @param [in]  configuration configuration to save project values to
 */

LOCAL void saveProjectConfiguration(GKeyFile *configuration)
{
  g_assert(configuration != NULL);

  // save configuration values
  g_key_file_set_string(configuration, CONFIGURATION_GROUP_BUILDER, "commandBuild",                     pluginData.projectProperties.commands.build.line->str);
  g_key_file_set_string(configuration, CONFIGURATION_GROUP_BUILDER, "workingDirectoryBuild",            pluginData.projectProperties.commands.build.workingDirectory->str);
  g_key_file_set_string(configuration, CONFIGURATION_GROUP_BUILDER, "commandClean",                     pluginData.projectProperties.commands.clean.line->str);
  g_key_file_set_string(configuration, CONFIGURATION_GROUP_BUILDER, "workingDirectoryClean",            pluginData.projectProperties.commands.clean.workingDirectory->str);
  g_key_file_set_string(configuration, CONFIGURATION_GROUP_BUILDER, "commandMakeAll",                   pluginData.projectProperties.commands.makeAll.line->str);
  g_key_file_set_string(configuration, CONFIGURATION_GROUP_BUILDER, "workingDirectoryMakeAll",          pluginData.projectProperties.commands.makeAll.workingDirectory->str);
  g_key_file_set_string(configuration, CONFIGURATION_GROUP_BUILDER, "commandMakeCustomTarget",          pluginData.projectProperties.commands.makeCustomTarget.line->str);
  g_key_file_set_string(configuration, CONFIGURATION_GROUP_BUILDER, "workingDirectoryMakeCustomTarget", pluginData.projectProperties.commands.makeCustomTarget.workingDirectory->str);
  g_key_file_set_string(configuration, CONFIGURATION_GROUP_BUILDER, "commandMakeObject",                pluginData.projectProperties.commands.makeObject.line->str);
  g_key_file_set_string(configuration, CONFIGURATION_GROUP_BUILDER, "workingDirectoryMakeObject",       pluginData.projectProperties.commands.makeObject.workingDirectory->str);

  g_key_file_set_string(configuration, CONFIGURATION_GROUP_BUILDER, "errorRegEx",                       pluginData.projectProperties.errorRegEx->str);
  g_key_file_set_string(configuration, CONFIGURATION_GROUP_BUILDER, "warningRegEx",                     pluginData.projectProperties.warningRegEx->str);
}

// ---------------------------------------------------------------------

/**
 * @brief enable/disable toolbar stop button
 *
 * @param [in]  enabled TRUE to enable
 */

LOCAL void setEnableStop(gboolean enabled)
{
  gtk_widget_set_sensitive(GTK_WIDGET(pluginData.widgets.buttonStop), enabled);
  gtk_widget_set_sensitive(GTK_WIDGET(pluginData.widgets.menuItems.stop), enabled);
}

/**
 * @brief enable/disable toolbar buttons
 *
 * @param [in]  enabled TRUE to enable
 */

LOCAL void setEnableToolbar(gboolean enabled)
{
  gtk_widget_set_sensitive(GTK_WIDGET(pluginData.widgets.buttonBuild), enabled);
  gtk_widget_set_sensitive(GTK_WIDGET(pluginData.widgets.buttonClean), enabled);
  gtk_widget_set_sensitive(GTK_WIDGET(pluginData.widgets.buttonRun), enabled);
  setEnableStop(!enabled);
}

/**
 * @brief clear all messages and indicators
 *
 */

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
  pluginData.build.errorsTreeIterValid             = FALSE;
  pluginData.build.warningsTreeIterValid           = FALSE;
  pluginData.build.lastInsertStore             = NULL;
  pluginData.build.lastInsertTreeIter              = NULL;
  pluginData.build.errorWarningIndicatorsCount = 0;

  gtk_label_set_text(GTK_LABEL(pluginData.widgets.errorsTabLabel), "Errors");
  gtk_label_set_text(GTK_LABEL(pluginData.widgets.warningsTabLabel), "Warnings");
}

/**
 * @brief show build messages tab
 *
 */

LOCAL void showBuildMessagesTab()
{
  gtk_notebook_set_current_page(GTK_NOTEBOOK(ui_lookup_widget(geany_data->main_widgets->window, "notebook_info")),
                                pluginData.widgets.tabIndex
                               );
  gtk_notebook_set_current_page(GTK_NOTEBOOK(pluginData.widgets.tabs),
                                pluginData.widgets.messagesTabIndex
                               );
}

/**
 * @brief show build errors tab
 *
 */

LOCAL void showBuildErrorsTab()
{
  gtk_notebook_set_current_page(GTK_NOTEBOOK(ui_lookup_widget(geany_data->main_widgets->window, "notebook_info")),
                                pluginData.widgets.tabIndex
                               );
  gtk_notebook_set_current_page(GTK_NOTEBOOK(pluginData.widgets.tabs),
                                pluginData.widgets.errorsTabIndex
                               );
}

/**
 * @brief show build warnings tab
 *
 */

LOCAL void showBuildWarningsTab()
{
  gtk_notebook_set_current_page(GTK_NOTEBOOK(ui_lookup_widget(geany_data->main_widgets->window, "notebook_info")),
                                pluginData.widgets.tabIndex
                               );
  gtk_notebook_set_current_page(GTK_NOTEBOOK(pluginData.widgets.tabs),
                                pluginData.widgets.warningsTabIndex
                               );
}

/**
 * @brief render combo box column
 *
 * @param [in]  cellLayout   cell layout
 * @param [in]  cellRenderer cell renderer
 * @param [in]  treeModel    tree data model
 * @param [in]  treeIter     tree entry iterator
 * @param [in]  data         user data (not used)
 */

LOCAL void  inputRegDialogCellRenderer(GtkCellLayout   *cellLayout,
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

/**
 * @brief update input regular expression dialog
 *
 * @param [in]  widgetLanguage     language widget
 * @param [in]  widgetGroup        group widget
 * @param [in]  widgetRegex        regular expression entry widget
 * @param [in]  widgetSample       sample entry widget
 * @param [in]  widgetFilePath     view file path entry widget
 * @param [in]  widgetLineNumber   view line number entry widget
 * @param [in]  widgetColumnNumber view column number entry widget
 * @param [in]  widgetMessage      view message entry widget
 * @param [in]  widgetOK           OK button widget
 */

LOCAL void inputRegexDialogUpdate(GtkWidget *widgetLanguage,
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
    GRegex      *regexCompiled;
    GMatchInfo  *matchInfo;
    regexCompiled = g_regex_new(gtk_entry_get_text(GTK_ENTRY(widgetRegex)),
                                0, // compile_optipns
                                0, // match option
                                NULL // error
                               );
    if (regexCompiled != NULL)
    {
      gtk_widget_set_sensitive(widgetOK, TRUE);

      if (g_regex_match(regexCompiled, gtk_entry_get_text(GTK_ENTRY(widgetSample)), 0, &matchInfo))
      {
        gint filePathMatchNumber         = g_regex_get_string_number(regexCompiled, "filePath");
        gint fileLineNumberMatchNumber   = g_regex_get_string_number(regexCompiled, "lineNumber");
        gint fileColumnNumberMatchNumber = g_regex_get_string_number(regexCompiled, "columnNumber");
        gint fileMessageMatchNumber      = g_regex_get_string_number(regexCompiled, "message");

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
      g_regex_unref(regexCompiled);
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

/**
 * @brief input regular expression changed callback
 *
 * @param [in]  entry entry
 * @param [in]  data  ok-button widget
 */

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

  inputRegexDialogUpdate(widgetLanguage,
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

/**
 * @brief validate group input: allow letters, digits, and _
 *
 * @param [in]  value new value to insert
 * @param [in]  data  user data (not used)
 * @return      TRUE iff input valid
 */

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

/**
 * @brief called on input regular expression group insert text
 *
 * @param [in]  entry    entry widget
 * @param [in]  text     text
 * @param [in]  length   length
 * @param [in]  position position
 * @param [in]  data     user data
 */

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

/**
 * @brief called on group changes
 *
 */

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

    gchar      *group;
    RegExTypes type;
    gchar      *regex;
    gtk_tree_model_get(treeModel,
                       &treeIter,
                       MODEL_REGEX_GROUP, &group,
                       MODEL_REGEX_TYPE,  &type,
                       MODEL_REGEX_REGEX, &regex,
                       -1
                      );
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radioEnter),     type == REGEX_TYPE_ENTER    );
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radioLeave),     type == REGEX_TYPE_LEAVE    );
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radioError),     type == REGEX_TYPE_ERROR    );
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radioWarning),   type == REGEX_TYPE_WARNING  );
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radioExtension), type == REGEX_TYPE_EXTENSION);
    gtk_entry_set_text(GTK_ENTRY(widgetRegex), regex);
    g_free(regex);
    g_free(group);

    inputRegexDialogUpdate(widgetLanguage,
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

/**
 * @brief update group store
 *
 * @param [in]  language    langauge or NULL
 * @param [in]  widgetGroup group widget
 * @param [in]  group       group or NULL
 * @return      active index
 */

LOCAL gint updateGroupStore(const gchar *language, GtkWidget *widgetGroup, const gchar *group)
{
  gint index = 0;

  gtk_list_store_clear(pluginData.builtInRegExStore);
  for (guint i = 0; i < ARRAY_SIZE(REGEX_BUILTIN); i++)
  {
    if (   EMPTY(language)
        || (strcmp(REGEX_BUILTIN[i].language, language) == 0)
        || (strcmp(REGEX_BUILTIN[i].language, "*") == 0)
       )
    {
      gtk_list_store_insert_with_values(pluginData.builtInRegExStore,
                                        NULL,
                                        -1,
                                        MODEL_REGEX_GROUP, REGEX_BUILTIN[i].group,
                                        MODEL_REGEX_TYPE,  REGEX_BUILTIN[i].type,
                                        MODEL_REGEX_REGEX, REGEX_BUILTIN[i].regex,
                                        -1
                                       );
      if (index < 0) index = (gint)i;
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

/**
 * @brief called on language changes
 *
 */

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
  updateGroupStore(language, widgetGroup, group);
  g_free(group);
  g_free(language);

  inputRegexDialogUpdate(widgetLanguage,
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

/**
 * @brief input regular expressiopn dialog
 *
 * @param [in]  parentWindow parent window
 * @param [in]  title        dialog title
 * @param [in]  text         dialog entry text
 * @param [in]  tooltipText  dialog tooltip text
 * @param [out] languageString language name
 * @param [out] groupString    group text
 * @param [out] regExType      regex type
 * @param [out] regExString    regular expression string
 * @return      TRUE on "ok", FALSE otherwise
 */

LOCAL gboolean inputRegexDialog(GtkWindow   *parentWindow,
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
        gint i = 0;
        const GSList *node;
        foreach_slist(node, filetypes_get_sorted_by_name())
        {
          GeanyFiletype *fileType = (GeanyFiletype*)node->data;
          gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(widgetLanguage), fileType->name);
          if (strcmp(languageString->str, fileType->name) == 0)
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
        gtk_combo_box_set_entry_text_column(GTK_COMBO_BOX(widgetGroup), MODEL_REGEX_GROUP);
        updateGroupStore(languageString->str, widgetGroup, groupString->str);

        GtkCellRenderer *cellRenderer;
        cellRenderer = gtk_cell_renderer_combo_new();
        gtk_cell_layout_set_cell_data_func(GTK_CELL_LAYOUT(widgetGroup), cellRenderer, inputRegDialogCellRenderer, NULL, NULL);
        gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(widgetGroup), cellRenderer, FALSE);
        gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(widgetGroup), cellRenderer, "text", MODEL_REGEX_TYPE, NULL);
        cellRenderer = gtk_cell_renderer_combo_new();
        gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(widgetGroup), cellRenderer, FALSE);
        gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(widgetGroup), cellRenderer, "text", MODEL_REGEX_REGEX, NULL);

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
      widgetRegex = addGrid(grid, 2, 1, 2, newEntry(G_OBJECT(dialog), "entry_regex",
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

  // initial update
  inputRegexDialogUpdate(widgetLanguage,
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

/**
 * @brief add regular expression
 *
 * @param [in]  sample sample for regex-match or ""
 */

LOCAL void addRegEx(const gchar *sample)
{
  GString    *languageString = g_string_new(NULL);
  GString    *groupString    = g_string_new(NULL);
  RegExTypes regExType       = REGEX_TYPE_NONE;
  GString    *regexString    = g_string_new(NULL);
  if (inputRegexDialog(GTK_WINDOW(geany_data->main_widgets->window),
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

/**
 * @brief clone regular expression
 *
 * @param [in]  sample sample for regex-match or ""
 */

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
  if (inputRegexDialog(GTK_WINDOW(geany_data->main_widgets->window),
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

/**
 * @brief add regular expression
 *
 * @param [in]  treeModel model
 * @param [in]  treeIter  iterator in model
 * @param [in]  sample    sample for regex-match or ""
 */

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
  if (inputRegexDialog(GTK_WINDOW(geany_data->main_widgets->window),
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

/**
 * @brief show source, load document if not already loaded
 *
 * @param [in]  directory               directory
 * @param [in]  filePath                file path
 * @param [in]  lineNumber,columnNumber line/column number
 */

LOCAL void showSource(const gchar *directory,
                      const gchar *filePath,
                      gint        lineNumber,
                      gint        columnNumber
                     )
{
  g_assert(filePath != NULL);

  // get absolute path
  gchar *absoluteFilePath;
  if (g_path_is_absolute(filePath) || (directory == NULL))
  {
    absoluteFilePath = g_strdup(filePath);
  }
  else
  {
    absoluteFilePath = g_strconcat(directory, G_DIR_SEPARATOR_S, filePath, NULL);
  }
  gchar *absoluteFilePathLocale = utils_get_locale_from_utf8(absoluteFilePath);

  // find/open document
  GeanyDocument *document = document_find_by_filename(filePath);
  if (document == NULL) document = document_open_file(absoluteFilePathLocale, FALSE, NULL, NULL);
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

/**
 * @brief show orevious error
 *
 */

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

/**
 * @brief show next error
 *
 */

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
                     MODEL_ERROR_WARNING_FILE_PATH,      &filePath,
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

/**
 * @brief show previous warning
 *
 */

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

/**
 * @brief show next warning
 *
 */

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

/**
 * @brief check if regular expression match to line
 *
 * @param [in]  regExString regular expression string
 * @param [in]  line        line
 * @param [out] filePath     file path
 * @param [out] lineNumber   line number
 * @param [out] columnNumber column number
 * @param [out] message      message
 * @return      TRUE iff reqular expression matches line
 */

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

/**
 * @brief check if regular expression from model match to line
 *
 * @param [in]  model tree data model
 * @param [in]  line  line
 * @param [out] treeModel           model with regular expressions
 * @param [out] regExTypeFilter     regular expression types filter
 * @param [out] line                compiler output line
 * @param [out] matchTreePathString tree path string on match
 * @param [out] filePathString      file path on match
 * @param [out] lineNumber          line number on match
 * @param [out] columnNumber        column number on match
 * @param [out] messageString       message on match
 * @return      TRUE iff at least one reqular expression matches line
 */

LOCAL gboolean isMatchingRegExs(GtkTreeModel *treeModel,
                                RegExTypes   regExTypeFilter,
                                const gchar  *line,
                                GString      *matchTreePathString,
                                GString      *filePathString,
                                guint        *lineNumber,
                                guint        *columnNumber,
                                GString      *messageString
                               )
{
  gboolean    matchesRegEx = FALSE;

  g_assert(treeModel != NULL);
  g_assert(line != NULL);
  g_assert(filePathString != NULL);

  GtkTreeIter treeIter;
  if (gtk_tree_model_get_iter_first(treeModel, &treeIter))
  {
    do
    {
      RegExTypes regExType;
      gchar      *regEx;
      gtk_tree_model_get(treeModel,
                         &treeIter,
                         MODEL_REGEX_TYPE,  &regExType,
                         MODEL_REGEX_REGEX, &regEx,
                         -1
                        );
      g_assert(regExType >= REGEX_TYPE_MIN);
      g_assert(regExType <= REGEX_TYPE_MAX);
      g_assert(regEx != NULL);

      if (regExType == regExTypeFilter)
      {
        GRegex     *regexCompiled;
        GMatchInfo *matchInfo;
        regexCompiled = g_regex_new(regEx,
                                    0, // compile_optipns
                                    0, // match option
                                    NULL // error
                                   );
        if (regexCompiled != NULL)
        {
          matchesRegEx = g_regex_match(regexCompiled, line, 0, &matchInfo);
          if (matchesRegEx)
          {
            gint directoryMatchNumber        = g_regex_get_string_number(regexCompiled, "directory");
            gint filePathMatchNumber         = g_regex_get_string_number(regexCompiled, "filePath");
            gint fileLineNumberMatchNumber   = g_regex_get_string_number(regexCompiled, "lineNumber");
            gint fileColumnNumberMatchNumber = g_regex_get_string_number(regexCompiled, "columnNumber");
            gint fileMessageMatchNumber      = g_regex_get_string_number(regexCompiled, "message");
            if (filePathMatchNumber         == -1) filePathMatchNumber         = 1;
            if (fileLineNumberMatchNumber   == -1) fileLineNumberMatchNumber   = 2;
            if (fileColumnNumberMatchNumber == -1) fileColumnNumberMatchNumber = 3;
            if (fileMessageMatchNumber      == -1) fileMessageMatchNumber      = 4;


            gchar *matchTreePath = gtk_tree_model_get_string_from_iter(treeModel, &treeIter);
            g_string_assign(matchTreePathString, matchTreePath);
            g_free(matchTreePath);

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
          }
          g_match_info_free(matchInfo);
          g_regex_unref(regexCompiled);
        }

        g_free(regEx);
      }
    }
    while (   !matchesRegEx
           && gtk_tree_model_iter_next(treeModel, &treeIter)
          );
  }

  return matchesRegEx;
}

/**
 * @brief handle stderr
 *
 * @param [in]  line        line
 * @param [in]  ioCondition i/o condition set
 * @param [in]  data        user data (not used)
 */

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
    GString     *matchTreePathString = g_string_new(NULL);
    GString     *filePathString      = g_string_new(NULL);
    guint       lineNumber, columnNumber;
    GString     *messageString       = g_string_new(NULL);
    if      (isMatchingRegExs(GTK_TREE_MODEL(pluginData.configuration.regExStore),
                              REGEX_TYPE_ENTER,
                              line->str,
                              matchTreePathString,
                              filePathString,
                              NULL,
                              NULL,
                              NULL
                             )
            )
    {
//fprintf(stderr,"%s:%d: prefoix=%s\n",__FILE__,__LINE__,filePath->str);
      // get directory prefix
      string_stack_push(pluginData.build.directoryPrefixStack , filePathString->str);

      matched = TRUE;
    }
    else if (isMatchingRegExs(GTK_TREE_MODEL(pluginData.configuration.regExStore),
                              REGEX_TYPE_LEAVE,
                              line->str,
                              matchTreePathString,
                              filePathString,
                              NULL,
                              NULL,
                              NULL
                             )
            )
    {
      // clear directory prefix
      string_stack_pop(pluginData.build.directoryPrefixStack);

      matched = TRUE;
    }
    else if (   (   (pluginData.projectProperties.errorRegEx->len > 0)
                 && isMatchingRegEx(pluginData.projectProperties.errorRegEx->str,
                                    line->str,
                                    filePathString,
                                    &lineNumber,
                                    &columnNumber,
                                    messageString
                                   )
                )
             || (   pluginData.configuration.addProjectRegExResults
                 && isMatchingRegExs(GTK_TREE_MODEL(pluginData.configuration.regExStore),
                                     REGEX_TYPE_ERROR,
                                     line->str,
                                     matchTreePathString,
                                     filePathString,
                                     &lineNumber,
                                     &columnNumber,
                                     messageString
                                    )
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
      pluginData.build.lastInsertStore = pluginData.build.errorsStore;
      pluginData.build.lastInsertTreeIter  = &pluginData.build.insertIter;

      matched = TRUE;
    }
    else if (   (   (pluginData.projectProperties.warningRegEx->len > 0)
                 && isMatchingRegEx(pluginData.projectProperties.warningRegEx->str,
                                    line->str,
                                    filePathString,
                                    &lineNumber,
                                    &columnNumber,
                                    messageString
                                   )
                )
             || (   pluginData.configuration.addProjectRegExResults
                 && isMatchingRegExs(GTK_TREE_MODEL(pluginData.configuration.regExStore),
                                     REGEX_TYPE_WARNING,
                                     line->str,
                                     matchTreePathString,
                                     filePathString,
                                     &lineNumber,
                                     &columnNumber,
                                     messageString
                                    )
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
      pluginData.build.lastInsertStore = pluginData.build.warningsStore;
      pluginData.build.lastInsertTreeIter  = &pluginData.build.insertIter;

      matched = TRUE;
    }
    else
    {
      // append to last error/warning message
      if (   (pluginData.build.lastInsertStore != NULL)
          && (pluginData.build.lastInsertTreeIter != NULL)
         )
      {
        if (isMatchingRegExs(GTK_TREE_MODEL(pluginData.configuration.regExStore),
                             REGEX_TYPE_EXTENSION,
                             line->str,
                             matchTreePathString,
                             filePathString,
                             &lineNumber,
                             &columnNumber,
                             messageString
                            )
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

          matched = TRUE;
        }
        else
        {
          gtk_tree_store_insert_with_values(pluginData.build.lastInsertStore,
                                            NULL,
                                            pluginData.build.lastInsertTreeIter,
                                            -1,
                                            MODEL_ERROR_WARNING_MESSAGE, line->str,
                                            -1
                                           );
        }
      }
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

    // free resources
    g_string_free(string, TRUE);
  }
}

/**
 * @brief handle exit of execute process
 *
 * @param [in]  pid    process id (not used)
 * @param [in]  status process exit status
 * @param [in]  data   user data (not used)
 */

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
                                    MODEL_ERROR_WARNING_COLOR,   COLOR_BUILD_INFO,
                                    MODEL_ERROR_WARNING_MESSAGE, doneMessage,
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

  // show first error/warning
  if      (pluginData.configuration.autoShowFirstError && pluginData.build.errorsTreeIterValid)
  {
    showBuildErrorsTab();
    showNextError();
  }
  else if (pluginData.configuration.autoShowFirstWarning && pluginData.build.warningsTreeIterValid)
  {
    showBuildWarningsTab();
    showNextWarning();
  }

  pluginData.build.pid = 0;
  setEnableToolbar(TRUE);
}

/**
 * @brief handle run stdout/stderr
 *
 * @param [in]  line        line
 * @param [in]  ioCondition i/o condition set
 * @param [in]  data        user data (not used)
 */

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

/**
 * @brief execute external command
 *
 * @param [in]  command       build command to execute
 * @param [in]  customText    custome text or NULL
 * @param [in]  stdoutHandler stdout-handler
 * @param [in]  stderrHandler stderr-handler
 */

LOCAL void executeCommand(const Command *command,
                          const gchar   *customText,
                          OutputHandler stdoutHandler,
                          OutputHandler stderrHandler
                         )
{
  GString *string;
  GError  *error;

  g_assert(command != NULL);

  if (command->line->len > 0)
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
            return;
          }
        }
      }
    }

    // expand macros
    gchar *commandLine      = expandMacros(project, document, command->line->str, NULL, customText);
    gchar *workingDirectory = expandMacros(project, document, command->workingDirectory->str, NULL, NULL);

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
      gchar *escapedCommandLine = g_strescape(commandLine, NULL);
      g_string_append(string, escapedCommandLine);
      g_free(escapedCommandLine);
      g_string_append(string, "' 1>&2");
      g_ptr_array_add(argv, string->str);
      g_string_free(string, FALSE);
    #else
      // parse and use command line directly
      gint commandLineArgc;
      gchar **commandLineArgv;
      g_shell_parse_argv(commandLine, &commandLineArgc, &commandLineArgv, NULL);
      for (guint i = 0; i < commandLineArgc; i++)
      {
        g_ptr_array_add(argv, g_strdup(commandLineArgv[i]));
      }
      g_strfreev(commandLineArgv);
    #endif
    g_ptr_array_add(argv, NULL);

    // if no working directory is given, use file/project or current directory
    if (workingDirectory == NULL)
    {
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

    // run build
    clearAll();
    string = g_string_new(NULL);
    g_string_printf(string, "Working directory: %s", workingDirectory);
    gtk_list_store_insert_with_values(pluginData.build.messagesStore,
                                      NULL,
                                      -1,
                                      MODEL_ERROR_WARNING_COLOR,   COLOR_BUILD_INFO,
                                      MODEL_ERROR_WARNING_MESSAGE, string->str,
                                      -1
                                     );
    g_string_printf(string, "Build command line: %s", commandLine);
    gtk_list_store_insert_with_values(pluginData.build.messagesStore,
                                      NULL,
                                      -1,
                                      MODEL_ERROR_WARNING_COLOR,   COLOR_BUILD_INFO,
                                      MODEL_ERROR_WARNING_MESSAGE, string->str,
                                      -1
                                     );
    g_string_free(string, TRUE);
    showLastLine(GTK_SCROLLED_WINDOW(pluginData.widgets.messagesTab));

    g_string_assign(pluginData.build.workingDirectory, workingDirectory);

    error = NULL;
    if (!spawn_with_callbacks(workingDirectory,
                              NULL, // commandLine,
                              (gchar**)argv->pdata,
                              NULL, // envp,
                              SPAWN_ASYNC|SPAWN_LINE_BUFFERED,
                              NULL, // stdin_cb,
                              NULL, // stdin_data,
                              stdoutHandler,
                              workingDirectory,
                              0, // stdout_max_length,
                              stderrHandler,
                              workingDirectory,
                              0, // stderr_max_length,
                              onExecuteCommandExit, // exit_cb,
                              NULL, // exit_data,
                              &pluginData.build.pid,
                              &error
                             )
       )
    {
      if (error != NULL)
      {
        dialogs_show_msgbox(GTK_MESSAGE_ERROR, "Cannot run '%s': %s", command->line->str, error->message);
      }
      else
      {
        dialogs_show_msgbox(GTK_MESSAGE_ERROR, "Cannot run '%s'", command->line->str);
      }
      g_error_free(error);
    }

    // free resources
    g_ptr_array_free(argv, TRUE);
    g_free(workingDirectory);
    g_free(commandLine);
  }
}

/**
 * @brief execute specific command
 *
 */

LOCAL void executeCommandBuild()
{
  setEnableToolbar(FALSE);
  showBuildMessagesTab();
  if      (pluginData.projectProperties.commands.build.line->len > 0)
  {
    executeCommand(&pluginData.projectProperties.commands.build, NULL, onExecuteCommandOutput, onExecuteCommandOutput);
  }
  else if (pluginData.configuration.commands.build.line->len > 0)
  {
    executeCommand(&pluginData.configuration.commands.build, NULL, onExecuteCommandOutput, onExecuteCommandOutput);
  }
  else
  {
    executeCommand(&pluginData.projectProperties.buildMenuCommands.build, NULL, onExecuteCommandOutput, onExecuteCommandOutput);
  }
}

LOCAL void executeCommandClean()
{
  setEnableToolbar(FALSE);
  showBuildMessagesTab();
  if      (pluginData.projectProperties.commands.clean.line->len > 0)
  {
    executeCommand(&pluginData.projectProperties.commands.clean, NULL, onExecuteCommandOutput, onExecuteCommandOutput);
  }
  else if (pluginData.configuration.commands.clean.line->len > 0)
  {
    executeCommand(&pluginData.configuration.commands.clean, NULL, onExecuteCommandOutput, onExecuteCommandOutput);
  }
}

LOCAL void executeCommandMakeAll()
{
  setEnableToolbar(FALSE);
  showBuildMessagesTab();
  if      (pluginData.projectProperties.commands.makeAll.line->len > 0)
  {
    executeCommand(&pluginData.projectProperties.commands.makeAll, NULL, onExecuteCommandOutput, onExecuteCommandOutput);
  }
  else if (pluginData.configuration.commands.makeAll.line->len > 0)
  {
    executeCommand(&pluginData.configuration.commands.makeAll, NULL, onExecuteCommandOutput, onExecuteCommandOutput);
  }
  else
  {
    executeCommand(&pluginData.projectProperties.buildMenuCommands.makeAll, NULL, onExecuteCommandOutput, onExecuteCommandOutput);
  }
}

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
      executeCommand(&pluginData.projectProperties.commands.makeCustomTarget, NULL, onExecuteCommandOutput, onExecuteCommandOutput);
    }
    else if (pluginData.configuration.commands.makeCustomTarget.line->len > 0)
    {
      executeCommand(&pluginData.configuration.commands.makeCustomTarget, NULL, onExecuteCommandOutput, onExecuteCommandOutput);
    }
    else
    {
      executeCommand(&pluginData.projectProperties.buildMenuCommands.makeCustomTarget, NULL, onExecuteCommandOutput, onExecuteCommandOutput);
    }
  }

  g_string_free(customTarget, TRUE);
}

LOCAL void executeCommandMakeObject()
{
  setEnableToolbar(FALSE);
  showBuildMessagesTab();
  if      (pluginData.projectProperties.commands.makeObject.line->len > 0)
  {
    executeCommand(&pluginData.projectProperties.commands.makeObject, NULL, onExecuteCommandOutput, onExecuteCommandOutput);
  }
  else if (pluginData.configuration.commands.makeObject.line->len > 0)
  {
    executeCommand(&pluginData.configuration.commands.makeObject, NULL, onExecuteCommandOutput, onExecuteCommandOutput);
  }
  else
  {
    executeCommand(&pluginData.projectProperties.buildMenuCommands.makeObject, NULL, onExecuteCommandOutput, onExecuteCommandOutput);
  }
}

LOCAL void executeCommandRun()
{
  setEnableToolbar(FALSE);
  msgwin_switch_tab(MSG_MESSAGE, FALSE);
  if      (pluginData.projectProperties.commands.run.line->len > 0)
  {
    executeCommand(&pluginData.projectProperties.commands.run, NULL, onExecuteCommandRunOutput, onExecuteCommandRunOutput);
  }
  else if (pluginData.configuration.commands.run.line->len > 0)
  {
    executeCommand(&pluginData.configuration.commands.run, NULL, onExecuteCommandRunOutput, onExecuteCommandRunOutput);
  }
  else
  {
    executeCommand(&pluginData.projectProperties.buildMenuCommands.run, NULL, onExecuteCommandRunOutput, onExecuteCommandRunOutput);
  }
}

LOCAL void executeCommandStop()
{
  if (pluginData.build.pid > 1)
  {
    spawn_kill_process(-pluginData.build.pid, NULL);
    setEnableStop(FALSE);
  }
}

// ---------------------------------------------------------------------

/**
 * @brief menu item callbacks
 *
 * @param [in]  widget widget (not used)
 * @param [in]  data   user data (not used)
 */

LOCAL void onMenuItemBuild(GtkWidget *widget, gpointer data)
{
  UNUSED_VARIABLE(widget);
  UNUSED_VARIABLE(data);

  executeCommandBuild();
}

LOCAL void onMenuItemClean(GtkWidget *widget, gpointer data)
{
  UNUSED_VARIABLE(widget);
  UNUSED_VARIABLE(data);

  executeCommandClean();
}

LOCAL void onMenuItemMakeAll(GtkWidget *widget, gpointer data)
{
  UNUSED_VARIABLE(widget);
  UNUSED_VARIABLE(data);

  executeCommandMakeAll();
}

LOCAL void onMenuItemMakeCustomTarget(GtkWidget *widget, gpointer data)
{
  UNUSED_VARIABLE(widget);
  UNUSED_VARIABLE(data);

  executeCommandMakeCustomTarget();
}

LOCAL void onMenuItemMakeObject(GtkWidget *widget, gpointer data)
{
  UNUSED_VARIABLE(widget);
  UNUSED_VARIABLE(data);

  executeCommandMakeObject();
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

LOCAL void onMenuItemRun(GtkWidget *widget, gpointer data)
{
  UNUSED_VARIABLE(widget);
  UNUSED_VARIABLE(data);

  executeCommandRun();
}

LOCAL void onMenuItemStop(GtkWidget *widget, gpointer data)
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

/**
 * @brief selection changed callback
 *
 * @param [in]  data tree widget
 */

LOCAL gboolean onMessageListSelectionChanged(gpointer data)
{
  GtkTreeView *messageList = GTK_TREE_VIEW(data);

  g_assert(messageList != NULL);

  GtkTreeSelection *treeSelection = gtk_tree_view_get_selection(messageList);

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

/**
 * @brief button press callback
 *
 * @param [in]  widget      widget
 * @param [in]  eventButton button event
 * @param [in]  data        popup menu
 */

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

/**
 * @brief selection changed callback
 *
 * @param [in]  data tree widget
 */

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

/**
 * @brief button press callback
 *
 * @param [in]  widget      widget
 * @param [in]  eventButton button event
 * @param [in]  data        user data (not used)
 */

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

/**
 * @brief key press callback
 *
 * @param [in]  widget   widget
 * @param [in]  eventKey key event
 * @param [in]  data     user data (not used)
 */

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

/**
 * @brief double-click callback: expand/collaps tree entry
 *
 * @param [in]  widget   widget
 * @param [in]  treePath tree path
 * @param [in]  column   column
 * @param [in]  data     user data (not used)
 */

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

/**
 * @brief handle key short-cut
 *
 * @param [in]  keyId key id
 */

LOCAL void onKeyBinding(guint keyId)
{
  switch (keyId)
  {
    case KEY_BINDING_BUILD:
      executeCommandBuild();
      break;

    case KEY_BINDING_CLEAN:
      executeCommandClean();
      break;

    case KEY_BINDING_MAKE_ALL:
      executeCommandMakeAll();
      break;

    case KEY_BINDING_MAKE_CUSTOM_TARGET:
      executeCommandMakeCustomTarget();
      break;

    case KEY_BINDING_MAKE_OBJECT:
      executeCommandMakeObject();
      break;

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

    case KEY_BINDING_RUN:
      executeCommandRun();
      break;

    case KEY_BINDING_STOP:
      executeCommandStop();
      break;
  }
}

/**
 * @brief init toolbar
 *
 * @param [in]  plugin Geany plugin
 */

LOCAL void initToolbar(GeanyPlugin *plugin)
{
  GtkWidget *menu;
  GtkWidget *menuItem;

  g_assert(plugin != NULL);

  // create toolbar buttons
  pluginData.widgets.buttonBuild = gtk_menu_tool_button_new(NULL, _("Builder"));
  plugin_signal_connect(geany_plugin,
                        G_OBJECT(pluginData.widgets.buttonBuild),
                        "clicked",
                        FALSE,
                        G_CALLBACK(onMenuItemBuild),
                        NULL
                       );
  plugin_add_toolbar_item(plugin, pluginData.widgets.buttonBuild);
  if (pluginData.configuration.buttons.build)
  {
    gtk_widget_show(GTK_WIDGET(pluginData.widgets.buttonBuild));
  }
  else
  {
    gtk_widget_hide(GTK_WIDGET(pluginData.widgets.buttonBuild));
  }

  pluginData.widgets.buttonClean = gtk_tool_button_new(NULL, _("Clean"));
  plugin_signal_connect(geany_plugin,
                        G_OBJECT(pluginData.widgets.buttonClean),
                        "clicked",
                        FALSE,
                        G_CALLBACK(onMenuItemClean),
                        NULL
                       );
  plugin_add_toolbar_item(plugin, pluginData.widgets.buttonClean);
  if (pluginData.configuration.buttons.clean)
  {
    gtk_widget_show(GTK_WIDGET(pluginData.widgets.buttonClean));
  }
  else
  {
    gtk_widget_hide(GTK_WIDGET(pluginData.widgets.buttonClean));
  }

  pluginData.widgets.buttonRun = gtk_tool_button_new(NULL, _("Run"));
  plugin_signal_connect(geany_plugin,
                        G_OBJECT(pluginData.widgets.buttonRun),
                        "clicked",
                        FALSE,
                        G_CALLBACK(onMenuItemRun),
                        NULL
                       );
  plugin_add_toolbar_item(plugin, pluginData.widgets.buttonRun);
  if (pluginData.configuration.buttons.run)
  {
    gtk_widget_show(GTK_WIDGET(pluginData.widgets.buttonRun));
  }
  else
  {
    gtk_widget_hide(GTK_WIDGET(pluginData.widgets.buttonRun));
  }

  pluginData.widgets.buttonStop = gtk_tool_button_new(NULL, _("Stop"));
  gtk_widget_set_sensitive(GTK_WIDGET(pluginData.widgets.buttonStop), FALSE);
  plugin_signal_connect(geany_plugin,
                        G_OBJECT(pluginData.widgets.buttonStop),
                        "clicked",
                        FALSE,
                        G_CALLBACK(onMenuItemStop),
                        NULL
                       );
  plugin_add_toolbar_item(plugin, pluginData.widgets.buttonStop);
  if (pluginData.configuration.buttons.stop)
  {
    gtk_widget_show(GTK_WIDGET(pluginData.widgets.buttonStop));
  }
  else
  {
    gtk_widget_hide(GTK_WIDGET(pluginData.widgets.buttonStop));
  }

  // create toolbar build button menu
  menu = gtk_menu_new();
  {
    pluginData.widgets.menuItems.build = ui_image_menu_item_new(GEANY_STOCK_BUILD, _("_Build"));
    gtk_container_add(GTK_CONTAINER(menu), pluginData.widgets.menuItems.build);
    plugin_signal_connect(geany_plugin,
                          G_OBJECT(pluginData.widgets.menuItems.build),
                          "activate",
                          FALSE,
                          G_CALLBACK(onMenuItemBuild),
                          NULL
                         );

    pluginData.widgets.menuItems.clean = gtk_menu_item_new_with_mnemonic(_("_Clean"));
    gtk_container_add(GTK_CONTAINER(menu), pluginData.widgets.menuItems.clean);
    plugin_signal_connect(geany_plugin,
                          G_OBJECT(pluginData.widgets.menuItems.clean),
                          "activate",
                          FALSE,
                          G_CALLBACK(onMenuItemClean),
                          NULL
                         );

    menuItem = gtk_separator_menu_item_new();
    gtk_container_add(GTK_CONTAINER(menu), menuItem);

    pluginData.widgets.menuItems.makeAll = gtk_menu_item_new_with_mnemonic(_("_Make All"));
    gtk_container_add(GTK_CONTAINER(menu), pluginData.widgets.menuItems.makeAll);
    plugin_signal_connect(geany_plugin,
                          G_OBJECT(pluginData.widgets.menuItems.makeAll),
                          "activate",
                          FALSE,
                          G_CALLBACK(onMenuItemMakeAll),
                          NULL
                         );

    pluginData.widgets.menuItems.makeCustomTarget = gtk_menu_item_new_with_mnemonic(_("Make Custom _Target..."));
    gtk_container_add(GTK_CONTAINER(menu), pluginData.widgets.menuItems.makeCustomTarget);
    plugin_signal_connect(geany_plugin,
                          G_OBJECT(pluginData.widgets.menuItems.makeCustomTarget),
                          "activate",
                          FALSE,
                          G_CALLBACK(onMenuItemMakeCustomTarget),
                          NULL
                         );

    pluginData.widgets.menuItems.makeObject = gtk_menu_item_new_with_mnemonic(_("Make _Object"));
    gtk_container_add(GTK_CONTAINER(menu), pluginData.widgets.menuItems.makeObject);
    plugin_signal_connect(geany_plugin,
                          G_OBJECT(pluginData.widgets.menuItems.makeObject),
                          "activate",
                          FALSE,
                          G_CALLBACK(onMenuItemMakeObject),
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

    pluginData.widgets.menuItems.run = gtk_menu_item_new_with_mnemonic(_("_Run"));
    gtk_container_add(GTK_CONTAINER(menu), pluginData.widgets.menuItems.run);
    plugin_signal_connect(geany_plugin,
                          G_OBJECT(pluginData.widgets.menuItems.run),
                          "activate",
                          FALSE,
                          G_CALLBACK(onMenuItemRun),
                          NULL
                         );

    pluginData.widgets.menuItems.stop = gtk_menu_item_new_with_mnemonic(_("Stop"));
    gtk_widget_set_sensitive(GTK_WIDGET(pluginData.widgets.menuItems.stop), FALSE);
    gtk_container_add(GTK_CONTAINER(menu), pluginData.widgets.menuItems.stop);
    plugin_signal_connect(geany_plugin,
                          G_OBJECT(pluginData.widgets.menuItems.stop),
                          "activate",
                          FALSE,
                          G_CALLBACK(onMenuItemStop),
                          NULL
                         );

    menuItem = gtk_separator_menu_item_new();
    gtk_container_add(GTK_CONTAINER(menu), menuItem);

    pluginData.widgets.menuItems.projectProperies = gtk_menu_item_new_with_label(_("Project properties"));
    gtk_container_add(GTK_CONTAINER(menu), pluginData.widgets.menuItems.projectProperies);
    plugin_signal_connect(geany_plugin,
                          G_OBJECT(pluginData.widgets.menuItems.projectProperies),
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

    gtk_menu_tool_button_set_menu(GTK_MENU_TOOL_BUTTON(pluginData.widgets.buttonBuild), menu);
  }
  gtk_widget_show_all(menu);
}

/**
 * @brief done toolbar
 *
 * @param [in]  plugin Geany plugin
 */

LOCAL void doneToolbar(GeanyPlugin *plugin)
{
  g_assert(plugin != NULL);

  UNUSED_VARIABLE(plugin);
}

/**
 * @brief add regular expression
 *
 * @param [in]  widget      widget (not used)
 * @param [in]  eventButton event button (not used)
 * @param [in]  data        user data (no used)
 */

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
  }
}

/**
 * @brief edit regular expression
 *
 * @param [in]  widget      widget (not used)
 * @param [in]  eventButton event button (not used)
 * @param [in]  data        user data (no used)
 */

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

/**
 * @brief render integer in cell
 *
 * @param [in]  column       tree column
 * @param [in]  cellRenderer cell renderer
 * @param [in]  treeModel    tree data model
 * @param [in]  treeIter     tree entry iterator
 * @param [in]  data         column number
 */

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

/**
 * @brief create new error/warning tree view
 *
 * @return      tree widget
 */

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

/**
 * @brief handle double-click: edit selected regular expression
 *
 * @param [in]  treeView tree view
 * @param [in]  treePath tree path (not used)
 * @param [in]  column   tree column (not used)
 * @param [in]  data     data (not used)
 */

LOCAL void onConfigureRegExDoubleClick(GtkTreeView       *treeView,
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

/**
 * @brief add regular expression
 *
 * @param [in]  widget      widget (not used)
 * @param [in]  eventButton event button (not used)
 * @param [in]  data        user data (not used)
 */

LOCAL void onConfigureAddRegEx(GtkWidget *widget, GdkEventButton *eventButton, gpointer data)
{
  UNUSED_VARIABLE(widget);
  UNUSED_VARIABLE(eventButton);
  UNUSED_VARIABLE(data);

  addRegEx("");
}

/**
 * @brief clone regular expression
 *
 * @param [in]  widget      widget (not used)
 * @param [in]  eventButton event button (not used)
 * @param [in]  data        list view widget
 */

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

/**
 * @brief edit regular expression
 *
 * @param [in]  widget      widget (not used)
 * @param [in]  eventButton event button (not used)
 * @param [in]  data        list view widget
 */

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

/**
 * @brief remove regular expression
 *
 * @param [in]  widget      widget (not used)
 * @param [in]  eventButton event button (not used)
 * @param [in]  data        list view widget
 */

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

/**
 * @brief init tab
 *
 * @param [in]  plugin Geany plugin
 */

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

/**
 * @brief init key bindings
 *
 * @param [in]  plugin Geany plugin
 */

LOCAL void initKeyBinding(GeanyPlugin *plugin)
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
                       pluginData.widgets.menuItems.build
                      );
  keybindings_set_item(keyGroup,
                       KEY_BINDING_CLEAN,
                       onKeyBinding,
                       0,
                       0,
                       "clean",
                       _("Clean"),
                       pluginData.widgets.menuItems.clean
                      );
  keybindings_set_item(keyGroup,
                       KEY_BINDING_MAKE_ALL,
                       onKeyBinding,
                       0,
                       0,
                       "make_all",
                       _("Make all"),
                       pluginData.widgets.menuItems.makeAll
                      );
  keybindings_set_item(keyGroup,
                       KEY_BINDING_MAKE_CUSTOM_TARGET,
                       onKeyBinding,
                       0,
                       0,
                       "make_custom_target",
                       _("Make custom target"),
                       pluginData.widgets.menuItems.makeCustomTarget
                      );
  keybindings_set_item(keyGroup,
                       KEY_BINDING_MAKE_OBJECT,
                       onKeyBinding,
                       0,
                       0,
                       "make_object",
                       _("Make object"),
                       pluginData.widgets.menuItems.makeObject
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
                       KEY_BINDING_RUN,
                       onKeyBinding,
                       0,
                       0,
                       "run",
                       _("Run"),
                       pluginData.widgets.menuItems.run
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
                       pluginData.widgets.menuItems.projectProperies
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

/**
 * @brief done tab
 *
 * @param [in]  plugin Geany plugin
 */

LOCAL void doneTab(GeanyPlugin *plugin)
{
  g_assert(plugin != NULL);

  gtk_notebook_remove_page(GTK_NOTEBOOK(ui_lookup_widget(plugin->geany_data->main_widgets->window, "notebook_info")),
                           pluginData.widgets.tabIndex
                          );
}

/**
 * @brief render cell 'type'
 *
 * @param [in]  column       column (not used)
 * @param [in]  cellRenderer cell renderer
 * @param [in]  treeModel    model
 * @param [in]  treeIter     element iterator in model
 * @param [in]  data         user data (not used)
 */

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

/**
 * @brief save configuration
 *
 * @param [in]  dialog   dialog
 * @param [in]  response dialog response code
 * @param [in]  data     user data (not used)
 */

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
  g_string_assign (pluginData.configuration.commands.build.line,                        gtk_entry_get_text(GTK_ENTRY(g_object_get_data(G_OBJECT(dialog), "command_line_build"))));
  g_string_assign (pluginData.configuration.commands.build.workingDirectory,            gtk_entry_get_text(GTK_ENTRY(g_object_get_data(G_OBJECT(dialog), "working_directory_build"))));
  g_string_assign (pluginData.configuration.commands.clean.line,                        gtk_entry_get_text(GTK_ENTRY(g_object_get_data(G_OBJECT(dialog), "command_line_clean"))));
  g_string_assign (pluginData.configuration.commands.clean.workingDirectory,            gtk_entry_get_text(GTK_ENTRY(g_object_get_data(G_OBJECT(dialog), "working_directory_clean"))));
  g_string_assign (pluginData.configuration.commands.makeAll.line,                      gtk_entry_get_text(GTK_ENTRY(g_object_get_data(G_OBJECT(dialog), "command_line_make_all"))));
  g_string_assign (pluginData.configuration.commands.makeAll.workingDirectory,          gtk_entry_get_text(GTK_ENTRY(g_object_get_data(G_OBJECT(dialog), "working_directory_make_all"))));
  g_string_assign (pluginData.configuration.commands.makeCustomTarget.line,             gtk_entry_get_text(GTK_ENTRY(g_object_get_data(G_OBJECT(dialog), "command_line_make_custom_target"))));
  g_string_assign (pluginData.configuration.commands.makeCustomTarget.workingDirectory, gtk_entry_get_text(GTK_ENTRY(g_object_get_data(G_OBJECT(dialog), "working_directory_make_custom_target"))));
  g_string_assign (pluginData.configuration.commands.makeObject.line,                   gtk_entry_get_text(GTK_ENTRY(g_object_get_data(G_OBJECT(dialog), "command_line_make_object"))));
  g_string_assign (pluginData.configuration.commands.makeObject.workingDirectory,       gtk_entry_get_text(GTK_ENTRY(g_object_get_data(G_OBJECT(dialog), "working_directory_make_object"))));
  g_string_assign (pluginData.configuration.commands.run.line,                          gtk_entry_get_text(GTK_ENTRY(g_object_get_data(G_OBJECT(dialog), "command_line_run"))));
  g_string_assign (pluginData.configuration.commands.run.workingDirectory,              gtk_entry_get_text(GTK_ENTRY(g_object_get_data(G_OBJECT(dialog), "working_directory_run"))));

  pluginData.configuration.buttons.build = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(dialog), "button_build")));
  pluginData.configuration.buttons.clean = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(dialog), "button_clean")));
  pluginData.configuration.buttons.run   = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(dialog), "button_run")));
  pluginData.configuration.buttons.stop  = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(dialog), "button_stop")));

  GdkRGBA color;
  gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(g_object_get_data(G_OBJECT(dialog), "error_indicator_color")), &color);
  pluginData.configuration.errorIndicators     = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(dialog), "error_indicators")));
  pluginData.configuration.errorIndicatorColor = color;
  gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(g_object_get_data(G_OBJECT(dialog), "warning_indicator_color")), &color);
  pluginData.configuration.warningIndicators     = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(dialog), "warning_indicators")));
  pluginData.configuration.warningIndicatorColor = color;

  pluginData.configuration.autoSaveAll            = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(dialog), "auto_save_all")));
  pluginData.configuration.addProjectRegExResults = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(dialog), "add_project_regex_results")));
  pluginData.configuration.autoShowFirstError     = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(dialog), "auto_show_first_error")));
  pluginData.configuration.autoShowFirstWarning   = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(dialog), "auto_show_first_warning")));

  // save configuration
  configurationSave();

  // update buttons view
  if (pluginData.configuration.buttons.build)
  {
    gtk_widget_show(GTK_WIDGET(pluginData.widgets.buttonBuild));
  }
  else
  {
    gtk_widget_hide(GTK_WIDGET(pluginData.widgets.buttonBuild));
  }
  if (pluginData.configuration.buttons.clean)
  {
    gtk_widget_show(GTK_WIDGET(pluginData.widgets.buttonClean));
  }
  else
  {
    gtk_widget_hide(GTK_WIDGET(pluginData.widgets.buttonClean));
  }
  if (pluginData.configuration.buttons.run)
  {
    gtk_widget_show(GTK_WIDGET(pluginData.widgets.buttonRun));
  }
  else
  {
    gtk_widget_hide(GTK_WIDGET(pluginData.widgets.buttonRun));
  }
  if (pluginData.configuration.buttons.stop)
  {
    gtk_widget_show(GTK_WIDGET(pluginData.widgets.buttonStop));
  }
  else
  {
    gtk_widget_hide(GTK_WIDGET(pluginData.widgets.buttonStop));
  }
}

/**
 * @brief global configuration dialog
 *
 * @param [in]  plugin Geany plugin
 * @param [in]  dialog dialog
 * @param [in]  data   user data (not used)
 */

LOCAL GtkWidget *configure(GeanyPlugin *plugin, GtkDialog *dialog, gpointer data)
{
  GtkBox *vbox;

  g_assert(plugin != NULL);

  UNUSED_VARIABLE(data);

  // init configuration settings
  vbox = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 6));
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
        addGrid(grid, 0, 0, 1, newLabel(G_OBJECT(dialog), NULL, "Build", NULL));
        addGrid(grid, 0, 1, 1, newEntry(G_OBJECT(dialog), "command_line_build", "Command to build project."));
        addGrid(grid, 0, 2, 1, newWorkingDirectoryChooser(G_OBJECT(dialog), "working_directory_build", "Working directory for build command."));

        addGrid(grid, 1, 0, 1, newLabel(G_OBJECT(dialog), NULL, "Clean", NULL));
        addGrid(grid, 1, 1, 1, newEntry(G_OBJECT(dialog), "command_line_clean",  "Command to clean."));
        addGrid(grid, 1, 2, 1, newWorkingDirectoryChooser(G_OBJECT(dialog), "working_directory_clean", "Working directory for clean command."));

        addGrid(grid, 2, 0, 1, newLabel(G_OBJECT(dialog), NULL, "All", NULL));
        addGrid(grid, 2, 1, 1, newEntry(G_OBJECT(dialog), "command_line_make_all",  "Command to make all."));
        addGrid(grid, 2, 2, 1, newWorkingDirectoryChooser(G_OBJECT(dialog), "working_directory_make_all", "Working directory for make all command."));

        addGrid(grid, 3, 0, 1, newLabel(G_OBJECT(dialog), NULL, "Custom", NULL));
        addGrid(grid, 3, 1, 1, newEntry(G_OBJECT(dialog), "command_line_make_custom_target",  "Command to make custom target."));
        addGrid(grid, 3, 2, 1, newWorkingDirectoryChooser(G_OBJECT(dialog), "working_directory_make_custom_target", "Working directory for make custom target command."));

        addGrid(grid, 4, 0, 1, newLabel(G_OBJECT(dialog), NULL, "Object", NULL));
        addGrid(grid, 4, 1, 1, newEntry(G_OBJECT(dialog), "command_line_make_object",  "Command to make object."));
        addGrid(grid, 4, 2, 1, newWorkingDirectoryChooser(G_OBJECT(dialog), "working_directory_make_object", "Working directory for make object command."));

        addGrid(grid, 5, 0, 1, newLabel(G_OBJECT(dialog), NULL, "Run", NULL));
        addGrid(grid, 5, 1, 1, newEntry(G_OBJECT(dialog), "command_line_run",  "Command to run application."));
        addGrid(grid, 5, 2, 1, newWorkingDirectoryChooser(G_OBJECT(dialog), "working_directory_run", "Working directory for run application command."));
      }
      gtk_container_add(GTK_CONTAINER(frame), GTK_WIDGET(grid));
    }
    addBox(vbox, FALSE, GTK_WIDGET(frame));

    // create error regular expression list
    frame = gtk_frame_new("Regular expressions");
    gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_IN);
    gtk_frame_set_label_align(GTK_FRAME(frame), .5, 0);
    gtk_widget_set_margin_top(gtk_frame_get_label_widget(GTK_FRAME(frame)), 6);
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
          GtkWidget *listView = gtk_tree_view_new();
          gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(listView), FALSE);
          g_object_set_data(G_OBJECT(dialog), "regex_list", listView);
          {
            GtkCellRenderer   *cellRenderer;
            GtkTreeViewColumn *column;

            cellRenderer = gtk_cell_renderer_text_new();
            column = gtk_tree_view_column_new();
            gtk_tree_view_column_pack_start(column, cellRenderer, TRUE);
            gtk_tree_view_column_set_attributes(column, cellRenderer, "text", MODEL_REGEX_LANGUAGE, NULL);
            gtk_tree_view_column_set_sort_indicator(column, FALSE);
            gtk_tree_view_column_set_sort_column_id(column, MODEL_REGEX_LANGUAGE);
            gtk_tree_view_column_set_resizable(column, TRUE);
            gtk_tree_view_append_column(GTK_TREE_VIEW(listView), column);

            cellRenderer = gtk_cell_renderer_text_new();
            column = gtk_tree_view_column_new();
            gtk_tree_view_column_pack_start(column, cellRenderer, TRUE);
            gtk_tree_view_column_set_attributes(column, cellRenderer, "text", MODEL_REGEX_GROUP, NULL);
            gtk_tree_view_column_set_sort_indicator(column, FALSE);
            gtk_tree_view_column_set_sort_column_id(column, MODEL_REGEX_GROUP);
            gtk_tree_view_column_set_resizable(column, TRUE);
            gtk_tree_view_append_column(GTK_TREE_VIEW(listView), column);

            cellRenderer = gtk_cell_renderer_text_new();
            column = gtk_tree_view_column_new();
            gtk_tree_view_column_pack_start(column, cellRenderer, TRUE);
            gtk_tree_view_column_set_attributes(column, cellRenderer, "text", MODEL_REGEX_TYPE, NULL);
            gtk_tree_view_column_set_cell_data_func(column, cellRenderer, configureCellRendererType, NULL, NULL);
            gtk_tree_view_column_set_sort_indicator(column, FALSE);
            gtk_tree_view_column_set_sort_column_id(column, MODEL_REGEX_TYPE);
            gtk_tree_view_column_set_resizable(column, TRUE);
            gtk_tree_view_append_column(GTK_TREE_VIEW(listView), column);

            cellRenderer = gtk_cell_renderer_text_new();
            column = gtk_tree_view_column_new();
            gtk_tree_view_column_pack_start(column, cellRenderer, TRUE);
            gtk_tree_view_column_set_attributes(column, cellRenderer, "text", MODEL_REGEX_REGEX, NULL);
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
                                  G_CALLBACK(onConfigureRegExDoubleClick),
                                  NULL
                                 );
          }
          gtk_container_add(GTK_CONTAINER(scrolledWindow), listView);
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
                                G_CALLBACK(onConfigureCloneRegEx),
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
                                G_CALLBACK(onConfigureEditRegEx),
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
                                G_CALLBACK(onConfigureRemoveRegEx),
                                g_object_get_data(G_OBJECT(dialog), "regex_list")
                               );
        }
        addBox(vbox, FALSE, GTK_WIDGET(hbox));
      }
      gtk_container_add(GTK_CONTAINER(frame), GTK_WIDGET(vbox));
    }
    addBox(vbox, TRUE, GTK_WIDGET(frame));

    GtkBox *hbox;

    hbox = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12));
    {
      addBox(hbox, FALSE, gtk_label_new("Buttons: "));

      addBox(hbox, FALSE, newCheckButton(G_OBJECT(dialog), "button_build", "build", "Show build button."));
      addBox(hbox, FALSE, newCheckButton(G_OBJECT(dialog), "button_clean", "clean", "Show cleanbutton."));
      addBox(hbox, FALSE, newCheckButton(G_OBJECT(dialog), "button_run",   "run",   "Show run button."));
      addBox(hbox, FALSE, newCheckButton(G_OBJECT(dialog), "button_stop",  "stop",  "Show stop button."));
    }
    addBox(GTK_BOX(vbox), FALSE, GTK_WIDGET(hbox));

    hbox = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12));
    {
      addBox(hbox, FALSE, gtk_label_new("Indicator colors: "));

      addBox(hbox, FALSE, newCheckButton(G_OBJECT(dialog), "error_indicators",  "errors",  "Show error indicators."));
      addBox(hbox, FALSE, newColorChooser(G_OBJECT(dialog), "error_indicator_color", "Error indicator color."));
      addBox(hbox, FALSE, newCheckButton(G_OBJECT(dialog), "warning_indicators",  "warnings",  "Show warning indicators."));
      addBox(hbox, FALSE, newColorChooser(G_OBJECT(dialog), "warning_indicator_color", "Warning indicator color."));
    }
    addBox(vbox, FALSE, GTK_WIDGET(hbox));

    GtkGrid *grid = GTK_GRID(gtk_grid_new());
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_widget_set_hexpand(GTK_WIDGET(grid), TRUE);
    {
      addGrid(grid, 0, 0, 1, newCheckButton(G_OBJECT(dialog), "auto_save_all",            "Auto save all", "Auto save all before build is started."));
      addGrid(grid, 1, 0, 1, newCheckButton(G_OBJECT(dialog), "add_project_regex_results","Add results of project regular expressions","Add results of project defined regular expression. If disabled only project regular expressions - if defined - are used only to detect errors or warnings."));
      addGrid(grid, 2, 0, 1, newCheckButton(G_OBJECT(dialog), "auto_show_first_error",    "Show first error", "Auto show first error after build is done."));
      addGrid(grid, 3, 0, 1, newCheckButton(G_OBJECT(dialog), "auto_show_first_warning",  "Show first warning", "Auto show first warning after build is done without errors."));
    }
    addBox(vbox, FALSE, GTK_WIDGET(grid));
  }
  gtk_widget_show_all(GTK_WIDGET(vbox));

  // set values
  gtk_entry_set_text(GTK_ENTRY(g_object_get_data(G_OBJECT(dialog),                   "command_line_build"                  )), pluginData.configuration.commands.build.line->str);
  gtk_entry_set_text(GTK_ENTRY(g_object_get_data(G_OBJECT(dialog),                   "working_directory_build"             )), pluginData.configuration.commands.build.workingDirectory->str);
  gtk_entry_set_text(GTK_ENTRY(g_object_get_data(G_OBJECT(dialog),                   "command_line_clean"                  )), pluginData.configuration.commands.clean.line->str);
  gtk_entry_set_text(GTK_ENTRY(g_object_get_data(G_OBJECT(dialog),                   "working_directory_clean"             )), pluginData.configuration.commands.clean.workingDirectory->str);
  gtk_entry_set_text(GTK_ENTRY(g_object_get_data(G_OBJECT(dialog),                   "command_line_make_all"               )), pluginData.configuration.commands.makeAll.line->str);
  gtk_entry_set_text(GTK_ENTRY(g_object_get_data(G_OBJECT(dialog),                   "working_directory_make_all"          )), pluginData.configuration.commands.makeAll.workingDirectory->str);
  gtk_entry_set_text(GTK_ENTRY(g_object_get_data(G_OBJECT(dialog),                   "command_line_make_custom_target"     )), pluginData.configuration.commands.makeCustomTarget.line->str);
  gtk_entry_set_text(GTK_ENTRY(g_object_get_data(G_OBJECT(dialog),                   "working_directory_make_custom_target")), pluginData.configuration.commands.makeCustomTarget.workingDirectory->str);
  gtk_entry_set_text(GTK_ENTRY(g_object_get_data(G_OBJECT(dialog),                   "command_line_make_object"            )), pluginData.configuration.commands.makeObject.line->str);
  gtk_entry_set_text(GTK_ENTRY(g_object_get_data(G_OBJECT(dialog),                   "working_directory_make_object"       )), pluginData.configuration.commands.makeObject.workingDirectory->str);
  gtk_entry_set_text(GTK_ENTRY(g_object_get_data(G_OBJECT(dialog),                   "command_line_run"                    )), pluginData.configuration.commands.run.line->str);
  gtk_entry_set_text(GTK_ENTRY(g_object_get_data(G_OBJECT(dialog),                   "working_directory_run"               )), pluginData.configuration.commands.run.workingDirectory->str);

  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(dialog), "button_build"                        )), pluginData.configuration.buttons.build);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(dialog), "button_clean"                        )), pluginData.configuration.buttons.clean);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(dialog), "button_run"                          )), pluginData.configuration.buttons.run);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(dialog), "button_stop"                         )), pluginData.configuration.buttons.stop);

  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(dialog), "error_indicators"                    )), pluginData.configuration.errorIndicators);
  gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(g_object_get_data(G_OBJECT(dialog),   "error_indicator_color"               )), &pluginData.configuration.errorIndicatorColor);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(dialog), "warning_indicators"                  )), pluginData.configuration.warningIndicators);
  gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(g_object_get_data(G_OBJECT(dialog),   "warning_indicator_color"             )), &pluginData.configuration.warningIndicatorColor);

  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(dialog), "auto_save_all"                       )), pluginData.configuration.autoSaveAll);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(dialog), "add_project_regex_results"           )), pluginData.configuration.addProjectRegExResults);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(dialog), "auto_show_first_error"               )), pluginData.configuration.autoShowFirstError);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(dialog), "auto_show_first_warning"             )), pluginData.configuration.autoShowFirstWarning);

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

/**
 * @brief show project settings tab
 *
 * @param [in]  data notebook widget
 */

LOCAL gboolean showProjectSettingsTab(gpointer data)
{
  GtkWidget *notebook = (GtkWidget*)data;

  g_assert(notebook != NULL);

  gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook),
                                pluginData.widgets.projectProperiesTabIndex
                               );

  return FALSE;
}

/**
 * @brief add project settings
 *
 * @param [in]  object   object (not used)
 * @param [in]  notebook notebook
 * @param [in]  data     user data (not used)
 */

static void onProjectDialogOpen(GObject   *object,
                                GtkWidget *notebook,
                                gpointer  data
                               )
{
  UNUSED_VARIABLE(object);
  UNUSED_VARIABLE(data);

  // init project configuration settings
  pluginData.widgets.projectProperies = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 6));
  gtk_widget_set_margin_top(GTK_WIDGET(pluginData.widgets.projectProperies), 6);
  gtk_widget_set_margin_bottom(GTK_WIDGET(pluginData.widgets.projectProperies), 6);
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
        addGrid(grid, 0, 0, 1, newLabel(G_OBJECT(pluginData.widgets.projectProperies), NULL, "Build", NULL));
        addGrid(grid, 0, 1, 1, newEntry(G_OBJECT(pluginData.widgets.projectProperies), "command_line_build", "Command to build project."));
        addGrid(grid, 0, 2, 1, newWorkingDirectoryChooser(G_OBJECT(pluginData.widgets.projectProperies), "working_directory_build", "Working directory for build command."));

        addGrid(grid, 1, 0, 1, newLabel(G_OBJECT(pluginData.widgets.projectProperies), NULL, "Clean", NULL));
        addGrid(grid, 1, 1, 1, newEntry(G_OBJECT(pluginData.widgets.projectProperies), "command_line_clean",  "Command to clean."));
        addGrid(grid, 1, 2, 1, newWorkingDirectoryChooser(G_OBJECT(pluginData.widgets.projectProperies), "working_directory_clean", "Working directory for clean command."));

        addGrid(grid, 2, 0, 1, newLabel(G_OBJECT(pluginData.widgets.projectProperies), NULL, "All", NULL));
        addGrid(grid, 2, 1, 1, newEntry(G_OBJECT(pluginData.widgets.projectProperies), "command_line_make_all",  "Command to make all."));
        addGrid(grid, 2, 2, 1, newWorkingDirectoryChooser(G_OBJECT(pluginData.widgets.projectProperies), "working_directory_make_all", "Working directory for make all command."));

        addGrid(grid, 3, 0, 1, newLabel(G_OBJECT(pluginData.widgets.projectProperies), NULL, "Custom", NULL));
        addGrid(grid, 3, 1, 1, newEntry(G_OBJECT(pluginData.widgets.projectProperies), "command_line_make_custom_target",  "Command to make custom target."));
        addGrid(grid, 3, 2, 1, newWorkingDirectoryChooser(G_OBJECT(pluginData.widgets.projectProperies), "working_directory_make_custom_target", "Working directory for make custom target command."));

        addGrid(grid, 4, 0, 1, newLabel(G_OBJECT(pluginData.widgets.projectProperies), NULL, "Object", NULL));
        addGrid(grid, 4, 1, 1, newEntry(G_OBJECT(pluginData.widgets.projectProperies), "command_line_make_object",  "Command to make object."));
        addGrid(grid, 4, 2, 1, newWorkingDirectoryChooser(G_OBJECT(pluginData.widgets.projectProperies), "working_directory_make_object", "Working directory for make object command."));

        addGrid(grid, 5, 0, 1, newLabel(G_OBJECT(pluginData.widgets.projectProperies), NULL, "Run", NULL));
        addGrid(grid, 5, 1, 1, newEntry(G_OBJECT(pluginData.widgets.projectProperies), "command_line_run",  "Command to run application."));
        addGrid(grid, 5, 2, 1, newWorkingDirectoryChooser(G_OBJECT(pluginData.widgets.projectProperies), "working_directory_run", "Working directory for run application command."));
      }
      gtk_container_add(GTK_CONTAINER(frame), GTK_WIDGET(grid));
    }
    addBox(GTK_BOX(pluginData.widgets.projectProperies), FALSE, GTK_WIDGET(frame));

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
      addGrid(grid, 0, 0, 1, newLabel(G_OBJECT(pluginData.widgets.projectProperies), NULL, "Error regular expression", NULL));
      addGrid(grid, 0, 1, 1, newEntry(G_OBJECT(pluginData.widgets.projectProperies), "error_regex", "Regular expression to recognize errors"));

      addGrid(grid, 1, 0, 1, newLabel(G_OBJECT(pluginData.widgets.projectProperies), NULL, "Warning regular expression", NULL));
      addGrid(grid, 1, 1, 1, newEntry(G_OBJECT(pluginData.widgets.projectProperies), "warning_regex", "Regular expression to recognize warnings"));
    }
    addBox(GTK_BOX(pluginData.widgets.projectProperies), FALSE, GTK_WIDGET(grid));
  }
  gtk_widget_show_all(GTK_WIDGET(pluginData.widgets.projectProperies));

  // add project properties tab
  pluginData.widgets.projectProperiesTabIndex = gtk_notebook_append_page(GTK_NOTEBOOK(notebook), GTK_WIDGET(pluginData.widgets.projectProperies), gtk_label_new("Builder"));

  // set values
  gtk_entry_set_text(GTK_ENTRY(g_object_get_data(G_OBJECT(pluginData.widgets.projectProperies), "command_line_build"                  )), pluginData.projectProperties.commands.build.line->str);
  gtk_entry_set_text(GTK_ENTRY(g_object_get_data(G_OBJECT(pluginData.widgets.projectProperies), "working_directory_build"             )), pluginData.projectProperties.commands.build.workingDirectory->str);
  gtk_entry_set_text(GTK_ENTRY(g_object_get_data(G_OBJECT(pluginData.widgets.projectProperies), "command_line_clean"                  )), pluginData.projectProperties.commands.clean.line->str);
  gtk_entry_set_text(GTK_ENTRY(g_object_get_data(G_OBJECT(pluginData.widgets.projectProperies), "working_directory_clean"             )), pluginData.projectProperties.commands.clean.workingDirectory->str);
  gtk_entry_set_text(GTK_ENTRY(g_object_get_data(G_OBJECT(pluginData.widgets.projectProperies), "command_line_make_all"               )), pluginData.projectProperties.commands.makeAll.line->str);
  gtk_entry_set_text(GTK_ENTRY(g_object_get_data(G_OBJECT(pluginData.widgets.projectProperies), "working_directory_make_all"          )), pluginData.projectProperties.commands.makeAll.workingDirectory->str);
  gtk_entry_set_text(GTK_ENTRY(g_object_get_data(G_OBJECT(pluginData.widgets.projectProperies), "command_line_make_custom_target"     )), pluginData.projectProperties.commands.makeCustomTarget.line->str);
  gtk_entry_set_text(GTK_ENTRY(g_object_get_data(G_OBJECT(pluginData.widgets.projectProperies), "working_directory_make_custom_target")), pluginData.projectProperties.commands.makeCustomTarget.workingDirectory->str);
  gtk_entry_set_text(GTK_ENTRY(g_object_get_data(G_OBJECT(pluginData.widgets.projectProperies), "command_line_make_object"            )), pluginData.projectProperties.commands.makeObject.line->str);
  gtk_entry_set_text(GTK_ENTRY(g_object_get_data(G_OBJECT(pluginData.widgets.projectProperies), "working_directory_make_object"       )), pluginData.projectProperties.commands.makeObject.workingDirectory->str);
  gtk_entry_set_text(GTK_ENTRY(g_object_get_data(G_OBJECT(pluginData.widgets.projectProperies), "command_line_run"                    )), pluginData.projectProperties.commands.run.line->str);
  gtk_entry_set_text(GTK_ENTRY(g_object_get_data(G_OBJECT(pluginData.widgets.projectProperies), "working_directory_run"               )), pluginData.projectProperties.commands.run.workingDirectory->str);

  gtk_entry_set_text(GTK_ENTRY(g_object_get_data(G_OBJECT(pluginData.widgets.projectProperies), "error_regex")), pluginData.projectProperties.errorRegEx->str);
  gtk_entry_set_text(GTK_ENTRY(g_object_get_data(G_OBJECT(pluginData.widgets.projectProperies), "warning_regex")), pluginData.projectProperties.warningRegEx->str);

  if (pluginData.widgets.showProjectPropertiesTab)
  {
    // show projet settings tab
    pluginData.widgets.showProjectPropertiesTab = FALSE;
    g_idle_add(showProjectSettingsTab, GTK_NOTEBOOK(notebook));
  }
}

/**
 * @brief handle project properties dialog confirm: save values
 *
 * @param [in]  object   object (not used)
 * @param [in]  notebook notebook (not used)
 * @param [in]  data     user data (not used)
 */

static void onProjectDialogConfirmed(GObject   *object,
                                     GtkWidget *notebook,
                                     gpointer  data
                                    )
{
  UNUSED_VARIABLE(object);
  UNUSED_VARIABLE(notebook);
  UNUSED_VARIABLE(data);

  // get values
  g_string_assign(pluginData.projectProperties.commands.build.line,                        gtk_entry_get_text(GTK_ENTRY(g_object_get_data(G_OBJECT(pluginData.widgets.projectProperies), "command_line_build"))));
  g_string_assign(pluginData.projectProperties.commands.build.workingDirectory,            gtk_entry_get_text(GTK_ENTRY(g_object_get_data(G_OBJECT(pluginData.widgets.projectProperies), "working_directory_build"))));
  g_string_assign(pluginData.projectProperties.commands.clean.line,                        gtk_entry_get_text(GTK_ENTRY(g_object_get_data(G_OBJECT(pluginData.widgets.projectProperies), "command_line_clean"))));
  g_string_assign(pluginData.projectProperties.commands.clean.workingDirectory,            gtk_entry_get_text(GTK_ENTRY(g_object_get_data(G_OBJECT(pluginData.widgets.projectProperies), "working_directory_clean"))));
  g_string_assign(pluginData.projectProperties.commands.makeAll.line,                      gtk_entry_get_text(GTK_ENTRY(g_object_get_data(G_OBJECT(pluginData.widgets.projectProperies), "command_line_make_all"))));
  g_string_assign(pluginData.projectProperties.commands.makeAll.workingDirectory,          gtk_entry_get_text(GTK_ENTRY(g_object_get_data(G_OBJECT(pluginData.widgets.projectProperies), "working_directory_make_all"))));
  g_string_assign(pluginData.projectProperties.commands.makeCustomTarget.line,             gtk_entry_get_text(GTK_ENTRY(g_object_get_data(G_OBJECT(pluginData.widgets.projectProperies), "command_line_make_custom_target"))));
  g_string_assign(pluginData.projectProperties.commands.makeCustomTarget.workingDirectory, gtk_entry_get_text(GTK_ENTRY(g_object_get_data(G_OBJECT(pluginData.widgets.projectProperies), "working_directory_make_custom_target"))));
  g_string_assign(pluginData.projectProperties.commands.makeObject.line,                   gtk_entry_get_text(GTK_ENTRY(g_object_get_data(G_OBJECT(pluginData.widgets.projectProperies), "command_line_make_object"))));
  g_string_assign(pluginData.projectProperties.commands.makeObject.workingDirectory,       gtk_entry_get_text(GTK_ENTRY(g_object_get_data(G_OBJECT(pluginData.widgets.projectProperies), "working_directory_make_object"))));
  g_string_assign(pluginData.projectProperties.commands.run.line,                          gtk_entry_get_text(GTK_ENTRY(g_object_get_data(G_OBJECT(pluginData.widgets.projectProperies), "command_line_run"))));
  g_string_assign(pluginData.projectProperties.commands.run.workingDirectory,              gtk_entry_get_text(GTK_ENTRY(g_object_get_data(G_OBJECT(pluginData.widgets.projectProperies), "working_directory_run"))));

  g_string_assign(pluginData.projectProperties.errorRegEx,                                 gtk_entry_get_text(GTK_ENTRY(g_object_get_data(G_OBJECT(pluginData.widgets.projectProperies), "error_regex"))));
  g_string_assign(pluginData.projectProperties.warningRegEx,                               gtk_entry_get_text(GTK_ENTRY(g_object_get_data(G_OBJECT(pluginData.widgets.projectProperies), "warning_regex"))));
}

/**
 * @brief handle project properties close
 *
 * @param [in]  object   object (not used)
 * @param [in]  notebook notebook
 * @param [in]  data     user data (not used)
 */

static void onProjectDialogClose(GObject   *object,
                                 GtkWidget *notebook,
                                 gpointer  data
                                )
{
  UNUSED_VARIABLE(object);
  UNUSED_VARIABLE(data);

  // remove project properties tab
  gtk_notebook_remove_page(GTK_NOTEBOOK(notebook), pluginData.widgets.projectProperiesTabIndex);
}

/**
 * @brief load project cconfiguration
 *
 * @param [in]  object        object (not used)
 * @param [in]  configuration project configuration to load values from
 * @param [in]  data          user data (not used)
 */

LOCAL void onProjectOpen(GObject  *object,
                         GKeyFile *configuration,
                         gpointer data
                        )
{
  UNUSED_VARIABLE(object);
  UNUSED_VARIABLE(data);

  loadProjectConfiguration(configuration);
}

/**
 * @brief save project configuration
 *
 * @param [in]  object        object (not used)
 * @param [in]  configuration project configuration to save values to
 * @param [in]  data          user data (not used)
 */

LOCAL void onProjectSave(GObject  *object,
                         GKeyFile *configuration,
                         gpointer data
                        )
{
  UNUSED_VARIABLE(object);
  UNUSED_VARIABLE(data);

  saveProjectConfiguration(configuration);
}

/**
 * @brief plugin init
 *
 * @param [in]  plugin Geany plugin
 * @param [in]  data   user data (not used)
 */

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
  initCommands(&pluginData.configuration.commands);
  pluginData.configuration.regExStore             = gtk_list_store_new(MODEL_REGEX_COUNT,
                                                                       G_TYPE_STRING,
                                                                       G_TYPE_STRING,
                                                                       G_TYPE_INT,
                                                                       G_TYPE_STRING
                                                                      );
  pluginData.configuration.buttons.build          = TRUE;
  pluginData.configuration.buttons.clean          = TRUE;
  pluginData.configuration.buttons.run            = TRUE;
  pluginData.configuration.buttons.stop           = TRUE;
  pluginData.configuration.errorIndicators        = TRUE;
  pluginData.configuration.errorIndicatorColor    = DEFAULT_ERROR_INDICATOR_COLOR;
  pluginData.configuration.warningIndicators      = FALSE;
  pluginData.configuration.warningIndicatorColor  = DEFAULT_WARNING_INDICATOR_COLOR;
  pluginData.configuration.addProjectRegExResults = FALSE;
  pluginData.configuration.autoSaveAll            = FALSE;
  pluginData.configuration.autoShowFirstError     = FALSE;
  pluginData.configuration.autoShowFirstWarning   = FALSE;
  initBuildMenuCommands(&pluginData.projectProperties.buildMenuCommands);
  initCommands(&pluginData.projectProperties.commands);
  pluginData.projectProperties.errorRegEx         = g_string_new(NULL);
  pluginData.projectProperties.warningRegEx       = g_string_new(NULL);

  pluginData.widgets.projectProperies             = NULL;
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

  // load global configuration
  loadConfiguration();

  // init GUI elements
  initToolbar(plugin);
  initTab(plugin);
  initKeyBinding(plugin);

  return TRUE;
}

/**
 * @brief plugin cleanup
 *
 * @param [in]  plugin Geany plugin
 * @param [in]  data   user data (not used)
 */

LOCAL void cleanup(GeanyPlugin *plugin, gpointer data)
{
  UNUSED_VARIABLE(data);

  doneTab(plugin);
  doneToolbar(plugin);

  g_string_free(pluginData.build.lastCustomTarget, TRUE);
  string_stack_free(pluginData.build.directoryPrefixStack);
  gtk_tree_path_free(pluginData.build.messagesTreePath);
  g_string_free(pluginData.build.workingDirectory, TRUE);
  g_string_free(pluginData.projectProperties.warningRegEx, TRUE);
  g_string_free(pluginData.projectProperties.errorRegEx, TRUE);
  doneCommands(&pluginData.projectProperties.commands);
  doneBuildMenuCommands(&pluginData.projectProperties.buildMenuCommands);
  doneCommands(&pluginData.configuration.commands);
  g_free(pluginData.configuration.filePath);
}

/**
 * @brief load Geany module
 *
 * @param [in]  plugin Geany plugin
 */

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
