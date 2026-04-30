#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdint.h>
#include <pthread.h>
#include <signal.h>
#include <netdb.h>


#include "mensajes.h"
#include "linked_list.h"

#define NUMBER_OF_PORTS 65535
#define MSG_MAX_SIZE 256 // Como mucho 255 + '/0', establecido por el enunciado

// Mutex que protege la lista enlazada con los usuarios
static pthread_mutex_t mutex_usuarios = PTHREAD_MUTEX_INITIALIZER;

// Definimos head, puntero que apuntará al primer elemento de la lista de usuarios
static user_node_t *head = NULL; // inicialmente lista vacía

// Manejador de la señal sigint
void handle_sigint(int sig) {
    // Cerrar descriptores y liberar memoria 
    printf("\ns> Servidor terminado por señal %d\n", sig);
    // TODO: En un futuro, iterar sobre 'head' y hacer free() de los nodos, podemos meter función auxiliar en linked_list que lo haga
    exit(0);
}

// Función para procesar las peticiones de los hilos
void *procesar_peticion(void* socket_especifico_fd){
    // Variables locales

    // El padre reserva este entero para evitar carreras al pasar el fd al hilo.
    int fd_local = *(int*)socket_especifico_fd;

    char instruccion[MSG_MAX_SIZE]; // buffer para las instrucciones
    char buffer[MSG_MAX_SIZE]; // buffer para el texto que acompaña a las instrucciones
    unsigned char resultado; // para devolver posteriormente el resultado de la operación
    user_node_t *user_actual = NULL; // donde almacenaremos el usuario en las operaciones


    // Ya hemos copiado el valor: liberamos la memoria dinámica cuanto antes.
    free(socket_especifico_fd);

    // ---- Tratamiento de la petición ----

    // Leer la instrucción a ejecutar

    if (readLine(fd_local, instruccion, MSG_MAX_SIZE) < 0){
        close(fd_local);
        pthread_exit(NULL);
    }

    // Lectura exitosa: procesar según qué operación sea

    if (strcmp(instruccion, "REGISTER") == 0){

        // Leer el nombre de usuario a registrar
        if (readLine(fd_local, buffer, MSG_MAX_SIZE) < 0){
            printf("REGISTER: Error leyendo el nombre de usuario\n");
            close(fd_local);
            pthread_exit(NULL);
        }

        // Bloqueamos por si hay varios clientes entrando al mismo tiempo
        pthread_mutex_lock(&mutex_usuarios);

        // Verificar que no existe otro usuario registrado con el mismo nombre: find_user
        usuario_actual = find_user(head, buffer);

        if (usuario_actual != NULL){
            resultado = 1; // Ya existe el usuario
        } else {
            // El usuario no existe: lo añadimos
            add_user(&head, buffer);
            resultado = 0; // éxito
            printf("s> REGISTER %s OK\n", buffer); // Mensaje de log
        }

        // Liberamos el mutex al terminar
        pthread_mutex_unlock(&mutex_usuarios);

        // Enviar el resultado
        if (sendMessage(fd_local, (char *)&resultado, sizeof(unsigned char)) < 0) {
            perror("Error enviando respuesta");
        }
    }
    else if (strcmp(instruccion, "UNREGISTER") == 0){}
    else if (strcmp(instruccion, "CONNECT") == 0){}
    else if (strcmp(instruccion, "DISCONNECT") == 0){}
    else if (strcmp(instruccion, "USERS") == 0){}
    else if (strcmp(instruccion, "SEND") == 0){}
    else if (strcmp(instruccion, "SENDATTACH") == 0){}
    else if (strcmp(instruccion, "QUIT") == 0){}
    else {
        printf("Comando desconocido: %s\n", instruccion);
        resultado = 2; // Error de comando

        if (sendMessage(fd_local, (char *)&resultado, sizeof(unsigned char)) < 0) {
            perror("Error enviando respuesta de error");
        }

        close(fd_local);
        pthread_exit(NULL);
    }

    close(fd_local);
    pthread_exit(NULL);
}


