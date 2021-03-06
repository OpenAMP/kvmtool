#ifndef KVM__VIRTIO_MMIO_H
#define KVM__VIRTIO_MMIO_H

#include <linux/types.h>
#include <linux/virtio_mmio.h>

#define VIRTIO_MMIO_MAX_VQ	32
#define VIRTIO_MMIO_MAX_CONFIG	1
#define VIRTIO_MMIO_IO_SIZE	0x200

struct kvm;

struct virtio_mmio_ioevent_param {
	struct virtio_device	*vdev;
	u32			vq;
};

struct virtio_mmio_hdr {
	char	magic[4];
	u32	version;
	u32	device_id;
	u32	vendor_id;
	u32	host_features;
	u32	host_features_sel;
	u32	reserved_1[2];
	u32	guest_features;
	u32	guest_features_sel;
	u32	guest_page_size;
	u32	reserved_2;
	u32	queue_sel;
	u32	queue_num_max;
	u32	queue_num;
	u32	queue_align;
	u32	queue_pfn;
	u32	reserved_3[3];
	u32	queue_notify;
	u32	reserved_4[3];
	u32	interrupt_state;
	u32	interrupt_ack;
	u32	reserved_5[2];
	u32	status;
#ifdef RSLD
	u32	reserved_6[14];
	u32	shm_sel;
	u32	shm_len_low;
	u32	shm_len_high;
	u32	shm_base_low;
	u32	shm_base_high;
#endif
} __attribute__((packed));

struct virtio_mmio {
	u32			addr;
	void			*dev;
	struct kvm		*kvm;
	u8			irq;
	struct virtio_mmio_hdr	hdr;
	struct device_header	dev_hdr;
#ifdef RSLD
	struct virtio_mmio_hdr	*static_hdr;
    u8 num_vqs;
#endif
	struct virtio_mmio_ioevent_param ioeventfds[VIRTIO_MMIO_MAX_VQ];
};

int virtio_mmio_signal_vq(struct kvm *kvm, struct virtio_device *vdev, u32 vq);
int virtio_mmio_signal_config(struct kvm *kvm, struct virtio_device *vdev);
int virtio_mmio_exit(struct kvm *kvm, struct virtio_device *vdev);
int virtio_mmio_reset(struct kvm *kvm, struct virtio_device *vdev);
int virtio_mmio_init(struct kvm *kvm, void *dev, struct virtio_device *vdev,
		      int device_id, int subsys_id, int class);
#endif
