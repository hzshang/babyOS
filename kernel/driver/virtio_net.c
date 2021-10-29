#include <virtio.h>
#include <pci.h>
#include <physical_page.h>
#include <libcc.h>
#include <picirq.h>
#include <x86.h>
#include <virtio_pci.h>
#include <virtio_net.h>
#include <virtio_ops.h>
#include <virtio.h>
#include <virtio_dev.h>
virtio_device* network_card;
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
    rx->used->flags = 1;
    rx->available->flags = 0;
    tx->used->flags = 0;
    tx->available->flags = 0;
    network_card = vdev;
    register_intr_handler(vdev->pdev.irq + IRQ_OFFSET,virtionet_handler);
    pic_enable(vdev->pdev.irq);
    PCI_enableBusmaster(vdev->pdev.pci);
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
    DISABLE_FEATURE(device_feature,VIRTIO_GUEST_TSO4);
    DISABLE_FEATURE(device_feature,VIRTIO_GUEST_TSO6);
    DISABLE_FEATURE(device_feature,VIRTIO_GUEST_UFO);
    DISABLE_FEATURE(device_feature,VIRTIO_EVENT_IDX);
    DISABLE_FEATURE(device_feature,VIRTIO_MRG_RXBUF);
    ENABLE_FEATURE(device_feature,VIRTIO_CSUM);
    pdev->ops->set_features(pdev,device_feature);
    pdev->guest_feature = device_feature;
    debug("current device feature: 0x%08x\n",device_feature);
    c |= VIRTIO_FEATURES_OK;
    pdev->ops->set_status(pdev,c);

    uint8_t virtio_status = pdev->ops->get_status(pdev);
    if((virtio_status&VIRTIO_FEATURES_OK) == 0){
        printf("feature is not ok\n");
        return 0;
    }
    for(int i=0;i<QUEUE_COUNT;i++){
        setup_virtqueue(vdev,i);
    }
    c |= VIRTIO_DRIVER_OK;
    pdev->ops->set_status(pdev,c);
    virtio_status = pdev->ops->get_status(pdev);
    if (virtio_status & VIRTIO_FAILED){
        printf("virtio init failed\n");
        return 0;
    }
    // show_device_status(vdev);
    return 1;
}

void virtionet_handler(struct trapframe* trap){
    printf("callback to recv packet\n");
    virtio_device* vdev = network_card;
    if(vdev == NULL){
        printf("network card is null\n");
        return;
    }
    uint8_t isr = vdev->pdev.ops->get_isr(&vdev->pdev);
    if(isr != 1)
        return;
    // debug("isr status: %d\n",isr);
    virt_queue* vq = &vdev->queue[0];
    virtio_disable_interrupts(vq);
    virtio_recv_buffer(vdev,0);
    virtq_desc buffer;
    buffer.length = FRAME_SIZE;
    buffer.flags = VIRTIO_DESC_FLAG_WRITE_ONLY;
    buffer.address = 0;
    virtio_fill_buffer(vdev, 0, &buffer, 1,1);
    virtio_enable_interrupts(vq);
    return;
}


void network_send_packet(uint8_t* pkt,size_t length){
    virtio_device* vdev = network_card;
    virtq_desc desc[2];
    virtio_net_hdr hr;
    hr.flags = VIRTIO_NET_HDR_F_NEEDS_CSUM;
    hr.gso_type = 0;
    hr.csum_start = 0;
    hr.csum_offset = length;
    desc[0].address = (uint64_t)(uint32_t)&hr;
    desc[0].length = sizeof(hr);
    desc[0].flags = 0;
    desc[1].address = (uint64_t)(uint32_t)pkt;
    desc[1].length = length;
    desc[1].flags = 0;
    virtio_fill_buffer(vdev, 1, desc, 2,1);
}




