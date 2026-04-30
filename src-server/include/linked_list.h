#ifndef LINKED_LIST_H
#define LINKED_LIST_H

#include <netinet/in.h> // Para tamaño estándar de IP (INET_ADDRSTRLEN)
#define MAX_STR_LEN 256 // Tamaño máximo de cadenas y mensajes establecido por el enunciado
#define ESTADO_DESCONECTADO 0 // Evitar números mágicos
#define ESTADO_CONECTADO 1

// ---- SE SUPONE QUE VALE TAMBIÉN PARA PARTE 2 SI DESCOMENTAMOS 4 COSAS


/* PARTE 2
/* Estructura para los mensajes pendientes de entrega.
 * Se enlazan para formar una cola por cada usuario.
 
typedef struct pending_message {
    unsigned int id;              // Identificador numérico del mensaje
    char remitente[MAX_STR_LEN];  // Nombre del usuario que envía el mensaje
    char texto[MAX_STR_LEN];      // Contenido (máx 255 + '\0')
    // char fichero[MAX_STR_LEN]; // Descomentar en la Parte 2 para SENDATTACH
    
    struct pending_message *next; // Puntero al siguiente mensaje pendiente
} mensaje_pendiente_t;
*/



/* Estructura principal para la lista de usuarios registrados.
 */
typedef struct user_node {
    char userName[MAX_STR_LEN];   // Alias del usuario (identificador único)
    int estado;                   // 0 = Desconectado, 1 = Conectado
    char ip[INET_ADDRSTRLEN];     // Dirección IP de escucha del cliente
    char puerto[16];              // Puerto de escucha del cliente (guardado como cadena)
    
    // El servidor asocia a cada mensaje enviado por este usuario un identificador.
    // Cuando el usuario se registra por primera vez, se pone a 0.
    unsigned int ultimo_id_msg;   
    
    // PARTE 2: Lista de mensajes almacenados esperando a que este usuario se conecte
    // mensaje_pendiente_t *mensajes; 

    struct user_node *next;       // Puntero al siguiente usuario en el sistema
} user_node_t;


/* --- Prototipos de funciones para gestionar la lista --- */

user_node_t* add_user(user_node_t **head, const char *userName); // const char porque solo leeremos, no modificaremos userName
int remove_user(user_node_t **head, const char *userName);
user_node_t* find_user(user_node_t *head, const char *userName);

// void add_pending_message(user_node_t *dest_user, const char *sender, unsigned int id, const char *text);
// void free_pending_messages(mensaje_pendiente_t *head);


#endif