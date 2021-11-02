include .env

INCLUDE_PATH :=include
KERN_LD_SCRIPT :=tools/kernel.ld.S

kernel := kernel/kernel.elf
libc := libs/libc.so
img := kernel.iso

.PHONY: $(img) $(kernel) clean build

all: $(img)
	@echo "make done"

$(img): $(kernel)
	mkdir iso
	mkdir iso/boot
	mkdir iso/boot/grub
	cp $< iso/boot/kernel.elf
	echo 'set timeout=0' > iso/boot/grub/grub.cfg
	echo 'set default=0' >> iso/boot/grub/grub.cfg
	echo '' >> iso/boot/grub/grub.cfg
	echo 'menuentry "my os" {' >> iso/boot/grub/grub.cfg
	echo '  multiboot /boot/kernel.elf' >> iso/boot/grub/grub.cfg
	echo '  boot' >> iso/boot/grub/grub.cfg
	echo '}' >> iso/boot/grub/grub.cfg
	grub-mkrescue --output=kernel.iso iso
	rm -rf iso

gdb:$(img)
	sudo $(QEMU) -S -s -curses $(img) -hdb disk.img \
		-netdev tap,id=n1,ifname=tap100 \
		-device virtio-net-pci,netdev=n1,mac=cc:dd:ee:ff:aa:bb\
		-smp 2

	#sudo $(QEMU) -enable-kvm -S -s $(img)


qemu: 
	sudo $(QEMU) $(img) -hdb disk.img \
		-netdev tap,id=n1,ifname=tap100 \
		-device virtio-net-pci,netdev=n1,mac=cc:dd:ee:ff:aa:bb,disable-modern="on" \
		-smp 2 -curses

	#  
	# -enable-kvm \
	#		-monitor stdio


	#sudo $(QEMU) -enable-kvm $(img)

$(kernel): |$(libc)
	$(MAKE) -C kernel

$(libc):
	+$(MAKE) -C libs

clean:
	+$(MAKE) clean -C libs
	+$(MAKE) clean -C kernel
	$(RM) $(img)
