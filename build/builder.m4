AC_DEFUN([GP_CHECK_BUILDER],
[
    GP_ARG_DISABLE([Builder], [auto])
    GP_COMMIT_PLUGIN_STATUS([Builder])
    AC_CONFIG_FILES([
        Builder/Makefile
        Builder/src/Makefile
    ])
])
