# Compilador y flags
CC = gcc
CFLAGS = -Wall -Wextra -pthread -I./include
LDFLAGS = -pthread

# Directorios
SRCDIR = src
INCDIR = include
BUILDDIR = build
ROOTDIR = ..

# Archivos objeto
OBJS = $(BUILDDIR)/mensajes.o $(BUILDDIR)/linked_list.o $(BUILDDIR)/server.o

# Ejecutable en la raíz
TARGET = $(ROOTDIR)/server

# Regla principal
all: $(BUILDDIR) $(TARGET)

# Crear directorio build si no existe
$(BUILDDIR):
	mkdir -p $(BUILDDIR)

# Compilar mensajes.c
$(BUILDDIR)/mensajes.o: $(SRCDIR)/mensajes.c $(INCDIR)/mensajes.h
	$(CC) $(CFLAGS) -c $< -o $@

# Compilar linked_list.c
$(BUILDDIR)/linked_list.o: $(SRCDIR)/linked_list.c $(INCDIR)/linked_list.h
	$(CC) $(CFLAGS) -c $< -o $@

# Compilar server.c
$(BUILDDIR)/server.o: $(SRCDIR)/server.c $(INCDIR)/mensajes.h $(INCDIR)/linked_list.h
	$(CC) $(CFLAGS) -c $< -o $@

# Enlazar ejecutable en la raíz
$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) $^ -o $@

# Limpiar
clean:
	rm -rf $(BUILDDIR) $(TARGET)

rebuild: clean all

.PHONY: all clean rebuild