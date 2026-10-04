/*
    *** Automatically generated from 'controller.conf'. Edits will be lost. ***
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.
*/
/* For comments and explanation of generated code look in writestart.c source code
   of the genmodule program */
#define MODULE_START_INTERNAL

#include <exec/types.h>
#include <exec/libraries.h>
#include <exec/resident.h>
#include <aros/libcall.h>
#include <aros/asmcall.h>
#include <aros/symbolsets.h>
#include <aros/genmodule.h>
#include <dos/dos.h>

#include "controller_libdefs.h"


#undef SysBase
#undef OOPBase
#undef UtilityBase

#include <proto/exec.h>
#include <proto/task.h>
#include <proto/alib.h>

#define LIBBASESIZE (sizeof(LIBBASETYPE) + sizeof(struct Library *)*0)
#if !defined(GM_CLASSPTR_FIELD) && !defined(Controller_CLASSPTR_FIELD)
static APTR GM_UNIQUENAME(ControllerClass);
#define GM_CLASSPTR_FIELD(LIBBASE) (GM_UNIQUENAME(ControllerClass))
#define Controller_CLASSPTR_FIELD(LIBBASE) (GM_UNIQUENAME(ControllerClass))
#define Controller_STORE_CLASSPTR 1
#elif defined(GM_CLASSPTR_FIELD) && !defined(Controller_CLASSPTR_FIELD)
#define Controller_CLASSPTR_FIELD(LIBBASE) (GM_CLASSPTR_FIELD(LIBBASE))
#elif !defined(GM_CLASSPTR_FIELD) && defined(Controller_CLASSPTR_FIELD)
#define GM_CLASSPTR_FIELD(LIBBASE) (Controller_CLASSPTR_FIELD(LIBBASE))
#endif
#if !defined(ControllerHW_CLASSPTR_FIELD)
static APTR GM_UNIQUENAME(ControllerHWClass);
#define ControllerHW_CLASSPTR_FIELD(LIBBASE) (GM_UNIQUENAME(ControllerHWClass))
#define ControllerHW_STORE_CLASSPTR 1
#endif

#include <intuition/classes.h>
#include <intuition/classusr.h>

#include <proto/utility.h>
#include <proto/intuition.h>

#include <aros/symbolsets.h>

#include <proto/oop.h>
#include <oop/oop.h>
#include <hidd/hidd.h>

extern const char GM_UNIQUENAME(End)[];
extern const APTR GM_UNIQUENAME(FuncTable)[];
struct InitTable
{
    IPTR              Size;
    const APTR       *FuncTable;
    struct DataTable *DataTable;
    APTR              InitLibTable;
};
static const struct InitTable GM_UNIQUENAME(InitTable);

extern const char GM_UNIQUENAME(LibName)[];
extern const char GM_UNIQUENAME(LibID)[];
extern const char GM_UNIQUENAME(Copyright)[];

#define __freebase(LIBBASE)\
do {\
    UWORD negsize, possize;\
    UBYTE *negptr = (UBYTE *)LIBBASE;\
    negsize = ((struct Library *)LIBBASE)->lib_NegSize;\
    negptr -= negsize;\
    possize = ((struct Library *)LIBBASE)->lib_PosSize;\
    FreeMem (negptr, negsize+possize);\
} while(0)

AROS_UFP3 (LIBBASETYPEPTR, GM_UNIQUENAME(InitLib),
    AROS_UFPA(LIBBASETYPEPTR, LIBBASE, D0),
    AROS_UFPA(BPTR, segList, A0),
    AROS_UFPA(struct ExecBase *, sysBase, A6)
);
AROS_LD1(BPTR, GM_UNIQUENAME(ExpungeLib),
    AROS_LDA(LIBBASETYPEPTR, extralh, D0),
    LIBBASETYPEPTR, LIBBASE, 3, Controller
);

