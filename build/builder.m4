AC_DEFUN([GP_CHECK_BUILDER],
[
    GP_ARG_DISABLE([Builder], [auto])
    GP_CHECK_PLUGIN_DEPS([Builder], [BUILDER],
                         [$GP_GTK_PACKAGE >= 3.0])
    GP_COMMIT_PLUGIN_STATUS([Builder])
    AC_CONFIG_FILES([
        builder/Makefile
        builder/src/Makefile
    ])
])
