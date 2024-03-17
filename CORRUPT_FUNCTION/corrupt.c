#include <stdio.h> 
#include <stdlib.h> 
#include "include/exec/memory.h"    
#include "hw/i386/intel_iommu.h"    
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "hw/irq.h"
#include "hw/pci/pci_device.h"
#include "hw/pci/pci_bus.h"
#include "hw/qdev-properties.h"
#include "hw/pci-host/dino.h"
#include "migration/vmstate.h"
#include "qom/object.h"
#include "corrupt.h"           
#include "sysemu/dma.h"

//the lower this number is the more likely for a random corruption. just note that setting it to below 1000 will cause some corruptions to 
//never happen(so dont set it below 1000 or else nothing will happen)
int intensity = 1000000;

//the int Settingtochange can have multiple values. each corresponding with the setting that you want to change. its kind of misleading
//because you cant really specify how you want to change it but whatever

//if Settingtochange equals one then its going to change to corruption intensity to be higher

//if Settingtochange equals two then its going to change to corruption intensity to be lower

//if Settingtochange equals -1 then its going to return the value of the intensity. as the opposite of changing a setting is viewing a setting
//which is why I set it to -1(ik ima genius)
int intensity_settings(int Settingtochange){
    switch (Settingtochange){   
        //increase intensity(the lower the intensity the more of a chance for a random corruption to happen)
        case 1:
            intensity = intensity / 10;
        break;
        //decrease intensity(the higer the intensity the less of a chance for a random corruption to happen)
        case 2:
            intensity = intensity * 10;
        break;
        //return the value of the intensity
        case -1:
            return intensity;
        break;
    }
    return 0;
}
//unassigned_mem_write


void Corrupt(int stop_bitching_at_me_compiler){
    stop_bitching_at_me_compiler = 1;
    int value = rand() % (intensity_settings(-1) + 1);
    printf("%d", intensity_settings(-1));
    //corrupt ram
    if(value > 980) {
        MemTxAttrs oof;
        printf("prepare for unforseen consquences");
        int randomVal = rand() % (0 + 0xff);
        int randomMemAddr = rand() % (0 + 0xff);
        dma_memory_write(&address_space_memory, randomMemAddr, &randomVal, 10, oof  );
    }
    //send garbage usb data

    //corrupt vram
}

