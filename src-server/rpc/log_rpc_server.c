/*
 * Implementación del servidor RPC de registro de operaciones.
 * Este archivo completa la plantilla generada por rpcgen.
 */

#include "log_rpc.h"
#include <stdio.h>
#include <string.h>


bool_t
registrar_operacion_1_svc(log_request arg1, int *result, struct svc_req *rqstp)
{
    // No se usa la información interna de la petición RPC.
    (void) rqstp;

    // Se comprueba que el puntero de resultado sea válido.
    if (result == NULL) {
        return FALSE;
    }

    // Se comprueba que los campos obligatorios hayan llegado correctamente.
    if (arg1.usuario == NULL || arg1.operacion == NULL) {
        printf("RPC LOG ERROR\n");
        fflush(stdout);

        *result = 1;
        return TRUE;
    }

    // Si la operación tiene fichero, se imprime también el nombre del fichero.
    if (arg1.tiene_fichero &&
        arg1.fichero != NULL &&
        strlen(arg1.fichero) > 0) {

        printf("%s %s %s\n", arg1.usuario, arg1.operacion, arg1.fichero);

    } else {
        // Si no hay fichero, se imprime solo usuario y operación.
        printf("%s %s\n", arg1.usuario, arg1.operacion);
    }

    // Se fuerza la salida por pantalla para ver el log inmediatamente.
    fflush(stdout);

    // Se devuelve 0 para indicar que el registro se ha hecho correctamente.
    *result = 0;

    // TRUE indica que la llamada RPC se ha procesado sin fallo interno.
    return TRUE;
}


int
log_rpc_prog_1_freeresult(SVCXPRT *transp, xdrproc_t xdr_result, caddr_t result)
{
    // No se usa directamente el transporte RPC.
    (void) transp;

    // Se libera memoria asociada al resultado, si XDR hubiera reservado algo.
    xdr_free(xdr_result, result);

    // Se indica que la liberación ha terminado correctamente.
    return 1;
}
