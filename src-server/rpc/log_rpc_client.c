#include "log_rpc_client.h"
#include "log_rpc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


int log_rpc_operacion(const char *usuario, const char *operacion, const char *fichero)
{
    // Se obtiene la IP del servidor RPC desde la variable de entorno.
    char *host = getenv("LOG_RPC_IP");

    // Si no existe LOG_RPC_IP, no se puede contactar con el servidor RPC.
    if (host == NULL || strlen(host) == 0) {
        fprintf(stderr, "LOG_RPC_IP no definida\n");
        return -1;
    }

    // Se comprueba que los datos obligatorios sean válidos.
    if (usuario == NULL || operacion == NULL) {
        fprintf(stderr, "Datos RPC invalidos\n");
        return -1;
    }

    // Se crea el cliente RPC contra el servidor indicado.
    CLIENT *clnt = clnt_create(host, LOG_RPC_PROG, LOG_RPC_VERS, "udp");

    // Si no se puede crear el cliente, se muestra el error.
    if (clnt == NULL) {
        clnt_pcreateerror(host);
        return -1;
    }

    // Se prepara la estructura que se enviará al servidor RPC.
    log_request req;
    memset(&req, 0, sizeof(req));

    // Se asigna el usuario que ha realizado la operación.
    req.usuario = (char *) usuario;

    // Se asigna el nombre de la operación realizada.
    req.operacion = (char *) operacion;

    // Se comprueba si la operación lleva fichero asociado.
    if (fichero != NULL && strlen(fichero) > 0) {
        req.fichero = (char *) fichero;
        req.tiene_fichero = 1;
    } else {
        req.fichero = "";
        req.tiene_fichero = 0;
    }

    // Variable donde se guardará el resultado devuelto por el servidor RPC.
    int resultado_rpc = 0;

    // Se llama a la función remota REGISTRAR_OPERACION.
    enum clnt_stat estado = registrar_operacion_1(req, &resultado_rpc, clnt);

    // Si falla la llamada RPC, se muestra el error.
    if (estado != RPC_SUCCESS) {
        clnt_perror(clnt, "call failed");
        clnt_destroy(clnt);
        return -1;
    }

    // Se destruye el cliente RPC porque la llamada ya ha terminado.
    clnt_destroy(clnt);

    // Se devuelve el resultado lógico del servidor RPC.
    return resultado_rpc;
}
