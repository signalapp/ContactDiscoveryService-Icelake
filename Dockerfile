FROM eclipse-temurin:17-jre-focal@sha256:2a745ae31642c48aaf024ac52054ee5a534ab695660c1e7ca6f3306893bb07c1

RUN apt update && apt install -y gnupg2 wget gpg software-properties-common && \
    echo "deb [arch=amd64] https://packages.microsoft.com/ubuntu/20.04/prod focal main" | tee /etc/apt/sources.list.d/msprod.list && \
    wget -qO - https://packages.microsoft.com/keys/microsoft.asc | apt-key add - && \
    echo 'deb [arch=amd64] https://download.01.org/intel-sgx/sgx_repo/ubuntu focal main' | tee /etc/apt/sources.list.d/intel-sgx.list && \
    wget -qO - https://download.01.org/intel-sgx/sgx_repo/ubuntu/intel-sgx-deb.key | apt-key add - && \
    apt update && \
    apt install -y \
          libsgx-dcap-ql=1.21.100.3-focal1 \
          libsgx-dcap-ql-dev=1.21.100.3-focal1 \
          libsgx-dcap-default-qpl=1.21.100.3-focal1 \
          libsgx-dcap-default-qpl-dev=1.21.100.3-focal1

WORKDIR /home/app
COPY classes /home/app/classes
COPY dependency/* /home/app/libs/
COPY classes/sgx_default_qcnl_azure.conf /etc/sgx_default_qcnl.conf
EXPOSE 8080

# Required, along with libsgx-quote-ex, for out-of-proc attestation. See
# https://docs.microsoft.com/en-us/azure/confidential-computing/confidential-nodes-aks-addon
ENV SGX_AESM_ADDR=1

RUN groupadd --gid 10000 cds && useradd --uid 10000 --gid 10000 cds
USER 10000

ENTRYPOINT ["java", "-cp", "/home/app/libs/*:/home/app/classes/", "org.signal.cdsi.Application"]