__section(".text.romtag") struct Resident const GM_UNIQUENAME(ROMTag) =
{
    RTC_MATCHWORD,
    (struct Resident *)&GM_UNIQUENAME(ROMTag),
    (APTR)&GM_UNIQUENAME(End),
    RESIDENTFLAGS,
    VERSION_NUMBER,
    NT_LIBRARY,
    RESIDENTPRI,
    (CONST_STRPTR)&GM_UNIQUENAME(LibName)[0],
    (CONST_STRPTR)&GM_UNIQUENAME(LibID)[6],
    (APTR)&GM_UNIQUENAME(InitTable)
};

__section(".text.romtag") static struct InitTable const GM_UNIQUENAME(InitTable) =
{
    LIBBASESIZE,
    &GM_UNIQUENAME(FuncTable)[0],
    NULL,
    (APTR)GM_UNIQUENAME(InitLib)
};

#if defined(MOD_NAME_STRING)
__section(".text.romtag") const char GM_UNIQUENAME(LibName)[] = MOD_NAME_STRING;
#endif
#if defined(VERSION_STRING)
__section(".text.romtag") const char GM_UNIQUENAME(LibID)[] = VERSION_STRING;
#endif
#if defined(COPYRIGHT_STRING)
__section(".text.romtag") const char GM_UNIQUENAME(Copyright)[] = COPYRIGHT_STRING;
#endif

THIS_PROGRAM_HANDLES_SYMBOLSET(INIT)
THIS_PROGRAM_HANDLES_SYMBOLSET(EXIT)
DECLARESET(INIT)
DECLARESET(EXIT)
THIS_PROGRAM_HANDLES_SYMBOLSET(PROGRAM_ENTRIES)
DECLARESET(PROGRAM_ENTRIES)
THIS_PROGRAM_HANDLES_SYMBOLSET(CTORS)
THIS_PROGRAM_HANDLES_SYMBOLSET(DTORS)
DECLARESET(CTORS)
DECLARESET(DTORS)
THIS_PROGRAM_HANDLES_SYMBOLSET(INIT_ARRAY)
THIS_PROGRAM_HANDLES_SYMBOLSET(FINI_ARRAY)
DECLARESET(INIT_ARRAY)
DECLARESET(FINI_ARRAY)
THIS_PROGRAM_HANDLES_SYMBOLSET(INITLIB)
THIS_PROGRAM_HANDLES_SYMBOLSET(EXPUNGELIB)
DECLARESET(INITLIB)
DECLARESET(EXPUNGELIB)
THIS_PROGRAM_HANDLES_SYMBOLSET(LIBS)
DECLARESET(LIBS)
THIS_PROGRAM_HANDLES_SYMBOLSET(OPENLIB)
THIS_PROGRAM_HANDLES_SYMBOLSET(CLOSELIB)
DECLARESET(OPENLIB)
DECLARESET(CLOSELIB)
THIS_PROGRAM_HANDLES_SYMBOLSET(CLASSESINIT)
THIS_PROGRAM_HANDLES_SYMBOLSET(CLASSESEXPUNGE)
DECLARESET(CLASSESINIT)
DECLARESET(CLASSESEXPUNGE)
#define ADD2INITCLASSES(symbol, pri) ADD2SET(symbol, CLASSESINIT, pri)
#define ADD2EXPUNGECLASSES(symbol, pri) ADD2SET(symbol, CLASSESEXPUNGE, pri)

static const struct __aros_libinit_sets GM_UNIQUENAME(InitSets) =
{
    SETNAME(LIBS),
    NULL,
    SETNAME(INIT),
    SETNAME(CLASSESINIT),
    SETNAME(CTORS),
    SETNAME(INIT_ARRAY),
    SETNAME(INITLIB),
    SETNAME(EXPUNGELIB),
    SETNAME(FINI_ARRAY),
    SETNAME(DTORS),
    SETNAME(EXIT),
    SETNAME(CLASSESEXPUNGE)
};

