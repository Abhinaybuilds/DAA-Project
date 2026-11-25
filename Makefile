CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -lws2_32
TARGET = smart_ambulance.exe
SRC = smart_ambulance.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

serve: $(TARGET)
	./$(TARGET) --serve 8080

watch: $(TARGET)
	./$(TARGET) --watch

clean:
	del /Q $(TARGET) state.json command.json 2>nul || rm -f $(TARGET) state.json command.json
