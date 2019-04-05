/*
 * Basic types definitions
 *
 * Copyright 1996 Alexandre Julliard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#ifndef _WINDEF_
#define _WINDEF_

#include "wine/winheader_enter.h"

#ifndef WINVER
#define WINVER 0x0500
#endif

#ifndef NO_STRICT
# ifndef STRICT
#  define STRICT
# endif /* STRICT */
#endif /* NO_STRICT */

#ifdef __cplusplus
extern "C" {
#endif

/* Calling conventions definitions */

#if (defined(__x86_64__) || defined(__powerpc64__) || defined(__aarch64__)) && !defined(_WIN64)
#define _WIN64
#endif

#ifndef _WIN64
# if defined(__i386__) && !defined(_X86_)
#  define _X86_
# endif
# if defined(_X86_) && !defined(__i386__)
#  define __i386__
# endif
#endif

#ifndef __stdcall
# ifdef __i386__
#  ifdef __GNUC__
#   if (__GNUC__ > 4) || ((__GNUC__ == 4) && (__GNUC_MINOR__ >= 2)) || defined(__APPLE__)
#    define __stdcall __attribute__((__stdcall__)) __attribute__((__force_align_arg_pointer__))
#   else
#    define __stdcall __attribute__((__stdcall__))
#   endif
#  elif defined(_MSC_VER)
    /* Nothing needs to be done. __stdcall already exists */
#  else
#   error You need to define __stdcall for your compiler
#  endif
# elif defined(__x86_64__) && defined (__GNUC__)
#  if (__GNUC__ > 5) || ((__GNUC__ == 5) && (__GNUC_MINOR__ >= 3))
#   define __stdcall __attribute__((ms_abi)) __attribute__((__force_align_arg_pointer__))
#  else
#   define __stdcall __attribute__((ms_abi))
#  endif
# elif defined(__arm__) && defined (__GNUC__) && !defined(__SOFTFP__)
#   define __stdcall __attribute__((pcs("aapcs-vfp")))
# elif defined(__aarch64__) && defined (__GNUC__)
#  define __stdcall __attribute__((ms_abi))
# else  /* __i386__ */
#  define __stdcall
# endif  /* __i386__ */
#endif /* __stdcall */

#ifndef __cdecl
# if defined(__i386__) && defined(__GNUC__)
#   if (__GNUC__ > 4) || ((__GNUC__ == 4) && (__GNUC_MINOR__ >= 2)) || defined(__APPLE__)
#   define __cdecl __attribute__((__cdecl__)) __attribute__((__force_align_arg_pointer__))
#  else
#   define __cdecl __attribute__((__cdecl__))
#  endif
# elif defined(__x86_64__) && defined (__GNUC__)
#  if (__GNUC__ > 5) || ((__GNUC__ == 5) && (__GNUC_MINOR__ >= 3))
#   define __cdecl __attribute__((ms_abi)) __attribute__((__force_align_arg_pointer__))
#  else
#   define __cdecl __attribute__((ms_abi))
#  endif
# elif defined(__arm__) && defined (__GNUC__) && !defined(__SOFTFP__)
#   define __cdecl __attribute__((pcs("aapcs-vfp")))
# elif defined(__aarch64__) && defined (__GNUC__)
#  define __cdecl __attribute__((ms_abi))
# elif !defined(_MSC_VER)
#  define __cdecl
# endif
#endif /* __cdecl */

#ifndef __fastcall
# ifndef _MSC_VER
#  define __fastcall __stdcall
# endif
#endif

#ifndef __thiscall
# define __thiscall __stdcall
#endif

#ifndef __ms_va_list
# if (defined(__x86_64__) || defined(__aarch64__)) && defined (__GNUC__)
#  define __ms_va_list __builtin_ms_va_list
#  define __ms_va_start(list,arg) __builtin_ms_va_start(list,arg)
#  define __ms_va_end(list) __builtin_ms_va_end(list)
#  define __ms_va_copy(dest,src) __builtin_ms_va_copy(dest,src)
# else
#  define __ms_va_list va_list
#  define __ms_va_start(list,arg) va_start(list,arg)
#  define __ms_va_end(list) va_end(list)
#  ifdef va_copy
#   define __ms_va_copy(dest,src) va_copy(dest,src)
#  else
#   define __ms_va_copy(dest,src) ((dest) = (src))
#  endif
# endif
#endif

#if defined(__arm__) && defined (__GNUC__) && !defined(__SOFTFP__)
# define WINAPIV __attribute__((pcs("aapcs")))
#else
# define WINAPIV __cdecl
#endif

#ifdef __WINESRC__
#define __ONLY_IN_WINELIB(x)	do_not_use_this_in_wine
#else
#define __ONLY_IN_WINELIB(x)	x
#endif

#ifndef pascal
#define pascal      __ONLY_IN_WINELIB(__stdcall)
#endif
#ifndef _pascal
#define _pascal	    __ONLY_IN_WINELIB(__stdcall)
#endif
#ifndef _stdcall
#define _stdcall    __ONLY_IN_WINELIB(__stdcall)
#endif
#ifndef _fastcall
#define _fastcall   __ONLY_IN_WINELIB(__stdcall)
#endif
#ifndef __fastcall
#define __fastcall  __ONLY_IN_WINELIB(__stdcall)
#endif
#ifndef __export
#define __export    __ONLY_IN_WINELIB(__stdcall)
#endif
#ifndef cdecl
#define cdecl       __ONLY_IN_WINELIB(__cdecl)
#endif
#ifndef _cdecl
#define _cdecl      __ONLY_IN_WINELIB(__cdecl)
#endif

#ifndef near
#define near        __ONLY_IN_WINELIB(/* nothing */)
#endif
#ifndef far
#define far         __ONLY_IN_WINELIB(/* nothing */)
#endif
#ifndef _near
#define _near       __ONLY_IN_WINELIB(/* nothing */)
#endif
#ifndef _far
#define _far        __ONLY_IN_WINELIB(/* nothing */)
#endif
#ifndef NEAR
#define NEAR        __ONLY_IN_WINELIB(/* nothing */)
#endif
#ifndef FAR
#define FAR         __ONLY_IN_WINELIB(/* nothing */)
#endif

#ifndef _MSC_VER
# ifndef _declspec
#  define _declspec(x)    __ONLY_IN_WINELIB(/* nothing */)
# endif
# ifndef __declspec
#  define __declspec(x)   __ONLY_IN_WINELIB(/* nothing */)
# endif
#endif

#ifdef _MSC_VER
# define inline __inline
#endif

#define CALLBACK    __stdcall
#define WINAPI      __stdcall
#define APIPRIVATE  __stdcall
#define PASCAL      __stdcall
#define CDECL       __cdecl
#define _CDECL      __cdecl
#define APIENTRY    WINAPI
#define CONST       __ONLY_IN_WINELIB(const)

/* Misc. constants. */

#undef NULL
#ifdef __cplusplus
#define NULL  0
#else
#define NULL  ((void*)0)
#endif

#ifdef FALSE
#undef FALSE
#endif
#define FALSE 0

#ifdef TRUE
#undef TRUE
#endif
#define TRUE  1

#ifndef IN
#define IN
#endif

#ifndef OUT
#define OUT
#endif

#ifndef OPTIONAL
#define OPTIONAL
#endif

/* Standard data types */

typedef void                                   *LPVOID;
typedef const void                             *LPCVOID;
typedef int             BOOL,       *PBOOL,    *LPBOOL;
typedef unsigned char   BYTE,       *PBYTE,    *LPBYTE;
typedef unsigned char   UCHAR,      *PUCHAR;
typedef unsigned short  WORD,       *PWORD,    *LPWORD;
typedef unsigned short  USHORT,     *PUSHORT;
typedef int             INT,        *PINT,     *LPINT;
typedef unsigned int    UINT,       *PUINT;
typedef float           FLOAT,      *PFLOAT;
typedef char                        *PSZ;
#ifdef _MSC_VER
typedef long                                   *LPLONG;
typedef unsigned long   DWORD,      *PDWORD,   *LPDWORD;
typedef unsigned long   ULONG,      *PULONG;
#else
typedef int                                    *LPLONG;
typedef unsigned int    DWORD,      *PDWORD,   *LPDWORD;
typedef unsigned int    ULONG,      *PULONG;
#endif

/* Macros to map Winelib names to the correct implementation name */
/* Note that Winelib is purely Win32.                             */

#ifdef __WINESRC__
#define WINE_NO_UNICODE_MACROS 1
#define WINE_STRICT_PROTOTYPES 1
#endif

#ifdef WINE_NO_UNICODE_MACROS
# define WINELIB_NAME_AW(func) \
    func##_must_be_suffixed_with_W_or_A_in_this_context \
    func##_must_be_suffixed_with_W_or_A_in_this_context
#else  /* WINE_NO_UNICODE_MACROS */
# ifdef UNICODE
#  define WINELIB_NAME_AW(func) func##W
# else
#  define WINELIB_NAME_AW(func) func##A
# endif
#endif  /* WINE_NO_UNICODE_MACROS */

#ifdef WINE_NO_UNICODE_MACROS
# define DECL_WINELIB_TYPE_AW(type)  /* nothing */
#else
# define DECL_WINELIB_TYPE_AW(type)  typedef WINELIB_NAME_AW(type) type;
#endif

#include <winnt.h>

/* Polymorphic types */

typedef UINT_PTR        WPARAM;
typedef LONG_PTR        LPARAM;
typedef LONG_PTR        LRESULT;

/* Integer types */

typedef WORD            ATOM;
typedef DWORD           COLORREF, *LPCOLORREF;


/* Handle types */

typedef int HFILE;
DECLARE_HANDLE(DPI_AWARENESS_CONTEXT);
DECLARE_HANDLE(HACCEL);
DECLARE_HANDLE(HBITMAP);
DECLARE_HANDLE(HBRUSH);
DECLARE_HANDLE(HCOLORSPACE);
DECLARE_HANDLE(HDC);
DECLARE_HANDLE(HDESK);
DECLARE_HANDLE(HENHMETAFILE);
DECLARE_HANDLE(HFONT);
DECLARE_HANDLE(HGLRC);
DECLARE_HANDLE(HHOOK);
DECLARE_HANDLE(HICON);
DECLARE_HANDLE(HINSTANCE);
DECLARE_HANDLE(HKEY);
typedef HKEY *PHKEY;
DECLARE_HANDLE(HKL);
DECLARE_HANDLE(HMENU);
DECLARE_HANDLE(HMETAFILE);
DECLARE_HANDLE(HMONITOR);
DECLARE_HANDLE(HPALETTE);
DECLARE_HANDLE(HPEN);
DECLARE_HANDLE(HRGN);
DECLARE_HANDLE(HRSRC);
DECLARE_HANDLE(HTASK);
DECLARE_HANDLE(HWINEVENTHOOK);
DECLARE_HANDLE(HWINSTA);
DECLARE_HANDLE(HWND);

/* Handle types that must remain interchangeable even with strict on */

typedef HINSTANCE HMODULE;
typedef HANDLE HGDIOBJ;
typedef HANDLE HGLOBAL;
typedef HANDLE HLOCAL;
typedef HANDLE GLOBALHANDLE;
typedef HANDLE LOCALHANDLE;
typedef HICON HCURSOR;

/* Callback function pointers types */

#ifdef WINE_STRICT_PROTOTYPES
typedef INT_PTR (CALLBACK *FARPROC)(void);
typedef INT_PTR (CALLBACK *NEARPROC)(void);
typedef INT_PTR (CALLBACK *PROC)(void);
#else
typedef INT_PTR (CALLBACK *FARPROC)();
typedef INT_PTR (CALLBACK *NEARPROC)();
typedef INT_PTR (CALLBACK *PROC)();
#endif

/* Macros to split words and longs. */

#define LOBYTE(w)              ((BYTE)((DWORD_PTR)(w) & 0xFF))
#define HIBYTE(w)              ((BYTE)((DWORD_PTR)(w) >> 8))

#define LOWORD(l)              ((WORD)((DWORD_PTR)(l) & 0xFFFF))
#define HIWORD(l)              ((WORD)((DWORD_PTR)(l) >> 16))

#define MAKEWORD(low,high)     ((WORD)(((BYTE)((DWORD_PTR)(low) & 0xFF)) | ((WORD)((BYTE)((DWORD_PTR)(high) & 0xFF))) << 8))
#define MAKELONG(low,high)     ((LONG)(((WORD)((DWORD_PTR)(low) & 0xFFFF)) | ((DWORD)((WORD)((DWORD_PTR)(high) & 0xFFFF))) << 16))

/* min and max macros */
#ifndef NOMINMAX
#ifndef max
#define max(a,b)   (((a) > (b)) ? (a) : (b))
#endif
#ifndef min
#define min(a,b)   (((a) < (b)) ? (a) : (b))
#endif
#endif  /* NOMINMAX */

#ifdef MAX_PATH /* Work-around for Mingw */ 
#undef MAX_PATH
#endif /* MAX_PATH */

#define MAX_PATH        260
#define HFILE_ERROR     ((HFILE)-1)

/* The SIZE structure */
typedef struct tagSIZE
{
    LONG cx;
    LONG cy;
} SIZE, *PSIZE, *LPSIZE;

typedef SIZE SIZEL, *PSIZEL, *LPSIZEL;

/* The POINT structure */
typedef struct tagPOINT
{
    LONG  x;
    LONG  y;
} POINT, *PPOINT, *LPPOINT;

typedef struct _POINTL
{
    LONG x;
    LONG y;
} POINTL, *PPOINTL;

/* The POINTS structure */

typedef struct tagPOINTS
{
#ifdef WORDS_BIGENDIAN
    SHORT y;
    SHORT x;
#else
    SHORT x;
    SHORT y;
#endif
} POINTS, *PPOINTS, *LPPOINTS;

typedef struct _FILETIME {
#ifdef WORDS_BIGENDIAN
    DWORD  dwHighDateTime;
    DWORD  dwLowDateTime;
#else
    DWORD  dwLowDateTime;
    DWORD  dwHighDateTime;
#endif
} FILETIME, *PFILETIME, *LPFILETIME;
#define _FILETIME_

/* The RECT structure */
typedef struct tagRECT
{
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
} RECT, *PRECT, *LPRECT;
typedef const RECT *LPCRECT;

typedef struct _RECTL
{
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
} RECTL, *PRECTL, *LPRECTL;

typedef const RECTL *LPCRECTL;

/* DPI awareness */
typedef enum DPI_AWARENESS
{
    DPI_AWARENESS_INVALID = -1,
    DPI_AWARENESS_UNAWARE = 0,
    DPI_AWARENESS_SYSTEM_AWARE,
    DPI_AWARENESS_PER_MONITOR_AWARE
} DPI_AWARENESS;

#define DPI_AWARENESS_CONTEXT_UNAWARE              ((DPI_AWARENESS_CONTEXT)-1)
#define DPI_AWARENESS_CONTEXT_SYSTEM_AWARE         ((DPI_AWARENESS_CONTEXT)-2)
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE    ((DPI_AWARENESS_CONTEXT)-3)
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)
#define DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED    ((DPI_AWARENESS_CONTEXT)-5)

