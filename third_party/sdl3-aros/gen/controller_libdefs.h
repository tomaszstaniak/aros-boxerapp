#ifndef _CONTROLLER_LIBDEFS_H
#define _CONTROLLER_LIBDEFS_H

#include <exec/types.h>

#define GM_UNIQUENAME(n) Controller_ ## n
#define LIBBASE          ControllerBase
#define LIBBASETYPE      struct controllerbase
#define LIBBASETYPEPTR   struct controllerbase *
#define MOD_NAME_STRING  "controller.hidd"
#define MOD_DATE_STRING  "2.10.2026"
#define MOD_VERS_STRING  "51.2"
#define VERSION_NUMBER   51
#define MAJOR_VERSION    51
#define REVISION_NUMBER  2
#define MINOR_VERSION    2
#define VERSION_STRING   "$VER: controller.hidd 51.2 (2.10.2026)\r\n"
#define COPYRIGHT_STRING ""
#define LIBEND           GM_UNIQUENAME(End)
#define LIBFUNCTABLE     GM_UNIQUENAME(FuncTable)
#define RESIDENTPRI      45
#define RESIDENTFLAGS    RTF_COLDSTART|RTF_AUTOINIT
#define FUNCTIONS_COUNT  4
#include <hidd/input.h>
#include <hidd/controller.h>
#include "controller_intern.h"
#define GM_SYSBASE_FIELD(lh) (((LIBBASETYPEPTR)lh)->csd.cs_SysBase)
#define GM_OOPBASE_FIELD(lh) (((LIBBASETYPEPTR)lh)->csd.cs_OOPBase)
#define Controller_STORE_CLASSPTR 1
#define GM_CLASSPTR_FIELD(lh) (((LIBBASETYPEPTR)lh)->csd.controllerClass)
#define Controller_CLASSPTR_FIELD(lh) (((LIBBASETYPEPTR)lh)->csd.controllerClass)
#define ControllerHW_STORE_CLASSPTR 1
#define ControllerHW_CLASSPTR_FIELD(lh) (((LIBBASETYPEPTR)lh)->csd.hwClass)

#endif /* _CONTROLLER_LIBDEFS_H */