extern const LONG __aros_libreq_SysBase __attribute__((weak));

AROS_UFH3 (LIBBASETYPEPTR, GM_UNIQUENAME(InitLib),
    AROS_UFHA(LIBBASETYPEPTR, LIBBASE, D0),
    AROS_UFHA(BPTR, segList, A0),
    AROS_UFHA(struct ExecBase *, sysBase, A6)
)
{
    AROS_USERFUNC_INIT

    int ok;
    struct ExecBase *SysBase = sysBase;

    if (!SysBase || SysBase->LibNode.lib_Version < __aros_libreq_SysBase)
        return NULL;

#ifdef GM_OOPBASE_FIELD
    GM_OOPBASE_FIELD(LIBBASE) = OpenLibrary("oop.library",0);
    if (GM_OOPBASE_FIELD(LIBBASE) == NULL)
        return NULL;
#endif

#ifdef GM_SYSBASE_FIELD
    GM_SYSBASE_FIELD(LIBBASE) = (APTR)SysBase;
#endif

#if defined(REVISION_NUMBER)
    ((struct Library *)LIBBASE)->lib_Revision = REVISION_NUMBER;
#endif
    ok = set_libinit(&GM_UNIQUENAME(InitSets), LIBBASE);

    if (!ok)
    {

        __freebase(LIBBASE);
        return NULL;
    }
    else
    {
        return  LIBBASE;
    }

    AROS_USERFUNC_EXIT
}

AROS_LH1 (LIBBASETYPEPTR, GM_UNIQUENAME(OpenLib),
    AROS_LHA (ULONG, version, D0),
    LIBBASETYPEPTR, LIBBASE, 1, Controller
)
{
    AROS_LIBFUNC_INIT

    if ( set_call_libfuncs(SETNAME(OPENLIB), 1, 1, LIBBASE) )
    {
        ((struct Library *)LIBBASE)->lib_OpenCnt++;
        ((struct Library *)LIBBASE)->lib_Flags &= ~LIBF_DELEXP;
        return LIBBASE;
    }

    return NULL;

    AROS_LIBFUNC_EXIT
}

AROS_LH0 (BPTR, GM_UNIQUENAME(CloseLib),
    LIBBASETYPEPTR, LIBBASE, 2, Controller
)
{
    AROS_LIBFUNC_INIT

    ((struct Library *)LIBBASE)->lib_OpenCnt--;
    set_call_libfuncs(SETNAME(CLOSELIB), -1, 0, LIBBASE);

    return BNULL;

    AROS_LIBFUNC_EXIT
}

AROS_LH1 (BPTR, GM_UNIQUENAME(ExpungeLib),
    AROS_LHA(LIBBASETYPEPTR, extralh, D0),
    LIBBASETYPEPTR, LIBBASE, 3, Controller
)
{
    AROS_LIBFUNC_INIT


    return BNULL;

    AROS_LIBFUNC_EXIT
}

AROS_LH0 (LIBBASETYPEPTR, GM_UNIQUENAME(ExtFuncLib),
    LIBBASETYPEPTR, LIBBASE, 4, Controller
)
{
    AROS_LIBFUNC_INIT
    return NULL;
    AROS_LIBFUNC_EXIT
}

DEFINESET(INIT)
DEFINESET(EXIT)
DEFINESET(CTORS)
DEFINESET(DTORS)
DEFINESET(INIT_ARRAY)
DEFINESET(FINI_ARRAY)
DEFINESET(INITLIB)
DEFINESET(EXPUNGELIB)
DEFINESET(OPENLIB)
DEFINESET(CLOSELIB)
DEFINESET(CLASSESINIT)
DEFINESET(CLASSESEXPUNGE)


