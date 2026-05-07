#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdint.h>
#include <pthread.h>

#include "mensajes.h"
#include "linked_list.h"
#include "log_rpc_client.h"
#include "operaciones.h"

#define MSG_MAX_SIZE 256 // Como mucho 255 + '/0', establecido por el enunciado.



// Funciones auxiliares compartidas.
/*
 * enviar_ack_remitente: Envía un ACK al remitente de un mensaje.
 * Si tiene_adjunto es 1, envía "SEND MESS ATTACH ACK" + id + fichero.
 * Si tiene_adjunto es 0, envía "SEND MESS ACK" + id.
 *
 * - ip_rem: IP del remitente.
 * - puerto_rem: Puerto del remitente.
 * - id_str: Identificador del mensaje como cadena.
 * - fichero: Nombre del fichero adjunto (solo se usa si tiene_adjunto == 1).
 * - tiene_adjunto: 0 = mensaje normal, 1 = mensaje con adjunto.
 */
void enviar_ack_remitente(const char *ip_rem, const char *puerto_rem,
                          const char *id_str, const char *fichero,
                          int tiene_adjunto)
{
    // Crear socket para enviar el ACK al remitente.
    int fd_rem = socket(AF_INET, SOCK_STREAM, 0);
    if (fd_rem < 0) return;

    struct sockaddr_in addr_rem;
    memset(&addr_rem, 0, sizeof(addr_rem));
    addr_rem.sin_family = AF_INET;
    addr_rem.sin_port = htons(atoi(puerto_rem));
    addr_rem.sin_addr.s_addr = inet_addr(ip_rem);

    // Conectar y enviar la instrucción de ACK junto con el ID.
    if (connect(fd_rem, (struct sockaddr *)&addr_rem, sizeof(addr_rem)) == 0) {
        if (tiene_adjunto) {
            sendMessage(fd_rem, "SEND MESS ATTACH ACK", strlen("SEND MESS ATTACH ACK") + 1);
            sendMessage(fd_rem, (char *)id_str, strlen(id_str) + 1);
            sendMessage(fd_rem, (char *)fichero, strlen(fichero) + 1);
        } else {
            sendMessage(fd_rem, "SEND MESS ACK", strlen("SEND MESS ACK") + 1);
            sendMessage(fd_rem, (char *)id_str, strlen(id_str) + 1);
        }
    }
    close(fd_rem);
}


/*
 * eliminar_mensaje_cola: Busca y elimina un mensaje de la cola de pendientes
 * del destinatario, identificándolo por su ID (y opcionalmente por adjunto/remitente/fichero).
 *
 * - destinatario: Nombre del usuario destinatario.
 * - msg_id: ID numérico del mensaje a eliminar.
 * - remitente: Nombre del remitente (para comparación más estricta en SENDATTACH, NULL si no se necesita).
 * - fichero: Nombre del fichero (para comparación más estricta en SENDATTACH, NULL si no se necesita).
 * - tiene_adjunto: -1 = no comprobar, 0 o 1 = comprobar que coincida.
 * - mutex: Puntero al mutex que protege la lista de usuarios.
 * - head: Puntero al puntero de la cabeza de la lista de usuarios.
 *
 * NOTA: esta función BLOQUEA y LIBERA el mutex internamente.
 */
void eliminar_mensaje_cola(const char *destinatario, unsigned int msg_id,
                           const char *remitente, const char *fichero,
                           int tiene_adjunto,
                           pthread_mutex_t *mutex, user_node_t **head)
{
    // Bloquear el mutex para eliminar el mensaje de la cola.
    pthread_mutex_lock(mutex);

    // Buscar nuevamente al destinatario por si su estado ha cambiado.
    user_node_t *check_dest = find_user(*head, destinatario);
    if (check_dest != NULL) {
        mensaje_pendiente_t *ant = NULL;
        mensaje_pendiente_t *act = check_dest->mensajes;

        // Recorrer la lista de mensajes pendientes.
        while (act != NULL) {
            // Buscar el mensaje por su ID exacto para evitar condiciones de carrera.
            int coincide = (act->id == msg_id);

            // Comparación extendida para SENDATTACH (fichero + remitente + flag adjunto).
            if (coincide && tiene_adjunto >= 0) {
                coincide = (act->tiene_adjunto == tiene_adjunto);
            }
            if (coincide && remitente != NULL) {
                coincide = (strcmp(act->remitente, remitente) == 0);
            }
            if (coincide && fichero != NULL) {
                coincide = (strcmp(act->fichero, fichero) == 0);
            }

            if (coincide) {
                // Desenlazar el nodo de la lista y liberar memoria asociada.
                if (ant == NULL) check_dest->mensajes = act->next;
                else ant->next = act->next;
                free(act);
                break;
            }
            ant = act;
            act = act->next;
        }
    }
    // Liberar el mutex al terminar de modificar la cola.
    pthread_mutex_unlock(mutex);
}


