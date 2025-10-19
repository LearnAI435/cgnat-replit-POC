CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2 -g
LDFLAGS = 
TARGET = cgnat
STRESS_TARGET = stress_test
SOURCES = main.c cgnat.c
STRESS_SOURCES = stress_test.c cgnat.c
OBJECTS = $(SOURCES:.c=.o)
STRESS_OBJECTS = $(STRESS_SOURCES:.c=.o)
HEADERS = cgnat.h uthash.h

all: $(TARGET) $(STRESS_TARGET)

$(TARGET): main.o cgnat.o
	$(CC) main.o cgnat.o -o $(TARGET) $(LDFLAGS)
	@echo "Build complete: $(TARGET)"

$(STRESS_TARGET): stress_test.o cgnat.o
	$(CC) stress_test.o cgnat.o -o $(STRESS_TARGET) $(LDFLAGS)
	@echo "Build complete: $(STRESS_TARGET)"

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o $(TARGET) $(STRESS_TARGET)
	@echo "Cleaned build artifacts"

run: $(TARGET)
	./$(TARGET)

stress: $(STRESS_TARGET)
	./$(STRESS_TARGET)

.PHONY: all clean run stress
