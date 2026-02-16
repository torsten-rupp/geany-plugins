/***********************************************************************\
*
* $Revision: 11537 $
* $Date: 2021-06-20 08:45:38 +0200 (Sun, 20 Jun 2021) $
* $Author: torsten $
* Contents: Geany builder plugin
* Systems: all
*
\***********************************************************************/

#ifndef BUILDER_H
#define BUILDER_H

/****************************** Includes *******************************/

/****************** Conditional compilation switches *******************/

/***************************** Constants *******************************/

/***************************** Datatypes *******************************/

/***************************** Variables *******************************/

/****************************** Macros *********************************/
#define INLINE inline
#define LOCAL static
#define LOCAL_INLINE static inline
#define UNUSED_VARIABLE(name) (void)name

#define ARRAY_SIZE(array) (sizeof(array) / sizeof(array[0]))

#define CALLBACK(function,userData) function, userData

#define LAMBDA(functionReturnType, functionSignature, functionBody) \
  ({ \
    auto functionReturnType __closure__ functionSignature; \
    functionReturnType __closure__ functionSignature functionBody \
    __closure__; \
  })

// global Geany data
extern GeanyPlugin *geany_plugin;
extern GeanyData   *geany_data;

/***************************** Forwards ********************************/

/***************************** Functions *******************************/

#endif // BUILDER_H

/* end of file */
