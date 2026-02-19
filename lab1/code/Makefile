CC = gcc

TARGET = task.z


$(TARGET): $(SRC)
	$(CC) -o $(TARGET) $(SRC) $(CFLAGS)



clean:
	rm -f $(TARGET)

c1:
	$(CC) task.c -o $(TARGET) -lpthread && ./$(TARGET) 13 3 5

c1c:
	clear && $(CC) task.c -o $(TARGET) -lpthread && ./$(TARGET) 13 3 5

c2:
	$(CC) task.c -o $(TARGET) -lpthread && ./$(TARGET) 25 5 6


c3:
	$(CC) task.c -o $(TARGET) -lpthread && ./$(TARGET) 36 3 6

c4:
	$(CC) task.c -o $(TARGET) -lpthread && ./$(TARGET) 20 6 8

st1:
	$(CC) task_straight.c -o l lpthread && ./l 13 3 5