__section(".text.romtag") const APTR GM_UNIQUENAME(FuncTable)[]=
{
    &AROS_SLIB_ENTRY(GM_UNIQUENAME(OpenLib),Controller,1),
    &AROS_SLIB_ENTRY(GM_UNIQUENAME(CloseLib),Controller,2),
    &AROS_SLIB_ENTRY(GM_UNIQUENAME(ExpungeLib),Controller,3),
    &AROS_SLIB_ENTRY(GM_UNIQUENAME(ExtFuncLib),Controller,4),
    (void *)-1
};
/* Initialisation routines of a OOP class */
/* =======================================*/

#   define Controller_DATA_SIZE (sizeof(struct ControllerInstData))
IPTR Controller__Root__New(OOP_Class *cl, OOP_Object *o, OOP_Msg msg);
IPTR Controller__Root__Dispose(OOP_Class *cl, OOP_Object *o, OOP_Msg msg);
IPTR Controller__Root__Get(OOP_Class *cl, OOP_Object *o, OOP_Msg msg);
IPTR Controller__Root__Set(OOP_Class *cl, OOP_Object *o, OOP_Msg msg);
IPTR Controller__Hidd_Controller__GetReading(OOP_Class *cl, OOP_Object *o, OOP_Msg msg);
IPTR Controller__Hidd_Controller__GetControlInfo(OOP_Class *cl, OOP_Object *o, OOP_Msg msg);
IPTR Controller__Hidd_Controller__GetOutputInfo(OOP_Class *cl, OOP_Object *o, OOP_Msg msg);
IPTR Controller__Hidd_Controller__GetBinding(OOP_Class *cl, OOP_Object *o, OOP_Msg msg);
IPTR Controller__Hidd_Controller__GetLabel(OOP_Class *cl, OOP_Object *o, OOP_Msg msg);
IPTR Controller__Hidd_Controller__GetPowerInfo(OOP_Class *cl, OOP_Object *o, OOP_Msg msg);
IPTR Controller__Hidd_Controller__GetSensorData(OOP_Class *cl, OOP_Object *o, OOP_Msg msg);
IPTR Controller__Hidd_Controller__GetTouchpadFinger(OOP_Class *cl, OOP_Object *o, OOP_Msg msg);
IPTR Controller__Hidd_Controller__GetEvent(OOP_Class *cl, OOP_Object *o, OOP_Msg msg);
IPTR Controller__Hidd_Controller__GetBindings(OOP_Class *cl, OOP_Object *o, OOP_Msg msg);
IPTR Controller__Hidd_Controller__SetRumble(OOP_Class *cl, OOP_Object *o, OOP_Msg msg);
IPTR Controller__Hidd_Controller__SetLED(OOP_Class *cl, OOP_Object *o, OOP_Msg msg);
IPTR Controller__Hidd_Controller__SetPlayerIndex(OOP_Class *cl, OOP_Object *o, OOP_Msg msg);
IPTR Controller__Hidd_Controller__SendEffect(OOP_Class *cl, OOP_Object *o, OOP_Msg msg);
IPTR Controller__Hidd_Controller__SetSensorsEnabled(OOP_Class *cl, OOP_Object *o, OOP_Msg msg);
IPTR Controller__Hidd_Controller__UploadEffect(OOP_Class *cl, OOP_Object *o, OOP_Msg msg);
IPTR Controller__Hidd_Controller__UpdateEffect(OOP_Class *cl, OOP_Object *o, OOP_Msg msg);
IPTR Controller__Hidd_Controller__PlayEffect(OOP_Class *cl, OOP_Object *o, OOP_Msg msg);
IPTR Controller__Hidd_Controller__StopEffect(OOP_Class *cl, OOP_Object *o, OOP_Msg msg);
IPTR Controller__Hidd_Controller__RemoveEffect(OOP_Class *cl, OOP_Object *o, OOP_Msg msg);
IPTR Controller__Hidd_Controller__SetGain(OOP_Class *cl, OOP_Object *o, OOP_Msg msg);
IPTR Controller__Hidd_Controller__SetAutocenter(OOP_Class *cl, OOP_Object *o, OOP_Msg msg);
IPTR Controller__Hidd_Controller__PushValue(OOP_Class *cl, OOP_Object *o, OOP_Msg msg);
IPTR Controller__Hidd_Controller__PushReport(OOP_Class *cl, OOP_Object *o, OOP_Msg msg);
IPTR Controller__Hidd_Controller__PushTouch(OOP_Class *cl, OOP_Object *o, OOP_Msg msg);
IPTR Controller__Hidd_Controller__PushSensor(OOP_Class *cl, OOP_Object *o, OOP_Msg msg);
IPTR Controller__Hidd_Controller__PushPower(OOP_Class *cl, OOP_Object *o, OOP_Msg msg);
IPTR Controller__Hidd_Controller__SetConnected(OOP_Class *cl, OOP_Object *o, OOP_Msg msg);


