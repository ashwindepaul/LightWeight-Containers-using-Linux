CC=gcc
CFLAGS=-O2 -Wall -Wextra -D_GNU_SOURCE
BIN_DIR=bin
BIN=$(BIN_DIR)/container_runtime

.PHONY: all build setup demo test clean help
all: build
build:
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -o $(BIN) src/container_runtime.c
	@chmod 755 $(BIN)
setup:
	@sudo bash setup_container.sh
demo: build setup
	@sudo ./minirun run demo --project . --host-runtime --network host --command 'echo "MiniRun works"; uname -a; ps -ef'
test:
	@bash tests/run_tests.sh
clean:
	rm -f $(BIN)
help:
	@echo 'MiniRun v3'
	@echo '  make build'
	@echo '  make setup'
	@echo '  ./minirun run NAME --project PATH --host-runtime --command "..."'
	@echo '  ./minirun create NAME --host-runtime --project PATH'
	@echo '  sudo ./minirun start NAME'
