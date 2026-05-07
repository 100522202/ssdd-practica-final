#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "linked_list.h"

user_node_t* add_user(user_node_t **head, const char *userName) {
    
    // Reservar memoria para el nuevo nodo (usuario).
    user_node_t *nuevo_usuario = (user_node_t*)malloc(sizeof(user_node_t));
    
    if (nuevo_usuario == NULL) {
        perror("Error al asignar memoria para nuevo usuario");
        return NULL; // Fallo de malloc.
    }

    // Inicializar los campos según las reglas de registro.
    // Usamos strncpy por seguridad, asegurando el límite de 255 + '\0'.
    strncpy(nuevo_usuario->userName, userName, MAX_STR_LEN - 1);
    nuevo_usuario->userName[MAX_STR_LEN - 1] = '\0'; 
    
    nuevo_usuario->estado = ESTADO_DESCONECTADO; // O directamente 0.
    nuevo_usuario->ultimo_id_msg = 0; // Obligatorio según el enunciado al registrar.
    nuevo_usuario->mensajes = NULL;   // Aún no tiene mensajes pendientes.
    
    // Dejamos IP y puerto vacíos (se rellenarán en CONNECT).
    memset(nuevo_usuario->ip, 0, INET_ADDRSTRLEN);
    memset(nuevo_usuario->puerto, 0, 16);

    // Insertar por head (es más rápido que recorrer toda la lista).
    // El 'next' del nuevo nodo apunta a lo que actualmente sea la cabecera.
    nuevo_usuario->next = *head; 
    
    // Modificar el puntero global de server.c para que el nuevo nodo sea la nueva cabecera.
    *head = nuevo_usuario; 

    return nuevo_usuario;
}

int remove_user(user_node_t **head, const char *userName) {
    // Si la lista está vacía, no hay nada que borrar.
    if (*head == NULL) {
        return -1; // Error: Usuario no existe (o lista vacía).
    }

    user_node_t *current = *head;
    user_node_t *previous = NULL;

    // Buscar el nodo que queremos borrar.
    while (current != NULL && strcmp(current->userName, userName) != 0) {
        previous = current;
        current = current->next;
    }

    // Si current es NULL, llegamos al final y no lo encontramos.
    if (current == NULL) {
        return -1; // Error: Usuario no existe.
    }

    // Encontramos el nodo. Hay que desvincularlo de la lista.
    
    if (previous == NULL) {
        // Caso 1: El nodo a borrar es el head.
        // La nueva cabecera será el segundo elemento.
        *head = current->next; 
    } else {
        // Caso 2: El nodo está en el medio o al final.
        // El anterior debe "saltarse" a current y apuntar al siguiente de current.
        previous->next = current->next;
    }

    // Limpiar recursos pendientes.
    // Cuando un usuario se da de baja, se borrarán todos los mensajes que no se le han entregado.
    mensaje_pendiente_t *msg_actual = current->mensajes;
    while (msg_actual != NULL) {
        mensaje_pendiente_t *temp = msg_actual;
        msg_actual = msg_actual->next;
        free(temp); // Liberamos cada mensaje pendiente.
    }

    // Liberar la memoria del nodo del usuario.
    free(current);

    return 0; // Éxito.
}

user_node_t* find_user(user_node_t *head, const char *userName){

    // Puntero auxiliar para recorrer la lista.
    user_node_t *current = head;

    // Recorrer la lista hasta el final.
    while (current != NULL) {
        
        // Comparar las cadenas: 0 si iguales.
        if (strcmp(current->userName, userName) == 0) {
            return current;
        }
        
        // Ir al siguiente.
        current = current->next;
    }

    // Si llegamos aquí, no existe el usuario, devolvemos puntero a NULL.
    return NULL;

}

void free_user_list(user_node_t *head) {
    user_node_t *current = head;

    while (current != NULL) {
        user_node_t *next_user = current->next;

        // Liberar todos los mensajes pendientes asociados a este usuario.
        mensaje_pendiente_t *msg_actual = current->mensajes;
        while (msg_actual != NULL) {
            mensaje_pendiente_t *next_msg = msg_actual->next;
            free(msg_actual);
            msg_actual = next_msg;
        }

        // Liberar el nodo de usuario.
        free(current);
        current = next_user;
    }
}