/*
 * entregar_pendientes: Entrega la cola de mensajes desacoplada a un usuario
 * recién conectado. Se usa durante CONNECT.
 *
 * - mensajes_a_enviar: Cabeza de la lista de mensajes desacoplados.
 * - dest_name: Nombre del usuario destinatario.
 * - dest_ip: IP del destinatario.
 * - dest_puerto: Puerto del destinatario.
 * - mutex: Puntero al mutex que protege la lista de usuarios.
 * - head: Puntero al puntero de la cabeza de la lista de usuarios.
 */
void entregar_pendientes(mensaje_pendiente_t *mensajes_a_enviar,
                         const char *dest_name,
                         const char *dest_ip,
                         const char *dest_puerto,
                         pthread_mutex_t *mutex, user_node_t **head)
{
    char id_str[16];
    int entrega_ok = 0;

    // Recorrer la lista de mensajes.
    mensaje_pendiente_t *act = mensajes_a_enviar;
    while (act != NULL) {
        entrega_ok = 0;

        // Guardar el puntero al siguiente mensaje antes de procesar/liberar el actual.
        mensaje_pendiente_t *sig = act->next;
        
        // Crear y configurar el socket para enviar el mensaje al destinatario.
        int fd_dest = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in addr_dest;
        memset(&addr_dest, 0, sizeof(addr_dest));
        addr_dest.sin_family = AF_INET;
        addr_dest.sin_port = htons(atoi(dest_puerto));
        addr_dest.sin_addr.s_addr = inet_addr(dest_ip);
        
        // ID número a str.
        sprintf(id_str, "%u", act->id);

        // Intentar establecer la conexión TCP con el destinatario recién conectado.
        if (connect(fd_dest, (struct sockaddr *)&addr_dest, sizeof(addr_dest)) == 0) {
            entrega_ok = 1; // Éxito (si falla se pone a 0 en los ifs siguientes).

            // Verificar que la operación y los datos se envían correctamente.
            if (act->tiene_adjunto) {
                if (sendMessage(fd_dest, "SEND MESSAGE ATTACH", strlen("SEND MESSAGE ATTACH") + 1) < 0) entrega_ok = 0;
                else if (sendMessage(fd_dest, act->remitente, strlen(act->remitente) + 1) < 0) entrega_ok = 0;
                else if (sendMessage(fd_dest, id_str, strlen(id_str) + 1) < 0) entrega_ok = 0;
                else if (sendMessage(fd_dest, act->texto, strlen(act->texto) + 1) < 0) entrega_ok = 0;
                else if (sendMessage(fd_dest, act->fichero, strlen(act->fichero) + 1) < 0) entrega_ok = 0;
            } else {
                if (sendMessage(fd_dest, "SEND MESSAGE", strlen("SEND MESSAGE") + 1) < 0) entrega_ok = 0;
                else if (sendMessage(fd_dest, act->remitente, strlen(act->remitente) + 1) < 0) entrega_ok = 0;
                else if (sendMessage(fd_dest, id_str, strlen(id_str) + 1) < 0) entrega_ok = 0;
                else if (sendMessage(fd_dest, act->texto, strlen(act->texto) + 1) < 0) entrega_ok = 0;
            }
            
            // Cerrar el socket de entrega.
            close(fd_dest);

            if (entrega_ok) {
                // Imprimir envío exitoso.
                printf("s> SEND MESSAGE %u FROM %s TO %s\n", act->id, act->remitente, dest_name);

                // Bloquear el mutex temporalmente para consultar el estado del remitente.
                pthread_mutex_lock(mutex);
                user_node_t *rem = find_user(*head, act->remitente);

                // Verificar si el remitente existe y sigue conectado.
                if (rem != NULL && rem->estado == ESTADO_CONECTADO) {
                    // Copiar datos del remitente para liberar cuanto antes el mutex.
                    char ip_rem[INET_ADDRSTRLEN], puerto_rem[16];
                    strncpy(ip_rem, rem->ip, INET_ADDRSTRLEN - 1);
                    ip_rem[INET_ADDRSTRLEN - 1] = '\0';
                    strncpy(puerto_rem, rem->puerto, 15);
                    puerto_rem[15] = '\0';
                    pthread_mutex_unlock(mutex);

                    // Enviar ACK al remitente usando la función auxiliar.
                    enviar_ack_remitente(ip_rem, puerto_rem, id_str,
                                        act->fichero, act->tiene_adjunto);
                } else {
                    // Liberar el mutex si el remitente está desconectado o no existe.
                    pthread_mutex_unlock(mutex);
                }

                // Liberar la memoria dinámica ocupada por el mensaje entregado.
                free(act); 
            }
        } else {
            // Fallo de conexión: cerrar socket.
            close(fd_dest);
        }

        // Manejar los fallos de red producidos por los envíos.
        if (!entrega_ok) {
            // Bloquear el mutex para marcar al usuario como desconectado por fallo de red.
            pthread_mutex_lock(mutex);
            user_node_t *u = find_user(*head, dest_name);
            if (u != NULL) {
                // Cambiar estado y limpiar datos de red.
                u->estado = ESTADO_DESCONECTADO;
                memset(u->ip, 0, INET_ADDRSTRLEN);
                memset(u->puerto, 0, 16);

                // Enganchar los mensajes no enviados de vuelta a la cola del usuario.
                if (u->mensajes == NULL) {
                    u->mensajes = act;
                } else {
                    // Si por algún motivo ya había mensajes nuevos, colocarlos al final.
                    mensaje_pendiente_t *aux = u->mensajes;
                    while (aux->next != NULL) aux = aux->next;
                    aux->next = act;
                }
            } else {
                // Liberar la memoria restante en el caso extremo de que el usuario haya sido borrado.
                while(act != NULL) {
                    mensaje_pendiente_t *t = act->next;
                    free(act);
                    act = t;
                }
            }
            // Liberar el mutex y cortar el bucle de envíos.
            pthread_mutex_unlock(mutex);
            break;
        }
        
        // Avanzar al siguiente mensaje de la lista.
        act = sig;
    }
}



// Funciones para cada operación del protocolo.

/*
 * procesar_register: Gestión de la operación REGISTER.
 * Lee el nombre de usuario y lo registra en la lista enlazada si no existe.
 */
void procesar_register(int fd_local, pthread_mutex_t *mutex, user_node_t **head)
{
    char buffer[MSG_MAX_SIZE]; // Buffer para el texto que acompaña a las instrucciones.
    unsigned char resultado; // Para devolver posteriormente el resultado de la operación.
    user_node_t *usuario_actual = NULL; // Donde almacenaremos el usuario en las operaciones.

    // Leer el nombre de usuario a registrar.
    if (readLine(fd_local, buffer, MSG_MAX_SIZE) < 0){
        printf("REGISTER: Error leyendo el nombre de usuario\n");
        close(fd_local);
        pthread_exit(NULL);
    }

    log_rpc_operacion(buffer, "REGISTER", NULL);

    // Bloqueamos por si hay varios clientes entrando al mismo tiempo.
    pthread_mutex_lock(mutex);

    // Verificar que existe otro usuario registrado con el mismo nombre: find_user.
    usuario_actual = find_user(*head, buffer);

    if (usuario_actual != NULL){
        resultado = 1; // Ya existe el usuario.
        printf("s> REGISTER %s FAIL\n", buffer); // Mensaje de log para error.
    } else {
        // El usuario no existe: lo añadimos.
        add_user(head, buffer);
        resultado = 0; // Éxito.
        printf("s> REGISTER %s OK\n", buffer); // Mensaje de log.
    }

    // Liberamos el mutex al terminar.
    pthread_mutex_unlock(mutex);

    // Enviar el resultado.
    if (sendMessage(fd_local, (char *)&resultado, sizeof(unsigned char)) < 0) {
        perror("Error enviando respuesta");
        // No hacemos pthread_exit ni close porque ya se hace al final.
        // De la función (que se ejecutará inmediatamente después de esto).
    }
}


/*
 * procesar_unregister: Gestión de la operación UNREGISTER.
 * Lee el nombre del usuario y lo elimina de la lista si existe.
 */
void procesar_unregister(int fd_local, pthread_mutex_t *mutex, user_node_t **head)
{
    char buffer[MSG_MAX_SIZE];
    unsigned char resultado;
    user_node_t *usuario_actual = NULL;

    // Leer el nombre del usuario a borrar.
    if (readLine(fd_local, buffer, MSG_MAX_SIZE) < 0){
        printf("UNREGISTER: Error leyendo el nombre de usuario\n");
        close(fd_local);
        pthread_exit(NULL);
    }

    log_rpc_operacion(buffer, "UNREGISTER", NULL);

    // Bloqueamos por si hay varios clientes entrando al mismo tiempo.
    pthread_mutex_lock(mutex);

    // Verificar que existe otro usuario registrado con el mismo nombre: find_user.
    usuario_actual = find_user(*head, buffer);

    if (usuario_actual != NULL){
        // Ya existe el usuario: lo borramos.
        remove_user(head, buffer);
        resultado = 0;
        printf("s> UNREGISTER %s OK\n", buffer); // Mensaje de log.
    } else {
        // El usuario no existe: error.
        printf("s> UNREGISTER %s FAIL\n", buffer); // Mensaje de log.
        resultado = 1; // Fracaso.
    }

    // Liberamos el mutex al terminar.
    pthread_mutex_unlock(mutex);

    // Enviar el resultado.
    if (sendMessage(fd_local, (char *)&resultado, sizeof(unsigned char)) < 0) {
        perror("Error enviando respuesta");
    }
}


/*
 * procesar_connect: Gestión de la operación CONNECT.
 * Lee el nombre de usuario y el puerto de escucha, actualiza el estado
 * del usuario y le entrega los mensajes pendientes si los tiene.
 */
void procesar_connect(int fd_local, pthread_mutex_t *mutex, user_node_t **head)
{
    char buffer[MSG_MAX_SIZE];
    unsigned char resultado;
    user_node_t *usuario_actual = NULL;

    // Variables para la conexión.
    char puerto_cliente[16];
    struct sockaddr_in peer_addr; // Almacena la dirección de red del cliente conectado (IP y puerto).
    socklen_t peer_len = sizeof(peer_addr); // Variable que indica el tamaño de la estructura peer_addr, necesaria para getpeername.
    char ip_cliente[INET_ADDRSTRLEN]; // Almacena la dirección IP del cliente en formato texto.

    // Variables para gestionar la entrega de mensajes pendientes.
    mensaje_pendiente_t *mensajes_a_enviar = NULL;
    char dest_name[MSG_MAX_SIZE];
    char dest_ip[INET_ADDRSTRLEN];
    char dest_puerto[16];

    // Leer el nombre de usuario.
    if (readLine(fd_local, buffer, MSG_MAX_SIZE) < 0){
        resultado = 3; // Fallo genérico.
        printf("s> CONNECT FAIL\n"); 
        
        if (sendMessage(fd_local, (char *)&resultado, sizeof(unsigned char)) < 0) {
            perror("Error enviando respuesta de CONNECT");
        }
        close(fd_local);
        pthread_exit(NULL);
    }

    // Leer el puerto de escucha del cliente.
    if (readLine(fd_local, puerto_cliente, 16) < 0){
        resultado = 3; // Fallo genérico.
        printf("s> CONNECT %s FAIL\n", buffer);

        if (sendMessage(fd_local, (char *)&resultado, sizeof(unsigned char)) < 0) {
            perror("Error enviando respuesta de CONNECT");
        }
        close(fd_local);
        pthread_exit(NULL);
    }

    log_rpc_operacion(buffer, "CONNECT", NULL);
    
    // Obtener la IP real del cliente a través del descriptor del socket.
    if (getpeername(fd_local, (struct sockaddr*)&peer_addr, &peer_len) == 0) {
        inet_ntop(AF_INET, &peer_addr.sin_addr, ip_cliente, INET_ADDRSTRLEN);
    } else {
        // Asignar IP por defecto en caso de fallo crítico en getpeername.
        strcpy(ip_cliente, "0.0.0.0"); 
    }

    // Modificar el estado del usuario: lock.
    pthread_mutex_lock(mutex);
    
    // Buscar al usuario.
    usuario_actual = find_user(*head, buffer);

    if (usuario_actual == NULL) {
        resultado = 1; // El usuario no existe.
        printf("s> CONNECT %s FAIL\n", buffer);
        pthread_mutex_unlock(mutex);
        if (sendMessage(fd_local, (char *)&resultado, 1) < 0) perror("Error enviando respuesta");
    } else if (usuario_actual->estado == ESTADO_CONECTADO) {
        resultado = 2; // El usuario ya está conectado.
        printf("s> CONNECT %s FAIL\n", buffer);
        pthread_mutex_unlock(mutex);
        if (sendMessage(fd_local, (char *)&resultado, 1) < 0) perror("Error enviando respuesta");
    } else {
        // Usuario existe y está desconectado: actualizamos datos.
        strncpy(usuario_actual->ip, ip_cliente, INET_ADDRSTRLEN - 1);
        usuario_actual->ip[INET_ADDRSTRLEN - 1] = '\0';
        strncpy(usuario_actual->puerto, puerto_cliente, 15);
        usuario_actual->puerto[15] = '\0';
        usuario_actual->estado = ESTADO_CONECTADO;
        
        resultado = 0; // Éxito.
        printf("s> CONNECT %s OK\n", buffer);
        
        // Desacoplar la cola de mensajes pendientes para procesarla sin bloquear el servidor.
        mensajes_a_enviar = usuario_actual->mensajes;
        // Vaciar la cola del usuario en la estructura global.
        usuario_actual->mensajes = NULL;
        
        // Guardar copias locales de los datos de conexión para usarlos fuera del mutex.
        strncpy(dest_name, buffer, MSG_MAX_SIZE - 1);
        dest_name[MSG_MAX_SIZE - 1] = '\0';
        strncpy(dest_ip, ip_cliente, INET_ADDRSTRLEN - 1);
        dest_ip[INET_ADDRSTRLEN - 1] = '\0';
        strncpy(dest_puerto, puerto_cliente, 15);
        dest_puerto[15] = '\0';

        pthread_mutex_unlock(mutex);

        // Enviar el OK al cliente ANTES de enviarle los mensajes.
        // Permite que el cliente cierre su connect() y abra el hilo de escucha a tiempo.
        if (sendMessage(fd_local, (char *)&resultado, sizeof(unsigned char)) < 0) {
            perror("Error enviando respuesta de CONNECT");
        }

        // PROCESAR LA COLA DE MENSAJES PENDIENTES.
        entregar_pendientes(mensajes_a_enviar, dest_name, dest_ip, dest_puerto, mutex, head);
    }
}


