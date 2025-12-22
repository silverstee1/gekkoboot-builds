#include "mmceblk.h"
#include "gctypes.h"
#include "ogc/disc_io.h"
#include "ogc/exi.h"
#include "ogc/semaphore.h"
#include "ogc/system.h"
#include "ogc/timesupp.h"
#include "subprojects/libogc2/gc/ogc/system.h"

#include <sys/unistd.h>
#include <stdbool.h>


#define MMCE_PAGE_SIZE512 512

static bool __mmce_init = false;

static sem_t __mmce_irq_sem;


static s32 __MMCE_EXI_Handler(s32 chan, s32 dev) {
    if (chan < 0 || chan > 1) return -1;
    
    LWP_SemPost(__mmce_irq_sem);
    
    return 1;
}

static bool __mmce_startup(DISC_INTERFACE* disc) {

    LWP_SemInit(&__mmce_irq_sem, 0, 1);

    return true;
}

static bool __mmce_isInserted(DISC_INTERFACE* disc) 
{
    s32 chan = (disc->ioType&0xff)-'0';
    u32 dev =  EXI_DEVICE_0;
    if (EXI_ProbeEx(chan))
    {
        bool err = false;
        u8 cmd[3];
        u32 id = 0x00;

        if (!EXI_LockEx(chan, dev)) return false;
        if (!EXI_Select(chan, dev, EXI_SPEED16MHZ)) {
            EXI_Unlock(chan);
            return false;
        }

        cmd[0] = 0x8B;
        cmd[1] = 0x00;

        err |= !EXI_ImmEx(chan, cmd, sizeof(cmd), EXI_WRITE);
        err |= !EXI_ImmEx(chan, &id, sizeof(id), EXI_READ);
        err |= !EXI_Deselect(chan);
        EXI_Unlock(chan);
        
        if (err)
            return false;
        else if (id >> 16 != 0x3842)
            return false;
        else
            return true;
    }
    return false;
}

