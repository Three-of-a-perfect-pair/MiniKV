#===============================================================================
# Opções de compilação
#
#   make                              Compila o programa.
#   make RELEASE=false                Compila com debug (-g) e mensagens plog().
#   make ADDONS=-fsanitize=thread     Compila com o ThreadSanitizer.
#   make clean                        Apaga os arquivos gerados.
#   make submission                   Gera o arquivo solution.tar.gz para entrega.
#
# Depois de mudar RELEASE ou ADDONS, rode "make clean" antes de compilar.
#===============================================================================

# Ferramenta para formatação
FORMAT = clang-format

# Versão final (true) ou de debug (false)?
RELEASE ?= true

# Flags extras de compilação e ligação (ex.: -fsanitize=thread).
ADDONS ?=

# Nome do executável.
EXEC = program

#===============================================================================

CC      ?= cc
CFLAGS  += -Wall -Werror -std=c11 -pthread -D_POSIX_C_SOURCE=200809L -Iinclude $(ADDONS)
LDFLAGS += $(ADDONS)
LIBS    += -lm -lpthread


ifeq ($(RELEASE), true)
	CFLAGS += -DNDEBUG
else
	CFLAGS += -O0 -g
endif

SRC = $(wildcard src/*.c)
OBJ = $(patsubst src/%.c, obj/%.o, $(SRC))
HDR = $(wildcard include/*.h)
FORMAT_SRC = $(wildcard src/*.c include/*.h)

.PHONY: all clean submission format format-check pre-commit

all: $(EXEC)

$(EXEC): $(OBJ)
	@echo [LD] $@
	@$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LIBS)

obj/%.o: src/%.c $(HDR)
	@mkdir -p obj
	@echo [CC] $<
	@$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf obj $(EXEC) wal.log solution.tar.gz

submission: clean
	@tmp=$$(mktemp) && tar -czf $$tmp . && mv $$tmp solution.tar.gz
	@echo "solution.tar.gz criado."

format:
	$(FORMAT) -i $(FORMAT_SRC)

format-check:
	$(FORMAT) --dry-run --Werror $(FORMAT_SRC)

pre-commit:
	@echo "[CHECK] Verificando formatação..."
	@if ! $(FORMAT) --dry-run --Werror $(FORMAT_SRC) 2>/dev/null; then \
		echo "[FORMAT] Corrigindo formatação..."; \
		$(FORMAT) -i $(FORMAT_SRC) || exit 1; \
	fi
	@echo "[BUILD] Compilando projeto..."
	@$(MAKE) $(EXEC)
	@echo "[OK] Todas as verificações passaram!"
