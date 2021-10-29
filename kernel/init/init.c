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
    // start_thread();
    printf("welcome come myOS!\n");
    void network_send_packet(uint8_t* pkt,size_t length);
    uint8_t buffer[0x100];
    int idx = 0;
    while(1){
        char a = get_c();
        switch(a){
            case 'a':
                memset(buffer,'a'+idx,sizeof(buffer));
                network_send_packet(buffer,sizeof(buffer));
                idx++;
            break;
        }
    }
}

