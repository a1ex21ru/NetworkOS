CC = gcc
CFLAGS = -Iinclude -pthread
SRC = src/main.c src/bathroom.c src/student.c
TARGET = bathroom_simulation


$(TARGET): $(SRC)
	$(CC) -o $(TARGET) $(SRC) $(CFLAGS)



clean:
	rm -f $(TARGET)



c1:
	$(CC) task.c -o task.z && ./task.z 12 3 5


c2:
	$(CC) task.c -o task.z && ./task.z 24 5 6


c3:
	$(CC) task.c -o task.z && ./task.z 36 3 6