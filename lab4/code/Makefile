# Компилятор
CC = gcc

# Флаги компиляции
CFLAGS = -Wall -Wextra -pthread #-std=c11
LDFLAGS = -pthread

# Цели
SERVER = server.z
CLIENT = client.z
ALL = $(SERVER) $(CLIENT)

# Исходные файлы
SERVER_SRC = server.c
CLIENT_SRC = client.c

# ============================================
# Правила по умолчанию
# ============================================
all: $(ALL)
	@echo ""
	@echo "=== Компиляция завершена ==="
	@echo "Запуск сервера: ./server.z"
	@echo "Запуск клиента: ./client.z"
	@echo "==========================="

# Компиляция сервера
$(SERVER): $(SERVER_SRC)
	$(CC) -o $(SERVER) $(SERVER_SRC) $(CFLAGS) $(LDFLAGS)
	@echo "Сервер скомпилирован: $(SERVER)"

# Компиляция клиента
$(CLIENT): $(CLIENT_SRC)
	$(CC) -o $(CLIENT) $(CLIENT_SRC) $(CFLAGS)
	@echo "Клиент скомпилирован: $(CLIENT)"

# ============================================
# Запуск сервера и клиентов
# ============================================

# Запуск сервера
run-server: $(SERVER)
	@echo "Запуск сервера на порту 5050..."
	@./$(SERVER)

# Запуск клиента (localhost)
run-client: $(CLIENT)
	@echo "Запуск клиента (localhost)..."
	@./$(CLIENT)

# Запуск клиента с указанием IP
run-client-ip: $(CLIENT)
	@echo "Запуск клиента..."
	@./$(CLIENT) $(IP)

# Компиляция и запуск сервера в одном терминале,
# клиента в другом (для тестирования)
test: $(ALL)
	@echo ""
	@echo "=== Тестирование ==="
	@echo "1. Запустите сервер: ./server"
	@echo "2. В другом терминале запустите клиент: ./client"
	@echo "3. Введите количество студентов (например: 15)"
	@echo "===================="


# ============================================
# Перезапуск (очистка и компиляция)
# ============================================
rebuild: clean all

# ============================================
# Справка
# ============================================
help:
	@echo ""
	@echo "=== Makefile для лабораторной работы №4 ==="
	@echo ""
	@echo "Цели:"
	@echo "  all              - компиляция сервера и клиента"
	@echo "  server           - компиляция сервера"
	@echo "  client           - компиляция клиента"
	@echo "  run-server       - запуск сервера"
	@echo "  run-client       - запуск клиента (localhost)"
	@echo "  test             - информация о тестировании"
	@echo "  clean            - удаление бинарных файлов"
	@echo "  rebuild         - перекомпиляция"
	@echo "  help             - справка"
	@echo ""
	@echo "Примеры использования:"
	@echo "  make                    # компиляция"
	@echo "  make server             # только сервер"
	@echo "  make client IP=192.168.1.100  # клиент с IP"
	@echo "  make clean              # очистка"
	@echo ""
	@echo "Порядок запуска:"
	@echo "  1. terminal 1: make run-server"
	@echo "  2. terminal 2: make run-client"
	@echo "  3. В клиенте ввести количество студентов"
	@echo "=============================================="
	@echo ""


server:
	$(CC) $(CFLAGS) -o server.z server_.c

client:
	$(CC) $(CFLAGS) -o client.z client_.c

clean:
	rm -f server client
