FROM eclipse-temurin:21-jre-jammy@sha256:034ce51c134cb9abcf28e2bda88fa36ac51369bb26351d697297e81caa9a5d76

COPY classes/sgx_runtime_libraries.sh /tmp/sgx_runtime_libraries.sh
RUN /tmp/sgx_runtime_libraries.sh

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