#ifdef __i386_on_x86_64__

#define __ASM_EXTRA_DIST "16"

#define __ASM_STR__(name)    #name
#define __ASM_STR(name)      __ASM_STR__(name)

#define __ASM_THUNK_PREFIX                            wine
#define __ASM_THUNK_MAKE_NAME__(thunk_prefix,name)    thunk_prefix##_thunk_##name
#define __ASM_THUNK_MAKE_NAME(thunk_prefix,name)      __ASM_THUNK_MAKE_NAME__(thunk_prefix, name)
#define __ASM_THUNK_NAME(name)                        __ASM_THUNK_MAKE_NAME(__ASM_THUNK_PREFIX, name)
#define __ASM_THUNK_SYMBOL(name_str)                  __ASM_NAME(__ASM_STR(__ASM_THUNK_PREFIX) "_thunk_" name_str)
#define __ASM_THUNK_MAGIC                             0x77496e4554683332
#define __ASM_THUNK_TARGET(thunk_addr)                ((void *)(*((ULONGLONG *)thunk_addr - 2) + thunk_addr +7))

#define __ASM_THUNK_DEFINE(name,suffix,code)                                                 \
asm(".text\n\t"                                                                              \
    ".balign 32\n\t"                                                                         \
    ".quad " __ASM_NAME(#name suffix) " - (" __ASM_THUNK_SYMBOL(#name suffix) " + 7)\n\t"    \
    ".quad " __ASM_STR(__ASM_THUNK_MAGIC) "\n\t"                                             \
    ".globl " __ASM_THUNK_SYMBOL(#name suffix) "\n\t"                                        \
    ".type  " __ASM_THUNK_SYMBOL(#name suffix) ",@function\n"                                \
    __ASM_THUNK_SYMBOL(#name suffix) ":\n\t"                                                 \
    ".cfi_startproc\n\t"                                                                     \
    ".code32\n\t"                                                                            \
    ".byte 0x8b, 0xff\n\t" /* movl %edi, %edi; hotpatch prolog */                            \
    code "\n\t"                                                                              \
    ".code64\n\t"                                                                            \
    ".cfi_endproc\n\t"                                                                       \
    ".previous");
#define __ASM_THUNK_STDCALL(name,args,code)    __ASM_THUNK_DEFINE(name,__ASM_STDCALL(args),code)
#define __ASM_THUNK_GLOBAL(name,code)          __ASM_THUNK_DEFINE(name,"",code)
#define __ASM_STDCALL_FUNC32(name,args,code)   __ASM_STDCALL_FUNC(name,args,".code32\n\t" code "\n\t.code64\n\t")
#define __ASM_GLOBAL_FUNC32(name,code)         __ASM_GLOBAL_FUNC(name,".code32\n\t" code "\n\t.code64\n\t")

static inline BOOL wine_is_thunk32to64(void *func)
{
    /* Function address must be an odd multiple of 16 bytes; function must begin with
       hotpatchable prolog; function must be preceded by magic number. */
    ULONGLONG *thunk_magic = (ULONGLONG *)func - 1;
    return (((uintptr_t)func & 0x1f) == 0x10 && *(WORD*)func == 0xff8b && *thunk_magic == __ASM_THUNK_MAGIC);
}

#endif /* __i386_on_x86_64__ */

#ifdef __cplusplus
}
#endif

#include "wine/winheader_exit.h"

#endif /* _WINDEF_ */
