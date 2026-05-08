#ifndef MENSAJES_H
#define MENSAJES_H

#include <unistd.h>
#include <stddef.h>

/**
 * @brief Lee una cadena de caracteres desde un descriptor de fichero hasta encontrar
 *        un salto de línea ('\n'), un carácter nulo ('\0') o alcanzar el límite.
 * 
 * @param fd Descriptor del socket o fichero.
 * @param buffer Puntero al espacio donde se guardará la cadena.
 * @param n Tamaño máximo del buffer (incluyendo el espacio para '\0').
 * @return Número de caracteres leídos (sin contar el '\0'), o -1 en caso de error.
 */
int readLine(int fd, char *buffer, size_t n);

/**
 * @brief Envía un bloque de datos completo a través de un socket, gestionando
 *        envíos parciales en un bucle.
 * 
 * @param socket Descriptor del socket.
 * @param buffer Puntero a los datos a enviar.
 * @param len Longitud total en bytes de los datos.
 * @return 0 si se envió todo con éxito, -1 si hubo un error.
 */
int sendMessage(int socket, char *buffer, int len);

#endif