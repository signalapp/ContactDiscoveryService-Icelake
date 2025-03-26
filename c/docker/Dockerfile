# To build use:
# docker build -t oebuild .
FROM amd64/debian:bookworm@sha256:e83f38eb264420870d48bccc73f04df5fffc710c66528ad424f857eeff269915

LABEL description="linux build environment for sgx."

RUN rm -rf /etc/apt/sources.list.d/*
COPY apt.conf sources.list /etc/apt/

RUN mkdir /src && \
    apt-get update && \
    apt-get -y install \
    gpg \
    gnupg2 \
    wget \
    software-properties-common \
    libssl-dev \
    gdb \
    libprotobuf32 \
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
    xz-utils

RUN apt-get -y install pipx pip python3-venv
ENV PIPX_HOME=/opt/pipx
ENV PIPX_BIN_DIR=/usr/local/bin
RUN pipx install pbtools==0.47.0 && echo "done"

COPY sgx.sources.list ms.sources.list /etc/apt/sources.list.d/

# ms and intel repos keep old packages around,
# however if they remove some of these in the future
# binary packages can be retrieved from github releases
RUN wget -qO - https://download.01.org/intel-sgx/sgx_repo/ubuntu/intel-sgx-deb.key | apt-key add - && \
    wget -qO - https://packages.microsoft.com/keys/microsoft.asc | apt-key add - && \
    apt-get update && \
    apt -y install \
    libsgx-ae-id-enclave=1.22.100.3-jammy1 \
    libsgx-ae-pce=2.25.100.3-jammy1 \
    libsgx-ae-qe3=1.22.100.3-jammy1 \
    libsgx-dcap-default-qpl=1.22.100.3-jammy1 \
    libsgx-dcap-default-qpl-dev=1.22.100.3-jammy1 \
    libsgx-dcap-ql=1.22.100.3-jammy1 \
    libsgx-dcap-ql-dev=1.22.100.3-jammy1 \
    libsgx-enclave-common=2.25.100.3-jammy1 \
    libsgx-headers=2.25.100.3-jammy1 \
    libsgx-pce-logic=1.22.100.3-jammy1 \
    libsgx-qe3-logic=1.22.100.3-jammy1 \
    libsgx-urts=2.25.100.3-jammy1 \
    && echo Done

ARG OPENENCLAVE_VERSION=0.19.10
ARG OPENENCLAVE_HASH=4a5f77edaaebb1fee52b6f5b2b5f1200fd57a048fb3fcf82afa5dd69ee3b1a8f
ADD --checksum=sha256:${OPENENCLAVE_HASH} \
    https://github.com/openenclave/openenclave/releases/download/v${OPENENCLAVE_VERSION}/Ubuntu_2204_open-enclave_${OPENENCLAVE_VERSION}_amd64.deb ./
RUN dpkg -i Ubuntu_2204_open-enclave_${OPENENCLAVE_VERSION}_amd64.deb

# Rather than ADD --checksum=xxx this file, we wget it within a RUN so the file itself,
# which is quite large, doesn't show up in any intermediate layers.
COPY clang+llvm-11.1.0-x86_64-linux-gnu-ubuntu-20.10.tar.xz.sha256 /tmp
RUN cd /tmp && \
    wget -nv https://github.com/llvm/llvm-project/releases/download/llvmorg-11.1.0/clang+llvm-11.1.0-x86_64-linux-gnu-ubuntu-20.10.tar.xz && \
    sha256sum -c clang+llvm-11.1.0-x86_64-linux-gnu-ubuntu-20.10.tar.xz.sha256 && \
    tar xvf clang+llvm-11.1.0-x86_64-linux-gnu-ubuntu-20.10.tar.xz \
        clang+llvm-11.1.0-x86_64-linux-gnu-ubuntu-20.10/bin/clang-11 \
        clang+llvm-11.1.0-x86_64-linux-gnu-ubuntu-20.10/lib/clang/11.1.0/include && \
    mv clang+llvm-11.1.0-x86_64-linux-gnu-ubuntu-20.10 /opt/clang && \
    ln -s /opt/clang/bin/clang-11 /opt/clang/bin/clang++-11 && \
    rm -fv clang+llvm-11.1.0-x86_64-linux-gnu-ubuntu-20.10.tar.xz

ENV PATH="/opt/openenclave/bin:/opt/clang/bin:${PATH}"
ENV JAVA_HOME="/usr/lib/jvm/java-17-openjdk-amd64"
ENV PKG_CONFIG_PATH="$PKG_CONFIG_PATH:/opt/openenclave/share/pkgconfig"

WORKDIR /src

CMD ["/bin/bash"]
