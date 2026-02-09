CC = gcc

TARGET = task.z


$(TARGET): $(SRC)
	$(CC) -o $(TARGET) $(SRC) $(CFLAGS)



clean:
	rm -f $(TARGET)

c1:
	$(CC) task.c -o $(TARGET) -lpthread && ./task.z 13 3 5


c2:
	$(CC) task.c -o $(TARGET) -lpthread && ./task.z 25 5 6


c3:
	$(CC) task.c -o $(TARGET) -lpthread && ./$(TARGET) 36 3 6

c4:
	$(CC) task.c -o $(TARGET) -lpthread && ./$(TARGET) 20 6 8