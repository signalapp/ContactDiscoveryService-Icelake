FROM amd64/debian:bookworm@sha256:376a959e4bc54d1bc51e9487a78e33deeef62b8f3dc7754ad5acd33330f38d7a

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
        xz-utils \
        opam \
        build-essential \
        m4 \
        unzip \
        bubblewrap \
        python3-distutils \
        libgmp-dev \
        libpcre3-dev \
        libmpfr-dev \
        libppl-dev \
        autoconf

RUN apt-get -y install pipx pip python3-venv
ENV PIPX_HOME=/opt/pipx
ENV PIPX_BIN_DIR=/usr/local/bin
RUN pipx install pbtools==0.47.0 && echo "done"

COPY sgx_runtime_libraries.sh /tmp/sgx_runtime_libraries.sh
RUN /tmp/sgx_runtime_libraries.sh

ARG OPENENCLAVE_VERSION=0.19.13
ARG OPENENCLAVE_HASH=10a74d365c1add73b95388f22dad89cd62cbac701dbe935aae39ecf07f29c510
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

# Install Jasmin via OPAM
ARG UID=1000
ARG GID=1000
RUN groupadd -g $GID signal && \
    useradd -m -u $UID -g $GID signal
USER signal
WORKDIR /home/signal

ENV OPAMYES=1
ENV OPAMJOBS=2
RUN opam init -y --disable-sandboxing -c ocaml-base-compiler.4.12.0 && \
    eval $(opam env) && \
    opam install -y jasmin.2025.06.1
ENV PATH="/home/signal/.opam/ocaml-base-compiler.4.12.0/bin:${PATH}"

WORKDIR /src

CMD ["/bin/bash"]
