# Compilador y flags
CC = gcc
CFLAGS = -Wall -Wextra -pthread -I./include -I./rpc -I/usr/include/tirpc
LDFLAGS = -pthread -ltirpc

# Directorios
SRCDIR = src
INCDIR = include
RPCDIR = rpc
BUILDDIR = build
ROOTDIR = ..

# Archivos objeto
OBJS = $(BUILDDIR)/mensajes.o $(BUILDDIR)/linked_list.o $(BUILDDIR)/server.o \
       $(BUILDDIR)/log_rpc_client.o $(BUILDDIR)/log_rpc_clnt.o $(BUILDDIR)/log_rpc_xdr.o

# Ejecutable en la raíz
TARGET = $(ROOTDIR)/server
RPC_TARGET = $(ROOTDIR)/log_rpc_server

# Regla principal
all: $(BUILDDIR) $(TARGET) $(RPC_TARGET)

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
$(BUILDDIR)/server.o: $(SRCDIR)/server.c $(INCDIR)/mensajes.h $(INCDIR)/linked_list.h $(RPCDIR)/log_rpc_client.h
	$(CC) $(CFLAGS) -c $< -o $@

# Compilar cliente auxiliar RPC
$(BUILDDIR)/log_rpc_client.o: $(RPCDIR)/log_rpc_client.c $(RPCDIR)/log_rpc_client.h $(RPCDIR)/log_rpc.h
	$(CC) $(CFLAGS) -c $< -o $@

# Compilar stub cliente RPC generado por rpcgen
$(BUILDDIR)/log_rpc_clnt.o: $(RPCDIR)/log_rpc_clnt.c $(RPCDIR)/log_rpc.h
	$(CC) $(CFLAGS) -c $< -o $@

# Compilar serialización RPC generada por rpcgen
$(BUILDDIR)/log_rpc_xdr.o: $(RPCDIR)/log_rpc_xdr.c $(RPCDIR)/log_rpc.h
	$(CC) $(CFLAGS) -Wno-unused-variable -c $< -o $@

# Compilar main del servidor RPC generado por rpcgen
$(BUILDDIR)/log_rpc_svc.o: $(RPCDIR)/log_rpc_svc.c $(RPCDIR)/log_rpc.h
	$(CC) $(CFLAGS) -Wno-unused-variable -Wno-unused-parameter -Wno-cast-function-type -c $< -o $@

# Compilar implementación del servidor RPC
$(BUILDDIR)/log_rpc_server.o: $(RPCDIR)/log_rpc_server.c $(RPCDIR)/log_rpc.h
	$(CC) $(CFLAGS) -c $< -o $@

# Enlazar ejecutable en la raíz
$(TARGET): $(OBJS)
	$(CC) $^ -o $@ $(LDFLAGS)

# Enlazar servidor RPC en la raíz
$(RPC_TARGET): $(BUILDDIR)/log_rpc_svc.o $(BUILDDIR)/log_rpc_server.o $(BUILDDIR)/log_rpc_xdr.o
	$(CC) $^ -o $@ $(LDFLAGS)

# Limpiar
clean:
	rm -rf $(BUILDDIR) $(TARGET) $(RPC_TARGET)

rebuild: clean all

.PHONY: all clean rebuild
