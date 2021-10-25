include ../.env

kernel :=kernel.elf
INCLUDE_PATH :=-I ../include -Imm -Itrap -Iinit -Idriver
ld_script :=kernel.ld.S
libc :=../libs/libc.so


object = init/init.o init/initcpu2.o init/smp.o init/initcpu2.o\
		 driver/pci.o\
		 driver/picirq.o driver/clock.o driver/keyboard.o \
		 driver/virtio.o driver/virtio_pci.o driver/virtio_ops.o driver/virtio_net.o\
		 driver/virtio_dev.o driver/virtio_gpu.o \
		 driver/smb.o \
		 mm/pmm.o\
		 trap/vector.o trap/trap.o trap/trapenter.o \
		 entry.o \
		 heap/heap.o heap/physical_page.o

CFLAGS :=$(INCLUDE_PATH) -fno-builtin-printf -c -ggdb -static -Wall -fno-stack-protector -nostdlib -Wno-builtin-declaration-mismatch

all: $(kernel)
	@echo "kernel compile success"

$(kernel): $(object)
	$(LD) -T $(ld_script) $^ $(libc) -o $(kernel)

trap/vector.o: trap/vectors.S
	$(CC) $(CFLAGS) -c $^ -o $@

init/initcpu2.o: init/initcpu2.S
	$(CC) $(CFLAGS) -c $^ -o init/initcpu2.1.o
	$(LD) -T init/initcpu2.ld.S init/initcpu2.1.o -o init/initcpu2.2.o
	objcopy -S -O binary -j .text init/initcpu2.2.o init/initcpu2.3.o
	objcopy -I binary -O elf32-i386 -B i386 init/initcpu2.3.o init/initcpu2.o

trap/trapenter.o: trap/trapenter.S
	$(CC) $(CFLAGS) -c $^ -o $@

clean:
	$(RM) $(kernel)
	$(RM) */*.o
	$(RM) entry.o
