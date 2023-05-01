BUILD_DIR := ./build
ifndef QEMU
QEMU := qemu-system-aarch64
endif

LAB := 1
# try to generate a unique GDB port
GDBPORT	:= 1234
QEMUOPTS = -machine raspi3b -serial null -serial mon:stdio -m size=1G -kernel $(BUILD_DIR)/kernel.img -gdb tcp::1234
IMAGES = $(BUILD_DIR)/kernel.img

all: build

gdb:
	gdb-multiarch -n -x .gdbinit

build: FORCE
	./scripts/build.sh

qemu: $(IMAGES) 
	$(QEMU) $(QEMUOPTS)

qemu-gdb: $(IMAGES)
	@echo "***"
	@echo "*** make qemu-gdb'." 1>&2
	@echo "***"
	$(QEMU) -nographic $(QEMUOPTS) -S

debug:
	qemu-system-aarch64 -machine raspi3b -nographic -serial null -serial mon:stdio -m size=1G -kernel ./build/kernel.img -S -s

run:
	qemu-system-aarch64 -machine raspi3b -nographic -serial null -serial mon:stdio -m size=1G -kernel ./build/kernel.img


gdbport:
	@echo $(GDBPORT)

docker: FORCE	
	./scripts/run_docker.sh

grade: build
	@echo "make grade"
	@echo "LAB"$(LAB)": test >>>>>>>>>>>>>>>>>"
ifeq ($(LAB), 2)
	./scripts/run_mm_test.sh
endif
	./scripts/grade-lab$(LAB)

.PHONY: clean
clean:
	@rm -rf build
	@rm -rf chcore.out

.PHONY: FORCE
FORCE:
