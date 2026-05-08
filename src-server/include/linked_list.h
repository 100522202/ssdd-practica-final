#ifndef LINKED_LIST_H
#define LINKED_LIST_H

#include <netinet/in.h> // Para tamaño estándar de IP (INET_ADDRSTRLEN).
#define MAX_STR_LEN 256 // Tamaño máximo de cadenas y mensajes establecido por el enunciado.
#define ESTADO_DESCONECTADO 0 // Evitar números mágicos.
#define ESTADO_CONECTADO 1

// ---- SE SUPONE QUE VALE TAMBIÉN PARA PARTE 2 SI DESCOMENTAMOS 4 COSAS.


/* PARTE 2.
 * Estructura para los mensajes pendientes de entrega.
 * Se enlazan para formar una cola por cada usuario.
 */
typedef struct pending_message {
    unsigned int id;              // Identificador numérico del mensaje.
    char remitente[MAX_STR_LEN];  // Nombre del usuario que envía el mensaje.
    char texto[MAX_STR_LEN];      // Contenido (máx 255 + '\0').
    char fichero[MAX_STR_LEN];    // Nombre/ruta del fichero asociado, si lo hay.
    int tiene_adjunto;            // 0 = mensaje normal, 1 = mensaje con adjunto.
    
    struct pending_message *next; // Puntero al siguiente mensaje pendiente.
} mensaje_pendiente_t;




/* Estructura principal para la lista de usuarios registrados.
 */
typedef struct user_node {
    char userName[MAX_STR_LEN];   // Alias del usuario (identificador único).
    int estado;                   // 0 = Desconectado, 1 = Conectado.
    char ip[INET_ADDRSTRLEN];     // Dirección IP de escucha del cliente.
    char puerto[16];              // Puerto de escucha del cliente (guardado como cadena).
    
    // El servidor asocia a cada mensaje enviado por este usuario un identificador.
    // Cuando el usuario se registra por primera vez, se pone a 0.
    unsigned int ultimo_id_msg;   
    
    // PARTE 2: Lista de mensajes almacenados esperando a que este usuario se conecte.
    mensaje_pendiente_t *mensajes; 

    struct user_node *next;       // Puntero al siguiente usuario en el sistema.
} user_node_t;


/* --- Prototipos de funciones para gestionar la lista ---. */

/**
 * @brief Añade un nuevo usuario a la lista de usuarios registrados.
 * 
 * Reserva memoria para un nuevo nodo, inicializa sus campos (estado desconectado,
 * sin mensajes pendientes, contador de mensajes a cero) e inserta el nodo
 * al principio de la lista.
 * 
 * @param head Puntero al puntero del primer elemento de la lista.
 * @param userName Nombre del usuario a registrar. Const char porque solo leeremos, no modificaremos userName.
 * @return user_node_t* Puntero al nuevo nodo creado, o NULL en caso de error de memoria.
 */
user_node_t* add_user(user_node_t **head, const char *userName);

/**
 * @brief Elimina un usuario de la lista y libera su memoria.
 * 
 * Busca al usuario por su nombre. Si lo encuentra, desvincula el nodo de la lista,
 * libera todos sus mensajes pendientes almacenados y finalmente libera la memoria del nodo.
 * 
 * @param head Puntero al puntero del primer elemento de la lista.
 * @param userName Nombre del usuario a eliminar.
 * @return int 0 si se eliminó correctamente, -1 si el usuario no existe.
 */
int remove_user(user_node_t **head, const char *userName);

/**
 * @brief Busca un usuario en la lista por su nombre.
 * 
 * @param head Puntero al primer elemento de la lista.
 * @param userName Nombre del usuario a buscar.
 * @return user_node_t* Puntero al nodo del usuario si se encuentra, NULL en caso contrario.
 */
user_node_t* find_user(user_node_t *head, const char *userName);

/**
 * @brief Libera la memoria de toda la lista de usuarios.
 * 
 * Recorre la lista completa liberando para cada usuario tanto sus mensajes
 * pendientes como el propio nodo del usuario.
 * 
 * @param head Puntero al primer elemento de la lista.
 */
void free_user_list(user_node_t *head);

#endif
