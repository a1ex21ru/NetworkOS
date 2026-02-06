# Лабораторная работа №4
# Makefile для сборки сервера и клиента

CC = gcc
CFLAGS = -Wall -Wextra -pthread
LDFLAGS = -lpthread

all: server client

server: server.c
	$(CC) $(CFLAGS) server.c -o server $(LDFLAGS)

client: client.c
	$(CC) $(CFLAGS) client.c -o client $(LDFLAGS)

clean:
	rm -f server client

# Запуск сервера (3 кабинки, макс 5 входов подряд)
run-server:
	./server 3 5

# Запуск 20 клиентов (50% M, 50% F) на localhost
run-clients:
	./client 127.0.0.1 --multi 20 50

# Запуск одного клиента-мужчины
run-male:
	./client 127.0.0.1 1 M

# Запуск одного клиента-женщины  
run-female:
	./client 127.0.0.1 2 F

.PHONY: all clean run-server run-clients run-male run-female

