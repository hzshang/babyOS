#include <stdio.h>
#include <pmm.h>
#include <trap.h>
#include <x86.h>
#include <picirq.h>
#include <clock.h>
#include <multiboot.h>
#include <heap.h>
#include <pci.h>
#include <keyboard.h>
#include <virtio_dev.h>
#include <physical_page.h>
#include <smp.h>
#include <virtio_gpu.h>
#include <virtio.h>
#include <smb.h>
#include <screen.h>


void kern_init(multiboot_info_t* mbd, unsigned int magic){
    extern char bss_start[],bss_end[];
    memset(bss_start,0,bss_end-bss_start);
    cga_init();
    pmm_init();
    pic_init(); // if not, os will stuck by int 13
    clock_init();
    keyboard_init();
    heap_init((uint8_t*)0x200000,0x100000);
    physical_page_init((uint8_t*)0x300000,0x500000);

    intr_init();
    intr_enable();
    pci_init();
    virtio_dev_install();
    smb_init();
    start_thread();
    printf("welcome come myOS!\n");
    while(1){
        asm("hlt");
    }
}

