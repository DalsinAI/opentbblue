/*
 * Portability for the shared core, included before everything else
 * (-include src/compat.h). Windows' mkdir takes no mode.
 */
#ifndef OTB_COMPAT_H
#define OTB_COMPAT_H
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <sys/stat.h>
#define mkdir(path, mode) _mkdir(path)
#endif
#endif
