#ifndef MENSAJES_H
#define MENSAJES_H

#include <unistd.h>
#include <stddef.h>

/**
 * @brief Lee una cadena de un descriptor de fichero (socket) hasta encontrar '\0'.
 * * Esta función es necesaria para el protocolo de la práctica porque la longitud
 * de los strings no se conoce a priori. Lee el contenido byte a byte 
 * para asegurar que no se consuman datos de la siguiente operación.
 *
 * @param fd Descriptor del socket.
 * @param buffer Puntero donde se almacenará la cadena leída.
 * @param n Tamaño máximo del buffer para evitar desbordamientos.
 * @return El número de caracteres leídos (incluyendo el '\0') o -1 en caso de error.
 */
int readLine(int fd, char *buffer, size_t n);
int sendMessage(int socket, char *buffer, int len);

#endif