# INF0336 - A01 - Emulando o Sistema PIX
# Compila o executavel unico 'pix' (Partes 1, 2 e 3).

CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -pthread
TARGET  = pix
SRC     = pix.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)

.PHONY: all clean
