KAFKA_LIBS_HOME    ?= /usr/local
KAFKA_LIB     ?= $(KAFKA_LIBS_HOME)/lib
JRE_LIB      ?= $(JAVA_HOME)jre/lib/amd64/server
LIB_GTEST= /usr/lib
LIB_MAPR= /opt/mapr/lib
LIBFLAGS=-L $(KAFKA_LIB)
LIBFLAGS+=-L $(LIB_MAPR)
LDFLAGS=-lMapRClient
EXE=
ifneq ($(OS),Windows_NT)
  LIBFLAGS += -L $(JRE_LIB)
  LDFLAGS  += -lrdkafka -L$(KAFKA_LIB)
  LDFLAGS  += -ljvm
  LDFLAGS  += -lgtest -L$(LIB_GTEST)
  LDFLAGS  += -lpthread
else
  EXE      = .exe
endif


SOURCES  = ProducerGtest.cc
OBJECTS  = $(patsubst %.cc,%.o,$(SOURCES))
EXECUTABLE= ProducerGtest$(EXE)

CC=g++
CFLAGS=-c -Wall -Wwrite-strings -std=c++0x -Wno-write-strings

all: $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(LIBFLAGS) $(OBJECTS) $(LDFLAGS) -o $(EXECUTABLE)

%.o: %.cc
	$(CC) $(CFLAGS) -g -O -c $< -o $@

clean:
	rm -rf $(EXECUTABLE) *.o
