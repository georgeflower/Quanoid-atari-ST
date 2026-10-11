.SECONDARY:
CC      = m68k-linux-gnu-gcc
LD      = m68k-linux-gnu-ld
CFLAGS  = -m68000 -mstrict-align -fno-store-merging -O2 -fno-ivopts -fomit-frame-pointer -ffreestanding -fno-builtin \
          -fno-pic -fno-asynchronous-unwind-tables -Wall -Wno-main
LIBGCC  = $(shell $(CC) -m68000 -print-libgcc-file-name)
OBJS    = build/crt0.o build/sys.o build/math.o build/gfx.o build/ikbd.o build/blitfast.o

all: build/QUANOID.PRG build/QUANTEST.PRG

src/main.c: src/main_head.c src/game_part.c src/boss_part.c tools/assemble.py
	python3 tools/assemble.py $@

src/levels.h: tools/levels.txt tools/genlevels.py
	python3 tools/genlevels.py $< $@

build/%.o: src/%.S
	$(CC) -m68000 -c $< -o $@

build/main.o: src/main.c src/levels.h src/font.h src/sfx.h
	$(CC) $(CFLAGS) -c $< -o $@
build/main_test.o: src/main.c src/levels.h src/font.h src/sfx.h
	$(CC) $(CFLAGS) -DTEST -c $< -o $@

build/%.elf: $(OBJS) build/%.o src/link.ld
	$(LD) -T src/link.ld --emit-relocs --no-warn-rwx-segments -z noexecstack -o $@ $(filter %.o,$^)

build/QUANOID.PRG: build/main.elf
	sh tools/checkalign.sh $<
	python3 tools/elf2prg.py $< $@
build/QUANTEST.PRG: build/main_test.elf
	sh tools/checkalign.sh $<
	python3 tools/elf2prg.py $< $@

clean:
	rm -f build/*

src/font.h: tools/genfont.py
	python3 tools/genfont.py $@

build/main_diag.o: src/main.c src/levels.h src/font.h
	$(CC) $(CFLAGS) -DDIAG -c $< -o $@
build/QUANDIAG.PRG: build/main_diag.elf
	python3 tools/elf2prg.py $< $@

src/sfx.h: tools/gensfx.py
	python3 tools/gensfx.py $@

build/main_sfxdemo.o: src/main.c src/levels.h src/font.h src/sfx.h
	$(CC) $(CFLAGS) -DSFXDEMO -c $< -o $@
build/QSFXDEMO.PRG: build/main_sfxdemo.elf
	sh tools/checkalign.sh $<
	python3 tools/elf2prg.py $< $@

build/main_nosound.o: src/main.c src/levels.h src/font.h src/sfx.h
	$(CC) $(CFLAGS) -DNOSOUND -c $< -o $@
build/QUANNS.PRG: build/main_nosound.elf
	sh tools/checkalign.sh $<
	python3 tools/elf2prg.py $< $@

build/main_snddiag.o: src/main.c src/levels.h src/font.h src/sfx.h
	$(CC) $(CFLAGS) -DSNDDIAG -c $< -o $@
build/QSNDDG.PRG: build/main_snddiag.elf
	sh tools/checkalign.sh $<
	python3 tools/elf2prg.py $< $@

build/main_qexita.o: src/main.c src/levels.h src/font.h src/sfx.h
	$(CC) $(CFLAGS) -DEXIT_NORESTORE -c $< -o $@
build/QEXITA.PRG: build/main_qexita.elf
	sh tools/checkalign.sh $<
	python3 tools/elf2prg.py $< $@

build/main_qexitb.o: src/main.c src/levels.h src/font.h src/sfx.h
	$(CC) $(CFLAGS) -DEXIT_NOTERM -c $< -o $@
build/QEXITB.PRG: build/main_qexitb.elf
	sh tools/checkalign.sh $<
	python3 tools/elf2prg.py $< $@

build/main_dosound.o: src/main.c src/levels.h src/font.h src/sfx.h
	$(CC) $(CFLAGS) -DDOSOUND -c $< -o $@
build/QDOSND.PRG: build/main_dosound.elf
	sh tools/checkalign.sh $<
	python3 tools/elf2prg.py $< $@

build/main_microbench.o: src/main.c src/levels.h src/font.h src/sfx.h
	$(CC) $(CFLAGS) -DTEST -DMICROBENCH -c $< -o $@
build/QMICRO.PRG: build/main_microbench.elf
	sh tools/checkalign.sh $<
	python3 tools/elf2prg.py $< $@
