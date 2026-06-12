# ETAPA 1: Usamos una máquina potente para compilar el código fuente en C++
FROM ubuntu:22.04 AS builder
ENV DEBIAN_FRONTEND=noninteractive

# Instalar las herramientas de desarrollo esenciales para el protocolo
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    boost-all-dev \
    libssl-dev \
    libgmp-dev \
    wget \
    curl

# Establecer el directorio de trabajo y copiar los archivos de tu repositorio
WORKDIR /workspace
COPY . .

# Crear la carpeta de compilación y compilar el binario de Lumex
RUN mkdir build && cd build && \
    cmake -DPLATFORM=linux -DCMAKE_BUILD_TYPE=Release .. && \
    make lumex_node -j$(nproc)

# ETAPA 2: Crear la imagen final de distribución (Súper ligera para Oracle)
FROM ubuntu:22.04
RUN apt-get update && apt-get install -y \
    libboost-program-options-dev \
    libboost-filesystem-dev \
    libboost-thread-dev \
    libssl-dev \
    && rm -rf /var/lib/apt/lists/*

# Copiar únicamente el nodo ya compilado para que no pese gigabytes
COPY --from=builder /workspace/build/lumex_node /usr/local/bin/lumex_node

# Configurar el espacio de datos persistente para el Ledger
WORKDIR /root/LumexData
VOLUME /root/LumexData

# Puertos por defecto de la red (Ajustar si tu fork usa otros)
EXPOSE 7075/udp 7075/tcp 7076/tcp

CMD ["lumex_node", "--daemon"]