/*
 * procesar_disconnect: Gestión de la operación DISCONNECT.
 * Lee el nombre de usuario y lo marca como desconectado si existe y estaba conectado.
 */
void procesar_disconnect(int fd_local, pthread_mutex_t *mutex, user_node_t **head)
{
    char buffer[MSG_MAX_SIZE];
    unsigned char resultado;
    user_node_t *usuario_actual = NULL;

    // Variables para comprobar la IP del cliente conectado (según Sección 8.4)
    struct sockaddr_in peer_addr;
    socklen_t peer_len = sizeof(peer_addr);
    char ip_cliente[INET_ADDRSTRLEN];

    // Leer el nombre de usuario.
    if (readLine(fd_local, buffer, MSG_MAX_SIZE) < 0){
        resultado = 3;
        printf("s> DISCONNECT FAIL\n");
        if (sendMessage(fd_local, (char *)&resultado, sizeof(unsigned char)) < 0) {
            perror("Error enviando respuesta de CONNECT");
        }
        close(fd_local);
        pthread_exit(NULL);
    }

    log_rpc_operacion(buffer, "DISCONNECT", NULL);

    // Obtener la IP real del cliente a través del descriptor del socket.
    if (getpeername(fd_local, (struct sockaddr*)&peer_addr, &peer_len) == 0) {
        inet_ntop(AF_INET, &peer_addr.sin_addr, ip_cliente, INET_ADDRSTRLEN);
    } else {
        strcpy(ip_cliente, "0.0.0.0"); 
    }

    // Desconectar al usuario.
    pthread_mutex_lock(mutex);
    
    usuario_actual = find_user(*head, buffer);

    if (usuario_actual == NULL) {
        resultado = 1; // El usuario no existe.
        printf("s> DISCONNECT %s FAIL\n", buffer);
    } else if (usuario_actual->estado == ESTADO_DESCONECTADO) {
        resultado = 2; // El usuario no estaba conectado.
        printf("s> DISCONNECT %s FAIL\n", buffer);
    } else if (strcmp(usuario_actual->ip, ip_cliente) != 0) {
        resultado = 3; // Intento de desconexión desde otra IP diferente a la que hizo CONNECT.
        printf("s> DISCONNECT %s FAIL\n", buffer);
    } else {
        // Usuario existe y estaba conectado desde la misma IP: limpiar datos y desconectar.
        memset(usuario_actual->ip, 0, INET_ADDRSTRLEN);
        memset(usuario_actual->puerto, 0, 16);
        usuario_actual->estado = ESTADO_DESCONECTADO;
        
        resultado = 0; // Éxito.
        printf("s> DISCONNECT %s OK\n", buffer);
    }

    pthread_mutex_unlock(mutex);

    // Enviar el código de respuesta al cliente.
    if (sendMessage(fd_local, (char *)&resultado, sizeof(unsigned char)) < 0) {
        perror("Error enviando respuesta de DISCONNECT");
    }
}


/*
 * procesar_users: Gestión de la operación USERS.
 * Lee el nombre del usuario solicitante y envía la lista de usuarios conectados
 * con su IP y puerto.
 */
