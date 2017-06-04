TARGETS = cmt_dsp cmt_pulse cmt_alsa
all:	$(TARGETS)

clean:
	rm $(TARGETS)

libcmtspeech.a: cmtspeech_config.h
	for a in cmtspeech_backend_common cmtspeech_msgs cmtspeech_nokiamodem sal_debug; do \
	    echo $$a; \
	    gcc -fPIC $$a.c -c -I. -o $$a.o; \
	done
	ar rcs libcmtspeech.a cmtspeech_backend_common.o cmtspeech_msgs.o cmtspeech_nokiamodem.o sal_debug.o

CFLAGS_CMT = -g -I . -I /usr/include/dbus-1.0/ -I /usr/lib/arm-linux-gnueabi/dbus-1.0/include/ utils/cmtspeech_ofono_test.c -lpthread -lrt libcmtspeech.a /usr/lib/arm-linux-gnueabi/libdbus-1.a -lm

CFLAGS_ATEST = -g atest.c

CMT_SRC = libcmtspeech.a utils/cmtspeech_ofono_test.c utils/audio.c

ATEST_SRC =  atest.c utils/audio.c

cmt_alsa: $(CMT_SRC) utils/alsa.c
	gcc $(CFLAGS_CMT) -DALSA -lasound -o cmt_alsa

cmt_pulse: $(CMT_SRC) utils/pulse.c
	gcc $(CFLAGS_CMT) $$(pkg-config --cflags --libs libpulse-simple) -DPULSE -o cmt_pulse

cmt_dsp: $(CMT_SRC) utils/dsp.c
	gcc $(CFLAGS_CMT) -DDSP -o cmt_dsp

atest_alsa: $(ATEST_SRC) utils/alsa.c
	gcc $(CFLAGS_ATEST) -DALSA -lasound -o atest_alsa

atest_pulse: $(ATEST_SRC) utils/pulse.c
	gcc $(CFLAGS_ATEST)  -I . -I /usr/include/dbus-1.0/ -I /usr/lib/arm-linux-gnueabi/dbus-1.0/include/  $$(pkg-config --cflags --libs libpulse-simple) -DPULSE -o atest_pulse

atest_dsp: $(ATEST_SRC) utils/dsp.c
	gcc $(CFLAGS_ATEST) -DDSP -o atest_dsp

pa_test: pa_test.c
	gcc $$(pkg-config --cflags --libs libpulse-simple) pa_test.c -o pa_test

alsa_test: alsa_test.c
	gcc -g -Wall alsa_test.c -o alsa_test -lasound -lm

loop_alsa: loop.c
	gcc -g -Wall loop.c -DALSA -o loop_alsa -lasound -lm

dsp2: dsp2.c
	gcc -g -Wall dsp2.c -o dsp2