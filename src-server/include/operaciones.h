#ifndef OPERACIONES_H
#define OPERACIONES_H

#include "linked_list.h"
#include <pthread.h>

// =====================================================================
// Funciones auxiliares compartidas entre operaciones.
// =====================================================================

/*
 * enviar_ack_remitente: Envía un ACK al remitente de un mensaje.
 * Si tiene_adjunto es 1, envía "SEND MESS ATTACH ACK" + id + fichero.
 * Si tiene_adjunto es 0, envía "SEND MESS ACK" + id.
 */
void enviar_ack_remitente(const char *ip_rem, const char *puerto_rem,
                          const char *id_str, const char *fichero,
                          int tiene_adjunto);

/*
 * eliminar_mensaje_cola: Busca y elimina un mensaje de la cola de pendientes
 * del destinatario.
 * NOTA: esta función BLOQUEA y LIBERA el mutex internamente.
 */
void eliminar_mensaje_cola(const char *destinatario, unsigned int msg_id,
                           const char *remitente, const char *fichero,
                           int tiene_adjunto,
                           pthread_mutex_t *mutex, user_node_t **head);

/*
 * entregar_pendientes: Entrega la cola de mensajes desacoplada a un usuario
 * recién conectado. Se usa durante CONNECT.
 */
void entregar_pendientes(mensaje_pendiente_t *mensajes_a_enviar,
                         const char *dest_name,
                         const char *dest_ip,
                         const char *dest_puerto,
                         pthread_mutex_t *mutex, user_node_t **head);


// =====================================================================
// Funciones para cada operación del protocolo.
// =====================================================================

/* Gestión de la operación REGISTER. */
void procesar_register(int fd_local, pthread_mutex_t *mutex, user_node_t **head);

/* Gestión de la operación UNREGISTER. */
void procesar_unregister(int fd_local, pthread_mutex_t *mutex, user_node_t **head);

/* Gestión de la operación CONNECT. */
void procesar_connect(int fd_local, pthread_mutex_t *mutex, user_node_t **head);

/* Gestión de la operación DISCONNECT. */
void procesar_disconnect(int fd_local, pthread_mutex_t *mutex, user_node_t **head);

/* Gestión de la operación USERS. */
void procesar_users(int fd_local, pthread_mutex_t *mutex, user_node_t **head);

/* Gestión de la operación SEND. */
void procesar_send(int fd_local, pthread_mutex_t *mutex, user_node_t **head);

/* Gestión de la operación SENDATTACH. */
void procesar_sendattach(int fd_local, pthread_mutex_t *mutex, user_node_t **head);

#endif
