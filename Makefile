CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2 -g
LDFLAGS = 
TARGET = cgnat
SOURCES = main.c cgnat.c
OBJECTS = $(SOURCES:.c=.o)
HEADERS = cgnat.h uthash.h

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TARGET) $(LDFLAGS)
	@echo "Build complete: $(TARGET)"

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)
	@echo "Cleaned build artifacts"

run: $(TARGET)
	./$(TARGET)

.PHONY: all clean run
