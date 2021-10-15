/*
 * pci.c
 * Copyright (C) 2021 mac <hzshang15@gmail.com>
 *
 * Distributed under terms of the MIT license.
 */

#include <pci.h>
#include <kprintf.h>

uint32_t PCI_read(uint8_t bus,uint8_t device,uint16_t func,uint8_t offset){
    uint32_t addr = (bus<<16) | (device<<11) \
                    | (func<<8) | (offset & 0xfc)|0x80000000;
    outl(CONFIG_ADDRESS,addr);
    uint32_t value = inl(CONFIG_DATA);
    return value;
}

void PCI_write(uint8_t bus,uint8_t device,uint16_t func,uint8_t offset,uint32_t value){
    uint32_t addr = (bus<<16) | (device<<11) \
                    | (func<<8) | (offset & 0xfc)|0x80000000;
    outl(CONFIG_ADDRESS,addr);
    outl(CONFIG_DATA,value);
}

void PCI_enableBusmaster(Device* d){

    uint32_t addr = (d->bus_id<<16) | (d->device_id<<11) \
                    | (4 & 0xfc)|0x80000000;
    outl(CONFIG_ADDRESS,addr);
    uint32_t value = inl(CONFIG_DATA);
    outl(CONFIG_DATA,value|4); // bus mastering
}

uint32_t PCI_readBar(Device* d,int idx){
    return PCI_read(d->bus_id,d->device_id,0,0x10+idx*4);
}

void PCI_writeBar(Device* d,int idx,uint32_t value){
    PCI_write(d->bus_id,d->device_id,0,0x10+idx*4,value);
}

uint32_t PCI_readoff(Device* d,uint32_t offset){
    uint32_t value = PCI_read(d->bus_id,d->device_id,0,offset);
    return value >> (8 * (offset % 4));
}


Device devices[0x20];
int device_num;

void pci_init(){
    for(uint16_t bus = 0;bus < 0x100;bus ++){
        for(uint16_t device = 0;device<8;device++){
            uint32_t value = PCI_read(bus,device,0,0);
            if(!value || value == 0xffffffff)
                continue;
            devices[device_num].vendor = value&0xffff;
            devices[device_num].device = value>>16;
            value = PCI_read(bus,device,0,0x3c);
            devices[device_num].irq = value&0x0ff;
            devices[device_num].irqpin = (value>>0x8)&0xff;

            value = PCI_read(bus,device,0,0x2c);
            devices[device_num].subsystem_id = value >> 16;
            devices[device_num].subsystem_vendorid = value&0xffff;
            devices[device_num].bus_id = bus;
            devices[device_num].device_id = device;
            kprintf("PCI at %x:%x vendor %x,device:%x\n"
                    "    subsystemID: %x subsystem vendorID: %x irq: %d\n",
                    bus,device,devices[device_num].vendor,
                    devices[device_num].device,
                    devices[device_num].subsystem_id,
                    devices[device_num].subsystem_vendorid,
                    devices[device_num].irq
                    );
            device_num++;
        }
    }
}

void PCI_loadbars(Device* f){
    uint32_t bar_size = 0;
    for(int i=PCI_MAPREG_START;i < PCI_MAPREG_END;i+=bar_size){
        uint32_t oldv = PCI_read(f->bus_id,f->device_id,0,i);
        bar_size = 4;
        PCI_write(f->bus_id,f->device_id,0,i,0xffffffff);
        uint32_t rv = PCI_read(f->bus_id,f->device_id,0,i);
        int regnum = PCI_MAPREG_NUM(i);
        if(rv == 0){
            kprintf("bar(%d) space size is 0\n",regnum);
            continue;
        }
        uint32_t base, size;
        if (PCI_MAPREG_TYPE(rv) == PCI_MAPREG_TYPE_MEM) {
            if (PCI_MAPREG_MEM_TYPE(rv) == PCI_MAPREG_MEM_TYPE_64BIT){
                bar_size = 8;
            }
            size = PCI_MAPREG_MEM_SIZE(rv);
            base = PCI_MAPREG_MEM_ADDR(oldv);
            //if(bar_size == 4)
            f->membase = base;
            kprintf("bar(%d) membase(%s): %x,size: %x\n",regnum,bar_size == 4?"32bit":"64bit",base,size);
        }else{
            size = PCI_MAPREG_IO_SIZE(rv);
            base = PCI_MAPREG_IO_ADDR(oldv);
            kprintf("bar(%d) iobase: %x,size: %x\n",regnum,base,size);
            f->iobase = base;
        }
        PCI_write(f->bus_id,f->device_id,0,i,oldv);
        f->reg_base[regnum] = base;
        f->reg_size[regnum] = size;
    }
}


