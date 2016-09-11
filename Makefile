all:	alsa2

libcmtspeech.a: cmtspeech_config.h
	for a in cmtspeech_backend_common cmtspeech_msgs cmtspeech_nokiamodem sal_debug; do \
	    echo $$a; \
	    gcc -fPIC $$a.c -c -I. -o $$a.o; \
	done
	ar rcs libcmtspeech.a cmtspeech_backend_common.o cmtspeech_msgs.o cmtspeech_nokiamodem.o sal_debug.o

CFLAGS_CMT = -g -I . -I /usr/include/dbus-1.0/ -I /usr/lib/arm-linux-gnueabi/dbus-1.0/include/ utils/cmtspeech_ofono_test.c -lpthread -lrt libcmtspeech.a /usr/lib/arm-linux-gnueabi/libdbus-1.a -lm

cmtspeech_ofono_test_alsa: libcmtspeech.a utils/cmtspeech_ofono_test.c utils/alsa.c
	gcc $(CFLAGS_CMT) -DALSA -lasound -o cmtspeech_ofono_test_alsa

cmtspeech_ofono_test_pulse: libcmtspeech.a utils/cmtspeech_ofono_test.c utils/pulse.c
	gcc $(CFLAGS_CMT) $(pkg-config --cflags --libs libpulse-simple) -DPULSE -o cmtspeech_ofono_test_pulse

cmtspeech_ofono_test_dsp: libcmtspeech.a utils/cmtspeech_ofono_test.c utils/dsp.c
	gcc $(CFLAGS_CMT) -DDSP -o cmtspeech_ofono_test_dsp

pa_test: pa_test.c
	gcc $(pkg-config --cflags --libs libpulse-simple) pa_test.c -o pa_test

alsa_test: alsa_test.c
	gcc -g -Wall alsa_test.c -o alsa_test -lasound -lm

alsa2: alsa2.c
	gcc -g -Wall alsa2.c -o alsa2 -lasound -lm

dsp2: dsp2.c
	gcc -g -Wall dsp2.c -o dsp2