#ifndef OPERACIONES_H
#define OPERACIONES_H

#include "linked_list.h"
#include <pthread.h>

// =====================================================================
// Funciones auxiliares compartidas entre operaciones.
// =====================================================================

/**
 * @brief Envía una notificación de confirmación (ACK) al remitente de un mensaje.
 * 
 * Dependiendo de si el mensaje original tenía un fichero adjunto o no, envía
 * la cadena de operación correspondiente seguida del ID del mensaje y, si aplica,
 * el nombre del fichero.
 * 
 * @param ip_rem Dirección IP del remitente.
 * @param puerto_rem Puerto de escucha del remitente.
 * @param id_str Identificador del mensaje en formato cadena.
 * @param fichero Nombre del fichero adjunto (puede ser NULL si no hay adjunto).
 * @param tiene_adjunto Booleano (0 o 1) que indica si el mensaje tiene adjunto.
 */
void enviar_ack_remitente(const char *ip_rem, const char *puerto_rem,
                          const char *id_str, const char *fichero,
                          int tiene_adjunto);

/**
 * @brief Busca y elimina un mensaje específico de la cola de pendientes de un usuario.
 * 
 * Se utiliza tras una entrega exitosa para limpiar la cola de mensajes almacenados.
 * Esta función gestiona internamente el bloqueo del mutex para garantizar la exclusión mutua.
 * 
 * @param destinatario Nombre del usuario cuya cola se va a modificar.
 * @param msg_id Identificador numérico del mensaje a eliminar.
 * @param remitente Nombre del remitente (para verificación adicional).
 * @param fichero Nombre del fichero (para verificación adicional).
 * @param tiene_adjunto Indica si el mensaje es normal (0), con adjunto (1) o sin comprobar (-1).
 * @param mutex Puntero al mutex que protege la lista de usuarios.
 * @param head Puntero a la cabeza de la lista de usuarios.
 */
void eliminar_mensaje_cola(const char *destinatario, unsigned int msg_id,
                           const char *remitente, const char *fichero,
                           int tiene_adjunto,
                           pthread_mutex_t *mutex, user_node_t **head);

/**
 * @brief Intenta entregar todos los mensajes acumulados en una lista a un destinatario.
 * 
 * Se llama durante el proceso de conexión (CONNECT) para vaciar los mensajes que llegaron
 * mientras el usuario estaba fuera de línea. Si un envío falla, el usuario se marca como
 * desconectado y los mensajes restantes se devuelven a su cola.
 * 
 * @param mensajes_a_enviar Puntero a la lista de mensajes extraída de la estructura del usuario.
 * @param dest_name Nombre del usuario destinatario.
 * @param dest_ip IP del destinatario.
 * @param dest_puerto Puerto del destinatario.
 * @param mutex Puntero al mutex que protege la lista de usuarios.
 * @param head Puntero a la cabeza de la lista de usuarios.
 */
void entregar_pendientes(mensaje_pendiente_t *mensajes_a_enviar,
                         const char *dest_name,
                         const char *dest_ip,
                         const char *dest_puerto,
                         pthread_mutex_t *mutex, user_node_t **head);


// =====================================================================
// Funciones para cada operación del protocolo.
// =====================================================================

/**
 * @brief Procesa una petición de registro (REGISTER).
 * 
 * Lee el nombre de usuario del socket, comprueba si ya existe y, si no, lo añade a la lista.
 * Responde al cliente con 0 (éxito) o 1 (usuario ya registrado).
 * 
 * @param fd_local Descriptor del socket con la conexión del cliente.
 * @param mutex Puntero al mutex de la lista de usuarios.
 * @param head Puntero a la cabeza de la lista de usuarios.
 */
void procesar_register(int fd_local, pthread_mutex_t *mutex, user_node_t **head);

/**
 * @brief Procesa una petición de baja (UNREGISTER).
 * 
 * Lee el nombre de usuario y lo elimina de la lista de registrados si existe.
 * Responde al cliente con 0 (éxito) o 1 (usuario no existe).
 * 
 * @param fd_local Descriptor del socket con la conexión del cliente.
 * @param mutex Puntero al mutex de la lista de usuarios.
 * @param head Puntero a la cabeza de la lista de usuarios.
 */
void procesar_unregister(int fd_local, pthread_mutex_t *mutex, user_node_t **head);

/**
 * @brief Procesa una petición de conexión (CONNECT).
 * 
 * Actualiza la IP y puerto del usuario, lo marca como conectado y procede a enviarle
 * los mensajes que tenga pendientes de entrega.
 * Responde al cliente con 0 (éxito), 1 (no existe), 2 (ya conectado) o 3 (error).
 * 
 * @param fd_local Descriptor del socket con la conexión del cliente.
 * @param mutex Puntero al mutex de la lista de usuarios.
 * @param head Puntero a la cabeza de la lista de usuarios.
 */
void procesar_connect(int fd_local, pthread_mutex_t *mutex, user_node_t **head);

/**
 * @brief Procesa una petición de desconexión (DISCONNECT).
 * 
 * Marca al usuario como desconectado y limpia sus datos de red. Verifica que la
 * desconexión se solicite desde la misma IP que realizó la conexión.
 * Responde con 0 (éxito), 1 (no existe), 2 (no conectado) o 3 (error/IP distinta).
 * 
 * @param fd_local Descriptor del socket con la conexión del cliente.
 * @param mutex Puntero al mutex de la lista de usuarios.
 * @param head Puntero a la cabeza de la lista de usuarios.
 */
void procesar_disconnect(int fd_local, pthread_mutex_t *mutex, user_node_t **head);

/**
 * @brief Procesa una petición de lista de usuarios (USERS).
 * 
 * Envía al cliente el número de usuarios actualmente conectados y sus datos (nombre, IP, puerto).
 * Responde con un código de resultado seguido de la información solicitada.
 * 
 * @param fd_local Descriptor del socket con la conexión del cliente.
 * @param mutex Puntero al mutex de la lista de usuarios.
 * @param head Puntero a la cabeza de la lista de usuarios.
 */
void procesar_users(int fd_local, pthread_mutex_t *mutex, user_node_t **head);

/**
 * @brief Procesa una petición de envío de mensaje de texto (SEND).
 * 
 * Almacena el mensaje en la cola del destinatario e intenta entregarlo de forma inmediata
 * si el destinatario está conectado.
 * Responde al remitente con 0 (éxito), 1 (usuario no existe) o 2 (error).
 * 
 * @param fd_local Descriptor del socket con la conexión del cliente.
 * @param mutex Puntero al mutex de la lista de usuarios.
 * @param head Puntero a la cabeza de la lista de usuarios.
 */
void procesar_send(int fd_local, pthread_mutex_t *mutex, user_node_t **head);

/**
 * @brief Procesa una petición de envío de mensaje con adjunto (SENDATTACH).
 * 
 * Similar a SEND, pero maneja un campo adicional para el nombre del fichero adjunto.
 * 
 * @param fd_local Descriptor del socket con la conexión del cliente.
 * @param mutex Puntero al mutex de la lista de usuarios.
 * @param head Puntero a la cabeza de la lista de usuarios.
 */
void procesar_sendattach(int fd_local, pthread_mutex_t *mutex, user_node_t **head);

#endif