static bool __mmce_readSectors(DISC_INTERFACE* disc, sec_t sector, sec_t numSectors, void* buffer) {
    u8 cmd[8] = {0x8B, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    
    s32 ret = 0,chan = (disc->ioType&0xff)-'0';
    u32 dev = EXI_DEVICE_0;
    // Wait for interrupt
    u8 retries = 5U;
    
    if(disc->ioType < DEVICE_TYPE_GAMECUBE_MCE(0)) return false;
    if(disc->ioType > DEVICE_TYPE_GAMECUBE_MCE(1)) return false;

    cmd[2] = (sector >> 24) & 0xFF;
    cmd[3] = (sector >> 16) & 0xFF;
    cmd[4] = (sector >> 8) & 0xFF;
    cmd[5] = sector & 0xFF;
    cmd[6] = (numSectors >> 8) & 0xFF;
    cmd[7] = numSectors & 0xFF;
    EXI_RegisterEXICallback(chan, __MMCE_EXI_Handler);

    if (!EXI_LockEx(chan, dev))
    {
        return false;
    }
    
    if (!EXI_Select(chan, dev, EXI_SPEED16MHZ)) 
    {
        EXI_Unlock(chan);
        return false;
    }

    if (!EXI_ImmEx(chan, &cmd, sizeof(cmd), EXI_WRITE)) {
        EXI_Deselect(chan);
        EXI_Unlock(chan);
        return false;
    }
    EXI_Deselect(chan);
    EXI_Unlock(chan);
    //EXI_RegisterEXICallback(chan, __MMCE_EXI_Handler);

    struct timespec timeout = {
        .tv_sec = 5,  // 5 second timeout
        .tv_nsec = 0
    };
    
    cmd[1] = 0x21; // Change to read command
    for (sec_t i = 0; i < numSectors; i++) {
        u8* ptr = (u8*)buffer + (i * MMCE_PAGE_SIZE512);
        if (LWP_SemTimedWait(__mmce_irq_sem, &timeout) != 0) {
            if (--retries > 0U) {
                i -= 1;
                sleep(2); // Sleep 2 seconds before retry
            }
            else
                break;
        }

        if (!EXI_LockEx(chan, dev)) {
            EXI_RegisterEXICallback(chan, NULL);
            return false;
        }
        if (!EXI_Select(chan, dev, EXI_SPEED32MHZ)) 
        {
            EXI_Unlock(chan);
            EXI_RegisterEXICallback(chan, NULL);
            return false;
        }
        if (!EXI_ImmEx(chan, cmd, 3, EXI_WRITE)
            || !EXI_ImmEx(chan, ptr, MMCE_PAGE_SIZE512, EXI_READ)) {
            EXI_Deselect(chan);
            EXI_Unlock(chan);
            EXI_RegisterEXICallback(chan, NULL);
            return false;
        }
        
        EXI_Deselect(chan);
        EXI_Unlock(chan);
    }
    EXI_RegisterEXICallback(chan, NULL);

    return ret == 0;
}

static bool __mmce_writeSectors(DISC_INTERFACE* disc, sec_t sector, sec_t numSectors, const void* buffer) {
    u8 cmd[8] = {0x8B, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    
    s32 ret = 0,chan = (disc->ioType&0xff)-'0';
    u32 dev = (chan == 0) ? EXI_DEVICE_0 : EXI_DEVICE_1;

    u64 start_time = gettime();

    if(disc->ioType < DEVICE_TYPE_GAMECUBE_MCE(0)) return false;
    if(disc->ioType > DEVICE_TYPE_GAMECUBE_MCE(1)) return false;

    cmd[2] = (sector >> 24) & 0xFF;
    cmd[3] = (sector >> 16) & 0xFF;
    cmd[4] = (sector >> 8) & 0xFF;
    cmd[5] = sector & 0xFF;
    cmd[6] = (numSectors >> 8) & 0xFF;
    cmd[7] = numSectors & 0xFF;

    if (EXI_Lock(chan, dev, NULL) < 0)
    {
        return false;
    }
    
    if (!EXI_Select(chan, dev, EXI_SPEED16MHZ)) 
    {
        EXI_Unlock(chan);
        return false;
    }

    ret |= !EXI_ImmEx(chan, &cmd, sizeof(cmd), EXI_WRITE);
    ret |= !EXI_Deselect(chan);

    EXI_Unlock(chan);
    EXI_RegisterEXICallback(chan, __MMCE_EXI_Handler);

    struct timespec timeout = {
        .tv_sec = 5,  // 5 second timeout
        .tv_nsec = 0
    };
    if (LWP_SemTimedWait(__mmce_irq_sem, &timeout) != 0) {
            return false;
    }

    // Wait for interrupt
    cmd[1] = 0x23; // Change to write command
    for (sec_t i = 0; i < numSectors; i++) {
        EXI_RegisterEXICallback(chan, __MMCE_EXI_Handler);

        EXI_Lock(chan, dev, NULL);
        u8* ptr = (u8*)buffer + (i * MMCE_PAGE_SIZE512);
        if (!EXI_Select(chan, dev, EXI_SPEED16MHZ)) 
        {
            EXI_Unlock(chan);
            return false;
        }
            EXI_ImmEx(chan, cmd, 2, EXI_WRITE);
        
        ret |= !EXI_DmaEx(chan, ptr, MMCE_PAGE_SIZE512, EXI_WRITE);
        ret |= !EXI_Deselect(chan);
        EXI_Unlock(chan);

        struct timespec timeout = {
            .tv_sec = 5,  // 5 second timeout
            .tv_nsec = 0
        };


        if (LWP_SemTimedWait(__mmce_irq_sem, &timeout) != 0) {
                    break;
        }
    }
    EXI_RegisterEXICallback(chan, NULL);

    return ret == 0;
}

static bool __mmce_clearStatus(DISC_INTERFACE* disc) {
    // No clear status needed.
    return true;
}

static bool __mmce_shutdown(DISC_INTERFACE* disc) {
    // No shutdown needed.
    return true;
}



DISC_INTERFACE __io_mmce0 = {
    DEVICE_TYPE_GAMECUBE_MCE(0),
    FEATURE_MEDIUM_CANREAD | FEATURE_MEDIUM_CANWRITE | FEATURE_GAMECUBE_SLOTA,
    &__mmce_startup,
    &__mmce_isInserted,
    &__mmce_readSectors,
    &__mmce_writeSectors,
    &__mmce_clearStatus,
    &__mmce_shutdown,
    ~0x0,
    MMCE_PAGE_SIZE512
};

DISC_INTERFACE __io_mmce1 = {
    DEVICE_TYPE_GAMECUBE_MCE(1),
    FEATURE_MEDIUM_CANREAD | FEATURE_MEDIUM_CANWRITE | FEATURE_GAMECUBE_SLOTB,
    __mmce_startup,
    __mmce_isInserted,
    __mmce_readSectors,
    __mmce_writeSectors,
    __mmce_clearStatus,
    __mmce_shutdown,
    ~0x0,
    MMCE_PAGE_SIZE512
};
