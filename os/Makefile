# Сборка NovaOS. Нужны: nasm, gcc (с поддержкой -m32), ld.
CFLAGS = -m32 -ffreestanding -fno-stack-protector -fno-pic -fno-builtin \
         -nostdlib -O2 -Wall -Wextra
OUT = build/nova.bin

all: $(OUT)

build/boot.o: src/boot.asm
	@mkdir -p build
	nasm -f elf32 $< -o $@

build/kernel.o: src/kernel.c
	@mkdir -p build
	gcc $(CFLAGS) -c $< -o $@

$(OUT): build/boot.o build/kernel.o src/linker.ld
	ld -m elf_i386 -T src/linker.ld -o $@ build/boot.o build/kernel.o

run: $(OUT)
	qemu-system-i386 -kernel $(OUT)

clean:
	rm -f build/*.o $(OUT)

.PHONY: all run clean
