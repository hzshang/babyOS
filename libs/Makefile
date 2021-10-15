
include ../.env
libc :=libc.so
INCLUDE_PATH :=../include


CFLAGS :=-I $(INCLUDE_PATH) -Wall -ggdb -c -fno-builtin -fno-stack-protector

objects =$(patsubst %.c,%.o,$(wildcard *.c))

all:$(libc)
	@echo "libc compile success"

$(libc):$(objects)
	$(AR) rcs $@ $^

.PHONY: clean
clean:
	-$(RM) $(objects) $(libc)
