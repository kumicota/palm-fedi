/* Minimal host-side stand-in for <PalmOS.h> so pure-logic sources
 * (proto.c, the URL helpers in net.c) can be unit tested on Linux. */
#ifndef SHIM_PALMOS_H
#define SHIM_PALMOS_H
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
typedef uint8_t UInt8; typedef int8_t Int8; typedef uint16_t UInt16; typedef int16_t Int16;
typedef uint32_t UInt32; typedef int32_t Int32; typedef unsigned char Boolean;
typedef UInt16 Err; typedef Int16 Coord; typedef UInt16 FontID; typedef UInt16 WChar;
typedef void *MemPtr; typedef void *MemHandle; typedef Int16 NetSocketRef; typedef UInt8 IndexedColorType;
typedef struct { Coord x, y; } PointType;
typedef struct { PointType topLeft, extent; } RectangleType;
typedef struct BitmapType BitmapType; typedef struct BitmapTypeV3 BitmapTypeV3;
typedef struct FormType *FormPtr; typedef struct EventType EventType;
#define true 1
#define false 0
#define errNone 0
#define StrChr strchr
#define StrLen strlen
#define StrCopy strcpy
#define StrCat strcat
#define StrCompare strcmp
#define MemMove memmove
#define MemSet(p, n, v) memset((p), (v), (n))
#define MemPtrNew malloc
#define MemPtrFree free
#define StrIToA(s, n) sprintf((s), "%ld", (long)(n))
#endif