/*** Library startup and shutdown *******************************************/
static int OOP_Controller_Startup(LIBBASETYPEPTR LIBBASE)
{
#ifdef GM_OOPBASE_FIELD
    struct Library *OOPBase = GM_OOPBASE_FIELD(LIBBASE);
#endif
    OOP_AttrBase MetaAttrBase = OOP_ObtainAttrBase(IID_Meta);
    OOP_Class *cl = NULL;

    struct OOP_MethodDescr Controller_Root_descr[] =
    {
        {(OOP_MethodFunc)Controller__Root__New, moRoot_New},
        {(OOP_MethodFunc)Controller__Root__Dispose, moRoot_Dispose},
        {(OOP_MethodFunc)Controller__Root__Get, moRoot_Get},
        {(OOP_MethodFunc)Controller__Root__Set, moRoot_Set},
        {NULL, 0}
    };
#define NUM_Controller_Root_METHODS 4

    struct OOP_MethodDescr Controller_Hidd_Controller_descr[] =
    {
        {(OOP_MethodFunc)Controller__Hidd_Controller__GetReading, moHidd_Controller_GetReading},
        {(OOP_MethodFunc)Controller__Hidd_Controller__GetControlInfo, moHidd_Controller_GetControlInfo},
        {(OOP_MethodFunc)Controller__Hidd_Controller__GetOutputInfo, moHidd_Controller_GetOutputInfo},
        {(OOP_MethodFunc)Controller__Hidd_Controller__GetBinding, moHidd_Controller_GetBinding},
        {(OOP_MethodFunc)Controller__Hidd_Controller__GetLabel, moHidd_Controller_GetLabel},
        {(OOP_MethodFunc)Controller__Hidd_Controller__GetPowerInfo, moHidd_Controller_GetPowerInfo},
        {(OOP_MethodFunc)Controller__Hidd_Controller__GetSensorData, moHidd_Controller_GetSensorData},
        {(OOP_MethodFunc)Controller__Hidd_Controller__GetTouchpadFinger, moHidd_Controller_GetTouchpadFinger},
        {(OOP_MethodFunc)Controller__Hidd_Controller__GetEvent, moHidd_Controller_GetEvent},
        {(OOP_MethodFunc)Controller__Hidd_Controller__GetBindings, moHidd_Controller_GetBindings},
        {(OOP_MethodFunc)Controller__Hidd_Controller__SetRumble, moHidd_Controller_SetRumble},
        {(OOP_MethodFunc)Controller__Hidd_Controller__SetLED, moHidd_Controller_SetLED},
        {(OOP_MethodFunc)Controller__Hidd_Controller__SetPlayerIndex, moHidd_Controller_SetPlayerIndex},
        {(OOP_MethodFunc)Controller__Hidd_Controller__SendEffect, moHidd_Controller_SendEffect},
        {(OOP_MethodFunc)Controller__Hidd_Controller__SetSensorsEnabled, moHidd_Controller_SetSensorsEnabled},
        {(OOP_MethodFunc)Controller__Hidd_Controller__UploadEffect, moHidd_Controller_UploadEffect},
        {(OOP_MethodFunc)Controller__Hidd_Controller__UpdateEffect, moHidd_Controller_UpdateEffect},
        {(OOP_MethodFunc)Controller__Hidd_Controller__PlayEffect, moHidd_Controller_PlayEffect},
        {(OOP_MethodFunc)Controller__Hidd_Controller__StopEffect, moHidd_Controller_StopEffect},
        {(OOP_MethodFunc)Controller__Hidd_Controller__RemoveEffect, moHidd_Controller_RemoveEffect},
        {(OOP_MethodFunc)Controller__Hidd_Controller__SetGain, moHidd_Controller_SetGain},
        {(OOP_MethodFunc)Controller__Hidd_Controller__SetAutocenter, moHidd_Controller_SetAutocenter},
        {(OOP_MethodFunc)Controller__Hidd_Controller__PushValue, moHidd_Controller_PushValue},
        {(OOP_MethodFunc)Controller__Hidd_Controller__PushReport, moHidd_Controller_PushReport},
        {(OOP_MethodFunc)Controller__Hidd_Controller__PushTouch, moHidd_Controller_PushTouch},
        {(OOP_MethodFunc)Controller__Hidd_Controller__PushSensor, moHidd_Controller_PushSensor},
        {(OOP_MethodFunc)Controller__Hidd_Controller__PushPower, moHidd_Controller_PushPower},
        {(OOP_MethodFunc)Controller__Hidd_Controller__SetConnected, moHidd_Controller_SetConnected},
        {NULL, 0}
    };
#define NUM_Controller_Hidd_Controller_METHODS 28

    struct OOP_InterfaceDescr Controller_ifdescr[] =
    {
        {Controller_Root_descr, IID_Root, NUM_Controller_Root_METHODS},
        {Controller_Hidd_Controller_descr, IID_Hidd_Controller, NUM_Controller_Hidd_Controller_METHODS},
        {NULL, NULL}
    };

    struct TagItem Controller_tags[] =
    {
        {aMeta_SuperID, (IPTR)CLID_Hidd_Input},
        {aMeta_InterfaceDescr, (IPTR)Controller_ifdescr},
        {aMeta_InstSize, (IPTR)Controller_DATA_SIZE},
        {aMeta_ID, (IPTR)CLID_Hidd_Controller},
        {TAG_DONE, (IPTR)0}
    };

    if (MetaAttrBase == 0)
        return FALSE;

    cl = OOP_NewObject(NULL, CLID_HiddMeta, Controller_tags);
    if (cl != NULL)
    {
        cl->UserData = (APTR)LIBBASE;
        Controller_CLASSPTR_FIELD(LIBBASE) = cl;
        OOP_AddClass(cl);
    }

    OOP_ReleaseAttrBase(IID_Meta);
    return cl != NULL;
}
static void OOP_Controller_Shutdown(LIBBASETYPEPTR LIBBASE)
{
#ifdef GM_OOPBASE_FIELD
    struct Library *OOPBase = GM_OOPBASE_FIELD(LIBBASE);
#endif
    if (Controller_CLASSPTR_FIELD(LIBBASE) != NULL)
    {
        OOP_RemoveClass(Controller_CLASSPTR_FIELD(LIBBASE));
        OOP_DisposeObject((OOP_Object *)Controller_CLASSPTR_FIELD(LIBBASE));
    }

}
ADD2INITCLASSES(OOP_Controller_Startup, -1)
ADD2EXPUNGECLASSES(OOP_Controller_Shutdown, -1)
/* Initialisation routines of a OOP class */
/* =======================================*/

