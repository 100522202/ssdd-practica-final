# ssdd-pfinal

Proyecto de Sistemas Distribuidos con:
- Servidor principal en C (`server`)
- Servidor RPC en C (`log_rpc_server`)
- Servicio web SOAP en Python (`servicio_web.py`)
- Cliente en Python (`client.py`)

## 1) Requisitos del sistema (Linux)

Instala dependencias de compilación, RPC/TIRPC y utilidades:

```bash
sudo apt update
sudo apt install -y build-essential make gcc rpcsvc-proto libtirpc-dev python3 python3-venv python3-pip zip unzip
```

## 2) Entorno Python

Desde la raíz del proyecto:

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install --upgrade pip
pip install -r requirements.txt
```

## 3) Compilación (desde la raíz)

```bash
make
```

Esto genera en la raíz:
- `./server`
- `./log_rpc_server`

Limpieza:

```bash
make clean
```

## 4) Ejecución (orden recomendado)

Abre 4 terminales en la raíz del proyecto.

### Terminal 1: servicio web SOAP

```bash
source .venv/bin/activate
python3 servicio_web.py -p 8000
```

### Terminal 2: servidor RPC

```bash
./log_rpc_server
```

### Terminal 3: servidor principal

```bash
export LOG_RPC_IP=127.0.0.1
./server -p 12345
```

### Terminal 4 (y más): clientes

```bash
source .venv/bin/activate
python3 client.py -s 127.0.0.1 -p 12345 --wsdl-url http://127.0.0.1:8000/?wsdl
```

Para abrir otro cliente, repite el mismo comando en otra terminal.

## 5) Comandos del cliente

```text
REGISTER <userName>
UNREGISTER <userName>
CONNECT <userName>
DISCONNECT <userName>
USERS
SEND <userName> <message>
SENDATTACH <userName> <message> <fileName>
GETFILE <userName> <fileName> <localFileName>
QUIT
```

## 6) Prueba rápida mínima

1. En cliente A:
   - `REGISTER ana`
   - `CONNECT ana`
2. En cliente B:
   - `REGISTER bob`
   - `CONNECT bob`
3. En cliente A:
   - `SEND bob hola    mundo`
4. En cliente B verás el mensaje normalizado por el servicio web.

## 7) Empaquetado zip de entrega (ejemplo)

```bash
zip -r ssdd_proyecto_A_B.zip .
```

