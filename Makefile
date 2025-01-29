TARGETS = cmt_dsp cmt_pulse cmt_alsa pa_test alsa_test loop_alsa
all:	$(TARGETS)

clean:
	rm -f $(TARGETS)

libcmtspeech.a: cmtspeech_config.h
	for a in cmtspeech_backend_common cmtspeech_msgs cmtspeech_nokiamodem sal_debug; do \
	    echo $$a; \
	    gcc -fPIC $$a.c -c -I. -o $$a.o; \
	done
	ar rcs libcmtspeech.a cmtspeech_backend_common.o cmtspeech_msgs.o cmtspeech_nokiamodem.o sal_debug.o

CFLAGS_CMT = -g -I . utils/cmtspeech_ofono_test.c -lpthread -lrt libcmtspeech.a $$(pkg-config --cflags --libs dbus-1) -lm

CFLAGS_ATEST = -g atest.c

CFLAGS_RAWPLAY = -g rawplay.c

CMT_SRC = libcmtspeech.a utils/cmtspeech_ofono_test.c utils/audio.c

ATEST_SRC =  atest.c utils/audio.c

RAWPLAY_SRC =  rawplay.c utils/audio.c

cmt_alsa: $(CMT_SRC) utils/alsa.c
	gcc $(CFLAGS_CMT) -DALSA -lasound -o cmt_alsa

cmt_pulse: $(CMT_SRC) utils/pulse.c
	gcc $(CFLAGS_CMT) -DPULSE -o cmt_pulse $$(pkg-config --cflags --libs libpulse-simple dbus-1)

cmt_dsp: $(CMT_SRC) utils/dsp.c
	gcc $(CFLAGS_CMT) -DDSP -o cmt_dsp

atest_alsa: $(ATEST_SRC) utils/alsa.c
	gcc $(CFLAGS_ATEST) -DALSA -lasound -o atest_alsa

atest_pulse: $(ATEST_SRC) utils/pulse.c
	gcc $(CFLAGS_ATEST)  -I . -DPULSE -o atest_pulse $$(pkg-config --cflags --libs libpulse-simple dbus-1)

atest_dsp: $(ATEST_SRC) utils/dsp.c
	gcc $(CFLAGS_ATEST) -DDSP -o atest_dsp

rawplay_alsa: $(RAWPLAY_SRC) utils/alsa.c
	gcc $(CFLAGS_RAWPLAY) -DALSA -lasound -o rawplay_alsa

pa_test: pa_test.c
	gcc pa_test.c -o pa_test $$(pkg-config --cflags --libs libpulse-simple)

alsa_test: alsa_test.c
	gcc -g -Wall alsa_test.c -o alsa_test -lasound -lm

loop_alsa: loop.c
	gcc -g -Wall loop.c -DALSA -o loop_alsa -lasound -lm

dsp2: dsp2.c
	gcc -g -Wall dsp2.c -o dsp2

install: cmt_alsa cmt_pulse cmt_dsp
	mkdir -p $(DESTDIR)/usr/bin/
	install cmt_alsa $(DESTDIR)/usr/bin/
	install cmt_pulse $(DESTDIR)/usr/bin/
	install cmt_dsp $(DESTDIR)/usr/bin/
	install pa_test $(DESTDIR)/usr/bin/
	install alsa_test $(DESTDIR)/usr/bin/
	install loop_alsa $(DESTDIR)/usr/bin/
