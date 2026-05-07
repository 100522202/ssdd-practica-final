/* Tamaños máximos de las cadenas enviadas al servidor RPC. */
const MAX_USER_LEN = 256;
const MAX_OP_LEN = 32;
const MAX_FILE_LEN = 256;

/* Estructura que se enviará desde server.c al servidor RPC. */
struct log_request {
    /* Nombre del usuario que realiza la operación. */
    string usuario<MAX_USER_LEN>;

    /* Nombre de la operación realizada: REGISTER, CONNECT, SEND, etc. */
    string operacion<MAX_OP_LEN>;

    /* Nombre del fichero, solo usado en SENDATTACH. */
    string fichero<MAX_FILE_LEN>;

    /* Indica si la operación lleva fichero asociado. */
    int tiene_fichero;
};

/* Programa RPC que registra las operaciones recibidas por el servidor principal. */
program LOG_RPC_PROG {
    version LOG_RPC_VERS {
        /* Función remota que recibe una operación y devuelve 0 si todo va bien. */
        int REGISTRAR_OPERACION(log_request) = 1;
    } = 1;
} = 99;