#   define ControllerHW_DATA_SIZE (sizeof(struct ControllerHWData))
IPTR ControllerHW__Root__New(OOP_Class *cl, OOP_Object *o, OOP_Msg msg);
IPTR ControllerHW__Root__Dispose(OOP_Class *cl, OOP_Object *o, OOP_Msg msg);
IPTR ControllerHW__Root__Get(OOP_Class *cl, OOP_Object *o, OOP_Msg msg);
IPTR ControllerHW__Root__Set(OOP_Class *cl, OOP_Object *o, OOP_Msg msg);
IPTR ControllerHW__HW__AddDriver(OOP_Class *cl, OOP_Object *o, OOP_Msg msg);
IPTR ControllerHW__HW__RemoveDriver(OOP_Class *cl, OOP_Object *o, OOP_Msg msg);
IPTR ControllerHW__HW__SetUpDriver(OOP_Class *cl, OOP_Object *o, OOP_Msg msg);
IPTR ControllerHW__HW__CleanUpDriver(OOP_Class *cl, OOP_Object *o, OOP_Msg msg);
IPTR ControllerHW__HW_Input__PushEvent(OOP_Class *cl, OOP_Object *o, OOP_Msg msg);
IPTR ControllerHW__HW_Controller__FindDevice(OOP_Class *cl, OOP_Object *o, OOP_Msg msg);
IPTR ControllerHW__HW_Controller__GetDeviceIDs(OOP_Class *cl, OOP_Object *o, OOP_Msg msg);
IPTR ControllerHW__HW_Controller__AddMapping(OOP_Class *cl, OOP_Object *o, OOP_Msg msg);
IPTR ControllerHW__HW_Controller__RemoveMapping(OOP_Class *cl, OOP_Object *o, OOP_Msg msg);
IPTR ControllerHW__HW_Controller__Configure(OOP_Class *cl, OOP_Object *o, OOP_Msg msg);
IPTR ControllerHW__HW_Controller__AssignSlot(OOP_Class *cl, OOP_Object *o, OOP_Msg msg);
IPTR ControllerHW__HW_Controller__GetSlotDevice(OOP_Class *cl, OOP_Object *o, OOP_Msg msg);
IPTR ControllerHW__HW_Controller__ReadJoyPort(OOP_Class *cl, OOP_Object *o, OOP_Msg msg);
IPTR ControllerHW__HW_Controller__SetJoyPortAttrs(OOP_Class *cl, OOP_Object *o, OOP_Msg msg);