int main(int argc, char * argv[]){
    
    if (argc != 3){
        printf("Uso: ./server  -p <port>\n");
        return -1;
    }

    // Capturar el número de puerto, strtol para robustez

    char * endptr;
    errno = 0;
    long puerto = strtol(argv[2], &endptr, 10);

    // Validar formato correcto
    if ((errno == ERANGE && (puerto == LONG_MAX || puerto == LONG_MIN)) || (errno != 0 && puerto == 0)) {
        perror("strtol");
        return -1;
    }

    if (puerto > NUMBER_OF_PORTS){
        printf("Error: Puerto %ld mayor que %d\n", puerto, NUMBER_OF_PORTS);
        return -1;
    }

    if (puerto < 0){
        printf("Error: Puerto %ld menor que 0\n", puerto);
        return -1;
    }

    if (endptr == argv[2]){
        printf("Error: Puerto no es un número\n");
        return -1;
    }
    
    if (*endptr != '\0'){
        printf("Error: Puerto contiene caracteres inválidos\n");
        return -1;
    }

    // Crear la dirección del socket iniciada a 0
    struct sockaddr_in socket_servidor_addr;
    memset(&socket_servidor_addr, 0, sizeof(socket_servidor_addr));

    // Rellenar los atributos
    socket_servidor_addr.sin_family = AF_INET;
    // Convertir host to network, 16 bits -> short (los datos viajarán por la red)
    socket_servidor_addr.sin_port = htons((uint16_t)puerto);
    socket_servidor_addr.sin_addr.s_addr = INADDR_ANY;

    // Crear descriptor del socket
    int socket_servidor_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (socket_servidor_fd < 0){
        perror("socket");
        return -1;
    }
    int reuse_addr = 1;
    if (setsockopt(socket_servidor_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(reuse_addr)) < 0) {
        perror("setsockopt SO_REUSEADDR");
        close(socket_servidor_fd);
        return -1;
    }

    // Unir addr y fd
    if (bind(socket_servidor_fd, (struct sockaddr *)&socket_servidor_addr, sizeof(socket_servidor_addr)) < 0){
        perror("bind");
        close(socket_servidor_fd);
        return -1;
    }

    // Poner el socket en escucha
    if (listen(socket_servidor_fd, SOMAXCONN) < 0) {
        perror("listen");
        close(socket_servidor_fd);
        return -1;
    }

    // Obtener la IP_local para imprimirla
    char host[256];
    struct hostent *hp;
    struct in_addr in;

    // Obtener el nombre del host
    if (gethostname(host, sizeof(host)) == -1) {
        perror("gethostname");
    } else {
        // Obtener la información de red del host
        hp = gethostbyname(host);
        if (hp == NULL) {
            printf("Error en gethostbyname\n");
            return -1;
        } else {
            // Copiar la dirección IP binaria
            memcpy(&in.s_addr, hp->h_addr_list[0], hp->h_length);
            
            // Si se llega hasta aquí es que el servidor está escuchando
            printf("s> init server %s:%ld\n", inet_ntoa(in), puerto);
            printf("s>\n");
        }
    }

    // Manejar la señal CTRL + C
    signal(SIGINT, handle_sigint);

    //Bucle principal del servidor
    while (1){

        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        // Socket nuevo para cada cliente

        int socket_especifico_fd = accept(socket_servidor_fd, (struct sockaddr *)&client_addr,  &client_len);
        
        // Si hay un fallo se lanza error y se continúa escuchando

        if (socket_especifico_fd < 0){
            perror("accept");
            continue;
        }


        // Crear un hilo para procesar cada solicitud
        
        // Reservamos un entero por conexión para pasar el fd al hilo sin compartir
        // la variable local del bucle principal.
        int *socket_hilo_fd = malloc(sizeof(*socket_hilo_fd));
        if (socket_hilo_fd == NULL) {
            perror("malloc socket_hilo_fd");
            close(socket_especifico_fd);
            continue;
        }
        *socket_hilo_fd = socket_especifico_fd;

        pthread_t id_hilo;
        pthread_attr_t attr_hilo;

        pthread_attr_init(&attr_hilo);
        pthread_attr_setdetachstate(&attr_hilo, PTHREAD_CREATE_DETACHED);

        if (pthread_create(&id_hilo, &attr_hilo, procesar_peticion, (void *)socket_hilo_fd) != 0) {
            perror("pthread_create");
            close(socket_especifico_fd);
            free(socket_hilo_fd);
        }
        pthread_attr_destroy(&attr_hilo);
    }

    return 0;
}
