#include "kvm/vesa.h"

#include "kvm/devices.h"
#include "kvm/virtio-pci-dev.h"
#include "kvm/framebuffer.h"
#include "kvm/kvm-cpu.h"
#include "kvm/ioport.h"
#include "kvm/util.h"
#include "kvm/irq.h"
#include "kvm/kvm.h"
#include "kvm/pci.h"

#include <linux/byteorder.h>
#include <sys/mman.h>
#include <linux/err.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <inttypes.h>
#include <unistd.h>

int VESA_WIDTH = _VESA_WIDTH;
int VESA_HEIGHT = _VESA_HEIGHT;
int VESA_MEM_SIZE = _VESA_MEM_SIZE;

static struct pci_device_header vesa_pci_device = {
	.vendor_id	= cpu_to_le16(PCI_VENDOR_ID_REDHAT_QUMRANET),
	.device_id	= cpu_to_le16(PCI_DEVICE_ID_VESA),
	.header_type	= PCI_HEADER_TYPE_NORMAL,
	.revision_id	= 0,
	.class[2]	= 0x03,
	.subsys_vendor_id = cpu_to_le16(PCI_SUBSYSTEM_VENDOR_ID_REDHAT_QUMRANET),
	.subsys_id	= cpu_to_le16(PCI_SUBSYSTEM_ID_VESA),
	.bar[1]		= cpu_to_le32(VESA_MEM_ADDR | PCI_BASE_ADDRESS_SPACE_MEMORY),
	.bar_size[1]	= _VESA_MEM_SIZE,
};

static struct device_header vesa_device = {
	.bus_type	= DEVICE_BUS_PCI,
	.data		= &vesa_pci_device,
};

static struct framebuffer vesafb = {
	.width		= _VESA_WIDTH,
	.height		= _VESA_HEIGHT,
	.depth		= VESA_BPP,
	.mem_addr	= VESA_MEM_ADDR,
	.mem_size	= _VESA_MEM_SIZE,
};

static bool vesa_pci_io_in(struct ioport *ioport, struct kvm_cpu *vcpu, u16 port, void *data, int size)
{
	return true;
}

static bool vesa_pci_io_out(struct ioport *ioport, struct kvm_cpu *vcpu, u16 port, void *data, int size)
{
	return true;
}

static struct ioport_operations vesa_io_ops = {
	.io_in			= vesa_pci_io_in,
	.io_out			= vesa_pci_io_out,
};

static int vesa__bar_activate(struct kvm *kvm, struct pci_device_header *pci_hdr,
			      int bar_num, void *data)
{
	/* We don't support remapping of the framebuffer. */
	return 0;
}

static int vesa__bar_deactivate(struct kvm *kvm, struct pci_device_header *pci_hdr,
				int bar_num, void *data)
{
	/* We don't support remapping of the framebuffer. */
	return -EINVAL;
}

#include <linux/fb.h>
#include <sys/ioctl.h>

static void * vesa_fb_passthrough (void)
{
	unsigned char *fb = MAP_FAILED;
	struct fb_var_screeninfo vi;
	int nbytes, fd = open("/dev/fb0", O_RDWR|O_DIRECT);

	if (fd < 0) {
		fprintf(stderr,"could not open /dev/fb0 for vesa fb passthrough\n");
		return (MAP_FAILED);
	}

	if (ioctl(fd, FBIOGET_VSCREENINFO, &vi) == -1)
		goto bail;

	if (vi.xres != vi.xres_virtual || vi.yres != vi.yres_virtual)
		fprintf(stderr,"Warning: /dev/fb0 scan lines not contiguous. Check with fbset\n");
	if (vi.bits_per_pixel != VESA_BPP)
		fprintf(stderr, "Warning: /dev/fb0 not %d bits per pixel\n",VESA_BPP);
	nbytes = vi.xres_virtual * vi.yres_virtual * vi.bits_per_pixel / 8;
	nbytes = (nbytes + 4095) & ~4095; /* round to next page */
	printf ("vesa: mapping %d bytes\n", nbytes);
	if ((fb = (unsigned char *) mmap(0, nbytes, PROT_RW,
			 MAP_SHARED|MAP_LOCKED, fd, 0)) == MAP_FAILED)
		goto bail;

	memset(fb, 0, nbytes);

	vesafb.width    = vi.xres_virtual;
	vesafb.height   = vi.yres_virtual;
   	vesafb.mem_size = nbytes;

	VESA_WIDTH    = vi.xres_virtual;
	VESA_HEIGHT   = vi.yres_virtual;
	VESA_MEM_SIZE = nbytes;

	vesa_pci_device.bar_size[1] = nbytes;

	/* fallthrough */
bail:
        close(fd);

        return (fb);
}

static int fb_passthrough__init(struct kvm *kvm)
{
        struct framebuffer *fb;

        if (!kvm->cfg.fb)
                return 0;

        fb = vesa__init(kvm);
        if (IS_ERR(fb)) {
                pr_err("vesa__init() failed with error %ld\n", PTR_ERR(fb));
                return PTR_ERR(fb);
        }

        return 0;
}
dev_init(fb_passthrough__init);


struct framebuffer *vesa__init(struct kvm *kvm)
{
	u16 vesa_base_addr;
	char *mem;
	int r;

	BUILD_BUG_ON(!is_power_of_two(VESA_MEM_SIZE));
	BUILD_BUG_ON(VESA_MEM_SIZE < VESA_BPP/8 * VESA_WIDTH * VESA_HEIGHT);

	vesa_base_addr = pci_get_io_port_block(PCI_IO_SIZE);
	r = ioport__register(kvm, vesa_base_addr, &vesa_io_ops, PCI_IO_SIZE, NULL);
	if (r < 0)
		goto out_error;

	vesa_pci_device.bar[0]		= cpu_to_le32(vesa_base_addr | PCI_BASE_ADDRESS_SPACE_IO);
	vesa_pci_device.bar_size[0]	= PCI_IO_SIZE;
	r = pci__register_bar_regions(kvm, &vesa_pci_device, vesa__bar_activate,
				      vesa__bar_deactivate, NULL);
	if (r < 0)
		goto unregister_ioport;

	r = device__register(&vesa_device);
	if (r < 0)
		goto unregister_ioport;

	if (kvm->cfg.fb)
		mem = vesa_fb_passthrough();
	else
		mem = mmap(NULL, VESA_MEM_SIZE, PROT_RW, MAP_ANON_NORESERVE, -1, 0);
	if (mem == MAP_FAILED) {
		r = -errno;
		goto unregister_device;
	}

	r = kvm__register_dev_mem(kvm, VESA_MEM_ADDR, VESA_MEM_SIZE, mem);
	if (r < 0)
		goto unmap_dev;

	vesafb.mem = mem;
	vesafb.kvm = kvm;
	return fb__register(&vesafb);

unmap_dev:
	munmap(mem, VESA_MEM_SIZE);
unregister_device:
	device__unregister(&vesa_device);
unregister_ioport:
	ioport__unregister(kvm, vesa_base_addr);
out_error:
	return ERR_PTR(r);
}
