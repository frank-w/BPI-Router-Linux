CC              = $(CROSS_COMPILE)gcc
#LD              = $(CROSS_COMPILE)ld
#AR              = $(CROSS_COMPILE)ar
#NM              = $(CROSS_COMPILE)nm
#OBJCOPY         = $(CROSS_COMPILE)objcopy
#OBJDUMP         = $(CROSS_COMPILE)objdump
#READELF         = $(CROSS_COMPILE)readelf
#STRIP           = $(CROSS_COMPILE)strip


EXEC = regs
all: $(EXEC)
$(EXEC): $(EXEC).c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $@.c $(LDLIBS)
romfs:
	$(ROMFSINST) /bin/$(EXEC)
clean:
	-rm -f $(EXEC) *.elf *.gdb *.o
