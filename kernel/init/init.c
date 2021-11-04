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
#include <mp.h>
#include <ioapic.h>
#include <lapic.h>
#include <ide.h>
void dma_test();
void kern_init(multiboot_info_t* mbd, unsigned int magic){
    extern char bss_start[],bss_end[];
    memset(bss_start,0,bss_end-bss_start);
    cga_init();
    pmm_init();
    // mpinit();
    heap_init((uint8_t*)0x200000,0x100000);
    physical_page_init((uint8_t*)0x300000,0x500000);


    intr_init();
    pic_init();
    // lapicinit();
    // ioapicinit();
    clock_init();
    keyboard_init();

    pci_init();
    virtio_dev_install();
    smb_init();

    intr_enable();
    ide_init();
    /* Make sure the magic number matches for memory mapping*/
    if(magic == MULTIBOOT_BOOTLOADER_MAGIC && (mbd->flags >> 6 & 0x1)) {
        /* Loop through the memory map and display the values */
        int i;
        for(i = 0; i < mbd->mmap_length;
                i += sizeof(multiboot_memory_map_t))
        {
            multiboot_memory_map_t* mmmt =
                (multiboot_memory_map_t*) (mbd->mmap_addr + i);
            printf("Start Addr: 0x%08x | Length: 0x%08x | Size: 0x%08x | Type: %d\n",
                    mmmt->addr, mmmt->len, mmmt->size, mmmt->type);
            if(mmmt->type == MULTIBOOT_MEMORY_AVAILABLE) {}
        }
    }
    start_thread();
    printf("welcome come myOS!\n");
    // void network_send_packet(uint8_t* pkt,size_t length);
    // ide_enable_dma(1);
    // ide_enable_dma(1);
    // dma_test();
    while(1){
        debug("press 1\n");
        char c = get_c();
        switch(c){
            case '1':
                debug("ready to crash\n");
                ide_atapi_bug_trigger();
                break;
            default:
                break;
        }
    }
}





