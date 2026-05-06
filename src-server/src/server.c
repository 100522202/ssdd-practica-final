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
    // TODO: En un futuro, iterar sobre 'head' y hacer free() de los nodos, podemos meter función auxiliar en linked_list que lo haga. 
    // A no ser que la memoria deba permanecer entre ejecuciones que no lo creo
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
    user_node_t *usuario_actual = NULL; // donde almacenaremos el usuario en las operaciones

    // Variables para la conexión
    char puerto_cliente[16];
    struct sockaddr_in peer_addr; // Almacena la dirección de red del cliente conectado (IP y puerto)
    socklen_t peer_len = sizeof(peer_addr); // Variable que indica el tamaño de la estructura peer_addr, necesaria para getpeername
    char ip_cliente[INET_ADDRSTRLEN]; // Almacena la dirección IP del cliente en formato texto

    // Variables para USERS (contar cuántos están conectados)
    uint32_t num_conectados = 0;
    user_node_t *curr;
    char num_str[16]; // Buffer para almacenar el número de usuarios como cadena
    char info_str[512]; // Búfer para construir la cadena de la Parte 2

    // Variables para SEND

    // Buffers de texto
    char remitente[MSG_MAX_SIZE];
    char destinatario[MSG_MAX_SIZE];
    char texto[MSG_MAX_SIZE];
    char id_str[16];

    unsigned int msg_id; // Id del mensaje

    // Usuarios remitente y destino
    user_node_t *user_rem;
    user_node_t *user_dest;

    // Variables para gestionar la entrega de mensajes pendientes
    mensaje_pendiente_t *mensajes_a_enviar = NULL;
    char dest_name[MSG_MAX_SIZE];
    char dest_ip[INET_ADDRSTRLEN];
    char dest_puerto[16];
    int entrega_ok = 0;     // Flag para manejar más fácilmente los errores

    // TODO: revisar luego sizeofs y cosas así con el profe sabiendo que hemos puesto las variables al ppio tal y como dijo

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
            // TODO: no sé cómo lanzar error si la cadena recibida es >256, creo que no necesario así que se queda así
            printf("REGISTER: Error leyendo el nombre de usuario\n");
            close(fd_local);
            pthread_exit(NULL);
        }

        // Bloqueamos por si hay varios clientes entrando al mismo tiempo
        pthread_mutex_lock(&mutex_usuarios);

        // Verificar que existe otro usuario registrado con el mismo nombre: find_user
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
            // No hacemos pthread_exit ni close porque ya se hace al final 
            // de la función (que se ejecutará inmediatamente después de esto)
        }

    } else if (strcmp(instruccion, "UNREGISTER") == 0){
        
        // Leer el nombre del usuario a borrar
        if (readLine(fd_local, buffer, MSG_MAX_SIZE) < 0){
            printf("UNREGISTER: Error leyendo el nombre de usuario\n");
            close(fd_local);
            pthread_exit(NULL);
        }

        // Bloqueamos por si hay varios clientes entrando al mismo tiempo
        pthread_mutex_lock(&mutex_usuarios);

        // Verificar que existe otro usuario registrado con el mismo nombre: find_user
        usuario_actual = find_user(head, buffer);

        if (usuario_actual != NULL){
            // Ya existe el usuario: lo borramos
            remove_user(&head, buffer);
            resultado = 0;
            printf("s> UNREGISTER %s OK\n", buffer); // Mensaje de log
        } else {
            // El usuario no existe: error
            printf("s> UNREGISTER %s FAIL\n", buffer); // Mensaje de log
            resultado = 1; // fracaso
        }

        // Liberamos el mutex al terminar
        pthread_mutex_unlock(&mutex_usuarios);

        // Enviar el resultado
        if (sendMessage(fd_local, (char *)&resultado, sizeof(unsigned char)) < 0) {
            perror("Error enviando respuesta");
        }
    } else if (strcmp(instruccion, "CONNECT") == 0) {

        // Leer el nombre de usuario
        if (readLine(fd_local, buffer, MSG_MAX_SIZE) < 0){
            resultado = 3; // Fallo genérico
            printf("s> CONNECT FAIL\n"); 
            
            if (sendMessage(fd_local, (char *)&resultado, sizeof(unsigned char)) < 0) {
                perror("Error enviando respuesta de CONNECT");
            }
            close(fd_local);
            pthread_exit(NULL);
        }

        // Leer el puerto de escucha del cliente
        if (readLine(fd_local, puerto_cliente, 16) < 0){
            resultado = 3; // Fallo genérico
            printf("s> CONNECT %s FAIL\n", buffer);

            if (sendMessage(fd_local, (char *)&resultado, sizeof(unsigned char)) < 0) {
                perror("Error enviando respuesta de CONNECT");
            }
            close(fd_local);
            pthread_exit(NULL);
        }
        
        // Obtener la IP real del cliente a través del descriptor del socket
        if (getpeername(fd_local, (struct sockaddr*)&peer_addr, &peer_len) == 0) {
            inet_ntop(AF_INET, &peer_addr.sin_addr, ip_cliente, INET_ADDRSTRLEN);
        } else {
            // Asignar IP por defecto en caso de fallo crítico en getpeername
            strcpy(ip_cliente, "0.0.0.0"); 
        }

        // Modificar el estado del usuario: lock
        pthread_mutex_lock(&mutex_usuarios);
        
        // Buscar al usuario
        usuario_actual = find_user(head, buffer);

        if (usuario_actual == NULL) {
            resultado = 1; // El usuario no existe
            printf("s> CONNECT %s FAIL\n", buffer);
            pthread_mutex_unlock(&mutex_usuarios);
            if (sendMessage(fd_local, (char *)&resultado, 1) < 0) perror("Error enviando respuesta");
        } else if (usuario_actual->estado == ESTADO_CONECTADO) {
            resultado = 2; // El usuario ya está conectado
            printf("s> CONNECT %s FAIL\n", buffer);
            pthread_mutex_unlock(&mutex_usuarios);
            if (sendMessage(fd_local, (char *)&resultado, 1) < 0) perror("Error enviando respuesta");
        } else {
            // Usuario existe y está desconectado: actualizamos datos
            strncpy(usuario_actual->ip, ip_cliente, INET_ADDRSTRLEN);
            strncpy(usuario_actual->puerto, puerto_cliente, 16);
            usuario_actual->estado = ESTADO_CONECTADO;
            
            resultado = 0; // Éxito
            printf("s> CONNECT %s OK\n", buffer);
            
            // Desacoplar la cola de mensajes pendientes para procesarla sin bloquear el servidor
            mensajes_a_enviar = usuario_actual->mensajes;
            // Vaciar la cola del usuario en la estructura global
            usuario_actual->mensajes = NULL;
            
            // Guardar copias locales de los datos de conexión para usarlos fuera del mutex
            strncpy(dest_name, buffer, MSG_MAX_SIZE);
            strncpy(dest_ip, ip_cliente, INET_ADDRSTRLEN);
            strncpy(dest_puerto, puerto_cliente, 16);

            pthread_mutex_unlock(&mutex_usuarios);

            // Enviar el OK al cliente ANTES de enviarle los mensajes
            // Permite que el cliente cierre su connect() y abra el hilo de escucha a tiempo
            if (sendMessage(fd_local, (char *)&resultado, sizeof(unsigned char)) < 0) {
                perror("Error enviando respuesta de CONNECT");
            }

            // PROCESAR LA COLA DE MENSAJES PENDIENTES
            mensaje_pendiente_t *act = mensajes_a_enviar;
            // Recorrer la lista de mensajes
            while (act != NULL) {
                // Guardar el puntero al siguiente mensaje antes de procesar/liberar el actual
                mensaje_pendiente_t *sig = act->next;
                
                // Crear y configurar el socket para enviar el mensaje al destinatario
                int fd_dest = socket(AF_INET, SOCK_STREAM, 0);
                struct sockaddr_in addr_dest;
                memset(&addr_dest, 0, sizeof(addr_dest));
                addr_dest.sin_family = AF_INET;
                addr_dest.sin_port = htons(atoi(dest_puerto));
                addr_dest.sin_addr.s_addr = inet_addr(dest_ip);
                
                // ID número a str
                sprintf(id_str, "%u", act->id);

                // Intentar establecer la conexión TCP con el destinatario recién conectado
                if (connect(fd_dest, (struct sockaddr *)&addr_dest, sizeof(addr_dest)) == 0) {
                    entrega_ok = 1; // Éxito (si falla se pone a 0 en los ifs siguientes)

                    // Verificar que la operación y los datos se envían correctamente
                    if (sendMessage(fd_dest, "SEND_MESSAGE", strlen("SEND_MESSAGE") + 1) < 0) entrega_ok = 0;
                    else if (sendMessage(fd_dest, act->remitente, strlen(act->remitente) + 1) < 0) entrega_ok = 0;
                    else if (sendMessage(fd_dest, id_str, strlen(id_str) + 1) < 0) entrega_ok = 0;
                    else if (sendMessage(fd_dest, act->texto, strlen(act->texto) + 1) < 0) entrega_ok = 0;
                    
                    // Cerrar el socket de entrega
                    close(fd_dest);

                    if (entrega_ok) {
                        // Imprimir envío exitoso
                        printf("s> SEND MESSAGE %u FROM %s TO %s\n", act->id, act->remitente, dest_name);

                        // Bloquear el mutex temporalmente para consultar el estado del remitente
                        pthread_mutex_lock(&mutex_usuarios);
                        user_node_t *rem = find_user(head, act->remitente);

                        // Verificar si el remitente existe y sigue conectado
                        if (rem != NULL && rem->estado == ESTADO_CONECTADO) {
                            // Copiar datos del remitente para liberar cuanto antes el mutex
                            char ip_rem[INET_ADDRSTRLEN], puerto_rem[16];
                            strncpy(ip_rem, rem->ip, INET_ADDRSTRLEN);
                            strncpy(puerto_rem, rem->puerto, 16);
                            pthread_mutex_unlock(&mutex_usuarios);

                            // Crear socket para enviar el ACK al remitente
                            int fd_rem = socket(AF_INET, SOCK_STREAM, 0);
                            struct sockaddr_in addr_rem;
                            memset(&addr_rem, 0, sizeof(addr_rem));
                            addr_rem.sin_family = AF_INET;
                            addr_rem.sin_port = htons(atoi(puerto_rem));
                            addr_rem.sin_addr.s_addr = inet_addr(ip_rem);

                            // Conectar y enviar la instrucción SEND_MESS_ACK junto con el ID
                            if (connect(fd_rem, (struct sockaddr *)&addr_rem, sizeof(addr_rem)) == 0) {
                                sendMessage(fd_rem, "SEND_MESS_ACK", strlen("SEND_MESS_ACK") + 1);
                                sendMessage(fd_rem, id_str, strlen(id_str) + 1);
                                close(fd_rem);
                            }
                        } else {
                            // Liberar el mutex si el remitente está desconectado o no existe
                            pthread_mutex_unlock(&mutex_usuarios);
                        }

                        // Liberar la memoria dinámica ocupada por el mensaje entregado
                        free(act); 
                    }
                } else {
                    // Fallo de conexión: cerrar socket
                    close(fd_dest);
                }

                // Manejar los fallos de red producidos por los envíos
                if (!entrega_ok) {
                    // Bloquear el mutex para marcar al usuario como desconectado por fallo de red
                    pthread_mutex_lock(&mutex_usuarios);
                    user_node_t *u = find_user(head, dest_name);
                    if (u != NULL) {
                        // Cambiar estado y limpiar datos de red
                        u->estado = ESTADO_DESCONECTADO;
                        memset(u->ip, 0, INET_ADDRSTRLEN);
                        memset(u->puerto, 0, 16);

                        // Enganchar los mensajes no enviados de vuelta a la cola del usuario
                        if (u->mensajes == NULL) {
                            u->mensajes = act;
                        } else {
                            // Si por algún motivo ya había mensajes nuevos, colocarlos al final
                            mensaje_pendiente_t *aux = u->mensajes;
                            while (aux->next != NULL) aux = aux->next;
                            aux->next = act;
                        }
                    } else {
                        // Liberar la memoria restante en el caso extremo de que el usuario haya sido borrado
                        while(act != NULL) {
                            mensaje_pendiente_t *t = act->next;
                            free(act);
                            act = t;
                        }
                    }
                    // Liberar el mutex y cortar el bucle de envíos
                    pthread_mutex_unlock(&mutex_usuarios);
                    break;
                }
                
                // Avanzar al siguiente mensaje de la lista
                act = sig;
            }
        }

    } else if (strcmp(instruccion, "DISCONNECT") == 0){

        // Leer el nombre de usuario
        if (readLine(fd_local, buffer, MSG_MAX_SIZE) < 0){
            resultado = 3;
            printf("s> DISCONNECT FAIL\n");
            if (sendMessage(fd_local, (char *)&resultado, sizeof(unsigned char)) < 0) {
                perror("Error enviando respuesta de CONNECT");
            }
            close(fd_local);
            pthread_exit(NULL);
        }

        // Desconectar al usuario
        pthread_mutex_lock(&mutex_usuarios);
        
        usuario_actual = find_user(head, buffer);

        if (usuario_actual == NULL) {
            resultado = 1; // El usuario no existe
            printf("s> DISCONNECT %s FAIL\n", buffer);
        } else if (usuario_actual->estado == ESTADO_DESCONECTADO) {
            resultado = 2; // El usuario no estaba conectado
            printf("s> DISCONNECT %s FAIL\n", buffer);
        } else {
            // Usuario existe y estaba conectado: limpiar datos y desconectar
            memset(usuario_actual->ip, 0, INET_ADDRSTRLEN);
            memset(usuario_actual->puerto, 0, 16);
            usuario_actual->estado = ESTADO_DESCONECTADO;
            
            resultado = 0; // Éxito
            printf("s> DISCONNECT %s OK\n", buffer);
        }

        pthread_mutex_unlock(&mutex_usuarios);

        // Enviar el código de respuesta al cliente
        if (sendMessage(fd_local, (char *)&resultado, sizeof(unsigned char)) < 0) {
            perror("Error enviando respuesta de DISCONNECT");
        }

    } else if (strcmp(instruccion, "USERS") == 0) {

        // Leer el nombre del usuario que hace la petición
        if (readLine(fd_local, buffer, MSG_MAX_SIZE) < 0) {
            resultado = 2; // Error de lectura (cualquier otro error = 2)
            if (sendMessage(fd_local, (char *)&resultado, 1) < 0) {
                perror("Error enviando resultado");
            }
            printf("s> CONNECTEDUSERS FAIL\n");
            close(fd_local);
            pthread_exit(NULL);
        }

        pthread_mutex_lock(&mutex_usuarios);

        // Buscar al usuario que hace la petición
        usuario_actual = find_user(head, buffer);

        if (usuario_actual == NULL) {
            // El usuario no está registrado
            resultado = 2;
            pthread_mutex_unlock(&mutex_usuarios);
            if (sendMessage(fd_local, (char *)&resultado, 1) < 0) {
                perror("Error enviando resultado");
            }
            printf("s> CONNECTEDUSERS FAIL\n");
        } 
        else if (usuario_actual->estado == ESTADO_DESCONECTADO) {
            // El usuario existe pero no está conectado
            resultado = 1;
            pthread_mutex_unlock(&mutex_usuarios);
            if (sendMessage(fd_local, (char *)&resultado, 1) < 0) {
                perror("Error enviando resultado");
            }
            printf("s> CONNECTEDUSERS FAIL\n");
        }
        else {
            // El usuario existe y está conectado (Éxito)
            resultado = 0;
            if (sendMessage(fd_local, (char *)&resultado, 1) < 0) {
                perror("Error enviando resultado");
            }

            curr = head;
            while (curr != NULL) {
                if (curr->estado == ESTADO_CONECTADO) num_conectados++;
                curr = curr->next;
            }

            // Enviar el número de usuarios como cadena terminada en \0
            sprintf(num_str, "%u", num_conectados);
            if (sendMessage(fd_local, num_str, strlen(num_str) + 1) < 0) {
                perror("Error enviando número de usuarios");
            }

            // Enviar los datos de cada usuario conectado
            curr = head;
            while (curr != NULL) {
                if (curr->estado == ESTADO_CONECTADO) {
                    // Fusionar en una sola cadena (Parte 2: "usuario:: IP:: puerto")
                    sprintf(info_str, "%s :: %s :: %s", curr->userName, curr->ip, curr->puerto);
                    
                    if (sendMessage(fd_local, info_str, strlen(info_str) + 1) < 0) {
                        perror("Error enviando información de usuario");
                    }
                }
                curr = curr->next;
            }
            pthread_mutex_unlock(&mutex_usuarios);
            
            // Imprimir traza en el servidor según especificación
            printf("s> CONNECTEDUSERS OK\n");
        }
    } else if (strcmp(instruccion, "SEND") == 0) {
        

        // 1. Leer remitente, destinatario y mensaje
        // Se leen secuencialmente los tres campos delimitados por \0
        if (readLine(fd_local, remitente, MSG_MAX_SIZE) < 0 ||
            readLine(fd_local, destinatario, MSG_MAX_SIZE) < 0 ||
            readLine(fd_local, texto, MSG_MAX_SIZE) < 0) { 
            
            resultado = 2; // Error genérico d lectura
            if (sendMessage(fd_local, (char *)&resultado, 1) < 0) perror("Error enviando error");
            close(fd_local);
            pthread_exit(NULL);
        }

        // Bloquear mutex para operar sobre la lista de usuarios
        pthread_mutex_lock(&mutex_usuarios);

        // 2. Comprobar que existen ambos usuarios
        user_rem = find_user(head, remitente);
        user_dest = find_user(head, destinatario);

        if (user_rem == NULL || user_dest == NULL) {
            resultado = 1; // Alguno de los dos usuarios no existe -> código 1
            pthread_mutex_unlock(&mutex_usuarios);
            
            if (sendMessage(fd_local, (char *)&resultado, 1) < 0) perror("Error enviando error");
            printf("s> SEND FAIL\n"); 
        } 
        else {
            // 3. Asignar ID al mensaje
            // La variable de tipo unsigned int vuelve a 0 si desborda, y el protocolo dice que el siguiente debe ser 1
            user_rem->ultimo_id_msg++;
            if (user_rem->ultimo_id_msg == 0) {
                user_rem->ultimo_id_msg = 1; 
            }
            msg_id = user_rem->ultimo_id_msg;
            sprintf(id_str, "%u", msg_id);

            // 4. Almacenar el mensaje en memoria dinámica
            mensaje_pendiente_t *nuevo_msg = (mensaje_pendiente_t *)malloc(sizeof(mensaje_pendiente_t));
            nuevo_msg->id = msg_id;

            // Forzar terminador nulo al final de las cadenas
            strncpy(nuevo_msg->remitente, remitente, MSG_MAX_SIZE - 1);
            nuevo_msg->remitente[MSG_MAX_SIZE - 1] = '\0';
            strncpy(nuevo_msg->texto, texto, MSG_MAX_SIZE - 1);
            nuevo_msg->texto[MSG_MAX_SIZE - 1] = '\0';
            // strncpy(nuevo_msg->fichero, "", MSG_MAX_SIZE - 1); // TODO: Descomentar en la Parte 2
            // nuevo_msg->fichero[MSG_MAX_SIZE - 1] = '\0';
            nuevo_msg->next = NULL;

            // Añadir el mensaje al final de la cola del destinatario
            if (user_dest->mensajes == NULL) {
                user_dest->mensajes = nuevo_msg;
            } else {
                mensaje_pendiente_t *aux = user_dest->mensajes;
                // Recorrer hasta el final si no está vacía
                while (aux->next != NULL) {
                    aux = aux->next;
                }
                aux->next = nuevo_msg;
            }

            // 5. Responder al remitente con éxito
            resultado = 0;
            sendMessage(fd_local, (char *)&resultado, 1);
            sendMessage(fd_local, id_str, strlen(id_str) + 1); // +1 para incluir \0

            // 6. Gestionar el envío asíncrono
            if (user_dest->estado == ESTADO_CONECTADO) {
                // Copiar IPs y puertos en variables locales para poder hacer el unlock del mutex 
                // antes de usar connect(), evitando bloquear el servidor si la red va lenta
                char ip_dest[INET_ADDRSTRLEN], puerto_dest[16];
                strncpy(ip_dest, user_dest->ip, INET_ADDRSTRLEN - 1); 
                ip_dest[INET_ADDRSTRLEN - 1] = '\0';
                strncpy(puerto_dest, user_dest->puerto, 15); 
                puerto_dest[15] = '\0';
                
                int rem_conectado = (user_rem->estado == ESTADO_CONECTADO);
                char ip_rem[INET_ADDRSTRLEN], puerto_rem[16];
                if (rem_conectado) {
                    // De nuevo, copiar las cadenas asegurando '\0'
                    strncpy(ip_rem, user_rem->ip, INET_ADDRSTRLEN - 1); 
                    ip_rem[INET_ADDRSTRLEN - 1] = '\0';
                    strncpy(puerto_rem, user_rem->puerto, 15); 
                    puerto_rem[15] = '\0';
                }

                pthread_mutex_unlock(&mutex_usuarios);

                // Aquí el servidor actuará como cliente
                
                // Conectar al destinatario
                int fd_dest = socket(AF_INET, SOCK_STREAM, 0);
                struct sockaddr_in addr_dest;
                memset(&addr_dest, 0, sizeof(addr_dest));
                addr_dest.sin_family = AF_INET;
                addr_dest.sin_port = htons(atoi(puerto_dest)); // cadena -> int -> red
                addr_dest.sin_addr.s_addr = inet_addr(ip_dest);

                int entrega_ok = 0; // Flag para control de errores: 1 si connect y sendMessages tienen éxito, 0 eoc
                
                // Intentar conectar al socket de escucha del destinatario
                if (connect(fd_dest, (struct sockaddr *)&addr_dest, sizeof(addr_dest)) == 0) {
                    entrega_ok = 1;
                    // Enviar operación, remitente, ID y texto secuencialmente, comprobando fallos
                    if (sendMessage(fd_dest, "SEND_MESSAGE", strlen("SEND_MESSAGE") + 1) < 0) entrega_ok = 0;
                    else if (sendMessage(fd_dest, remitente, strlen(remitente) + 1) < 0) entrega_ok = 0;
                    else if (sendMessage(fd_dest, id_str, strlen(id_str) + 1) < 0) entrega_ok = 0;
                    else if (sendMessage(fd_dest, texto, strlen(texto) + 1) < 0) entrega_ok = 0;
                    
                    // Cerrar la conexión con el destinatario tras el envío
                    close(fd_dest);

                    // Proceder con el ACK y el borrado solo si la entrega fue exitosa
                    if (entrega_ok) {

                        // Enviar notificación (ACK) al remitente si se encuentra conectado
                        if (rem_conectado) {

                            // Crear y configurar socket correspondiente
                            int fd_rem = socket(AF_INET, SOCK_STREAM, 0);
                            struct sockaddr_in addr_rem;
                            memset(&addr_rem, 0, sizeof(addr_rem));
                            addr_rem.sin_family = AF_INET;
                            addr_rem.sin_port = htons(atoi(puerto_rem));
                            addr_rem.sin_addr.s_addr = inet_addr(ip_rem);

                            // Conectar y enviar la instrucción SEND_MESS_ACK junto con el ID
                            if (connect(fd_rem, (struct sockaddr *)&addr_rem, sizeof(addr_rem)) == 0) {
                                sendMessage(fd_rem, "SEND_MESS_ACK", strlen("SEND_MESS_ACK") + 1);
                                sendMessage(fd_rem, id_str, strlen(id_str) + 1);
                                close(fd_rem);
                            }
                        }

                        // Bloquear el mutex para eliminar el mensaje de la cola
                        pthread_mutex_lock(&mutex_usuarios);

                        // Buscar nuevamente al destinatario por si su estado ha cambiado
                        user_node_t *check_dest = find_user(head, destinatario);
                        if (check_dest != NULL) {
                            mensaje_pendiente_t *ant = NULL;
                            mensaje_pendiente_t *act = check_dest->mensajes;

                            // Recorrer la lista de mensajes pendientes
                            while (act != NULL) {
                                // Buscar el mensaje por su ID exacto para evitar condiciones de carrera
                                if (act->id == msg_id) {

                                    // Desenlazar el nodo de la lista y liberar memoria asociada
                                    if (ant == NULL) check_dest->mensajes = act->next;
                                    else ant->next = act->next;
                                    free(act);
                                    break;
                                }
                                ant = act;
                                act = act->next;
                            }
                        }
                        // Liberar el mutex al terminar de modificar la cola
                        pthread_mutex_unlock(&mutex_usuarios);

                        // Imprimir éxito
                        printf("s> SEND MESSAGE %u FROM %s TO %s\n", msg_id, remitente, destinatario);
                    }
                } 
                else {
                    close(fd_dest); // Fallo de conexión
                }

                // Gestionar errores de red (fallo en connect o en los sendMessage)
                if (!entrega_ok) {
                    // Bloquear mutex para modificar el estado del destinatario
                    pthread_mutex_lock(&mutex_usuarios);

                    user_node_t *check_dest = find_user(head, destinatario);
                    if (check_dest != NULL) {
                        // Marcar como desconectado y limpiar sus datos
                        check_dest->estado = ESTADO_DESCONECTADO;
                        memset(check_dest->ip, 0, sizeof(check_dest->ip));
                        memset(check_dest->puerto, 0, sizeof(check_dest->puerto));
                    }
                    pthread_mutex_unlock(&mutex_usuarios);
                    
                    // El mensaje se mantiene STORED porque no fue eliminado de la cola
                    printf("s> MESSAGE %u FROM %s TO %s STORED\n", msg_id, remitente, destinatario);
                }
            } 
            else {
                // El destinatario ya estaba desconectado
                pthread_mutex_unlock(&mutex_usuarios);
                printf("s> MESSAGE %u FROM %s TO %s STORED\n", msg_id, remitente, destinatario);
            }
        }
    } else if (strcmp(instruccion, "SENDATTACH") == 0){

    } else {
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

    // Validar flag -p

    if (strcmp(argv[1], "-p") != 0){
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
