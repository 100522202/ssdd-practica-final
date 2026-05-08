#ifndef LOG_RPC_CLIENT_H
#define LOG_RPC_CLIENT_H

/**
 * @brief Registra una operación en el servidor RPC de log.
 * 
 * Envía los detalles de la operación realizada por un usuario al servidor de log
 * para que quede constancia.
 * 
 * @param usuario Nombre del usuario que realiza la acción.
 * @param operacion Nombre de la operación (REGISTER, CONNECT, etc.).
 * @param fichero Nombre del fichero si la operación es SENDATTACH, NULL en caso contrario.
 * @return int 0 si el registro fue exitoso, -1 si hubo algún error.
 */
int log_rpc_operacion(const char *usuario, const char *operacion, const char *fichero);

#endif