void procesar_users(int fd_local, pthread_mutex_t *mutex, user_node_t **head)
{
    char buffer[MSG_MAX_SIZE];
    unsigned char resultado;
    user_node_t *usuario_actual = NULL;

    // Variables para USERS (contar cuántos están conectados).
    uint32_t num_conectados = 0;
    user_node_t *curr;
    char num_str[16]; // Buffer para almacenar el número de usuarios como cadena.
    char user_info[512]; // Buffer para enviar "usuario :: IP :: puerto".

    // Leer el nombre del usuario que hace la petición.
    if (readLine(fd_local, buffer, MSG_MAX_SIZE) < 0) {
        resultado = 2; // Error de lectura (cualquier otro error = 2).
        if (sendMessage(fd_local, (char *)&resultado, 1) < 0) {
            perror("Error enviando resultado");
        }
        printf("s> CONNECTEDUSERS FAIL\n");
        close(fd_local);
        pthread_exit(NULL);
    }

    log_rpc_operacion(buffer, "USERS", NULL);

    pthread_mutex_lock(mutex);

    // Buscar al usuario que hace la petición.
    usuario_actual = find_user(*head, buffer);

    if (usuario_actual == NULL) {
        // El usuario no está registrado.
        resultado = 2;
        pthread_mutex_unlock(mutex);
        if (sendMessage(fd_local, (char *)&resultado, 1) < 0) {
            perror("Error enviando resultado");
        }
        printf("s> CONNECTEDUSERS FAIL\n");
    } 
    else if (usuario_actual->estado == ESTADO_DESCONECTADO) {
        // El usuario existe pero no está conectado.
        resultado = 1;
        pthread_mutex_unlock(mutex);
        if (sendMessage(fd_local, (char *)&resultado, 1) < 0) {
            perror("Error enviando resultado");
        }
        printf("s> CONNECTEDUSERS FAIL\n");
    }
    else {
        // El usuario existe y está conectado (Éxito).
        resultado = 0;
        if (sendMessage(fd_local, (char *)&resultado, 1) < 0) {
            perror("Error enviando resultado");
        }

        curr = *head;
        while (curr != NULL) {
            if (curr->estado == ESTADO_CONECTADO) num_conectados++;
            curr = curr->next;
        }

        // Enviar el número de usuarios como cadena terminada en \0.
        sprintf(num_str, "%u", num_conectados);
        if (sendMessage(fd_local, num_str, strlen(num_str) + 1) < 0) {
            perror("Error enviando número de usuarios");
        }

        // Enviar los datos de cada usuario conectado.
        curr = *head;
        while (curr != NULL) {
            if (curr->estado == ESTADO_CONECTADO) {
                snprintf(user_info, sizeof(user_info), "%s :: %s :: %s", curr->userName, curr->ip, curr->puerto);
                if (sendMessage(fd_local, user_info, strlen(user_info) + 1) < 0) {
                    perror("Error enviando información de usuario");
                }
            }
            curr = curr->next;
        }
        pthread_mutex_unlock(mutex);
        
        // Imprimir traza en el servidor según especificación.
        printf("s> CONNECTEDUSERS OK\n");
    }
}


/*
 * procesar_envio_comun: Lógica común para SEND y SENDATTACH.
 * El parámetro 'tiene_adjunto' indica el tipo de mensaje:
 * - Si es 1: Se trata de un SENDATTACH (mensaje con fichero adjunto).
 * - Si es 0: Se trata de un SEND normal (solo texto).
 * Mantiene los comentarios originales del código.
 */