/*** Library startup and shutdown *******************************************/
static int OOP_ControllerHW_Startup(LIBBASETYPEPTR LIBBASE)
{
#ifdef GM_OOPBASE_FIELD
    struct Library *OOPBase = GM_OOPBASE_FIELD(LIBBASE);
#endif
    OOP_AttrBase MetaAttrBase = OOP_ObtainAttrBase(IID_Meta);
    OOP_Class *cl = NULL;

    struct OOP_MethodDescr ControllerHW_Root_descr[] =
    {
        {(OOP_MethodFunc)ControllerHW__Root__New, moRoot_New},
        {(OOP_MethodFunc)ControllerHW__Root__Dispose, moRoot_Dispose},
        {(OOP_MethodFunc)ControllerHW__Root__Get, moRoot_Get},
        {(OOP_MethodFunc)ControllerHW__Root__Set, moRoot_Set},
        {NULL, 0}
    };
#define NUM_ControllerHW_Root_METHODS 4

    struct OOP_MethodDescr ControllerHW_HW_descr[] =
    {
        {(OOP_MethodFunc)ControllerHW__HW__AddDriver, moHW_AddDriver},
        {(OOP_MethodFunc)ControllerHW__HW__RemoveDriver, moHW_RemoveDriver},
        {(OOP_MethodFunc)ControllerHW__HW__SetUpDriver, moHW_SetUpDriver},
        {(OOP_MethodFunc)ControllerHW__HW__CleanUpDriver, moHW_CleanUpDriver},
        {NULL, 0}
    };
#define NUM_ControllerHW_HW_METHODS 4

    struct OOP_MethodDescr ControllerHW_HW_Input_descr[] =
    {
        {(OOP_MethodFunc)ControllerHW__HW_Input__PushEvent, moHW_Input_PushEvent},
        {NULL, 0}
    };
#define NUM_ControllerHW_HW_Input_METHODS 1

    struct OOP_MethodDescr ControllerHW_HW_Controller_descr[] =
    {
        {(OOP_MethodFunc)ControllerHW__HW_Controller__FindDevice, moHW_Controller_FindDevice},
        {(OOP_MethodFunc)ControllerHW__HW_Controller__GetDeviceIDs, moHW_Controller_GetDeviceIDs},
        {(OOP_MethodFunc)ControllerHW__HW_Controller__AddMapping, moHW_Controller_AddMapping},
        {(OOP_MethodFunc)ControllerHW__HW_Controller__RemoveMapping, moHW_Controller_RemoveMapping},
        {(OOP_MethodFunc)ControllerHW__HW_Controller__Configure, moHW_Controller_Configure},
        {(OOP_MethodFunc)ControllerHW__HW_Controller__AssignSlot, moHW_Controller_AssignSlot},
        {(OOP_MethodFunc)ControllerHW__HW_Controller__GetSlotDevice, moHW_Controller_GetSlotDevice},
        {(OOP_MethodFunc)ControllerHW__HW_Controller__ReadJoyPort, moHW_Controller_ReadJoyPort},
        {(OOP_MethodFunc)ControllerHW__HW_Controller__SetJoyPortAttrs, moHW_Controller_SetJoyPortAttrs},
        {NULL, 0}
    };
#define NUM_ControllerHW_HW_Controller_METHODS 9

    struct OOP_InterfaceDescr ControllerHW_ifdescr[] =
    {
        {ControllerHW_Root_descr, IID_Root, NUM_ControllerHW_Root_METHODS},
        {ControllerHW_HW_descr, IID_HW, NUM_ControllerHW_HW_METHODS},
        {ControllerHW_HW_Input_descr, IID_HW_Input, NUM_ControllerHW_HW_Input_METHODS},
        {ControllerHW_HW_Controller_descr, IID_HW_Controller, NUM_ControllerHW_HW_Controller_METHODS},
        {NULL, NULL}
    };

    struct TagItem ControllerHW_tags[] =
    {
        {aMeta_SuperID, (IPTR)CLID_HW_Input},
        {aMeta_InterfaceDescr, (IPTR)ControllerHW_ifdescr},
        {aMeta_InstSize, (IPTR)ControllerHW_DATA_SIZE},
        {aMeta_ID, (IPTR)CLID_HW_Controller},
        {TAG_DONE, (IPTR)0}
    };

    if (MetaAttrBase == 0)
        return FALSE;

    cl = OOP_NewObject(NULL, CLID_HiddMeta, ControllerHW_tags);
    if (cl != NULL)
    {
        cl->UserData = (APTR)LIBBASE;
        ControllerHW_CLASSPTR_FIELD(LIBBASE) = cl;
        OOP_AddClass(cl);
    }

    OOP_ReleaseAttrBase(IID_Meta);
    return cl != NULL;
}
static void OOP_ControllerHW_Shutdown(LIBBASETYPEPTR LIBBASE)
{
#ifdef GM_OOPBASE_FIELD
    struct Library *OOPBase = GM_OOPBASE_FIELD(LIBBASE);
#endif
    if (ControllerHW_CLASSPTR_FIELD(LIBBASE) != NULL)
    {
        OOP_RemoveClass(ControllerHW_CLASSPTR_FIELD(LIBBASE));
        OOP_DisposeObject((OOP_Object *)ControllerHW_CLASSPTR_FIELD(LIBBASE));
    }

}
ADD2INITCLASSES(OOP_ControllerHW_Startup, -1)
ADD2EXPUNGECLASSES(OOP_ControllerHW_Shutdown, -1)
