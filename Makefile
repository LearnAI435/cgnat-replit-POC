CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2 -g
LDFLAGS = -lpthread
TARGET = cgnat
STRESS_TARGET = stress_test
WEB_TARGET = web_server
SOURCES = main.c cgnat.c
STRESS_SOURCES = stress_test.c cgnat.c
WEB_SOURCES = web_server.c cgnat.c
OBJECTS = $(SOURCES:.c=.o)
STRESS_OBJECTS = $(STRESS_SOURCES:.c=.o)
WEB_OBJECTS = $(WEB_SOURCES:.c=.o)
HEADERS = cgnat.h

all: $(TARGET) $(STRESS_TARGET) $(WEB_TARGET)

$(TARGET): main.o cgnat.o
	$(CC) main.o cgnat.o -o $(TARGET) $(LDFLAGS)
	@echo "Build complete: $(TARGET)"

$(STRESS_TARGET): stress_test.o cgnat.o
	$(CC) stress_test.o cgnat.o -o $(STRESS_TARGET) $(LDFLAGS)
	@echo "Build complete: $(STRESS_TARGET)"

$(WEB_TARGET): web_server.o cgnat.o
	$(CC) web_server.o cgnat.o -o $(WEB_TARGET) $(LDFLAGS)
	@echo "Build complete: $(WEB_TARGET)"

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o $(TARGET) $(STRESS_TARGET) $(WEB_TARGET)
	@echo "Cleaned build artifacts"

run: $(TARGET)
	./$(TARGET)

stress: $(STRESS_TARGET)
	./$(STRESS_TARGET)

web: $(WEB_TARGET)
	./$(WEB_TARGET)

.PHONY: all clean run stress web