static void procesar_envio_comun(int fd_local, pthread_mutex_t *mutex, user_node_t **head, int tiene_adjunto)
{
    // Buffers de texto.
    char remitente[MSG_MAX_SIZE];
    char destinatario[MSG_MAX_SIZE];
    char texto[MSG_MAX_SIZE];
    char id_str[16];
    char fichero[MSG_MAX_SIZE]; // Para SENDATTACH

    unsigned char resultado;
    unsigned int msg_id; // Id del mensaje.

    // Usuarios remitente y destino.
    user_node_t *user_rem;
    user_node_t *user_dest;

    // 1. Leer remitente, destinatario, mensaje (y fichero si tiene adjunto).
    if (readLine(fd_local, remitente, MSG_MAX_SIZE) < 0 ||
        readLine(fd_local, destinatario, MSG_MAX_SIZE) < 0 ||
        readLine(fd_local, texto, MSG_MAX_SIZE) < 0 ||
        (tiene_adjunto && readLine(fd_local, fichero, MSG_MAX_SIZE) < 0)) { 
        
        resultado = 2; // Error genérico de lectura.
        if (sendMessage(fd_local, (char *)&resultado, 1) < 0) {
            perror("Error enviando error");
        }
        close(fd_local);
        pthread_exit(NULL);
    }

    // Llamada a log RPC dependiendo de si hay adjunto
    if (tiene_adjunto) {
        log_rpc_operacion(remitente, "SENDATTACH", fichero);
    } else {
        log_rpc_operacion(remitente, "SEND", NULL);
    }

    // Bloquear mutex para operar sobre la lista de usuarios.
    pthread_mutex_lock(mutex);

    // 2. Comprobar que existen ambos usuarios.
    user_rem = find_user(*head, remitente);
    user_dest = find_user(*head, destinatario);

    if (user_rem == NULL || user_dest == NULL) {
        resultado = 1; // Alguno de los dos usuarios no existe -> código 1.
        pthread_mutex_unlock(mutex);
        
        if (sendMessage(fd_local, (char *)&resultado, 1) < 0) {
            perror("Error enviando error");
        }
        printf("s> %s FAIL\n", tiene_adjunto ? "SENDATTACH" : "SEND"); 
    } 
    else {
        // 3. Asignar ID al mensaje.
        // La variable de tipo unsigned int vuelve a 0 si desborda, y el protocolo dice que el siguiente debe ser 1.
        user_rem->ultimo_id_msg++;
        if (user_rem->ultimo_id_msg == 0) {
            user_rem->ultimo_id_msg = 1; 
        }
        msg_id = user_rem->ultimo_id_msg;
        sprintf(id_str, "%u", msg_id);

        // 4. Almacenar el mensaje en memoria dinámica.
        mensaje_pendiente_t *nuevo_msg = malloc(sizeof(mensaje_pendiente_t));
        if (nuevo_msg == NULL) {
            resultado = 2;
            pthread_mutex_unlock(mutex);
            if (sendMessage(fd_local, (char *)&resultado, 1) < 0) perror("Error enviando error");
            printf("s> %s FAIL\n", tiene_adjunto ? "SENDATTACH" : "SEND");
            close(fd_local);
            pthread_exit(NULL);
        }
        nuevo_msg->id = msg_id;

        // Forzar terminador nulo al final de las cadenas.
        strncpy(nuevo_msg->remitente, remitente, MSG_MAX_SIZE - 1);
        nuevo_msg->remitente[MSG_MAX_SIZE - 1] = '\0';
        strncpy(nuevo_msg->texto, texto, MSG_MAX_SIZE - 1);
        nuevo_msg->texto[MSG_MAX_SIZE - 1] = '\0';
        
        if (tiene_adjunto) {
            strncpy(nuevo_msg->fichero, fichero, MSG_MAX_SIZE - 1);
            nuevo_msg->fichero[MSG_MAX_SIZE - 1] = '\0';
            nuevo_msg->tiene_adjunto = 1;
        } else {
            nuevo_msg->fichero[0] = '\0';
            nuevo_msg->tiene_adjunto = 0;
        }
        nuevo_msg->next = NULL;

        // Añadir el mensaje al final de la cola del destinatario.
        if (user_dest->mensajes == NULL) {
            user_dest->mensajes = nuevo_msg;
        } else {
            mensaje_pendiente_t *aux = user_dest->mensajes;
            // Recorrer hasta el final si no está vacía.
            while (aux->next != NULL) {
                aux = aux->next;
            }
            aux->next = nuevo_msg;
        }

        // 5. Responder al remitente con éxito.
        resultado = 0;
        if (sendMessage(fd_local, (char *)&resultado, 1) < 0) perror("Error enviando resultado");
        if (sendMessage(fd_local, id_str, strlen(id_str) + 1) < 0) perror("Error enviando id");

        // 6. Gestionar el envío asíncrono.
        if (user_dest->estado == ESTADO_CONECTADO) {
            // Copiar IPs y puertos en variables locales para poder hacer el unlock del mutex.
            char ip_dest[INET_ADDRSTRLEN], puerto_dest[16];
            strncpy(ip_dest, user_dest->ip, INET_ADDRSTRLEN - 1); 
            ip_dest[INET_ADDRSTRLEN - 1] = '\0';
            strncpy(puerto_dest, user_dest->puerto, 15); 
            puerto_dest[15] = '\0';
            
            // Comprobar si el remitente está conectado para enviarle un ACK luego.
            int rem_conectado = (user_rem->estado == ESTADO_CONECTADO);
            char ip_rem[INET_ADDRSTRLEN], puerto_rem[16];
            if (rem_conectado) {
                // Copiar IP y puerto del remitente de forma segura.
                strncpy(ip_rem, user_rem->ip, INET_ADDRSTRLEN - 1); 
                ip_rem[INET_ADDRSTRLEN - 1] = '\0';
                strncpy(puerto_rem, user_rem->puerto, 15); 
                puerto_rem[15] = '\0';
            }

            pthread_mutex_unlock(mutex);

            // Aquí el servidor actuará como cliente creando un socket hacia el destino.
            int fd_dest = socket(AF_INET, SOCK_STREAM, 0);
            int entrega_ok = 0;

            if (fd_dest >= 0) {
                // Inicializar y rellenar los datos de red del destino.
                struct sockaddr_in addr_dest;
                memset(&addr_dest, 0, sizeof(addr_dest));
                addr_dest.sin_family = AF_INET;
                addr_dest.sin_port = htons(atoi(puerto_dest));
                addr_dest.sin_addr.s_addr = inet_addr(ip_dest);
                
                // Intentar establecer conexión con el hilo de escucha del destinatario.
                if (connect(fd_dest, (struct sockaddr *)&addr_dest, sizeof(addr_dest)) == 0) {
                    entrega_ok = 1;
                    if (tiene_adjunto) {
                        // Enviar campos estructurados para un mensaje con fichero adjunto.
                        if (sendMessage(fd_dest, "SEND MESSAGE ATTACH", strlen("SEND MESSAGE ATTACH") + 1) < 0) entrega_ok = 0;
                        else if (sendMessage(fd_dest, remitente, strlen(remitente) + 1) < 0) entrega_ok = 0;
                        else if (sendMessage(fd_dest, id_str, strlen(id_str) + 1) < 0) entrega_ok = 0;
                        else if (sendMessage(fd_dest, texto, strlen(texto) + 1) < 0) entrega_ok = 0;
                        else if (sendMessage(fd_dest, fichero, strlen(fichero) + 1) < 0) entrega_ok = 0;
                    } else {
                        // Enviar campos estructurados para un mensaje de texto normal.
                        if (sendMessage(fd_dest, "SEND MESSAGE", strlen("SEND MESSAGE") + 1) < 0) entrega_ok = 0;
                        else if (sendMessage(fd_dest, remitente, strlen(remitente) + 1) < 0) entrega_ok = 0;
                        else if (sendMessage(fd_dest, id_str, strlen(id_str) + 1) < 0) entrega_ok = 0;
                        else if (sendMessage(fd_dest, texto, strlen(texto) + 1) < 0) entrega_ok = 0;
                    }
                }
                // Cerrar la conexión con el destinatario tras intentar el envío.
                close(fd_dest);
            }

            // Proceder con el ACK y el borrado solo si la entrega fue exitosa.
            if (entrega_ok) {
                // Enviar notificación (ACK) al remitente si se encuentra conectado.
                if (rem_conectado) {
                    enviar_ack_remitente(ip_rem, puerto_rem, id_str, tiene_adjunto ? fichero : NULL, tiene_adjunto);
                }

                // Eliminar el mensaje de la cola del destinatario.
                eliminar_mensaje_cola(destinatario, msg_id, remitente, tiene_adjunto ? fichero : NULL, tiene_adjunto, mutex, head);

                // Imprimir éxito.
                printf("s> SEND MESSAGE %u FROM %s TO %s\n", msg_id, remitente, destinatario);
            } else {
                // Bloquear mutex para modificar el estado del destinatario.
                pthread_mutex_lock(mutex);
                user_node_t *check_dest = find_user(*head, destinatario);
                if (check_dest != NULL) {
                    check_dest->estado = ESTADO_DESCONECTADO;
                    memset(check_dest->ip, 0, sizeof(check_dest->ip));
                    memset(check_dest->puerto, 0, sizeof(check_dest->puerto));
                }
                pthread_mutex_unlock(mutex);
                
                // El mensaje se mantiene STORED porque no fue eliminado de la cola.
                printf("s> MESSAGE %u FROM %s TO %s STORED\n", msg_id, remitente, destinatario);
            }
        } 
        else {
            // El destinatario ya estaba desconectado.
            pthread_mutex_unlock(mutex);
            printf("s> MESSAGE %u FROM %s TO %s STORED\n", msg_id, remitente, destinatario);
        }
    }
}

/*
 * procesar_send: Gestión de la operación SEND.
 * Llama a la función común indicando que no hay adjunto.
 */
void procesar_send(int fd_local, pthread_mutex_t *mutex, user_node_t **head)
{
    procesar_envio_comun(fd_local, mutex, head, 0);
}

/*
 * procesar_sendattach: Gestión de la operación SENDATTACH.
 * Llama a la función común indicando que sí hay adjunto.
 */
void procesar_sendattach(int fd_local, pthread_mutex_t *mutex, user_node_t **head)
{
    procesar_envio_comun(fd_local, mutex, head, 1);
}
