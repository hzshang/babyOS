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
#include <virtio.h>
#include <physical_page.h>
void cga_init(){
    for(int i=0;i<80;i++){
        kprintf("\n");
    }
    set_cursor(0,0);
}

void intr_enable(){
    sti();
}

void intr_disable(){
    cli();
}

void kern_init(multiboot_info_t* mbd, unsigned int magic){
    extern char bss_start[],bss_end[];
    memset(bss_start,0,bss_end-bss_start);
    cga_init();
    kprintf("hello\n");
    /* Make sure the magic number matches for memory mapping*/
    if(magic == MULTIBOOT_BOOTLOADER_MAGIC && (mbd->flags >> 6 & 0x1)) {
        /* Loop through the memory map and display the values */
        int i;
        for(i = 0; i < mbd->mmap_length;
                i += sizeof(multiboot_memory_map_t))
        {
            multiboot_memory_map_t* mmmt =
                (multiboot_memory_map_t*) (mbd->mmap_addr + i);
            kprintf("Start Addr: %x | Length: %x | Size: %x | Type: %d\n",
                    mmmt->addr, mmmt->len, mmmt->size, mmmt->type);
            if(mmmt->type == MULTIBOOT_MEMORY_AVAILABLE) {}
        }
    }
    pmm_init();
    pic_init(); // if not, os will stuck by int 13
    clock_init();
    keyboard_init();
    intr_init();

    kprintf("welcome to my os\n");
    heap_init((uint8_t*)0x200000,0x100000);

    physical_page_init((uint8_t*)0x300000,0x500000);
    intr_enable();

    pci_init();
    virtio_net_install();
    extern virtio_device vdevs[0x10];
    virtio_device* net = &vdevs[0];

    // char data[0x200];
    while(1){
        // memcpy(data,"\x11\x22\x33\x44\x55\x66",6);
        // memcpy(&data[6],net->pdev.macaddr,6);
        // data[12] = 0x08;
        // data[13] = 0x00;
        // memset(&data[14],'b',sizeof(data)-14);
        kprintf("trigger to send packet\n");
        if(get_c() == 'a')
            virtio_bug_trigger2(net);
        // virtionet_send(net,data,sizeof(data));
        // show_device_status(net);
    }
}

