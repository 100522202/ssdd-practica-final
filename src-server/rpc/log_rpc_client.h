#ifndef LOG_RPC_CLIENT_H
#define LOG_RPC_CLIENT_H

// Registra una operación en el servidor RPC.
// Si fichero es NULL o "", se registra una operación normal.
int log_rpc_operacion(const char *usuario, const char *operacion, const char *fichero);

#endif