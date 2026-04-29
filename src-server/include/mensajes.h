#ifndef MENSAJES_H
#define MENSAJES_H

#include <unistd.h>
#include <stddef.h>


// Funciones auxiliares para leer/enviar
int readLine(int fd, char *buffer, size_t n);
int sendMessage(int socket, char *buffer, int len);

#endif