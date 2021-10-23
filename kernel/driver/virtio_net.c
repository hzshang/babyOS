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
#include <virtio.h>
#include <virtio_dev.h>

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
    // kprintf("vdev irq: %d\n",vdev->pdev.irq);
}


int network_card_init(virtio_device* vdev){
    virtio_pci_dev* pdev = &vdev->pdev;
    // reset
    pdev->ops->set_status(pdev,0);

    uint8_t c = VIRTIO_ACKNOWLEDGE;
    pdev->ops->set_status(pdev,c);
    c |= VIRTIO_DRIVER;
    pdev->ops->set_status(&vdev->pdev,c);
    uint64_t device_feature = pdev->ops->get_features(pdev);
    // kprintf("device feature: %x\n",device_feature);
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
    // show_device_status(vdev);
    return 1;
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
