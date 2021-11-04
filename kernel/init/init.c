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
    // start_thread();
    printf("welcome come myOS!\n");
    void network_send_packet(uint8_t* pkt,size_t length);
    
    // ide_enable_dma(1);
    // ide_enable_dma(1);
    dma_test();
}

void dma_test(){
/*
在Parallel ATA DMA/PIO 均通过
在QEMU ATA PIO通过 DMA未通过

ATAPI均未通过
*/
    int enable_dma = 0;
    int ata_device = 0;
    int atapi_device = 1;
    int nsecs = 1;
    int lba = 0;
    while(1){

    printf("\n1> read from device\n");
    printf("2> write to device\n");
    printf("3> switch mode\n");
    printf("current mode: %s\n",(char* []){"PIO","DMA"}[enable_dma]);
    printf("input cmd: ");
    uint8_t* tmp_buffer = physical_alloc(0x400,0x10000);
    char a = get_c();
    switch(a){
        case '1':
        {
            printf("\nread from Device [1] ATA [2] ATAPI ?");
            char x = get_c();
            memset(tmp_buffer,'\xcc',0x400);
            if(x == '1'){
                ide_read_sectors(ata_device,nsecs,lba,tmp_buffer);
            }else{
                ide_read_sectors(atapi_device,nsecs,lba,tmp_buffer);
            }
            dumpmem(tmp_buffer,0x50);
        }
            break;
        case '2':
        {
            printf("\nRead a char: ");
            char x = get_c();
            memset(tmp_buffer,x,0x400);
            printf("\nDevice [1] ATA [2] ATAPI ?");
            char o = get_c();
            if(o == '1'){
                ide_write_sectors(ata_device,nsecs,lba,tmp_buffer);
                printf("\nwrite char '%c' to ATA\n",x);
            }else{
                ide_write_sectors(atapi_device,nsecs,lba,tmp_buffer);
                printf("\nwrite char '%c' to ATAPI\n",x);
            }
        }
            break;
        case '3':
            if(enable_dma){
                enable_dma = 0;
                ide_disable_dma(ata_device);
                ide_disable_dma(atapi_device);
            }else{
                enable_dma = 1;
                ide_enable_dma(ata_device);
                ide_enable_dma(atapi_device);
            }
            break;
        default:
            break;
    }

    }
}




