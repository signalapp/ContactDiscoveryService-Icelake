# To build use:
# docker build -t oebuild .
FROM amd64/debian:bullseye-20240408@sha256:ad47d883924a75f704037e49c71c20ef209fb3ba05be7070322cb45d2e089261

LABEL description="linux build environment for sgx."

COPY apt.conf sources.list /etc/apt/

RUN mkdir /src && \
    apt-get update && \
    apt-get -y install \
    gpg \
    gnupg2 \
    wget \
    software-properties-common \
    clang-11 \
    libssl-dev \
    gdb \
    libprotobuf17 \
    openjdk-17-jdk-headless \
    libtool \
    bison \
    automake \
    flex \
    libcurl4 \
    afl++ \
    afl++-clang \
    valgrind \
    pkg-config \
    pip && \
    pip install pbtools==0.45.2

COPY sgx.sources.list ms.sources.list /etc/apt/sources.list.d/

# ms and intel repos keep old packages around,
# however if they remove some of these in the future
# binary packages can be retrieved from github releases
RUN wget -qO - https://download.01.org/intel-sgx/sgx_repo/ubuntu/intel-sgx-deb.key | apt-key add - && \
    wget -qO - https://packages.microsoft.com/keys/microsoft.asc | apt-key add - && \
    apt-get update && \
    apt -y install \
    libsgx-ae-id-enclave=1.20.100.2-focal1 \
    libsgx-ae-pce=2.23.100.2-focal1 \
    libsgx-ae-qe3=1.20.100.2-focal1 \
    libsgx-dcap-ql=1.20.100.2-focal1 \
    libsgx-dcap-ql-dev=1.20.100.2-focal1 \
    libsgx-enclave-common=2.23.100.2-focal1 \
    libsgx-headers=2.23.100.2-focal1 \
    libsgx-pce-logic=1.20.100.2-focal1 \
    libsgx-qe3-logic=1.20.100.2-focal1 \
    libsgx-urts=2.23.100.2-focal1 \
    open-enclave=0.19.4

ENV PATH="/opt/openenclave/bin:${PATH}"
ENV JAVA_HOME="/usr/lib/jvm/java-17-openjdk-amd64"
ENV PKG_CONFIG_PATH="$PKG_CONFIG_PATH:/opt/openenclave/share/pkgconfig"

WORKDIR /src

CMD ["/bin/bash"]
