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
uint8_t tmp_buffer[2048];
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

    // start_thread();
    printf("welcome come myOS!\n");
    void network_send_packet(uint8_t* pkt,size_t length);
    
        ide_enable_dma(0);
        ide_enable_dma(1);
        int enable = 1;
    while(1){
        char a = get_c();
        int idx = 0;
        int lba = 0;
        int nsecs = 1;
        

        debug("opcode: %c\n",a);
        switch(a){
            case 'a':
                printf("read using %s\n",enable?"dma":"pio");
                memset(tmp_buffer,'\xcc',sizeof(tmp_buffer));
                ide_read_sectors(idx,nsecs,lba,tmp_buffer);
                printf("read from ide:\n");
                dumpmem(tmp_buffer,0x50);
                break;
            case 'b':
                char x = get_c();
                printf("write using %s\n",enable?"dma":"pio");
                memset(tmp_buffer,x,sizeof(tmp_buffer));
                ide_write_sectors(idx,nsecs,lba,tmp_buffer);
                printf("write %c to ide\n",x);
                break;
            case 'c':
                if(enable){
                    printf("disable dma\n");
                    enable = 0;
                    ide_disable_dma(0);
                    ide_disable_dma(1);
                }else{
                    printf("enable dma\n");
                    enable = 1;
                    ide_enable_dma(0);
                    ide_enable_dma(1);
                }
                break;
            default:
                break;
        }
    }
}






