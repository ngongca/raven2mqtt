RAVEN2MQTT = raven2mqtt.o parse.o dbglog.o

ALLOBJ = (RAVEN2MQTT)


ALLPGM =  raven2mqtt


CC = gcc

IFLAGS = -I/usr/include -I/usr/local/include
CFLAGS = -g -c $(IFLAGS)

LFLAGS = -L/usr/lib -L/usr/local/lib

all : $(ALLPGM)


raven2mqtt : $(RAVEN2MQTT)
	gcc $(LFLAGS) -l paho-mqtt3c -g -o $@ $(RAVEN2MQTT)

%.o:%.c makefile
	$(CC) $(CFLAGS) $< -o $@

.PHONY : clean

clean:
	rm -f *.o $(ALLPGM)
