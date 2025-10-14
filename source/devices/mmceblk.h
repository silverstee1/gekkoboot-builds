/* mmceblk.h
	- Memory Card Emulator Block Device Interface
	by emu_kidid
 */

#ifndef __MMCEBLK_H__
#define __MMCEBLK_H__

#include <stdint.h>
#include "ogc/disc_io.h"

#ifdef __cplusplus
extern "C" {
#endif

extern DISC_INTERFACE __io_mmce0, __io_mmce1;

// Block device interface for MMCE (Memory Card Emulator)
#define DEVICE_TYPE_GAMECUBE_MCE(x) (('M'<<24)|('C'<<16)|('E'<<8)|('0'+(x)))


#ifdef __cplusplus
}
#endif

#endif // __MMCEBLK_H__
