#include "mensajes.h"
#include <unistd.h>
#include <errno.h>

// De un descriptor obtiene una cadena de char
int readLine(int fd, char *buffer, size_t n) {
    ssize_t numRead;  // bytes leídos en la última llamada a read()
    size_t totRead;   // total de bytes leídos hasta el momento
    char *buf;
    char ch;

    if (n <= 0 || buffer == NULL) { 
        errno = EINVAL;
        return -1;
    }

    buf = buffer;
    totRead = 0;

    for (;;) {
        numRead = read(fd, &ch, 1); // lee un byte

        if (numRead == -1) {
            if (errno == EINTR) // si fue interrumpido, reinicia el read()
                continue;
            else
                return -1; // error leyendo
        } else if (numRead == 0) { // EOF (fin de fichero)
            if (totRead == 0) // no se leen bytes -> return 0
                return 0;
            else
                break;
        } else { // numRead debe ser 1 si llegamos aquí
            if (ch == '\n')
                break;
            if (ch == '\0')
                break;

            if (totRead < n - 1) { // descarta bytes que superen n-1
                totRead++;
                *buf++ = ch;
            }
        } 
    }

    *buf = '\0'; // finaliza la cadena correctamente
    return totRead; // devuelve el número de bytes leídos
}

int sendMessage(int socket, char *buffer, int len){
    int r;
    int l = len;
    do { r = write(socket, buffer, l);
        l = l - r;
        buffer = buffer + r;
    } while ((l>0) && (r>=0));
    if (r < 0)
        return (-1); /* fallo */
    else 
        return(0); /* se ha enviado longitud */
}