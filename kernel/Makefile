include ../.env

kernel :=kernel.elf
INCLUDE_PATH :=-I ../include -Imm -Itrap -Iinit -Idriver
ld_script :=kernel.ld.S
libc :=../libs/libc.so


object = init/init.o driver/pci.o\
		 driver/picirq.o driver/clock.o driver/keyboard.o \
		 driver/virtio.o driver/virtio_pci.o driver/virtio_ops.o\
		 mm/pmm.o\
		 trap/vector.o trap/trap.o trap/trapenter.o \
		 entry.o \
		 heap/heap.o heap/physical_page.o

CFLAGS :=$(INCLUDE_PATH) -c -ggdb -static -Wall -fno-stack-protector -nostdlib -Wno-builtin-declaration-mismatch

all: $(kernel)
	@echo "kernel compile success"

$(kernel): $(object)
	$(LD) -T $(ld_script) $^ $(libc) -o $(kernel)

trap/vector.o: trap/vectors.S
	$(CC) $(CFLAGS) -c $^ -o $@

trap/trapenter.o: trap/trapenter.S
	$(CC) $(CFLAGS) -c $^ -o $@

clean:
	$(RM) $(kernel)
	$(RM) */*.o
	$(RM) entry.o
