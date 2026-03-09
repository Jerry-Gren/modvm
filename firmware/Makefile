# firmware/Makefile

# 使用标准的 GNU 汇编器进行交叉编译
AS = as
LD = ld

all: boot.bin

# 编译汇编代码为 32 位 ELF 格式对象文件
boot.o: boot.s
	$(AS) --32 -o boot.o boot.s

# 链接并剥离多余格式，输出纯粹的 flat binary
# -Ttext=0x0000 确保代码在客户机 0 地址处绝对寻址正确
boot.bin: boot.o
	$(LD) -m elf_i386 -Ttext=0x0000 --oformat binary -o boot.bin boot.o

clean:
	rm -f boot.o boot.bin